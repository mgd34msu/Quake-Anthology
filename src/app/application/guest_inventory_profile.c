#include "guest_inventory_profile.h"
#include "qa/binary.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false;
}

static bool word(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, id, &value, error)) return false;
    if (value > UINT32_MAX) return fail(error, "Original inventory declaration exceeds a source word");
    *out = (uint32_t)value; return true;
}

static bool list(const qa_json_document *doc, qa_json_id id, qa_error *error)
{
    return qa_json_type(doc, id) == QA_JSON_ARRAY || fail(error, "Original inventory requires a source list");
}

static bool constant(const qa_qvm_image *image, const qa_json_document *doc,
    qa_json_id id, int32_t *out, qa_error *error)
{
    uint32_t index; size_t count;
    if (!word(doc, id, &index, error)) return false;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    if (index >= count || code[index].opcode != QA_QVM_CONST)
        return fail(error, "Original inventory operand is not its declared OP_CONST");
    *out = code[index].operand; return true;
}

static bool source_word(const qa_qvm_image *image, const qa_json_document *doc,
    qa_json_id id, bool counter, guest_inventory_word *out, qa_error *error)
{
    qa_json_id kind = qa_json_get(doc, id, "kind");
    if (qa_json_string_equal(doc, kind, "constant")) {
        out->kind = GUEST_INVENTORY_CONSTANT;
        return constant(image, doc, qa_json_get(doc, id, "instruction"), &out->value, error);
    }
    if (qa_json_string_equal(doc, kind, "weapon")) out->kind = GUEST_INVENTORY_WEAPON;
    else if (qa_json_string_equal(doc, kind, "client")) out->kind = GUEST_INVENTORY_CLIENT;
    else if (qa_json_string_equal(doc, kind, "entity")) out->kind = GUEST_INVENTORY_ENTITY;
    else if (qa_json_string_equal(doc, kind, "client-number")) out->kind = GUEST_INVENTORY_CLIENT_NUMBER;
    else if (counter && qa_json_string_equal(doc, kind, "maximum-grant")) out->kind = GUEST_INVENTORY_MAXIMUM_GRANT;
    else return fail(error, "Original inventory query has an unknown source argument");
    return true;
}

void application_guest_public_inventory_profile_free(guest_public_inventory_profile *profile)
{
    if (!profile) return;
    free(profile->functions); free(profile->locals); free(profile->inputs); free(profile->weapon_limits);
    *profile = (guest_public_inventory_profile){0};
}

bool application_guest_public_inventory_profile_read(const qa_qvm_image *image, qa_qvm *vm, qa_qvm_abi abi,
    const qa_json_document *doc, qa_json_id id, guest_public_inventory_profile *out, qa_error *error)
{
    if (!image || !vm || !doc || !out || qa_qvm_get_role(vm) != QA_QVM_GAME || qa_qvm_get_abi(vm) != abi ||
        memcmp(qa_qvm_digest(vm), qa_qvm_image_digest(image), sizeof(qa_sha256_digest)))
        return fail(error, "Original inventory profile lacks its admitted GAME image");
    guest_public_inventory_profile p = {.image = image, .vm = vm, .abi = abi};
    bool ok = word(doc, qa_json_get(doc, id, "weaponsOffset"), &p.weapons_offset, error) &&
        word(doc, qa_json_get(doc, id, "ammoOffset"), &p.ammo_offset, error);
    size_t bytes = qa_qvm_player_bytes(abi);
    if (ok && ((p.weapons_offset | p.ammo_offset) & 3 || bytes < 64 ||
        p.weapons_offset > bytes - 4 || p.ammo_offset > bytes - 64 ||
        (p.weapons_offset >= p.ammo_offset && p.weapons_offset < (uint64_t)p.ammo_offset + 64)))
        ok = fail(error, "Original inventory fields leave or overlap the public player record");
    qa_json_id capacity = qa_json_get(doc, id, "capacity"), kind = qa_json_get(doc, capacity, "kind");
    if (ok && qa_json_string_equal(doc, kind, "constant")) {
        p.capacity_kind = GUEST_PUBLIC_CONSTANT;
        ok = constant(image, doc, qa_json_get(doc, capacity, "instruction"), &p.constant, error);
        if (ok && p.constant < 0) ok = fail(error, "Original ammo capacity is negative");
    } else if (ok && qa_json_string_equal(doc, kind, "global")) {
        p.capacity_kind = GUEST_PUBLIC_GLOBAL;
        int32_t address;
        ok = constant(image, doc, qa_json_get(doc, capacity, "addressInstruction"), &address, error);
        if (ok && (address < 0 || (address & 3) || (uint64_t)(uint32_t)address + 4 > qa_qvm_image_memory_size(image)))
            ok = fail(error, "Original capacity global leaves allocated module data");
        if (ok) p.global = (uint32_t)address;
    } else if (ok) {
        bool counter = qa_json_string_equal(doc, kind, "counter");
        if (!counter && !qa_json_string_equal(doc, kind, "region"))
            ok = fail(error, "Original inventory capacity has no source query kind");
        p.capacity_kind = counter ? GUEST_PUBLIC_COUNTER : GUEST_PUBLIC_REGION;
        if (ok) ok = word(doc, qa_json_get(doc, capacity, "function"), &p.function, error);
        qa_json_id arguments = qa_json_get(doc, capacity, "arguments");
        if (ok) ok = list(doc, arguments, error);
        if (ok) {
            p.argument_count = qa_json_size(doc, arguments);
            if (p.argument_count > GUEST_INVENTORY_ARGUMENTS) ok = fail(error, "Original inventory arguments exceed the source invocation");
        }
        for (size_t i = 0; ok && i < p.argument_count; ++i)
            ok = source_word(image, doc, qa_json_at(doc, arguments, i), counter, &p.arguments[i], error);
        if (ok && counter) {
            qa_json_id functions = qa_json_get(doc, capacity, "functions");
            ok = list(doc, functions, error);
            if (ok) p.function_count = qa_json_size(doc, functions);
            if (ok && p.function_count > SIZE_MAX / sizeof(*p.functions)) ok = fail(error, "Original inventory function list is too large");
            if (ok && p.function_count) {
                p.functions = calloc(p.function_count, sizeof(*p.functions));
                if (!p.functions) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating original inventory functions"); ok = false; }
            }
            size_t count; const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
            bool found = false;
            for (size_t i = 0; ok && i < p.function_count; ++i) {
                ok = word(doc, qa_json_at(doc, functions, i), &p.functions[i], error);
                if (ok && (p.functions[i] >= count || code[p.functions[i]].opcode != QA_QVM_ENTER))
                    ok = fail(error, "Original inventory counter helper is not a source function");
                for (size_t j = 0; ok && j < i; ++j)
                    if (p.functions[i] == p.functions[j]) ok = fail(error, "Original inventory counter helper is duplicated");
                found |= p.functions[i] == p.function;
            }
            if (ok && !found) ok = fail(error, "Original inventory counter omits its actual entry");
        } else if (ok) {
            qa_json_id region = qa_json_get(doc, capacity, "region"), locals = qa_json_get(doc, region, "inputs"), inputs = qa_json_get(doc, capacity, "inputs");
            uint32_t result;
            ok = word(doc, qa_json_get(doc, region, "entry"), &p.region.entry, error) &&
                word(doc, qa_json_get(doc, region, "join"), &p.region.join, error) &&
                word(doc, qa_json_get(doc, region, "result"), &result, error) &&
                list(doc, locals, error) && list(doc, inputs, error);
            if (ok && result > INT32_MAX) ok = fail(error, "Original inventory result exceeds its source frame");
            if (ok) {
                p.region.result_offset = (int32_t)result; p.region.read_only = true;
                p.region.input_count = qa_json_size(doc, locals);
                if (p.region.input_count != qa_json_size(doc, inputs) || p.region.input_count > SIZE_MAX / sizeof(*p.inputs))
                    ok = fail(error, "Original inventory live-ins differ from the source frame");
            }
            if (ok && p.region.input_count) {
                p.locals = calloc(p.region.input_count, sizeof(*p.locals));
                p.inputs = calloc(p.region.input_count, sizeof(*p.inputs));
                if (!p.locals || !p.inputs) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating original inventory live-ins"); ok = false; }
            }
            p.region.inputs = p.locals;
            for (size_t i = 0; ok && i < p.region.input_count; ++i)
                ok = word(doc, qa_json_at(doc, locals, i), &p.locals[i], error) &&
                    source_word(image, doc, qa_json_at(doc, inputs, i), false, &p.inputs[i], error);
            if (ok) ok = qa_qvm_qualify_region(image, p.function, &p.region, error);
        }
    }
    qa_json_id stack = qa_json_get(doc, capacity, "stack");
    if (ok && stack != QA_JSON_NONE) {
        if (p.capacity_kind != GUEST_PUBLIC_REGION && p.capacity_kind != GUEST_PUBLIC_COUNTER)
            ok = fail(error, "Original capacity stack is only valid for a source query");
        if (ok) ok = word(doc, qa_json_get(doc, stack, "start"), &p.stack.floor, error) &&
            word(doc, qa_json_get(doc, stack, "end"), &p.stack.top, error) &&
            qa_qvm_qualify_evaluation_stack(image, &p.stack, error);
        p.has_stack = ok;
    }
    if (!ok) { application_guest_public_inventory_profile_free(&p); return false; }
    *out = p; return true;
}

static bool source_constant(const qa_qvm_image *image, uint32_t index, int32_t *out, qa_error *error)
{
    size_t count; const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    if (index >= count || code[index].opcode != QA_QVM_CONST)
        return fail(error, "Qualified original inventory operand is not OP_CONST");
    *out = code[index].operand; return true;
}

static bool source_limit(const qa_qvm_image *image, uint32_t comparison, uint32_t store,
    int32_t *out, qa_error *error)
{
    int32_t a, b; size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    if (!source_constant(image, comparison, &a, error) || !source_constant(image, store, &b, error)) return false;
    if (a < 0 || a != b || (uint64_t)comparison + 1 >= count || (uint64_t)store + 1 >= count ||
        code[comparison + 1].opcode != QA_QVM_LEI || code[store + 1].opcode != QA_QVM_STORE4)
        return fail(error, "Qualified original ammo comparison and store disagree");
    *out = a; return true;
}

static bool source_threshold(const qa_qvm_image *image, uint32_t index,
    uint32_t ammo_offset, uint32_t *weapon, int32_t *limit, qa_error *error)
{
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    static const qa_qvm_opcode operations[] = {
        QA_QVM_LOCAL, QA_QVM_LOAD4, QA_QVM_CONST, QA_QVM_ADD, QA_QVM_LOAD4,
        QA_QVM_CONST, QA_QVM_LTI, QA_QVM_LOCAL, QA_QVM_LOAD4, QA_QVM_CONST,
        QA_QVM_ADD, QA_QVM_LOAD4, QA_QVM_CONST, QA_QVM_NE, QA_QVM_CONST,
        QA_QVM_LEAVE
    };
    if (index < 2 || (uint64_t)index + 14 > count)
        return fail(error, "Qualified original ammo threshold leaves its source function");
    for (size_t i = 0; i < sizeof(operations) / sizeof(*operations); ++i)
        if (code[index - 2 + i].opcode != operations[i])
            return fail(error, "Qualified original ammo threshold lost its source comparison");
    int32_t field = code[index].operand, tag = code[index + 10].operand;
    if (code[index - 2].operand != 80 || code[index + 5].operand != 16 ||
        code[index + 7].operand != 40 || code[index + 12].operand != 0 ||
        code[index + 13].operand != 64 ||
        code[index + 4].operand != code[index + 11].operand ||
        code[index + 4].operand <= (int32_t)(index + 13) ||
        (uint32_t)code[index + 4].operand >= count ||
        field < (int32_t)ammo_offset || ((uint32_t)field - ammo_offset) % 4 ||
        ((uint32_t)field - ammo_offset) / 4 > 15 || tag < 1 || tag > 15 ||
        code[index + 3].operand < 0)
        return fail(error, "Qualified original ammo threshold lost its item or player field");
    *weapon = ((uint32_t)field - ammo_offset) / 4;
    /* The Source grenade pickup also tests its genuine mirrored hand-grenade counter. */
    if (*weapon != (uint32_t)tag && !(*weapon == 11 && tag == 4))
        return fail(error, "Qualified original ammo threshold disagrees with its item tag");
    *limit = code[index + 3].operand; return true;
}

static bool client_limits(const qa_qvm_image *image, guest_public_inventory_profile *p,
    qa_error *error)
{
    static const uint32_t thresholds[2][9] = {
        {6617, 6635, 6653, 6671, 6689, 6707, 6725, 6743, 6761},
        {6786, 6804, 6822, 6840, 6858, 6876, 6894, 6912, 6930}
    };
    int32_t selector, boosted_selector, weapons_offset, ammo_offset;
    if (!source_constant(image, 6610, &selector, error) ||
        !source_constant(image, 6779, &boosted_selector, error) ||
        !source_constant(image, 6613, &p->client_selector_values[0], error) ||
        !source_constant(image, 6782, &p->client_selector_values[1], error) ||
        !source_constant(image, 143361, &weapons_offset, error) ||
        !source_constant(image, 143228, &ammo_offset, error)) return false;
    size_t count, bytes = qa_qvm_player_bytes(p->abi);
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    if (count <= 143380 || selector < 0 || selector != boosted_selector ||
        (selector & 3) || bytes < 4 || (uint32_t)selector > bytes - 4 ||
        weapons_offset < 0 || weapons_offset != (int32_t)p->weapons_offset ||
        ammo_offset < 0 || ammo_offset != (int32_t)p->ammo_offset ||
        bytes < 64 || p->ammo_offset > bytes - 64 || p->weapons_offset > bytes - 4 ||
        p->client_selector_values[0] != 0 || p->client_selector_values[1] != 1 ||
        code[6608].opcode != QA_QVM_LOCAL || code[6608].operand != 80 ||
        code[6609].opcode != QA_QVM_LOAD4 || code[6611].opcode != QA_QVM_ADD ||
        code[6612].opcode != QA_QVM_LOAD4 || code[6614].opcode != QA_QVM_NE || code[6614].operand != 6777 ||
        code[6777].opcode != QA_QVM_LOCAL || code[6777].operand != 80 ||
        code[6778].opcode != QA_QVM_LOAD4 || code[6780].opcode != QA_QVM_ADD ||
        code[6781].opcode != QA_QVM_LOAD4 || code[6783].opcode != QA_QVM_NE || code[6783].operand != 6946 ||
        code[143360].opcode != QA_QVM_LOAD4 || code[143362].opcode != QA_QVM_ADD ||
        code[143369].opcode != QA_QVM_CONST || code[143369].operand != 1 ||
        code[143378].opcode != QA_QVM_LSH || code[143379].opcode != QA_QVM_BOR ||
        code[143380].opcode != QA_QVM_STORE4 ||
        code[143221].opcode != QA_QVM_CONST || code[143221].operand != 2 ||
        code[143222].opcode != QA_QVM_LSH || code[143227].opcode != QA_QVM_LOAD4 ||
        code[143229].opcode != QA_QVM_ADD || code[143230].opcode != QA_QVM_ADD ||
        code[143231].opcode != QA_QVM_STORE4)
        return fail(error, "Qualified original ammo selector lost its actual player field");
    p->client_selector_offset = (uint32_t)selector;
    p->weapon_limit_count = 16;
    p->weapon_limits = malloc(2 * p->weapon_limit_count * sizeof(*p->weapon_limits));
    if (!p->weapon_limits) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original player ammo thresholds"); return false; }
    for (size_t i = 0; i < 2 * p->weapon_limit_count; ++i) p->weapon_limits[i] = -1;
    for (size_t mode = 0; mode < 2; ++mode)
        for (size_t i = 0; i < 9; ++i) {
            uint32_t weapon; int32_t limit;
            if (!source_threshold(image, thresholds[mode][i], p->ammo_offset, &weapon, &limit, error)) return false;
            int32_t *row = p->weapon_limits + mode * p->weapon_limit_count;
            if (row[weapon] >= 0) return fail(error, "Qualified original player ammo threshold repeats its slot");
            row[weapon] = limit;
        }
    return true;
}

bool application_guest_public_inventory_profile_default(const qa_qvm_image *image, qa_qvm *vm,
    qa_qvm_abi abi, guest_public_inventory_profile *out, bool *found, qa_error *error)
{
    if (!image || !vm || !out || !found || qa_qvm_get_role(vm) != QA_QVM_GAME ||
        qa_qvm_get_abi(vm) != abi || !qa_sha256_equal(qa_qvm_digest(vm), qa_qvm_image_digest(image)))
        return fail(error, "Original inventory default lacks its actual GAME owner");
    char digest[65]; qa_sha256_hex(qa_qvm_image_digest(image), digest);
    bool stock = !strcmp(digest, "57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219");
    bool threewave = !strcmp(digest, "9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337");
    bool lrctf = !strcmp(digest, "b9e396cf5ed2b913548cd92e2b0886ad5992653c8903fa3f9ed0b1f4167ca43e");
    if (!stock && !threewave && !lrctf) { *found = false; return true; }
    if (abi != QA_QVM_Q3_MODERN) return fail(error, "Qualified original inventory requires the modern GAME ABI");
    guest_public_inventory_profile p = {.image = image, .vm = vm, .abi = abi,
        .ammo_offset = 376, .weapons_offset = threewave ? 204 : 192,
        .capacity_kind = stock ? GUEST_PUBLIC_CONSTANT : threewave ? GUEST_PUBLIC_THREEWAVE : GUEST_PUBLIC_CLIENT_LIMITS};
    bool ok;
    if (stock) ok = source_limit(image, 103202, 103216, &p.constant, error);
    else if (lrctf) ok = client_limits(image, &p, error);
    else {
        int32_t game_type, lithium, last, jump_table;
        ok = source_limit(image, 166830, 166844, &p.constant, error) &&
            source_limit(image, 166753, 166764, &p.fallback, error) &&
            source_constant(image, 166769, &game_type, error) &&
            source_constant(image, 166771, &p.special_mode, error) &&
            source_constant(image, 166773, &lithium, error) &&
            source_constant(image, 166498, &p.first_weapon, error) &&
            source_constant(image, 166507, &last, error) &&
            source_constant(image, 166514, &jump_table, error);
        qa_bytes initialized = qa_qvm_image_initialized_data(image);
        if (ok && (game_type < 0 || lithium < 0 || (game_type & 3) || (lithium & 3) ||
            (uint64_t)(uint32_t)game_type + 4 > qa_qvm_image_memory_size(image) ||
            (uint64_t)(uint32_t)lithium + 4 > qa_qvm_image_memory_size(image) ||
            p.first_weapon < 0 || last < p.first_weapon || jump_table < 0 ||
            (uint64_t)(uint32_t)jump_table + ((uint64_t)(uint32_t)last + 1) * 4 > initialized.size))
            ok = fail(error, "Qualified original capacity switch leaves its actual source data");
        if (ok) {
            p.game_type = (uint32_t)game_type; p.lithium = (uint32_t)lithium;
            p.weapon_limit_count = (size_t)((int64_t)last - p.first_weapon + 1);
            if (p.weapon_limit_count > SIZE_MAX / sizeof(*p.weapon_limits))
                ok = fail(error, "Qualified original capacity switch is too large");
        }
        if (ok) {
            p.weapon_limits = malloc(p.weapon_limit_count * sizeof(*p.weapon_limits));
            if (!p.weapon_limits) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original capacity switch"); ok = false; }
        }
        for (size_t i = 0; ok && i < p.weapon_limit_count; ++i) {
            uint32_t weapon = (uint32_t)p.first_weapon + (uint32_t)i;
            int32_t branch = qa_load_i32le(initialized.data + (uint32_t)jump_table + (size_t)weapon * 4);
            if (branch < 0 || (uint64_t)(uint32_t)branch + 21 > UINT32_MAX)
                ok = fail(error, "Qualified original capacity switch has an invalid branch");
            else ok = source_limit(image, (uint32_t)branch + 10, (uint32_t)branch + 21, p.weapon_limits + i, error);
        }
    }
    if (!ok) { application_guest_public_inventory_profile_free(&p); return false; }
    *out = p; *found = true; return true;
}

static int32_t source_value(guest_inventory_word word, const guest_inventory_source *source)
{
    switch (word.kind) {
    case GUEST_INVENTORY_CONSTANT: return word.value;
    case GUEST_INVENTORY_WEAPON: return (int32_t)source->weapon;
    case GUEST_INVENTORY_CLIENT: return (int32_t)source->client;
    case GUEST_INVENTORY_ENTITY: return (int32_t)source->entity;
    case GUEST_INVENTORY_CLIENT_NUMBER: return (int32_t)source->client_number;
    case GUEST_INVENTORY_MAXIMUM_GRANT: return INT32_MAX;
    }
    return 0;
}

bool application_guest_public_inventory_capacity(const guest_public_inventory_profile *p,
    qa_qvm *vm, const guest_inventory_source *source, int32_t *out, qa_error *error)
{
    if (!p || !source || !out || !vm || p->vm != vm || !p->image || qa_qvm_get_role(vm) != QA_QVM_GAME ||
        qa_qvm_get_abi(vm) != p->abi || memcmp(qa_qvm_digest(vm), qa_qvm_image_digest(p->image), sizeof(qa_sha256_digest)) ||
        source->weapon < 1 || source->weapon > 15 || source->client > INT32_MAX || source->entity > INT32_MAX || source->client_number > INT32_MAX)
        return fail(error, "Original inventory capacity lost its actual GAME source");
    if (!qa_qvm_read(vm, 0, NULL, 0, error)) return false;
    int32_t value = 0; bool ok = true;
    if (p->capacity_kind == GUEST_PUBLIC_CONSTANT) value = p->constant;
    else if (p->capacity_kind == GUEST_PUBLIC_GLOBAL) {
        uint8_t bytes[4]; ok = qa_qvm_read(vm, p->global, bytes, sizeof(bytes), error);
        if (ok) value = qa_load_i32le(bytes);
    } else if (p->capacity_kind == GUEST_PUBLIC_THREEWAVE) {
        uint8_t bytes[4];
        ok = qa_qvm_read(vm, p->game_type, bytes, sizeof(bytes), error);
        bool special = ok && qa_load_i32le(bytes) == p->special_mode;
        if (ok && !special) {
            ok = qa_qvm_read(vm, p->lithium, bytes, sizeof(bytes), error);
            special = ok && qa_load_i32le(bytes) != 0;
        }
        value = p->constant;
        if (ok && special) {
            int64_t index = (int64_t)source->weapon - p->first_weapon;
            value = index >= 0 && (uint64_t)index < p->weapon_limit_count ? p->weapon_limits[index] : p->fallback;
        }
    } else if (p->capacity_kind == GUEST_PUBLIC_CLIENT_LIMITS) {
        uint64_t address = (uint64_t)source->client + p->client_selector_offset;
        uint8_t bytes[4];
        if (address > UINT32_MAX) return fail(error, "Original ammo selector address overflowed");
        if (!qa_qvm_read(vm, (uint32_t)address, bytes, sizeof(bytes), error)) return false;
        int32_t selector = qa_load_i32le(bytes);
        size_t mode;
        if (selector == p->client_selector_values[0]) mode = 0;
        else if (selector == p->client_selector_values[1]) mode = 1;
        else return fail(error, "Original ammo selector has no qualified source threshold");
        if (source->weapon >= p->weapon_limit_count || !p->weapon_limits)
            return fail(error, "Original ammo threshold lacks its actual weapon slot");
        value = p->weapon_limits[mode * p->weapon_limit_count + source->weapon];
    } else {
        int32_t arguments[GUEST_INVENTORY_ARGUMENTS] = {0};
        for (size_t i = 0; i < p->argument_count; ++i) arguments[i] = source_value(p->arguments[i], source);
        size_t count = p->argument_count < 10 ? 10 : p->argument_count;
        const qa_qvm_evaluation_stack *stack = p->has_stack ? &p->stack : NULL;
        if (p->capacity_kind == GUEST_PUBLIC_COUNTER) {
            uint64_t address = (uint64_t)source->client + p->ammo_offset + source->weapon * 4;
            if (address > UINT32_MAX) return fail(error, "Original ammo counter address overflowed");
            ok = qa_qvm_evaluate_counter(vm, (uint32_t)address, 0, p->functions, p->function_count,
                p->function, arguments, count, stack, &value, error);
        } else {
            int32_t *inputs = p->region.input_count ? malloc(p->region.input_count * sizeof(*inputs)) : NULL;
            if (p->region.input_count && !inputs) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating original capacity values"); return false; }
            for (size_t i = 0; i < p->region.input_count; ++i) inputs[i] = source_value(p->inputs[i], source);
            ok = qa_qvm_evaluate_region(vm, p->function, arguments, count, &p->region, inputs, stack, &value, error);
            free(inputs);
        }
    }
    if (!ok) return false;
    if (value < 0) return fail(error, "Original ammo capacity is negative");
    *out = value; return true;
}

#include "guest_q3_weapon_profile.h"
#include "guest_q3_private.h"
#include "qa/json.h"

static bool u32(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, id, &value, error)) return false;
    if (value > UINT32_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "Original weapon word exceeds uint32");
        return false;
    }
    *out = (uint32_t)value; return true;
}
static bool word(const qa_json_document *doc, qa_json_id object, const char *key,
    uint32_t *out, qa_error *error)
{ return u32(doc, qa_json_get(doc, object, key), out, error); }
static bool i32(const qa_json_document *doc, qa_json_id id, int32_t *out, qa_error *error)
{
    int64_t value;
    if (!qa_json_i64(doc, id, &value, error)) return false;
    if (value < INT32_MIN || value > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon scalar exceeds int32");
    *out = (int32_t)value; return true;
}
static bool signed_word(const qa_json_document *doc, qa_json_id object, const char *key,
    int32_t *out, qa_error *error)
{ return i32(doc, qa_json_get(doc, object, key), out, error); }
static bool list(const qa_json_document *doc, qa_json_id id, void **out, size_t *count,
    size_t element, qa_error *error)
{
    if (qa_json_type(doc, id) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon declaration requires a source list");
    size_t n = qa_json_size(doc, id);
    if (n > SIZE_MAX / element)
        return application_fail(error, QA_ERROR_MEMORY, "Original weapon list exceeds address space");
    void *rows = n ? calloc(n, element) : NULL;
    if (n && !rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining original weapon declaration");
    *out = rows; *count = n; return true;
}
static bool identity(q3g_role *role, const qa_json_document *doc, qa_json_id id,
    uint32_t *out, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_json_string(doc, id, &text, error)) return false;
    const uint8_t *colon = memchr(text.data, ':', text.size);
    bool valid = colon && colon != text.data && colon != text.data + text.size - 1 && !memchr(text.data, 0, text.size);
    bool ok = valid ? qa_strings_intern(qa_session_strings(role->engine->provider->application->session),
        (qa_bytes){text.data, text.size}, out, error) :
        application_fail(error, QA_ERROR_FORMAT, "Original weapon identity requires a namespace");
    qa_buffer_free(&text); return ok;
}
static bool client_field(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    return (qa_json_string_equal(doc, qa_json_get(doc, id, "record"), "client") ||
        application_fail(error, QA_ERROR_FORMAT, "Primary weapon field requires its original client")) &&
        word(doc, id, "offset", out, error);
}
static bool match_text(q3g_role *role, const qa_json_document *doc, qa_json_id id,
    bool nullable, bool argument, qa_string_id *out, qa_error *error)
{
    *out = 0;
    if (nullable && qa_json_type(doc, id) == QA_JSON_NULL) return true;
    qa_buffer text = {0};
    if (!qa_json_string(doc, id, &text, error)) return false;
    bool valid = !memchr(text.data, 0, text.size) &&
        (!argument || (text.size && !memchr(text.data, '\r', text.size) && !memchr(text.data, '\n', text.size)));
    bool ok = valid ? qa_strings_intern(qa_session_strings(role->engine->provider->application->session),
        (qa_bytes){text.data, text.size}, out, error) :
        application_fail(error, QA_ERROR_FORMAT, "Original team command has invalid source text");
    qa_buffer_free(&text); return ok;
}
static bool match_declaration(q3g_role *role, const qa_json_document *doc, qa_json_id match,
    application_q3_weapon_profile *p, qa_error *error)
{
    qa_json_id rows = qa_json_get(doc, match, "teams"); void *allocation = NULL;
    if (!word(doc, match, "score", &p->score, error) ||
        !list(doc, rows, &allocation, &p->team_count, sizeof(*p->teams), error)) return false;
    p->teams = allocation;
    for (size_t i = 0; i < p->team_count; ++i) {
        application_q3_weapon_team_command *command = p->teams + i;
        qa_json_id row = qa_json_at(doc, rows, i), args = qa_json_get(doc, row, "arguments");
        allocation = NULL;
        if (!match_text(role, doc, qa_json_get(doc, row, "source"), true, false, &command->source, error) ||
            !match_text(role, doc, qa_json_get(doc, row, "team"), true, false, &command->team, error) ||
            !list(doc, args, &allocation, &command->argument_count, sizeof(*command->arguments), error)) return false;
        command->arguments = allocation;
        if (!command->argument_count)
            return application_fail(error, QA_ERROR_FORMAT, "Original team command requires its source arguments");
        for (size_t j = 0; j < command->argument_count; ++j)
            if (!match_text(role, doc, qa_json_at(doc, args, j), false, true, command->arguments + j, error)) return false;
        for (size_t j = 0; j < i; ++j)
            if (p->teams[j].source == command->source || p->teams[j].team == command->team)
                return application_fail(error, QA_ERROR_FORMAT, "Original team commands repeat source or shared identity");
    }
    return true;
}
static bool region(const qa_json_document *doc, qa_json_id id,
    application_q3_weapon_region *out, qa_error *error)
{ return word(doc, id, "entry", &out->entry, error) && word(doc, id, "join", &out->join, error); }
static bool evaluation(const qa_json_document *doc, qa_json_id id,
    qa_qvm_region_evaluation *out, uint32_t **storage, qa_error *error)
{
    qa_json_id inputs = qa_json_get(doc, id, "inputs"), result = qa_json_get(doc, id, "result");
    size_t count; void *allocation = NULL;
    if (!word(doc, id, "entry", &out->entry, error) || !word(doc, id, "join", &out->join, error) ||
        !list(doc, inputs, &allocation, &count, sizeof(**storage), error)) return false;
    *storage = allocation;
    out->inputs = *storage; out->input_count = count; out->result_offset = -1;
    for (size_t i = 0; i < count; ++i)
        if (!u32(doc, qa_json_at(doc, inputs, i), *storage + i, error)) return false;
    if (qa_json_type(doc, result) != QA_JSON_NULL) {
        uint32_t offset;
        if (!u32(doc, result, &offset, error)) return false;
        if (offset > INT32_MAX) return application_fail(error, QA_ERROR_FORMAT, "Original region local exceeds its source frame");
        out->result_offset = (int32_t)offset;
    }
    return true;
}
static bool tests(const qa_json_document *doc, qa_json_id rows,
    application_q3_weapon_test **out, size_t *count, qa_error *error)
{
    void *allocation = NULL;
    if (!list(doc, rows, &allocation, count, sizeof(**out), error)) return false;
    *out = allocation;
    for (size_t i = 0; i < *count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i), mask = qa_json_get(doc, row, "mask"), comparison = qa_json_get(doc, row, "comparison");
        application_q3_weapon_test *test = *out + i;
        test->masked = qa_json_type(doc, mask) != QA_JSON_NULL;
        test->at_most = qa_json_string_equal(doc, comparison, "at-most");
        if ((!test->at_most && !qa_json_string_equal(doc, comparison, "equals")) ||
            !client_field(doc, qa_json_get(doc, row, "field"), &test->offset, error) ||
            !signed_word(doc, row, "value", &test->value, error) ||
            (test->masked && !u32(doc, mask, &test->mask, error)))
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon test has no source comparison");
    }
    return true;
}

void application_q3_weapon_profile_free(application_q3_weapon_profile *p)
{
    if (!p) return;
    free(p->indirections); free(p->predicates); free(p->settled); free(p->accepted);
    free(p->catalog); free(p->availability.excluded); free(p->contexts);
    free(p->delay_inputs); free(p->teleport_inputs); free(p->objective_inputs);
    for (size_t i = 0; p->teams && i < p->team_count; ++i) free(p->teams[i].arguments);
    free(p->teams);
    *p = (application_q3_weapon_profile){0};
}

static bool declared(q3g_role *role, qa_bytes bytes, application_q3_weapon_profile *p, qa_error *error)
{
    qa_json_document *doc = NULL;
    if (!qa_json_parse(bytes, &doc, error)) return false;
    qa_json_id root = qa_json_get(doc, qa_json_root(doc), "weapons"), stage = qa_json_get(doc, root, "stage"),
        dispatcher = qa_json_get(doc, stage, "dispatcher"), actor = qa_json_get(doc, dispatcher, "actor"),
        pointer = qa_json_get(doc, actor, "pointer"), selection = qa_json_get(doc, stage, "selection"),
        request = qa_json_get(doc, stage, "request"), move = qa_json_get(doc, root, "equipmentMovement"),
        availability = qa_json_get(doc, root, "availability"), powers = qa_json_get(doc, root, "powerups"),
        animation = qa_json_get(doc, root, "torsoAnimation"), water = qa_json_get(doc, root, "waterLevel"),
        damage = qa_json_get(doc, root, "damageFactor"), delay = qa_json_get(doc, root, "delayPlayer"),
        teleport = qa_json_get(doc, root, "teleport"), drop = qa_json_get(doc, root, "drop"),
        give = qa_json_get(doc, root, "give"), named = qa_json_get(doc, give, "named");
    bool ok = word(doc, root, "entityStride", &p->entity_stride, error) &&
        word(doc, root, "clientStride", &p->client_stride, error) && word(doc, root, "clientPointer", &p->client_pointer, error) &&
        word(doc, dispatcher, "entry", &p->dispatcher, error) &&
        qa_json_string_equal(doc, qa_json_get(doc, actor, "record"), "client");
    qa_json_id kind = qa_json_get(doc, pointer, "kind");
    p->pointer_global = qa_json_string_equal(doc, kind, "global");
    if (ok && !p->pointer_global && !qa_json_string_equal(doc, kind, "argument"))
        ok = application_fail(error, QA_ERROR_FORMAT, "Original weapon pointer requires a source addressing mode");
    if (ok) ok = word(doc, pointer, p->pointer_global ? "address" : "index", &p->pointer_base, error) &&
        word(doc, pointer, "offset", &p->pointer_offset, error);
    qa_json_id rows = qa_json_get(doc, pointer, "indirections");
    if (ok) { void *allocation = NULL; ok = list(doc, rows, &allocation, &p->indirection_count, sizeof(*p->indirections), error); p->indirections = allocation; }
    for (size_t i = 0; ok && i < p->indirection_count; ++i) ok = u32(doc, qa_json_at(doc, rows, i), p->indirections + i, error);
    rows = qa_json_get(doc, stage, "predicates");
    if (ok) { void *allocation = NULL; ok = list(doc, rows, &allocation, &p->predicate_count, sizeof(*p->predicates), error); p->predicates = allocation; }
    for (size_t i = 0; ok && i < p->predicate_count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i);
        ok = word(doc, row, "instruction", &p->predicates[i].instruction, error) &&
            qa_json_bool(doc, qa_json_get(doc, row, "unselected"), &p->predicates[i].unselected, error);
    }
    if (ok) ok = tests(doc, qa_json_get(doc, stage, "settled"), &p->settled, &p->settled_count, error) &&
        tests(doc, qa_json_get(doc, request, "accepted"), &p->accepted, &p->accepted_count, error) &&
        word(doc, request, "entry", &p->request, error) && word(doc, request, "argument", &p->request_argument, error) &&
        client_field(doc, qa_json_get(doc, selection, "field"), &p->selection_offset, error);
    rows = qa_json_get(doc, selection, "values");
    if (ok) { void *allocation = NULL; ok = list(doc, rows, &allocation, &p->catalog_count, sizeof(*p->catalog), error); p->catalog = allocation; }
    for (size_t i = 0; ok && i < p->catalog_count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i);
        ok = signed_word(doc, row, "value", &p->catalog[i].weapon, error) &&
            identity(role, doc, qa_json_get(doc, row, "item"), &p->catalog[i].item, error);
    }
    if (ok) ok = word(doc, move, "move", &p->movement_move, error) && word(doc, move, "slice", &p->movement_slice, error) &&
        word(doc, move, "duck", &p->movement_duck, error) && word(doc, move, "movementGlobal", &p->movement_global, error) &&
        word(doc, move, "mins", &p->movement_mins, error) && word(doc, move, "maxs", &p->movement_maxs, error) &&
        region(doc, qa_json_get(doc, move, "locomotion"), &p->locomotion, error);
    qa_json_id trace = qa_json_get(doc, move, "bodyTrace");
    p->body_trace = trace != QA_JSON_NONE;
    if (ok && p->body_trace) ok = word(doc, trace, "callback", &p->movement_trace_callback, error) &&
        word(doc, trace, "mask", &p->movement_trace_mask, error);
    if (ok) ok = word(doc, root, "maxHealth", &p->max_health, error) &&
        word(doc, root, "persistentMaxHealth", &p->persistent_max_health, error) &&
        word(doc, availability, "movementType", &p->availability.movement_type, error) &&
        word(doc, availability, "health", &p->availability.health, error) &&
        word(doc, availability, "team", &p->availability.team, error) &&
        word(doc, availability, "flags", &p->availability.flags, error) &&
        signed_word(doc, availability, "spectatorTeam", &p->availability.spectator_team, error) &&
        signed_word(doc, availability, "respawnFlag", &p->availability.respawn_flag, error);
    rows = qa_json_get(doc, availability, "excluded");
    if (ok) { void *allocation = NULL; ok = list(doc, rows, &allocation, &p->availability.excluded_count,
        sizeof(*p->availability.excluded), error); p->availability.excluded = allocation; }
    for (size_t i = 0; ok && i < p->availability.excluded_count; ++i)
        ok = i32(doc, qa_json_at(doc, rows, i), p->availability.excluded + i, error);
    const char *power_names[] = {"quad", "haste", "flight"};
    for (size_t i = 0; ok && i < 3; ++i) ok = word(doc, powers, power_names[i], p->powerups + i, error);
    if (ok) ok = word(doc, animation, "entry", &p->animation.entry, error) &&
        signed_word(doc, animation, "attack", &p->animation.attack, error) && signed_word(doc, animation, "melee", &p->animation.melee, error) &&
        word(doc, water, "entityOffset", &p->water_entity, error) && word(doc, water, "movementOffset", &p->water_movement, error) &&
        word(doc, damage, "entry", &p->damage.entry, error) && word(doc, damage, "result", &p->damage.result, error) &&
        region(doc, qa_json_get(doc, damage, "stop"), &p->damage.stop, error) &&
        evaluation(doc, qa_json_get(doc, root, "delay"), &p->delay, &p->delay_inputs, error) &&
        word(doc, delay, "movementGlobal", &p->delay_global, error) && word(doc, delay, "playerOffset", &p->delay_player, error) &&
        word(doc, teleport, "entry", &p->teleport_entry, error) && word(doc, teleport, "spawn", &p->spawn, error) &&
        word(doc, teleport, "view", &p->view, error) && evaluation(doc, qa_json_get(doc, teleport, "region"), &p->teleport, &p->teleport_inputs, error) &&
        evaluation(doc, qa_json_get(doc, teleport, "objectives"), &p->objectives, &p->objective_inputs, error);
    if (ok) ok = word(doc, drop, "entry", &p->drop.entry, error) && word(doc, drop, "argument", &p->drop.argument, error) &&
        word(doc, drop, "weapon", &p->drop.weapon, error) && region(doc, qa_json_get(doc, drop, "region"), &p->drop.region, error);
    p->drop.inventory = qa_json_string_equal(doc, qa_json_get(doc, drop, "ammo"), "inventory");
    if (ok && !p->drop.inventory) ok = word(doc, drop, "ammo", &p->drop.ammo, error);
    if (ok) ok = word(doc, give, "entry", &p->give.entry, error) && word(doc, give, "argument", &p->give.argument, error) &&
        word(doc, give, "weapons", &p->give.weapons, error) && word(doc, give, "ammo", &p->give.ammo, error) &&
        region(doc, named, &p->give.named, error) && word(doc, named, "name", &p->give.name, error) && word(doc, named, "item", &p->give.item, error);
    rows = qa_json_get(doc, root, "equipmentContexts");
    if (ok) { void *allocation = NULL; ok = list(doc, rows, &allocation, &p->context_count, sizeof(*p->contexts), error); p->contexts = allocation; }
    for (size_t i = 0; ok && i < p->context_count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i), item = qa_json_get(doc, row, "item");
        ok = identity(role, doc, qa_json_get(doc, row, "provider"), &p->contexts[i].provider, error);
        if (ok && qa_json_type(doc, item) != QA_JSON_NULL) ok = identity(role, doc, item, &p->contexts[i].item, error);
    }
    qa_json_id match = qa_json_get(doc, root, "match");
    p->has_match = match != QA_JSON_NONE;
    if (ok && p->has_match) ok = match_declaration(role, doc, match, p, error);
    qa_json_destroy(doc);
    if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Incomplete original weapon declaration");
    return ok;
}

static bool original_entry(const qa_qvm_instruction *code, size_t count, uint32_t at, qa_error *error)
{
    return (at < count && code[at].opcode == QA_QVM_ENTER) ||
        application_fail(error, QA_ERROR_FORMAT, "Original weapon callback is not an admitted function");
}
static bool original_branch(const qa_qvm_instruction *code, size_t count, uint32_t owner, uint32_t at, qa_error *error)
{
    if (at <= owner || at >= count || code[at].opcode < QA_QVM_EQ || code[at].opcode > QA_QVM_GEF)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon decision is not a conditional");
    for (uint32_t i = owner + 1; i <= at; ++i)
        if (code[i].opcode == QA_QVM_ENTER)
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon decision crosses its function");
    return true;
}
static bool aligned(uint32_t offset, size_t bytes, size_t extent, qa_error *error)
{
    return (!(offset & 3) && bytes >= extent && offset <= bytes - extent) ||
        application_fail(error, QA_ERROR_FORMAT, "Original weapon field leaves its declared record");
}
static bool qualify(q3g_role *role, application_q3_weapon_profile *p, qa_error *error)
{
    size_t count, memory = qa_qvm_image_memory_size(role->image);
    const qa_qvm_instruction *code = qa_qvm_image_instructions(role->image, &count);
    if ((p->entity_stride & 3) || (p->client_stride & 3) || p->entity_stride < qa_qvm_shared_entity_bytes(role->abi) ||
        p->client_stride < qa_qvm_player_bytes(role->abi) || p->entity_stride > memory || p->client_stride > memory ||
        !aligned(p->client_pointer, p->entity_stride, 4, error))
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon records differ from their admitted source ABI");
    const uint32_t entries[] = {p->dispatcher, p->request, p->movement_duck, p->give.entry,
        p->drop.entry, p->damage.entry, p->teleport_entry};
    for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i) {
        if (!original_entry(code, count, entries[i], error)) return false;
        for (size_t j = 0; j < i; ++j) if (entries[i] == entries[j])
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon interfaces overlap function ownership");
    }
    if (!original_entry(code, count, p->movement_move, error) || !original_entry(code, count, p->movement_slice, error) ||
        !original_entry(code, count, p->animation.entry, error) || !original_entry(code, count, p->spawn, error) ||
        !original_entry(code, count, p->view, error) || !p->predicate_count || !p->settled_count || !p->accepted_count ||
        p->request_argument >= 62 || p->give.argument >= 62 || p->drop.argument >= 62 ||
        p->availability.respawn_flag <= 0 || p->animation.attack < 0 || p->animation.melee < 0)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon interface omits its decisions or argument ABI");
    if (p->pointer_offset & 3) return application_fail(error, QA_ERROR_FORMAT, "Original weapon pointer path is unaligned");
    if (p->pointer_global) {
        if (!qa_qvm_qualify_global_word(role->image, p->pointer_base, error)) return false;
    } else if (p->pointer_base >= 62)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon actor argument exceeds its Source ABI");
    for (size_t i = 0; i < p->indirection_count; ++i) if (p->indirections[i] & 3)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon pointer indirection is unaligned");
    for (size_t i = 0; i < p->predicate_count; ++i) {
        if (!original_branch(code, count, p->dispatcher, p->predicates[i].instruction, error)) return false;
        for (size_t j = 0; j < i; ++j) if (p->predicates[i].instruction == p->predicates[j].instruction)
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon predicate is repeated");
    }
    const uint32_t fields[] = {p->selection_offset, p->max_health, p->persistent_max_health,
        p->availability.movement_type, p->availability.health, p->availability.team, p->availability.flags,
        p->powerups[0], p->powerups[1], p->powerups[2]};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
        if (!aligned(fields[i], p->client_stride, 4, error)) return false;
    for (size_t i = 0; i < p->settled_count; ++i) if (!aligned(p->settled[i].offset, p->client_stride, 4, error)) return false;
    for (size_t i = 0; i < p->accepted_count; ++i) if (!aligned(p->accepted[i].offset, p->client_stride, 4, error)) return false;
    if (!aligned(p->water_entity, p->entity_stride, 4, error) || !aligned(p->water_movement, memory, 4, error) ||
        !aligned(p->movement_mins, memory, 12, error) || !aligned(p->movement_maxs, memory, 12, error) ||
        !aligned(p->delay_player, memory, 4, error) || !aligned(p->drop.weapon, p->entity_stride, 4, error) ||
        (!p->drop.inventory && !aligned(p->drop.ammo, p->client_stride, 64, error)) ||
        (p->has_match && !aligned(p->score, p->client_stride, 4, error)) ||
        !qa_qvm_qualify_global_word(role->image, p->movement_global, error) ||
        !qa_qvm_qualify_global_word(role->image, p->delay_global, error) ||
        !qa_qvm_qualify_global_word(role->image, p->damage.result, error)) return false;
    if (p->body_trace && (!aligned(p->movement_trace_callback, memory, 4, error) ||
        !aligned(p->movement_trace_mask, memory, 4, error))) return false;
    for (size_t i = 0; i < p->catalog_count; ++i) {
        if (p->catalog[i].weapon <= 0 || !p->catalog[i].item ||
            (!p->drop.inventory && (uint64_t)p->drop.ammo + (uint64_t)p->catalog[i].weapon * 4 + 4 > p->client_stride))
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon selection leaves its inventory declaration");
        for (size_t j = 0; j < i; ++j) if (p->catalog[j].weapon == p->catalog[i].weapon || p->catalog[j].item == p->catalog[i].item)
            return application_fail(error, QA_ERROR_FORMAT, "Original weapon selection repeats a source value or item");
    }
    for (size_t i = 0; i < p->context_count; ++i) {
        bool found = !p->contexts[i].item;
        for (size_t j = 0; j < p->catalog_count; ++j) if (p->contexts[i].item == p->catalog[j].item) found = true;
        if (!found) return application_fail(error, QA_ERROR_FORMAT, "Equipment cadence item is absent from original weapon selection");
        for (size_t j = 0; j < i; ++j) if (p->contexts[i].provider == p->contexts[j].provider)
            return application_fail(error, QA_ERROR_FORMAT, "Equipment source context repeats its provider");
    }
    if (p->delay.input_count != 1 || p->delay.result_offset < 0 || p->teleport.input_count || p->objectives.input_count)
        return application_fail(error, QA_ERROR_FORMAT, "Original effect region live-ins differ from their player ABI");
    int32_t frame = code[p->give.entry].operand;
    if (frame < 12 || !aligned(p->give.name, (uint32_t)frame, 4, error) || p->give.name < 8 ||
        !aligned(p->give.item, (uint32_t)frame, 4, error) || p->give.item < 8)
        return application_fail(error, QA_ERROR_FORMAT, "Named grant locals leave their original frame");
    uint32_t scratch;
    return qa_qvm_source_scratch_qualify(role->image, 36, &scratch, error) &&
        original_branch(code, count, p->give.entry, p->give.weapons, error) &&
        original_branch(code, count, p->give.entry, p->give.ammo, error) &&
        qa_qvm_qualify_source_region(role->image, p->movement_slice, p->locomotion.entry, p->locomotion.join, error) &&
        qa_qvm_qualify_source_region(role->image, p->damage.entry, p->damage.stop.entry, p->damage.stop.join, error) &&
        qa_qvm_qualify_region(role->image, p->dispatcher, &p->delay, error) &&
        qa_qvm_qualify_region(role->image, p->teleport_entry, &p->teleport, error) &&
        qa_qvm_qualify_region(role->image, p->teleport_entry, &p->objectives, error) &&
        qa_qvm_qualify_source_region(role->image, p->give.entry, p->give.named.entry, p->give.named.join, error) &&
        qa_qvm_qualify_source_region(role->image, p->drop.entry, p->drop.region.entry, p->drop.region.join, error);
}

static bool stock(q3g_role *role, const application_q3_weapon_catalog_entry *catalog, size_t count,
    application_q3_weapon_profile *p, qa_error *error)
{
    char digest[65]; qa_sha256_hex(qa_qvm_image_digest(role->image), digest);
    if (strcmp(digest, "9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337")) return true;
    *p = (application_q3_weapon_profile){.present = true, .entity_stride = 876, .client_stride = 944, .client_pointer = 516,
        .dispatcher = 33648, .request = 33226, .selection_offset = 144,
        .pointer_global = true, .pointer_base = 1091860, .indirection_count = 1,
        .predicate_count = 1, .settled_count = 2, .accepted_count = 1, .catalog_count = count,
        .movement_move = 35535, .movement_slice = 34707, .movement_duck = 32561, .movement_global = 1091860,
        .movement_mins = 180, .movement_maxs = 192, .movement_trace_callback = 224, .movement_trace_mask = 28,
        .body_trace = true, .locomotion = {35397, 35503}, .max_health = 220, .persistent_max_health = 548,
        .availability = {.movement_type = 4, .health = 184, .team = 260, .spectator_team = 3,
            .flags = 12, .respawn_flag = 512, .excluded_count = 5}, .powerups = {312, 320, 332},
        .animation = {27646, 7, 8}, .water_entity = 788, .water_movement = 208,
        .damage = {217003, 1616724, {217081, 217113}}, .context_count = 1,
        .delay = {.entry = 34318, .join = 34350, .input_count = 1, .result_offset = 12},
        .delay_global = 1091860, .teleport_entry = 118339, .spawn = 127849, .view = 128463,
        .teleport = {.entry = 118552, .join = 118726, .result_offset = -1},
        .objectives = {.entry = 118552, .join = 118701, .result_offset = -1},
        .drop = {.entry = 158347, .weapon = 192, .ammo = 376, .region = {158571, 158674}},
        .give = {.entry = 139391, .weapons = 139524, .ammo = 139574, .name = 24, .item = 36, .named = {139936, 139947}},
        .has_match = true, .score = 248, .team_count = 3};
    p->indirections = calloc(1, sizeof(*p->indirections));
    p->predicates = malloc(sizeof(*p->predicates)); p->settled = malloc(2 * sizeof(*p->settled));
    p->accepted = malloc(sizeof(*p->accepted)); p->catalog = count ? malloc(count * sizeof(*p->catalog)) : NULL;
    p->availability.excluded = malloc(5 * sizeof(*p->availability.excluded));
    p->contexts = calloc(1, sizeof(*p->contexts)); p->delay_inputs = malloc(sizeof(*p->delay_inputs));
    p->teams = calloc(p->team_count, sizeof(*p->teams));
    if (!p->indirections || !p->predicates || !p->settled || !p->accepted || (count && !p->catalog) ||
        !p->availability.excluded || !p->contexts || !p->delay_inputs || !p->teams)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining authored original weapon profile");
    p->predicates[0] = (application_q3_weapon_predicate){34044, false};
    p->settled[0] = (application_q3_weapon_test){.offset = 44, .at_most = true};
    p->settled[1] = (application_q3_weapon_test){.offset = 148};
    p->accepted[0] = (application_q3_weapon_test){.offset = 148, .value = 2};
    if (count) memcpy(p->catalog, catalog, count * sizeof(*catalog));
    const int32_t excluded[] = {1, 2, 4, 7, 8}; memcpy(p->availability.excluded, excluded, sizeof(excluded));
    *p->delay_inputs = 12; p->delay.inputs = p->delay_inputs;
    qa_strings *strings = qa_session_strings(role->engine->provider->application->session);
    const char *sources[] = {"q3:1", "q3:2", NULL}, *teams[] = {"team:red", "team:blue", NULL},
        *arguments[] = {"red", "blue", "free"};
    for (size_t i = 0; i < p->team_count; ++i) {
        application_q3_weapon_team_command *command = p->teams + i;
        command->arguments = calloc(2, sizeof(*command->arguments)); command->argument_count = 2;
        if (!command->arguments)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining original Source team command arguments");
        if ((sources[i] && !qa_strings_intern_cstr(strings, sources[i], &command->source, error)) ||
            (teams[i] && !qa_strings_intern_cstr(strings, teams[i], &command->team, error)) ||
            !qa_strings_intern_cstr(strings, "team", command->arguments, error) ||
            !qa_strings_intern_cstr(strings, arguments[i], command->arguments + 1, error)) return false;
    }
    return qa_strings_intern_cstr(strings, "q2:equipment/hand-grenades", &p->contexts[0].provider, error);
}

bool application_q3_weapon_profile_read(q3g_role *role, qa_bytes primary,
    const application_q3_weapon_catalog_entry *catalog, size_t count, application_q3_weapon_profile *out, qa_error *error)
{
    if (!role || !role->image || role->kind != QA_QVM_GAME || !out || (primary.size && !primary.data) ||
        (count && !catalog) || count > SIZE_MAX / sizeof(*catalog))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapons require their actual GAME artifact and item catalog");
    application_q3_weapon_profile p = {0};
    bool ok = primary.size ? (p.present = true, declared(role, primary, &p, error)) : stock(role, catalog, count, &p, error);
    if (ok && p.present) {
        p.digest = *qa_qvm_image_digest(role->image); p.abi = role->abi;
        if (catalog) ok = application_q3_weapon_profile_catalog(&p, catalog, count, error);
        if (ok) ok = qualify(role, &p, error);
    }
    if (!ok) { application_q3_weapon_profile_free(&p); return false; }
    *out = p; return true;
}

bool application_q3_weapon_profile_catalog(application_q3_weapon_profile *p,
    const application_q3_weapon_catalog_entry *catalog, size_t count, qa_error *error)
{
    if (!p || !p->present || (count && !catalog) || p->catalog_count != count)
        return application_fail(error, QA_ERROR_FORMAT, "Original weapon selection differs from its actual item table");
    for (size_t i = 0; i < count; ++i) {
        bool found = false;
        for (size_t j = 0; j < count; ++j) if (p->catalog[i].weapon == catalog[j].weapon && p->catalog[i].item == catalog[j].item) { found = true; break; }
        if (!found) return application_fail(error, QA_ERROR_FORMAT, "Original weapon selection names an item absent from its actual source catalog");
        for (size_t j = 0; j < i; ++j) if (catalog[i].weapon == catalog[j].weapon || catalog[i].item == catalog[j].item)
            return application_fail(error, QA_ERROR_FORMAT, "Actual source weapon table repeats a slot or item");
    }
    for (size_t i = 0; i < count; ++i)
        for (size_t j = 0; j < count; ++j) if (p->catalog[i].weapon == catalog[j].weapon && p->catalog[i].item == catalog[j].item) {
            p->catalog[i].ammo = catalog[j].ammo; break;
        }
    p->catalog_qualified = true; return true;
}

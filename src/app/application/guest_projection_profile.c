#include "guest_projection_private.h"

static bool integer(const qa_json_document *doc, qa_json_id id, int32_t *out,
                    qa_error *error)
{
    int64_t value;
    if (!qa_json_i64(doc, id, &value, error)) return false;
    if (value < INT32_MIN || value > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest projection integer exceeds its source word");
    *out = (int32_t)value; return true;
}

static bool word(const qa_json_document *doc, qa_json_id id, uint32_t *out,
                 qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, id, &value, error)) return false;
    if (value > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest projection word exceeds its source range");
    *out = (uint32_t)value; return true;
}

static bool array(const qa_json_document *doc, qa_json_id id, qa_error *error)
{
    return qa_json_type(doc, id) == QA_JSON_ARRAY ||
        application_fail(error, QA_ERROR_FORMAT, "Guest projection requires an original source list");
}

static bool item(q3g_role *role, const qa_json_document *doc, qa_json_id id,
                 qa_item_id *out, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_json_string(doc, id, &text, error)) return false;
    const uint8_t *colon = memchr(text.data, ':', text.size);
    bool valid = colon && colon != text.data && colon != text.data + text.size - 1 &&
        !memchr(text.data, 0, text.size);
    bool ok = valid ? qa_strings_intern(qa_session_strings(role->engine->provider->application->session),
        (qa_bytes){text.data, text.size}, out, error) :
        application_fail(error, QA_ERROR_FORMAT, "Guest inventory item requires a namespaced identity");
    qa_buffer_free(&text); return ok;
}

static bool field(const application_guest_projection *p, const qa_json_document *doc,
                  qa_json_id id, guest_field *out, qa_error *error)
{
    guest_field value;
    qa_json_id record = qa_json_get(doc, id, "record");
    if (qa_json_string_equal(doc, record, "client")) value.record = GUEST_CLIENT_RECORD;
    else if (qa_json_string_equal(doc, record, "entity")) value.record = GUEST_ENTITY_RECORD;
    else return application_fail(error, QA_ERROR_FORMAT, "Guest inventory field has no qualified source record");
    if (!word(doc, qa_json_get(doc, id, "offset"), &value.offset, error)) return false;
    uint32_t stride = value.record == GUEST_CLIENT_RECORD ? p->client_stride : p->entity_stride;
    if (value.offset % 4 || stride < 4 || value.offset > stride - 4)
        return application_fail(error, QA_ERROR_FORMAT, "Guest inventory field leaves its source record");
    *out = value; return true;
}

static bool constant(q3g_role *role, const qa_json_document *doc, qa_json_id id,
                     int32_t *out, qa_error *error)
{
    uint32_t instruction; size_t count;
    if (!word(doc, id, &instruction, error)) return false;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(role->image, &count);
    if (instruction >= count || code[instruction].opcode != QA_QVM_CONST || code[instruction].operand < 0)
        return application_fail(error, QA_ERROR_FORMAT, "Guest inventory capacity is not its original nonnegative constant");
    *out = code[instruction].operand; return true;
}

static bool capacity(application_guest_projection *p, const qa_json_document *doc,
                     qa_json_id id, guest_capacity *out, qa_error *error)
{
    qa_json_id kind = qa_json_get(doc, id, "kind");
    if (qa_json_string_equal(doc, kind, "constant")) {
        out->kind = GUEST_CAPACITY_CONSTANT;
        if (!integer(doc, qa_json_get(doc, id, "value"), &out->value.constant, error)) return false;
        return out->value.constant >= 0 || application_fail(error, QA_ERROR_FORMAT, "Guest source capacity is negative");
    }
    if (qa_json_string_equal(doc, kind, "field")) {
        out->kind = GUEST_CAPACITY_FIELD;
        return field(p, doc, qa_json_get(doc, id, "field"), &out->value.field, error);
    }
    if (!qa_json_string_equal(doc, kind, "source"))
        return application_fail(error, QA_ERROR_FORMAT, "Guest private capacity has no original storage mode");
    out->kind = GUEST_CAPACITY_SOURCE;
    if (!constant(p->role, doc, qa_json_get(doc, id, "instruction"), &out->value.constant, error)) return false;
    qa_json_id list = qa_json_get(doc, id, "overrides");
    if (!array(doc, list, error)) return false;
    out->override_count = qa_json_size(doc, list);
    if (out->override_count) {
        out->overrides = calloc(out->override_count, sizeof(*out->overrides));
        if (!out->overrides) return application_fail(error, QA_ERROR_MEMORY, "Allocating source capacity selectors");
    }
    for (size_t i = 0; i < out->override_count; ++i) {
        qa_json_id at = qa_json_at(doc, list, i);
        guest_capacity_override *value = &out->overrides[i];
        if (!word(doc, qa_json_get(doc, at, "address"), &value->condition.address, error) ||
            !integer(doc, qa_json_get(doc, at, "value"), &value->condition.value, error) ||
            !constant(p->role, doc, qa_json_get(doc, at, "instruction"), &value->value, error)) return false;
        qa_json_id comparison = qa_json_get(doc, at, "comparison");
        value->condition.equal = qa_json_string_equal(doc, comparison, "equals");
        if (!value->condition.equal && !qa_json_string_equal(doc, comparison, "not-equals"))
            return application_fail(error, QA_ERROR_FORMAT, "Guest capacity selector leaves source storage");
        if (!qa_qvm_qualify_global_word(p->role->image, value->condition.address, error)) return false;
    }
    return true;
}

static bool same_field(guest_field a, guest_field b)
{
    return a.record == b.record && a.offset == b.offset;
}

static bool fraction(const qa_json_document *doc, qa_json_id id, float *out, qa_error *error)
{
    double value;
    if (!qa_json_number(doc, id, &value, error)) return false;
    float native = (float)value;
    if (!isfinite(value) || value < 0 || value > 1 || (double)native != value)
        return application_fail(error, QA_ERROR_FORMAT, "Guest armor protection requires its original binary32 fraction");
    *out = native; return true;
}

static bool private_field(application_guest_projection *p, const qa_json_document *doc,
                          qa_json_id id, uint32_t *out, qa_error *error)
{
    if (!word(doc, id, out, error)) return false;
    return (*out % 4 == 0 && *out >= qa_qvm_shared_entity_bytes(p->role->abi) &&
        *out <= p->entity_stride - 4) ||
        application_fail(error, QA_ERROR_FORMAT, "Guest combat field leaves its qualified private entity record");
}

static bool stat(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    return word(doc, id, out, error) && (*out < 16 ||
        application_fail(error, QA_ERROR_FORMAT, "Guest combat stat leaves its public player ABI"));
}

static bool flag(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    return word(doc, id, out, error) && ((*out && !(*out & (*out - 1))) ||
        application_fail(error, QA_ERROR_FORMAT, "Guest combat flag is not a single original bit"));
}

static bool state_profile(application_guest_projection *p, const qa_json_document *doc,
                           qa_json_id combat, qa_error *error)
{
    guest_state_profile *s = &p->state;
    uint32_t entity_stride, client_stride, client_pointer;
    qa_json_id fields = qa_json_get(doc, combat, "fields"), reactions = qa_json_get(doc, combat, "reactions");
    qa_json_id state = qa_json_get(doc, combat, "state"), team = qa_json_get(doc, state, "team");
    qa_json_id flags = qa_json_get(doc, state, "flags"), mass = qa_json_get(doc, state, "mass");
    qa_json_id armor = qa_json_get(doc, combat, "armor"), tiers = qa_json_get(doc, armor, "tiers");
    if (!word(doc, qa_json_get(doc, combat, "entityStride"), &entity_stride, error) ||
        !word(doc, qa_json_get(doc, combat, "clientStride"), &client_stride, error) ||
        !private_field(p, doc, qa_json_get(doc, fields, "client"), &client_pointer, error) ||
        !private_field(p, doc, qa_json_get(doc, fields, "inuse"), &s->inuse, error) ||
        !private_field(p, doc, qa_json_get(doc, fields, "health"), &s->health, error) ||
        !private_field(p, doc, qa_json_get(doc, fields, "takedamage"), &s->takedamage, error) ||
        !private_field(p, doc, qa_json_get(doc, reactions, "flags"), &s->flags, error) ||
        !stat(doc, qa_json_get(doc, state, "healthStat"), &s->health_stat, error) ||
        !stat(doc, qa_json_get(doc, team, "persistentStat"), &s->team_stat, error) ||
        !stat(doc, qa_json_get(doc, armor, "pointsStat"), &s->armor_stat, error) ||
        !flag(doc, qa_json_get(doc, flags, "invulnerable"), &s->invulnerable, error) ||
        !flag(doc, qa_json_get(doc, flags, "noKnockback"), &s->no_knockback, error) ||
        !flag(doc, qa_json_get(doc, flags, "notarget"), &s->notarget, error) ||
        !fraction(doc, qa_json_get(doc, armor, "protection"), &s->armor_protection, error)) return false;
    if (entity_stride != p->entity_stride || client_stride != p->client_stride || client_pointer != p->client_pointer ||
        s->invulnerable == s->no_knockback || s->invulnerable == s->notarget || s->no_knockback == s->notarget)
        return application_fail(error, QA_ERROR_FORMAT, "Guest combat state disagrees with qualified primary records or flags");
    qa_json_id values = qa_json_get(doc, team, "values");
    if (!array(doc, values, error)) return false;
    s->team_count = qa_json_size(doc, values);
    if (s->team_count) {
        s->teams = calloc(s->team_count, sizeof(*s->teams));
        if (!s->teams) return application_fail(error, QA_ERROR_MEMORY, "Retaining guest source team identities");
    }
    for (size_t i = 0; i < s->team_count; ++i) {
        qa_json_id at = qa_json_at(doc, values, i);
        if (!integer(doc, qa_json_get(doc, at, "value"), &s->teams[i].value, error) ||
            !item(p->role, doc, qa_json_get(doc, at, "team"), &s->teams[i].team, error)) return false;
        for (size_t j = 0; j < i; ++j) if (s->teams[j].value == s->teams[i].value)
            return application_fail(error, QA_ERROR_FORMAT, "Guest team has duplicate source values");
    }
    if (qa_json_string_equal(doc, qa_json_get(doc, mass, "kind"), "constant")) {
        double value;
        if (!qa_json_number(doc, qa_json_get(doc, mass, "value"), &value, error)) return false;
        s->mass_kind = GUEST_MASS_CONSTANT; s->mass.constant = (float)value;
        if (!isfinite(value) || value < 0 || !isfinite(s->mass.constant))
            return application_fail(error, QA_ERROR_FORMAT, "Guest mass is not finite and nonnegative");
    } else {
        if (!qa_json_string_equal(doc, qa_json_get(doc, mass, "kind"), "entity") ||
            !private_field(p, doc, qa_json_get(doc, mass, "offset"), &s->mass.offset, error))
            return application_fail(error, QA_ERROR_FORMAT, "Guest mass has no qualified source field");
        qa_json_id storage = qa_json_get(doc, mass, "storage");
        if (qa_json_string_equal(doc, storage, "int32")) s->mass_kind = GUEST_MASS_INT32;
        else if (qa_json_string_equal(doc, storage, "float32")) s->mass_kind = GUEST_MASS_FLOAT32;
        else return application_fail(error, QA_ERROR_FORMAT, "Guest mass has no original numeric representation");
    }
    if (qa_json_type(doc, tiers) != QA_JSON_NULL) {
        if (!stat(doc, qa_json_get(doc, tiers, "stat"), &s->tier_stat, error) || s->tier_stat == s->armor_stat ||
            !fraction(doc, qa_json_get(doc, tiers, "fallback"), &s->tier_fallback, error))
            return application_fail(error, QA_ERROR_FORMAT, "Guest armor tier aliases its points or lacks fallback");
        qa_json_id conditions = qa_json_get(doc, tiers, "whenAny");
        values = qa_json_get(doc, tiers, "values");
        if (!array(doc, conditions, error) || !array(doc, values, error)) return false;
        s->tier_condition_count = qa_json_size(doc, conditions); s->tier_count = qa_json_size(doc, values);
        if (!s->tier_count || !s->tier_condition_count)
            return application_fail(error, QA_ERROR_FORMAT, "Guest armor tier has an empty selection");
        s->tier_conditions = calloc(s->tier_condition_count, sizeof(*s->tier_conditions));
        s->tiers = calloc(s->tier_count, sizeof(*s->tiers));
        if (!s->tiers || !s->tier_conditions) return application_fail(error, QA_ERROR_MEMORY, "Retaining guest source armor tiers");
        size_t memory = qa_qvm_image_memory_size(p->role->image);
        for (size_t i = 0; i < s->tier_condition_count; ++i) {
            qa_json_id at = qa_json_at(doc, conditions, i);
            guest_condition *condition = &s->tier_conditions[i];
            if (!word(doc, qa_json_get(doc, at, "offset"), &condition->address, error) ||
                !integer(doc, qa_json_get(doc, at, "value"), &condition->value, error)) return false;
            qa_json_id comparison = qa_json_get(doc, at, "comparison");
            condition->equal = qa_json_string_equal(doc, comparison, "equal");
            if ((!condition->equal && !qa_json_string_equal(doc, comparison, "not-equal")) ||
                condition->address % 4 || condition->address > memory - 4)
                return application_fail(error, QA_ERROR_FORMAT, "Guest armor selector leaves original data");
        }
        for (size_t i = 0; i < s->tier_count; ++i) {
            qa_json_id at = qa_json_at(doc, values, i);
            if (!integer(doc, qa_json_get(doc, at, "tier"), &s->tiers[i].value, error) ||
                !fraction(doc, qa_json_get(doc, at, "protection"), &s->tiers[i].protection, error)) return false;
            for (size_t j = 0; j < i; ++j) if (s->tiers[j].value == s->tiers[i].value)
                return application_fail(error, QA_ERROR_FORMAT, "Guest armor tier value is ambiguous");
        }
    }
    p->has_state = true; return true;
}

static bool append(application_guest_projection *p, guest_inventory_field value,
                   bool same_storage, qa_error *error)
{
    for (size_t i = 0; i < p->inventory_count; ++i) {
        const guest_inventory_field *previous = &p->inventory[i];
        if (previous->item == value.item)
            return application_fail(error, QA_ERROR_FORMAT, "Guest inventory item has duplicate source storage");
        bool shared_bits = same_storage && previous->mask && value.mask &&
            same_field(previous->field, value.field);
        if ((!shared_bits && same_field(previous->field, value.field)) ||
            (previous->capacity.kind == GUEST_CAPACITY_FIELD && same_field(previous->capacity.value.field, value.field)) ||
            (value.capacity.kind == GUEST_CAPACITY_FIELD && same_field(previous->field, value.capacity.value.field)))
            return application_fail(error, QA_ERROR_FORMAT, "Guest inventory storage overlaps a source field");
    }
    if (value.capacity.kind == GUEST_CAPACITY_FIELD && same_field(value.field, value.capacity.value.field))
        return application_fail(error, QA_ERROR_FORMAT, "Guest item count aliases its capacity");
    if (p->inventory_count == SIZE_MAX / sizeof(*p->inventory))
        return application_fail(error, QA_ERROR_MEMORY, "Guest inventory declaration is too large");
    guest_inventory_field *items = realloc(p->inventory, (p->inventory_count + 1) * sizeof(*items));
    if (!items) return application_fail(error, QA_ERROR_MEMORY, "Retaining guest source inventory fields");
    p->inventory = items; p->inventory[p->inventory_count++] = value; return true;
}

static bool private_inventory(application_guest_projection *p, const qa_json_document *doc,
                               qa_json_id storage, qa_error *error)
{
    if (!array(doc, storage, error)) return false;
    for (size_t i = 0; i < qa_json_size(doc, storage); ++i) {
        qa_json_id at = qa_json_at(doc, storage, i), kind = qa_json_get(doc, at, "kind");
        guest_inventory_field value = {0};
        if (!field(p, doc, qa_json_get(doc, at, "field"), &value.field, error)) return false;
        if (qa_json_string_equal(doc, kind, "counter")) {
            if (!item(p->role, doc, qa_json_get(doc, at, "item"), &value.item, error) ||
                !capacity(p, doc, qa_json_get(doc, at, "capacity"), &value.capacity, error)) {
                free(value.capacity.overrides); return false;
            }
            if (!append(p, value, false, error)) { free(value.capacity.overrides); return false; }
        } else if (qa_json_string_equal(doc, kind, "bits")) {
            qa_json_id items = qa_json_get(doc, at, "items");
            if (!array(doc, items, error) || !word(doc, qa_json_get(doc, at, "privateMask"), &value.private_mask, error)) return false;
            size_t count = qa_json_size(doc, items), first = p->inventory_count;
            if (!count) return application_fail(error, QA_ERROR_FORMAT, "Guest packed inventory has no item bits");
            uint32_t occupied = value.private_mask;
            value.capacity = (guest_capacity){.kind = GUEST_CAPACITY_CONSTANT, .value.constant = 1};
            for (size_t j = 0; j < count; ++j) {
                qa_json_id bit = qa_json_at(doc, items, j);
                if (!item(p->role, doc, qa_json_get(doc, bit, "item"), &value.item, error) ||
                    !word(doc, qa_json_get(doc, bit, "mask"), &value.mask, error)) return false;
                if (!value.mask || (value.mask & (value.mask - 1)) || (occupied & value.mask))
                    return application_fail(error, QA_ERROR_FORMAT, "Guest packed inventory bits overlap");
                occupied |= value.mask;
                if (!append(p, value, j != 0, error)) return false;
            }
            for (size_t j = first; j < p->inventory_count; ++j) p->inventory[j].allowed_mask = occupied;
        } else return application_fail(error, QA_ERROR_FORMAT, "Guest inventory has no declared source storage kind");
    }
    p->has_inventory = true; return true;
}

void application_guest_projection_profile_free(application_guest_projection *p)
{
    if (!p) return;
    for (size_t i = 0; i < p->inventory_count; ++i) {
        guest_capacity *capacity = &p->inventory[i].capacity;
        free(capacity->overrides);
    }
    free(p->inventory);
    application_guest_public_inventory_profile_free(&p->public_inventory);
    free(p->state.teams); free(p->state.tiers); free(p->state.tier_conditions);
    p->inventory = NULL; p->inventory_count = 0;
}

bool application_guest_projection_profile_read(q3g_role *role, qa_bytes primary,
                                                application_guest_projection *p, qa_error *error)
{
    p->role = role;
    if (!role->image) return true;
    if (!primary.size) {
        bool found;
        if (!application_guest_public_inventory_profile_default(role->image, role->vm, role->abi,
            &p->public_inventory, &found, error)) return false;
        p->has_inventory = p->inventory_public = p->located_inventory = found;
        return true;
    }
    qa_json_document *doc = NULL;
    if (!qa_json_parse(primary, &doc, error)) return false;
    qa_json_id root = qa_json_root(doc), input = qa_json_get(doc, root, "input");
    bool ok = word(doc, qa_json_get(doc, input, "entityStride"), &p->entity_stride, error) &&
        word(doc, qa_json_get(doc, input, "clientStride"), &p->client_stride, error) &&
        word(doc, qa_json_get(doc, input, "clientPointer"), &p->client_pointer, error);
    if (ok && (p->entity_stride < qa_qvm_shared_entity_bytes(role->abi) ||
        p->client_stride < qa_qvm_player_bytes(role->abi) || p->entity_stride % 4 ||
        p->client_stride % 4 || p->client_pointer % 4 || p->client_pointer > p->entity_stride - 4 ||
        p->entity_stride > qa_qvm_image_memory_size(role->image) ||
        p->client_stride > qa_qvm_image_memory_size(role->image)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Guest projection records differ from their source ABI");
    qa_json_id inventory = qa_json_get(doc, root, "inventory");
    qa_json_id storage = qa_json_get(doc, inventory, "storage");
    if (ok && storage != QA_JSON_NONE) ok = private_inventory(p, doc, storage, error);
    else if (ok && inventory != QA_JSON_NONE) {
        ok = application_guest_public_inventory_profile_read(role->image, role->vm, role->abi,
            doc, inventory, &p->public_inventory, error);
        p->has_inventory = p->inventory_public = ok;
    }
    if (ok) ok = state_profile(p, doc, qa_json_get(doc, root, "combat"), error);
    qa_json_destroy(doc); return ok;
}

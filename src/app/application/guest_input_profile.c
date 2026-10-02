#include "guest_input_private.h"
#include "qa/json.h"

static bool word(const qa_json_document *doc, qa_json_id object, const char *key,
                  uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, key), &value, error)) return false;
    if (value > UINT32_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "Guest input word exceeds its source range");
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool mode(const qa_json_document *doc, qa_json_id object, const char *key,
                  int32_t *out, qa_error *error)
{
    int64_t value;
    if (!qa_json_i64(doc, qa_json_get(doc, object, key), &value, error)) return false;
    if (value < INT32_MIN || value > INT32_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "Guest input mode exceeds its source range");
        return false;
    }
    *out = (int32_t)value;
    return true;
}

static bool entry(const qa_qvm_instruction *code, size_t count, uint32_t at,
                   qa_error *error)
{
    return (at < count && code[at].opcode == QA_QVM_ENTER) ||
           application_fail(error, QA_ERROR_FORMAT, "Guest input entry is not an original function");
}

static bool region(const qa_qvm_image *image, const qa_qvm_instruction *code, size_t count, uint32_t owner,
                    uint32_t first, uint32_t join, qa_error *error)
{
    if (owner >= first || first >= join || join >= count)
        return application_fail(error, QA_ERROR_FORMAT, "Guest movement region leaves its source function");
    for (uint32_t at = owner + 1; at <= join; ++at)
        if (code[at].opcode == QA_QVM_ENTER)
            return application_fail(error, QA_ERROR_FORMAT, "Guest movement region crosses a source function");
    return qa_qvm_qualify_source_region(image, owner, first, join, error);
}

void application_guest_input_profile_free(application_guest_input_profile *profile)
{
    if (!profile) return;
    free(profile->intermission_modes);
    *profile = (application_guest_input_profile){0};
}

static bool qualify(q3g_role *role, application_guest_input_profile *p, qa_error *error)
{
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(role->image, &count);
    const uint32_t entries[] = {p->client_think, p->run_client, p->client_spawn, p->move, p->slice};
    size_t memory = qa_qvm_image_memory_size(role->image);
    if (p->entity_stride < qa_qvm_shared_entity_bytes(role->abi) ||
        p->client_stride < qa_qvm_player_bytes(role->abi) ||
        p->entity_stride > memory || p->client_stride > memory ||
        p->entity_stride % 4 || p->client_stride % 4 || p->client_pointer % 4 ||
        p->client_pointer > p->entity_stride - 4)
        return application_fail(error, QA_ERROR_FORMAT, "Guest input records differ from their source ABI");
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        if (!entry(code, count, entries[i], error)) return false;
    if (p->has_locomotion) {
        if (!region(role->image, code, count, p->slice, p->locomotion_entry, p->locomotion_join, error))
            return false;
        if (p->movement_global % 4 || p->movement_global > memory - 4 ||
            p->movement_mins % 4 || p->movement_maxs % 4 || p->movement_water % 4)
            return application_fail(error, QA_ERROR_FORMAT, "Guest movement projection is unaligned");
    }
    if (p->has_duck && !entry(code, count, p->duck, error)) return false;
    if (p->has_body_trace && (!p->has_duck || !p->has_locomotion ||
        p->movement_trace_callback % 4 || p->movement_trace_mask % 4 ||
        p->movement_trace_callback > memory - 4 || p->movement_trace_mask > memory - 4 ||
        p->movement_mins > memory - 12 || p->movement_maxs > memory - 12))
        return application_fail(error, QA_ERROR_FORMAT, "Guest body trace leaves its admitted movement record layout");
    return true;
}

bool application_guest_input_profile_read(q3g_role *role, qa_bytes primary,
                                           application_guest_input_profile *out, qa_error *error)
{
    if (!role || !out || role->kind != QA_QVM_GAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input profile needs a game role");
    application_guest_input_profile p = {0};
    if (!role->image) { *out = p; return true; }
    if (!primary.size) {
        char digest[65]; qa_sha256_hex(qa_qvm_image_digest(role->image), digest);
        if (!strcmp(digest, "b9e396cf5ed2b913548cd92e2b0886ad5992653c8903fa3f9ed0b1f4167ca43e")) {
            p = (application_guest_input_profile){.entity_stride = 856, .client_stride = 872,
                .client_pointer = 516, .client_think = 114858, .run_client = 114917,
                .client_spawn = 125027, .move = 22369, .slice = 21620,
                .input_present = true, .has_modes = true, .normal_mode = 0,
                .noclip_mode = 1, .freeze_mode = 4};
        } else if (!strcmp(digest, "9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337")) {
            p = (application_guest_input_profile){.entity_stride = 876, .client_stride = 944,
                .client_pointer = 516, .client_think = 120292, .run_client = 120383,
                .client_spawn = 132015, .move = 35535, .slice = 34707,
                .input_present = true, .has_modes = true, .normal_mode = 0,
                .noclip_mode = 1, .freeze_mode = 4, .has_locomotion = true,
                .locomotion_entry = 35397, .locomotion_join = 35503, .movement_global = 1091860,
                .movement_mins = 180, .movement_maxs = 192, .movement_water = 208,
                .has_duck = true, .duck = 32561, .has_body_trace = true,
                .movement_trace_callback = 224, .movement_trace_mask = 28};
        }
        if (p.input_present && !qualify(role, &p, error)) {
            application_guest_input_profile_free(&p); return false;
        }
        *out = p; return true;
    }
    qa_json_document *doc;
    if (!qa_json_parse(primary, &doc, error)) return false;
    qa_json_id root = qa_json_root(doc), input = qa_json_get(doc, root, "input");
    qa_json_id entries = qa_json_get(doc, input, "entries");
    bool ok = word(doc, input, "entityStride", &p.entity_stride, error) &&
        word(doc, input, "clientStride", &p.client_stride, error) &&
        word(doc, input, "clientPointer", &p.client_pointer, error) &&
        word(doc, entries, "clientThink", &p.client_think, error) &&
        word(doc, entries, "runClient", &p.run_client, error) &&
        word(doc, entries, "clientSpawn", &p.client_spawn, error) &&
        word(doc, entries, "move", &p.move, error) && word(doc, entries, "slice", &p.slice, error);
    qa_json_id modes = qa_json_get(doc, input, "movementModes");
    if (ok && modes != QA_JSON_NONE) {
        ok = mode(doc, modes, "normal", &p.normal_mode, error) &&
             mode(doc, modes, "noclip", &p.noclip_mode, error) &&
             mode(doc, modes, "freeze", &p.freeze_mode, error);
        p.has_modes = ok;
    }
    qa_json_id intermission = qa_json_get(doc, input, "intermission");
    if (ok && qa_json_type(doc, intermission) != QA_JSON_ARRAY)
        ok = application_fail(error, QA_ERROR_FORMAT, "Guest input intermission modes require a source list");
    p.intermission_count = qa_json_size(doc, intermission);
    if (ok && p.intermission_count) {
        p.intermission_modes = calloc(p.intermission_count, sizeof(*p.intermission_modes));
        if (!p.intermission_modes)
            ok = application_fail(error, QA_ERROR_MEMORY, "Allocating guest intermission modes");
    }
    for (size_t i = 0; ok && i < p.intermission_count; ++i) {
        int64_t value;
        ok = qa_json_i64(doc, qa_json_at(doc, intermission, i), &value, error);
        if (ok && (value < INT32_MIN || value > INT32_MAX))
            ok = application_fail(error, QA_ERROR_FORMAT, "Guest intermission mode exceeds its source word");
        if (ok) p.intermission_modes[i] = (int32_t)value;
    }
    qa_json_id weapons = qa_json_get(doc, root, "weapons");
    qa_json_id movement = qa_json_get(doc, weapons, "equipmentMovement");
    qa_json_id locomotion = qa_json_get(doc, movement, "locomotion");
    uint32_t move, slice;
    if (ok) {
        uint32_t entity_stride, client_stride, client_pointer;
        ok = word(doc, weapons, "entityStride", &entity_stride, error) &&
            word(doc, weapons, "clientStride", &client_stride, error) &&
            word(doc, weapons, "clientPointer", &client_pointer, error);
        if (ok && (entity_stride != p.entity_stride || client_stride != p.client_stride ||
                   client_pointer != p.client_pointer))
            ok = application_fail(error, QA_ERROR_FORMAT, "Guest input and weapon record layouts disagree");
        if (ok) ok = word(doc, movement, "move", &move, error) && word(doc, movement, "slice", &slice, error) &&
            word(doc, movement, "movementGlobal", &p.movement_global, error) &&
            word(doc, movement, "mins", &p.movement_mins, error) &&
            word(doc, movement, "maxs", &p.movement_maxs, error) &&
            word(doc, qa_json_get(doc, weapons, "waterLevel"), "movementOffset", &p.movement_water, error) &&
            word(doc, locomotion, "entry", &p.locomotion_entry, error) &&
            word(doc, locomotion, "join", &p.locomotion_join, error);
        if (ok && (move != p.move || slice != p.slice))
            ok = application_fail(error, QA_ERROR_FORMAT, "Guest input and weapon movement entries disagree");
        p.has_locomotion = ok;
    }
    qa_json_id duck = qa_json_get(doc, movement, "duck");
    if (ok && duck != QA_JSON_NONE) {
        ok = word(doc, movement, "duck", &p.duck, error);
        p.has_duck = ok;
    }
    qa_json_id body_trace = qa_json_get(doc, movement, "bodyTrace");
    if (ok && body_trace != QA_JSON_NONE) {
        ok = word(doc, body_trace, "callback", &p.movement_trace_callback, error) &&
             word(doc, body_trace, "mask", &p.movement_trace_mask, error);
        p.has_body_trace = ok;
    }
    qa_json_destroy(doc);
    p.input_present = ok;
    if (ok) ok = qualify(role, &p, error);
    if (!ok) { application_guest_input_profile_free(&p); return false; }
    *out = p; return true;
}

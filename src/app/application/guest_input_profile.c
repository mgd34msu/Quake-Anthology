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

void application_guest_input_profile_free(application_guest_input_profile *profile)
{
    if (!profile) return;
    free(profile->intermission_modes);
    *profile = (application_guest_input_profile){0};
}

static bool qualify(q3g_role *role, const application_guest_input_profile *p, qa_error *error)
{
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(role->image, &count);
    const uint32_t entries[] = {p->client_think, p->run_client, p->client_spawn};
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        if (!entry(code, count, entries[i], error)) return false;
    const application_q3_weapon_profile *source = p->source;
    if (!source) return application_fail(error, QA_ERROR_FORMAT, "Guest input has no qualified source movement declaration");
    if (source->present) return true;
    size_t memory = qa_qvm_image_memory_size(role->image);
    if (source->entity_stride < qa_qvm_shared_entity_bytes(role->abi) ||
        source->client_stride < qa_qvm_player_bytes(role->abi) ||
        source->entity_stride > memory || source->client_stride > memory ||
        source->entity_stride % 4 || source->client_stride % 4 || source->client_pointer % 4 ||
        source->client_pointer > source->entity_stride - 4)
        return application_fail(error, QA_ERROR_FORMAT, "Guest input records differ from their source ABI");
    return entry(code, count, source->movement_move, error) &&
        entry(code, count, source->movement_slice, error);
}

bool application_guest_input_profile_read(q3g_role *role, qa_bytes primary,
                                           application_guest_input_profile *out, qa_error *error)
{
    if (!role || !out || role->kind != QA_QVM_GAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input profile needs a game role");
    application_guest_input_profile p = {.source = application_q3_weapons_profile(role->weapons)};
    if (!role->image) { *out = p; return true; }
    if (!primary.size) {
        char digest[65]; qa_sha256_hex(qa_qvm_image_digest(role->image), digest);
        if (!strcmp(digest, "b9e396cf5ed2b913548cd92e2b0886ad5992653c8903fa3f9ed0b1f4167ca43e")) {
            static const application_q3_weapon_profile source = {
                .entity_stride = 856, .client_stride = 872, .client_pointer = 516,
                .movement_move = 22369, .movement_slice = 21620};
            p = (application_guest_input_profile){.source = &source,
                .client_think = 114858, .run_client = 114917, .client_spawn = 125027,
                .input_present = true, .has_modes = true, .normal_mode = 0,
                .noclip_mode = 1, .freeze_mode = 4};
        } else if (!strcmp(digest, "9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337")) {
            p.client_think = 120292; p.run_client = 120383; p.client_spawn = 132015;
            p.input_present = p.has_modes = true;
            p.normal_mode = 0; p.noclip_mode = 1; p.freeze_mode = 4;
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
    uint32_t entity_stride, client_stride, client_pointer, move, slice;
    bool ok = p.source && p.source->present;
    if (!ok) application_fail(error, QA_ERROR_FORMAT, "Guest input requires its actual retained weapon movement declaration");
    if (ok) ok = word(doc, input, "entityStride", &entity_stride, error) &&
        word(doc, input, "clientStride", &client_stride, error) &&
        word(doc, input, "clientPointer", &client_pointer, error) &&
        word(doc, entries, "clientThink", &p.client_think, error) &&
        word(doc, entries, "runClient", &p.run_client, error) &&
        word(doc, entries, "clientSpawn", &p.client_spawn, error) &&
        word(doc, entries, "move", &move, error) && word(doc, entries, "slice", &slice, error);
    if (ok && (entity_stride != p.source->entity_stride || client_stride != p.source->client_stride ||
        client_pointer != p.source->client_pointer || move != p.source->movement_move || slice != p.source->movement_slice))
        ok = application_fail(error, QA_ERROR_FORMAT, "Guest input differs from its declared weapon records or movement entries");
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
    qa_json_destroy(doc);
    p.input_present = ok;
    if (ok) ok = qualify(role, &p, error);
    if (!ok) { application_guest_input_profile_free(&p); return false; }
    *out = p; return true;
}

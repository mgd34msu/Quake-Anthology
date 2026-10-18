#include "guest_q3_reference.h"
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

static bool command_pointer(q3g_role *role, const application_guest_input_profile *p,
    uint32_t at, qa_error *error)
{
    const q3_reference_instruction expected[] = {
        {at, QA_QVM_LOCAL, 24}, {at + 1, QA_QVM_LOAD4, 0},
        {at + 2, QA_QVM_CONST, (int32_t)p->source->client_pointer},
        {at + 3, QA_QVM_ADD, 0}, {at + 4, QA_QVM_LOAD4, 0},
        {at + 5, QA_QVM_CONST, (int32_t)p->client_command},
        {at + 6, QA_QVM_ADD, 0}
    };
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(role->image, &count);
    for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); ++i)
        if (expected[i].at >= count || code[expected[i].at].opcode != expected[i].opcode ||
            code[expected[i].at].operand != expected[i].operand)
            return application_fail(error, QA_ERROR_FORMAT,
                "Original RunClient command differs from its declared source pointer");
    return true;
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
    if (p->respawn && !entry(code, count, p->respawn, error)) return false;
    if (p->bot_setup.entry) {
        if (!entry(code, count, p->bot_setup.entry, error) ||
            !entry(code, count, p->bot_setup.connect, error)) return false;
        uint32_t call = p->bot_setup.call;
        if (call <= p->bot_setup.connect || call >= count - 1 ||
            code[call].opcode != QA_QVM_CONST || code[call].operand != (int32_t)p->bot_setup.entry ||
            code[call + 1].opcode != QA_QVM_CALL)
            return application_fail(error, QA_ERROR_FORMAT, "Guest bot setup differs from its original Connect call");
        for (uint32_t pc = p->bot_setup.connect + 1; pc <= call; ++pc)
            if (code[pc].opcode == QA_QVM_ENTER)
                return application_fail(error, QA_ERROR_FORMAT, "Guest bot setup call leaves its original Connect function");
    }
    const application_q3_movement_profile *source = p->source;
    if (!source) return application_fail(error, QA_ERROR_FORMAT, "Guest input has no qualified source movement declaration");
    if (p->client_command != UINT32_MAX && (p->client_command % 4 ||
        source->client_stride < 24 || p->client_command > source->client_stride - 24))
        return application_fail(error, QA_ERROR_FORMAT, "Guest retained command leaves its declared client record");
    return true;
}

bool application_guest_input_profile_read(q3g_role *role, qa_bytes primary,
                                           application_guest_input_profile *out, qa_error *error)
{
    if (!role || !out || role->kind != QA_QVM_GAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest input profile needs a game role");
    application_guest_input_profile p = {.source = &role->movement,
        .client_command = UINT32_MAX};
    if (!role->image) { *out = p; return true; }
    bool base = application_q3_reference_image(role->image, Q3_REFERENCE_BASE_GAME);
    if (base) {
        p.bot_setup.entry = 56212;
        p.bot_setup.connect = 84729;
        p.bot_setup.call = 84804;
    }
    if (!primary.size) {

        if (base) {
            p.client_think = 80328; p.run_client = 80387; p.client_spawn = 89754;
            p.client_command = 472;
            p.respawn = 88047;
            const q3_reference_instruction respawn[] = {
                {88047, QA_QVM_ENTER, 28},
                {88051, QA_QVM_CONST, 87579}, {88052, QA_QVM_CALL, 0},
                {88057, QA_QVM_CONST, 89754}, {88058, QA_QVM_CALL, 0},
                {88068, QA_QVM_CONST, 42}, {88069, QA_QVM_ARG, 12},
                {88071, QA_QVM_CONST, 129852}, {88072, QA_QVM_CALL, 0},
                {88094, QA_QVM_LEAVE, 28}
            };
            size_t instructions;
            const qa_qvm_instruction *code = qa_qvm_image_instructions(role->image, &instructions);
            for (size_t i = 0; i < sizeof(respawn) / sizeof(*respawn); ++i)
                if (respawn[i].at >= instructions || code[respawn[i].at].opcode != respawn[i].opcode ||
                    code[respawn[i].at].operand != respawn[i].operand)
                    return application_fail(error, QA_ERROR_FORMAT,
                        "Original respawn differs from its corpse, spawn and teleport sequence");
            p.input_present = p.has_modes = p.has_dead_mode = true;
            p.normal_mode = 0; p.noclip_mode = 1; p.freeze_mode = 4; p.dead_mode = 3;
            const int32_t intermission[] = {5, 6};
            p.intermission_modes = malloc(sizeof(intermission));
            if (!p.intermission_modes)
                return application_fail(error, QA_ERROR_MEMORY, "Retaining original input intermission modes");
            memcpy(p.intermission_modes, intermission, sizeof(intermission));
            p.intermission_count = sizeof(intermission) / sizeof(*intermission);
        } else if (application_q3_reference_image(role->image, Q3_REFERENCE_LRCTF_GAME)) {
            p = (application_guest_input_profile){.source = &role->movement,
                .client_think = 114858, .run_client = 114917, .client_spawn = 125027,
                .client_command = 472,
                .input_present = true, .has_modes = true, .has_dead_mode = true,
                .normal_mode = 0, .noclip_mode = 1, .freeze_mode = 4, .dead_mode = 3};
        } else if (application_q3_reference_image(role->image, Q3_REFERENCE_THREEWAVE_GAME)) {
            p.client_think = 120292; p.run_client = 120383; p.client_spawn = 132015;
            p.input_present = p.has_modes = p.has_dead_mode = true;
            p.normal_mode = 0; p.noclip_mode = 1; p.freeze_mode = 4; p.dead_mode = 3;
        }
        if ((base || application_q3_reference_image(role->image, Q3_REFERENCE_LRCTF_GAME)) &&
            !command_pointer(role, &p, base ? 80408 : 114938, error)) {
            application_guest_input_profile_free(&p); return false;
        }
        if (p.input_present && !qualify(role, &p, error)) {
            application_guest_input_profile_free(&p); return false;
        }
        *out = p; return true;
    }
    qa_json_document *doc;
    if (!qa_json_parse(QA_JSON_STRICT, primary, &doc, error)) return false;
    qa_json_id root = qa_json_root(doc), input = qa_json_get(doc, root, "input");
    qa_json_id entries = qa_json_get(doc, input, "entries");
    bool ok = p.source->entity_stride != 0;
    if (!ok) application_fail(error, QA_ERROR_FORMAT, "Guest input requires its actual retained movement declaration");
    if (ok) ok = word(doc, entries, "clientThink", &p.client_think, error) &&
        word(doc, entries, "runClient", &p.run_client, error) &&
        word(doc, entries, "clientSpawn", &p.client_spawn, error);
    if (ok && qa_json_get(doc, entries, "respawn") != QA_JSON_NONE)
        ok = word(doc, entries, "respawn", &p.respawn, error);
    if (ok && qa_json_get(doc, input, "clientCommand") != QA_JSON_NONE)
        ok = word(doc, input, "clientCommand", &p.client_command, error);
    qa_json_id modes = qa_json_get(doc, input, "movementModes");
    if (ok && modes != QA_JSON_NONE) {
        ok = mode(doc, modes, "normal", &p.normal_mode, error) &&
             mode(doc, modes, "noclip", &p.noclip_mode, error) &&
             mode(doc, modes, "freeze", &p.freeze_mode, error);
        p.has_modes = ok;
        if (ok && qa_json_get(doc, modes, "dead") != QA_JSON_NONE) {
            ok = mode(doc, modes, "dead", &p.dead_mode, error);
            p.has_dead_mode = ok;
        }
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

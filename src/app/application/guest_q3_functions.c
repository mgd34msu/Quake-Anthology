#include "guest_q3_functions.h"
#include "guest_input_private.h"
#include "qa/source_save.h"

typedef struct function_inventory {
    qa_qvm_saved_function *descriptors;
    size_t input_count, equipment_count, count;
} function_inventory;

static bool inventory(q3g_role *role, function_inventory *out, qa_error *error)
{
    qa_qvm_saved_function input[7] = {0};
    function_inventory value = {0};
    if (!role || !application_guest_input_descriptors(role, input, &value.input_count, error)) return false;
    value.equipment_count = application_q3_equipment_descriptor_count(role->equipment);
    if (value.equipment_count > SIZE_MAX / sizeof(*value.descriptors) - value.input_count)
        return application_fail(error, QA_ERROR_MEMORY, "Complete source callback inventory exceeds address space");
    value.count = value.input_count + value.equipment_count;
    value.descriptors = value.count ? calloc(value.count, sizeof(*value.descriptors)) : NULL;
    if (value.count && !value.descriptors)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining complete actual source callback descriptors");
    if (value.input_count) memcpy(value.descriptors, input, value.input_count * sizeof(*input));
    qa_qvm_saved_function *equipment = value.equipment_count ? value.descriptors + value.input_count : NULL;
    if (!application_q3_equipment_descriptors(role->equipment, equipment, value.equipment_count, error)) {
        free(value.descriptors); return false;
    }
    *out = value;
    return true;
}

static bool signature(qa_source_save_io *io)
{
    uint8_t magic[8] = {'Q','A','G','3','F','N',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','F','N',0,0};
    uint32_t version = 1;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || !qa_source_save_u32(io, &version)) return false;
    return (!memcmp(magic, expected, sizeof(magic)) && version == 1) ||
        application_fail(io->error, QA_ERROR_FORMAT, "Invalid composed Q3 callback continuation");
}

static bool state_fields(qa_source_save_io *io, application_q3_equipment_saved *state)
{
    uint32_t warning = state->draw.warning;
    bool ok = qa_source_save_actor(io, &state->draw.actor) &&
        qa_source_save_u32(io, &warning) && qa_source_save_bool(io, &state->draw.selected) &&
        qa_source_save_bool(io, &state->draw.view_visible) &&
        qa_source_save_bool(io, &state->hud_requested) && qa_source_save_bool(io, &state->view_requested);
    if (ok && warning > QA_APPLICATION_AMMO_EMPTY)
        return application_fail(io->error, QA_ERROR_FORMAT, "Saved equipment warning leaves its real enum");
    if (ok && io->direction == QA_SOURCE_SAVE_READ) state->draw.warning = (qa_application_ammo_warning)warning;
    return ok;
}

bool application_guest_q3_functions_checkpoint(q3g_role *role, qa_buffer *out, qa_error *error)
{
    function_inventory functions = {0};
    qa_buffer input = {0};
    application_q3_equipment_saved state = {0};
    if (!out || !inventory(role, &functions, error)) return false;
    bool ok = qa_qvm_checkpoint_functions(role->vm, functions.descriptors, functions.count, error) &&
        application_guest_input_checkpoint(role, &input, error) &&
        application_q3_equipment_state_read(role->equipment, &state, error);
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, role->engine->provider->application->session, error) && signature(&io);
    size_t length = input.size;
    bool present = role->equipment != NULL;
    size_t count = functions.equipment_count;
    if (ok) ok = qa_source_save_count(&io, &length, SIZE_MAX) &&
        qa_source_save_bytes(&io, input.data, input.size) &&
        qa_source_save_bool(&io, &present) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; ok && i < count; ++i)
        ok = qa_source_save_u64(&io, &functions.descriptors[functions.input_count + i].binding);
    if (ok) ok = state_fields(&io, &state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_buffer_free(&input); free(functions.descriptors);
    return ok;
}

bool application_guest_q3_functions_restore(q3g_role *role, qa_bytes bytes,
    qa_bytes executor, qa_error *error)
{
    function_inventory functions = {0};
    if (!inventory(role, &functions, error)) return false;
    if (!role->engine->restore_pending || role->initialized) {
        free(functions.descriptors);
        return application_fail(error, QA_ERROR_ARGUMENT, "Composed source import requires its isolated uninitialized executor");
    }
    qa_qvm_binding *bindings = functions.count ? calloc(functions.count, sizeof(*bindings)) : NULL;
    if (functions.count && !bindings) {
        free(functions.descriptors);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining complete saved source callback identities");
    }
    application_guest_input_saved input = {0};
    application_q3_equipment_saved state = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, role->engine->provider->application->session, bytes, error) && signature(&io);
    size_t length = 0;
    if (ok) ok = qa_source_save_count(&io, &length, io.input.size - io.offset);
    qa_bytes input_bytes = {0};
    if (ok && length > io.input.size - io.offset)
        ok = application_fail(error, QA_ERROR_FORMAT, "Truncated real input continuation in composed source record");
    if (ok) {
        input_bytes = (qa_bytes){io.input.data + io.offset, length};
        io.offset += length;
        ok = application_guest_input_prepare_restore(role, input_bytes, &input, error);
    }
    if (ok && input.binding_count != functions.input_count)
        ok = application_fail(error, QA_ERROR_FORMAT, "Composed input identities differ from their actual constructor");
    if (ok && functions.input_count)
        memcpy(bindings, input.bindings, functions.input_count * sizeof(*bindings));
    bool present = false;
    size_t count = 0;
    if (ok) ok = qa_source_save_bool(&io, &present) &&
        qa_source_save_count(&io, &count, functions.equipment_count);
    if (ok && (present != (role->equipment != NULL) || count != functions.equipment_count))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved equipment callbacks differ from the genuine artifact constructor");
    for (size_t i = 0; ok && i < count; ++i)
        ok = qa_source_save_u64(&io, bindings + functions.input_count + i);
    if (ok) ok = state_fields(&io, &state) && qa_source_save_finish(&io, NULL) &&
        application_q3_equipment_state_qualify(role->equipment, &state, error) &&
        qa_qvm_restore_candidate_bindings(role->vm, executor, functions.descriptors,
            bindings, functions.count, error);
    if (ok) {
        application_guest_input_adopt_restore(role, &input);
        application_q3_equipment_adopt(role->equipment,
            functions.equipment_count ? bindings + functions.input_count : NULL);
        application_q3_equipment_state_adopt(role->equipment, &state);
    }
    qa_source_save_dispose(&io);
    free(bindings); free(functions.descriptors);
    return ok;
}

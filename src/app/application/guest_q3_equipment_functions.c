#include "guest_q3_equipment_functions.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include "qa/source_save.h"

static bool signature(qa_source_save_io *io)
{
    uint8_t magic[8] = {'Q','A','C','3','F','N',0,0};
    const uint8_t expected[8] = {'Q','A','C','3','F','N',0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic))) return false;
    return !memcmp(magic, expected, sizeof(magic)) ||
        application_fail(io->error, QA_ERROR_FORMAT, "Invalid CGAME equipment callback continuation");
}

static bool state_fields(qa_source_save_io *io, application_q3_equipment_saved *state)
{
    uint32_t warning = state->draw.warning;
    bool okay = qa_source_save_actor(io, &state->draw.actor) &&
        qa_source_save_u32(io, &warning) && qa_source_save_bool(io, &state->draw.selected) &&
        qa_source_save_bool(io, &state->draw.view_visible) &&
        qa_source_save_bool(io, &state->hud_requested) && qa_source_save_bool(io, &state->view_requested);
    if (okay && warning > QA_AMMO_EMPTY)
        return application_fail(io->error, QA_ERROR_FORMAT, "CGAME equipment warning leaves its real enum");
    if (okay && io->direction == QA_SOURCE_SAVE_READ) state->draw.warning = (qa_ammo_warning)warning;
    return okay;
}

static bool inventory(qa_qvm *vm, application_q3_equipment *equipment,
    qa_qvm_saved_function **out, size_t *count, qa_error *error)
{
    if (!vm || qa_qvm_get_role(vm) != QA_QVM_CGAME || !out || !count)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME equipment inventory requires its actual executor");
    size_t length = application_q3_equipment_descriptor_count(equipment);
    if (length > SIZE_MAX / sizeof(**out))
        return application_fail(error, QA_ERROR_MEMORY, "CGAME equipment inventory exceeds address space");
    qa_qvm_saved_function *functions = length ? calloc(length, sizeof(*functions)) : NULL;
    if (length && !functions)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining complete CGAME equipment inventory");
    if (!application_q3_equipment_descriptors(equipment, functions, length, error) ||
        !qa_qvm_checkpoint_functions(vm, functions, length, error)) {
        free(functions); return false;
    }
    *out = functions; *count = length; return true;
}

bool application_q3_equipment_functions_checkpoint(qa_session *session, qa_qvm *vm,
    application_q3_equipment *equipment, qa_buffer *out, qa_error *error)
{
    qa_qvm_saved_function *functions = NULL; size_t count = 0;
    application_q3_equipment_saved state = {0}; qa_source_save_io io = {0};
    if (!session || !out || out->data || out->size ||
        !application_q3_equipment_executor(equipment, session, vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME equipment capture lost its actual module/session");
    if (!inventory(vm, equipment, &functions, &count, error)) return false;
    bool present = equipment != NULL;
    bool okay = application_q3_equipment_state_read(equipment, &state, error) &&
        qa_source_save_writer(&io, session, error) && signature(&io) &&
        qa_source_save_bool(&io, &present) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; okay && i < count; ++i) okay = qa_source_save_u64(&io, &functions[i].binding);
    if (okay) okay = state_fields(&io, &state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(functions); return okay;
}

bool application_q3_equipment_functions_restore(qa_session *session, qa_qvm *vm,
    application_q3_equipment *equipment, qa_bytes bytes, qa_bytes executor, qa_error *error)
{
    qa_qvm_saved_function *functions = NULL; size_t constructed_count = 0;
    if (!session || !application_q3_equipment_executor(equipment, session, vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME equipment import lost its actual module/session");
    if (!inventory(vm, equipment, &functions, &constructed_count, error)) return false;
    qa_qvm_binding *bindings = constructed_count ? calloc(constructed_count, sizeof(*bindings)) : NULL;
    if (constructed_count && !bindings) {
        free(functions);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining saved CGAME equipment bindings");
    }
    application_q3_equipment_saved state = {0}; qa_source_save_io io = {0};
    bool present = false; size_t count = 0;
    bool okay = qa_source_save_reader(&io, session, bytes, error) && signature(&io) &&
        qa_source_save_bool(&io, &present) && qa_source_save_count(&io, &count, constructed_count);
    if (okay && (present != (equipment != NULL) || count != constructed_count))
        okay = application_fail(error, QA_ERROR_FORMAT, "CGAME equipment inventory differs from its admitted artifact");
    for (size_t i = 0; okay && i < count; ++i) okay = qa_source_save_u64(&io, &bindings[i]);
    if (okay) okay = state_fields(&io, &state) && qa_source_save_finish(&io, NULL) &&
        application_q3_equipment_state_qualify(equipment, &state, error) &&
        qa_qvm_restore_candidate_bindings(vm, executor, functions, bindings, constructed_count, error);
    if (okay) {
        application_q3_equipment_adopt(equipment, bindings);
        application_q3_equipment_state_adopt(equipment, &state);
    }
    qa_source_save_dispose(&io); free(bindings); free(functions); return okay;
}

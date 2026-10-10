#include "native_q3_client_modules_private.h"
#include "guest_q3_body_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

typedef struct client_functions {
    qa_qvm_saved_function *descriptors;
    size_t equipment, body, count;
} client_functions;

static bool inventory(native_client_module *role, client_functions *out, qa_error *error)
{
    if (!role || role->kind != QA_QVM_CGAME || !role->vm ||
        !application_q3_equipment_executor(role->equipment, role->owner->app->session, role->vm) ||
        !application_q3_body_executor(role->body, role->owner->app->session, role->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME callbacks require their actual retained executor");
    client_functions value = {.equipment=application_q3_equipment_descriptor_count(role->equipment),
        .body=application_q3_body_descriptor_count(role->body)};
    if (value.equipment > SIZE_MAX / sizeof(*value.descriptors) ||
        value.body > SIZE_MAX / sizeof(*value.descriptors) - value.equipment)
        return application_fail(error, QA_ERROR_MEMORY, "CGAME callback inventory exceeds address space");
    value.count = value.equipment + value.body;
    value.descriptors = value.count ? calloc(value.count, sizeof(*value.descriptors)) : NULL;
    if (value.count && !value.descriptors)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining complete acquired CGAME callbacks");
    if (!application_q3_equipment_descriptors(role->equipment, value.descriptors, value.equipment, error) ||
        !application_q3_body_descriptors(role->body,
            value.body ? value.descriptors + value.equipment : NULL, value.body, error)) {
        free(value.descriptors); return false;
    }
    *out = value; return true;
}

static bool signature(qa_source_save_io *io)
{
    char magic[8] = {'Q','A','N','3','F','N',0,0};
    const char expected[8] = {'Q','A','N','3','F','N',0,0};
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        (!memcmp(magic, expected, sizeof(magic)) ||
            application_fail(io->error, QA_ERROR_FORMAT, "Invalid complete acquired CGAME callback continuation"));
}

static bool child(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t length = bytes->size;
    if (!qa_source_save_count(io, &length, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated CGAME body callback continuation");
        *bytes = (qa_bytes){io->input.data + io->offset, length}; io->offset += length; return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, length);
}

static bool state_fields(qa_source_save_io *io, application_q3_equipment_saved *state)
{
    uint32_t warning = state->draw.warning;
    bool okay = qa_source_save_actor(io, &state->draw.actor) && qa_source_save_u32(io, &warning) &&
        qa_source_save_bool(io, &state->draw.selected) && qa_source_save_bool(io, &state->draw.view_visible) &&
        qa_source_save_bool(io, &state->hud_requested) && qa_source_save_bool(io, &state->view_requested);
    if (okay && warning > QA_AMMO_EMPTY)
        return application_fail(io->error, QA_ERROR_FORMAT, "CGAME equipment warning leaves its actual enum");
    if (okay && io->direction == QA_SOURCE_SAVE_READ) state->draw.warning = (qa_ammo_warning)warning;
    return okay;
}

bool native_client_module_functions_checkpoint(native_client_module *role, qa_buffer *out, qa_error *error)
{
    client_functions functions = {0};
    if (!out || out->data || out->size || !inventory(role, &functions, error)) return false;
    application_q3_equipment_saved state = {0};
    qa_buffer body = {0}; qa_source_save_io io = {0};
    bool present = role->equipment != NULL;
    bool okay = qa_qvm_checkpoint_functions(role->vm, functions.descriptors, functions.count, error) &&
        application_q3_body_checkpoint(role->body, &body, error) &&
        application_q3_equipment_state_read(role->equipment, &state, error) &&
        qa_source_save_writer(&io, role->owner->app->session, error) && signature(&io);
    qa_bytes body_bytes = {body.data, body.size};
    if (okay) okay = child(&io, &body_bytes) && qa_source_save_bool(&io, &present) &&
        qa_source_save_count(&io, &functions.equipment, SIZE_MAX);
    for (size_t i = 0; okay && i < functions.equipment; ++i)
        okay = qa_source_save_u64(&io, &functions.descriptors[i].binding);
    if (okay) okay = state_fields(&io, &state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&body); free(functions.descriptors); return okay;
}

bool native_client_module_functions_restore(native_client_module *role, qa_bytes bytes,
    qa_bytes executor, qa_error *error)
{
    if (!role || role->kind != QA_QVM_CGAME || !role->owner->restore_pending || role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME callback import requires its isolated actual executor");
    qa_source_save_io io = {0}; qa_bytes body_bytes = {0};
    application_q3_body_saved body = {0};
    bool okay = qa_source_save_reader(&io, role->owner->app->session, bytes, error) && signature(&io) &&
        child(&io, &body_bytes) && application_q3_body_saved_read(
            role->body ? &role->body_profile : NULL, body_bytes, &body, error);
    if (okay && role->body) {
        okay = application_q3_body_destroy(role->body, error);
        if (okay) {
            role->body = NULL;
            application_q3_body_module module = {.session=role->owner->app->session,
                .vm=role->vm, .image=role->image, .profile=&role->body_profile,
                .receiver=role->owner->source.receiver.receiver, .seat=role->owner->source.receiver.seat,
                .client=role->client};
            okay = application_q3_body_create_module(&module, &role->body_services, &body, &role->body, error);
        }
    }
    client_functions functions = {0};
    if (okay) okay = inventory(role, &functions, error);
    qa_qvm_binding *bindings = okay && functions.count ? calloc(functions.count, sizeof(*bindings)) : NULL;
    if (okay && functions.count && !bindings)
        okay = application_fail(error, QA_ERROR_MEMORY, "Retaining complete saved CGAME callback identities");
    bool present = false; size_t count = 0;
    application_q3_equipment_saved state = {0};
    if (okay) okay = qa_source_save_bool(&io, &present) &&
        qa_source_save_count(&io, &count, functions.equipment);
    if (okay && (present != (role->equipment != NULL) || count != functions.equipment || body.count != functions.body))
        okay = application_fail(error, QA_ERROR_FORMAT, "CGAME callbacks differ from their actual artifact constructors");
    for (size_t i = 0; okay && i < count; ++i) okay = qa_source_save_u64(&io, bindings + i);
    if (okay && body.count) memcpy(bindings + functions.equipment, body.bindings, body.count * sizeof(*bindings));
    if (okay) okay = state_fields(&io, &state) && qa_source_save_finish(&io, NULL) &&
        application_q3_equipment_state_qualify(role->equipment, &state, error) &&
        qa_qvm_restore_candidate_bindings(role->vm, executor, functions.descriptors, bindings, functions.count, error);
    if (okay) {
        application_q3_equipment_adopt(role->equipment, functions.equipment ? bindings : NULL);
        application_q3_equipment_state_adopt(role->equipment, &state);
        application_q3_body_adopt(role->body, functions.body ? bindings + functions.equipment : NULL);
    }
    application_q3_body_saved_free(&body); qa_source_save_dispose(&io);
    free(bindings); free(functions.descriptors); return okay;
}

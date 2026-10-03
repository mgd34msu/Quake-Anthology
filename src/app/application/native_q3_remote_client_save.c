#include "native_q3_remote_client.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

static bool value_fields(qa_source_save_io *io, qa_native_q3_client_cvar *value)
{
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(value->value) : 0;
    if (!qa_source_save_count(io, &length, sizeof(value->value) - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) memset(value->value, 0, sizeof(value->value));
    if (!qa_source_save_bytes(io, value->value, length) || memchr(value->value, 0, length)) return false;
    value->value[length] = 0;
    return qa_source_save_f32(io, &value->number) && qa_source_save_i32(io, &value->integer) &&
        qa_source_save_u64(io, &value->modification_count);
}
static bool fields(qa_source_save_io *io, qa_native_q3_remote_client_service *service)
{
    uint8_t magic[4] = {'Q','N','R','C'}; uint32_t product = service->services.basis.product;
    uint32_t physical = service->services.basis.physical_client;
    int32_t initial_message = service->services.basis.initial_message, initial_command = service->services.basis.initial_command;
    size_t count = native_client_definition_count;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QNRC", 4) ||
        !qa_source_save_u32(io, &product) || product != (uint32_t)service->services.basis.product ||
        !qa_source_save_u32(io, &physical) || physical != service->services.basis.physical_client ||
        !qa_source_save_i32(io, &initial_message) || initial_message != service->services.basis.initial_message ||
        !qa_source_save_i32(io, &initial_command) || initial_command != service->services.basis.initial_command ||
        !qa_source_save_count(io, &count, QA_NATIVE_CLIENT_CVARS) || count != native_client_definition_count ||
        !qa_source_save_bool(io, &service->registered) || !qa_source_save_bool(io, &service->services.basis.client.initialized) ||
        (service->services.basis.client.initialized && !service->registered) ||
        !qa_source_save_u64(io, &service->cache_revision) || !qa_source_save_u64(io, &service->force_model_count) ||
        !qa_source_save_u64(io, &service->overlay_count) || !qa_source_save_bool(io, &service->overlay_initial) ||
        !qa_source_save_i32(io, &service->local_server)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!value_fields(io, &service->cache[i])) return false;
        if (native_client_definitions[i].missionpack && service->services.basis.product != QA_Q3_TEAM_ARENA) {
            const qa_native_q3_client_cvar *value = &service->cache[i];
            if (*value->value || value->number != 0 || value->integer || value->modification_count) return false;
        }
    }
    bool present = service->system_info != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (present) {
        size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(service->system_info) : 0;
        if (!qa_source_save_count(io, &length, QA_Q3_BIG_INFO_CHARS - 1)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            service->system_info = malloc(length + 1); if (!service->system_info) return false;
        }
        if (!qa_source_save_bytes(io, service->system_info, length) || memchr(service->system_info, 0, length)) return false;
        service->system_info[length] = 0;
    }
    return true;
}
bool qa_native_q3_remote_client_checkpoint(const qa_native_q3_remote_client_service *service, qa_buffer *out, qa_error *error)
{
    if (!service || !out || out->data || out->size || !qa_native_q3_remote_client_current(service) ||
        !qa_native_q3_remote_client_idle(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native CGAME capture requires its actual idle received graph");
    qa_native_q3_remote_client_service state = *service; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_native_q3_remote_client_restore(qa_native_q3_remote_client_services *services,
    qa_native_q3_character_selection *character, qa_bytes bytes, qa_native_q3_remote_client_service **out, qa_error *error)
{
    qa_native_q3_remote_client_service *service = NULL;
    if (!out || *out || !native_remote_client_allocate(services, character, &service, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, service) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && service->registered) for (size_t i = 0; ok && i < native_client_definition_count; ++i)
        if (!native_client_definitions[i].missionpack || service->services.basis.product == QA_Q3_TEAM_ARENA)
            ok = qa_cvars_find(service->services.basis.client.cvars, native_client_definitions[i].name) != NULL;
    bool initialized = service->services.basis.client.initialized;
    service->services.basis.client.initialized = services->basis.client.initialized;
    if (ok) ok = native_remote_client_commit(service, error);
    if (ok && initialized) ok = application_native_q3_remote_role_initialized(service->provider,
        service->services.basis.client.seat, service, error);
    if (!ok) {
        if (service->attached) application_native_q3_remote_role_detach(service->provider, service->services.basis.client.seat, service, NULL);
        free(service->system_info); qa_launch_instance_lease_release(service->descriptor); free(service);
        if (!error || error->code == QA_OK) native_client_fail(error, QA_ERROR_FORMAT, "Invalid remote native CGAME continuation");
        return false;
    }
    service->services.basis.client.initialized = initialized;
    *services = (qa_native_q3_remote_client_services){0}; *character = (qa_native_q3_character_selection){0}; *out = service; return true;
}

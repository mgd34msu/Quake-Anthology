#include "guest_q3_private.h"
#include "guest_qc_internal.h"

bool qa_application_guest_source_actor(qa_application *app, qa_actor_owner owner,
                                        uint32_t seat, int32_t number,
                                        qa_actor_id *out, qa_error *error)
{
    if (!app || !owner || !out || number < 0 || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid source actor observation");
    *out = (qa_actor_id){0};
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == owner && app->providers[i]->attached) {
            provider = app->providers[i]; break;
        }
    if (!provider) return application_fail(error, QA_ERROR_NOT_FOUND, "Source actor owner is retired");
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        qa_qc_slot_binding binding;
        if (qa_qc_slot(provider->state.qc.instance, (uint32_t)number, &binding) &&
            binding.kind != QA_QC_SLOT_FREE) *out = binding.actor;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        qa_q3_host *host = NULL;
        bool external_client = false;
        if (engine)
            for (q3g_role *role = engine->roles; role; role = role->next)
                if (role->kind == QA_QVM_CGAME && role->seat == seat &&
                    role->ready && !role->retired) {
                    external_client = true;
                    bool present = false;
                    if (role->client_services.source_actor &&
                        !role->client_services.source_actor(role->client_services.context,
                            (uint32_t)number, out, &present, error)) return false;
                    if (!present) *out = (qa_actor_id){0};
                    break;
                }
        if (!external_client && engine && engine->game)
            host = engine->game->host;
        if (host) {
            qa_q3_host_game_data data;
            if (qa_q3_host_game_data_read(host, &data) && (uint32_t)number < data.entity_count &&
                !qa_q3_host_actor(host, (uint32_t)number, false, out, error)) return false;
        } else if (!engine && provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.host) {
            qa_native_slot_binding binding;
            qa_native_entity_table table;
            qa_native_instance *instance = qa_native_host_instance(provider->state.native.host);
            if (!qa_native_entity_table_get(instance, &table, error)) return false;
            if ((uint32_t)number < table.capacity) {
                if (!qa_native_slot(instance, (uint32_t)number, &binding, error)) return false;
                if (binding.kind != QA_NATIVE_SLOT_FREE) *out = binding.actor;
            }
        }
    }
    if (!out->registry || !qa_actors_get(qa_session_actors(app->session), *out))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Source actor has no live canonical projection");
    return true;
}

#include "guest_native_q2_private.h"
#include "map_players_private.h"
#include "qa/application_native_q2_client.h"

bool qa_application_native_q2_presentation_local(qa_application *app,
    const qa_application_native_q2_presentation *source, uint32_t seat,
    qa_application_native_q2_client *out, bool *found, qa_error *error)
{
    if (!out || !found || !qa_application_native_q2_presentation_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 local recipient requires its completed physical GAME source");
    *found = false;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!app->players) return true;
    if (!provider || app->players->map_provider != provider || provider->owner != source->source_owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 local recipient lost its actual world player roster");
    qa_application_native_q2_client client = {0};
    bool present = false;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = &app->players->records[i];
        if (row->seat != seat || row->retiring || row->remote || row->bot || row->source_begin_pending ||
            !qa_actors_get(qa_session_actors(app->session), row->actor)) continue;
        if (source->kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
            qa_builtin_player_info physical;
            if (provider->kind != APPLICATION_PROVIDER_Q2 || provider->state.q2 != source->source.game ||
                !qa_q2_player_projection(provider->state.q2, row->actor, &physical) ||
                !physical.connected || physical.slot != row->client_slot)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q2 local recipient differs from its physical GAME client");
        } else {
            const struct application_native_q2 *engine = provider->state.native.q2_engine;
            if (provider->kind != APPLICATION_PROVIDER_NATIVE || !engine ||
                provider->state.native.host != source->source.original.host || row->client_slot >= 256)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q2 local recipient lost its original GAME source");
            uint32_t slot = row->client_slot + 1;
            const application_native_q2_client *physical = engine->clients + slot;
            qa_native_slot_binding binding;
            if (!physical->connected || !physical->begun || physical->bot || physical->disconnect_started)
                continue;
            if (!qa_actor_id_equal(physical->actor, row->actor) ||
                !qa_native_slot(qa_native_host_instance(provider->state.native.host), slot, &binding, error) ||
                binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, row->actor))
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q2 local recipient differs from its full original source binding");
        }
        if (present)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 local recipient repeats its actual viewing seat");
        client = (qa_application_native_q2_client){row->actor, seat, row->client_slot};
        present = true;
    }
    if (!qa_application_native_q2_presentation_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 local recipient left its completed source frame");
    if (present) { *out = client; *found = true; }
    return true;
}

#include "native_q3_wire.h"
#include "qa/application_native_q3_visibility.h"

bool qa_application_native_q3_presentation_visible(qa_application *app,
    const qa_application_native_q3_presentation *source, uint32_t seat,
    qa_application_native_q3_view *out, bool *found, qa_error *error)
{
    if (!out || !found || !qa_application_native_q3_presentation_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Native Q3 visibility requires its completed source cut");
    *found = false;
    qa_application_native_q3_view value = {0};
    bool local;
    if (!qa_application_native_q3_presentation_local(app, source, seat,
        &value.physical_client, &value.actor, &value.player, &local, error)) return false;
    if (!local) return true;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider || provider->owner != source->source_owner ||
        provider->kind != APPLICATION_PROVIDER_Q3 || provider->state.q3 != source->source_game)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Native Q3 visibility lost its selected physical GAME");
    if (!application_native_q3_wire_current_view(provider, value.physical_client,
        &value.player, &value.visible, error)) return false;
    uint32_t physical;
    qa_actor_id actor;
    qa_q3_player player;
    if (!qa_application_native_q3_presentation_local(app, source, seat,
        &physical, &actor, &player, &local, error) || !local ||
        physical != value.physical_client || !qa_actor_id_equal(actor, value.actor) ||
        player.clientNum != value.player.clientNum)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Native Q3 visibility changed its actual local recipient");
    *out = value;
    *found = true;
    return true;
}

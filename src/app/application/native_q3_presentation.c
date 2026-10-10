#include "native_q3_presentation.h"
#include "map_players_private.h"
#include "native_q3_settings.h"
#include "native_q3_wire.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_configstrings.h"

static application_provider *source(qa_application *app, qa_actor_owner owner)
{
    if (!app || !owner || !app->session || !app->world || app->destroy_requested ||
        app->state != QA_APPLICATION_RUNNING || app->q3_round_active || app->q3_world_restart ||
        app->routing_snapshot || !app->map_view_ready ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world))
        return NULL;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    return provider && provider->application == app && provider->owner == owner &&
        provider->kind == APPLICATION_PROVIDER_Q3 && provider->state.q3 && provider->native_q3_wire && provider->map_bound &&
        provider->constructed && provider->attached && !provider->close_pending &&
        provider->launch && provider->launch->content && provider->product &&
        provider->product->family == QA_GAME_Q3 && application_native_q3_wire_idle(provider)
        ? provider : NULL;
}

bool qa_application_native_q3_presentation_read(qa_application *app, qa_actor_owner owner,
    qa_application_native_q3_presentation *out, qa_error *error)
{
    application_provider *provider = source(app, owner);
    if (!provider || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 presentation requires its exact completed GAME owner");
    qa_clock_state clock;
    int32_t wire_time;
    qa_application_native_q3_presentation view = {.session = app->session,
        .source_game = provider->state.q3, .publication = qa_application_launch(app),
        .launch = provider->launch, .content = provider->launch->content,
        .source_owner = owner, .content_product = provider->launch->selection.product,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision};
    if (!view.publication || !qa_session_clock(app->session, owner, &clock) ||
        !application_q3_wire_time(provider, &wire_time, error) ||
        !qa_q3_source_clock(view.source_game, &view.source_time_ms, error) ||
        !qa_q3_source_match_context_read(view.source_game, &view.product, &view.match_start_time_ms, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &view.game_type, error) ||
        !qa_q3_source_max_clients(view.source_game, &view.max_clients, error) ||
        !qa_q3_source_entity_count(view.source_game, &view.entity_count, error))
        return false;
    if ((clock.frame.number && view.source_time_ms != wire_time) || !view.max_clients || view.max_clients > QA_Q3_SOURCE_CLIENTS ||
        view.entity_count > QA_Q3_SOURCE_NONE)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 presentation source clock or physical extent disagrees");
    view.source_frame = clock.frame;
    *out = view;
    return true;
}

bool qa_application_native_q3_presentation_selected(qa_application *app,
    qa_application_native_q3_presentation *out, bool *found, qa_error *error)
{
    if (!app || !out || !found || app->destroy_requested || !qa_application_launch(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 source discovery requires its actual published application");
    *found = false;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3) return true;
    if (!qa_application_native_q3_presentation_read(app, provider->owner, out, error)) return false;
    *found = true;
    return true;
}

bool qa_application_native_q3_presentation_current(qa_application *app,
    const qa_application_native_q3_presentation *saved)
{
    qa_application_native_q3_presentation actual;
    return saved && qa_application_native_q3_presentation_read(app, saved->source_owner, &actual, NULL) &&
        actual.session == saved->session && actual.source_game == saved->source_game &&
        actual.publication == saved->publication && actual.launch == saved->launch &&
        actual.content == saved->content && actual.content_product == saved->content_product &&
        actual.product == saved->product && actual.publication_generation == saved->publication_generation &&
        actual.map_revision == saved->map_revision && actual.source_time_ms == saved->source_time_ms &&
        actual.match_start_time_ms == saved->match_start_time_ms && actual.game_type == saved->game_type &&
        actual.max_clients == saved->max_clients && actual.entity_count == saved->entity_count &&
        actual.source_frame.provider == saved->source_frame.provider &&
        actual.source_frame.kind == saved->source_frame.kind && actual.source_frame.phase == saved->source_frame.phase &&
        actual.source_frame.number == saved->source_frame.number &&
        actual.source_frame.start_ns == saved->source_frame.start_ns &&
        actual.source_frame.time_ns == saved->source_frame.time_ns &&
        actual.source_frame.elapsed_ns == saved->source_frame.elapsed_ns;
}

static bool binding_current(qa_application *app, const qa_application_native_q3_presentation *view,
    uint32_t slot, qa_actor_id actor, qa_error *error)
{
    qa_q3_source_binding actual;
    uint32_t physical;
    if (!qa_application_native_q3_presentation_current(app, view) ||
        !qa_q3_source_binding_read(view->source_game, slot, &actual, error) ||
        !qa_actor_id_equal(actual.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 presentation left its exact source cut or actor generation");
    if (!actor.registry) return !actual.in_use ||
        application_fail(error, QA_ERROR_FORMAT, "Native Q3 active source row has no actual actor");
    return qa_actors_get(qa_session_actors(app->session), actor) &&
        qa_q3_source_actor_slot(view->source_game, actor, &physical, error) && physical == slot;
}

bool qa_application_native_q3_presentation_entity(qa_application *app,
    const qa_application_native_q3_presentation *view, uint32_t slot,
    qa_application_native_q3_entity *out, qa_error *error)
{
    if (!out || !qa_application_native_q3_presentation_current(app, view) || slot >= view->entity_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 entity observation requires its physical source row");
    qa_application_native_q3_entity value = {0};
    if (!qa_q3_source_binding_read(view->source_game, slot, &value.binding, error) ||
        !binding_current(app, view, slot, value.binding.actor, error) ||
        !qa_q3_wire_entity_read(view->source_game, slot, &value.state, &value.visibility, error) ||
        !qa_q3_wire_native_visibility_read(view->source_game, slot, &value.native_visibility, error) ||
        !binding_current(app, view, slot, value.binding.actor, error)) return false;
    value.present = value.binding.in_use && value.visibility.present;
    *out = value;
    return true;
}

bool qa_application_native_q3_presentation_client(qa_application *app,
    const qa_application_native_q3_presentation *view, uint32_t slot,
    qa_application_native_q3_client *out, qa_error *error)
{
    if (!out || !qa_application_native_q3_presentation_current(app, view) || slot >= view->max_clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client observation requires its fixed physical source slot");
    qa_application_native_q3_client value = {0};
    if (!qa_q3_source_binding_read(view->source_game, slot, &value.binding, error) ||
        !qa_q3_client_slot_read(view->source_game, slot, &value.client, error) ||
        !binding_current(app, view, slot, value.binding.actor, error)) return false;
    value.present = value.binding.in_use && value.binding.actor.registry && value.binding.body_attached &&
        value.client.connected == QA_Q3_CLIENT_CONNECTED;
    if (value.present && !qa_q3_wire_player_read(view->source_game, slot, &value.player, error)) return false;
    if (!binding_current(app, view, slot, value.binding.actor, error)) return false;
    *out = value;
    return true;
}

bool qa_application_native_q3_presentation_configstring(qa_application *app,
    const qa_application_native_q3_presentation *view, uint32_t index,
    const char **text, uint64_t *revision, qa_error *error)
{
    if (!text || !revision || !qa_application_native_q3_presentation_current(app, view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 configstring observation requires its current GAME cut");
    const char *value;
    uint64_t identity;
    if (!qa_q3_configstring_read(view->source_game, index, &value, error) ||
        !qa_q3_configstring_revision(view->source_game, index, &identity, error) ||
        !qa_application_native_q3_presentation_current(app, view)) return false;
    *text = value; *revision = identity;
    return true;
}

bool qa_application_native_q3_presentation_local(qa_application *app,
    const qa_application_native_q3_presentation *view, uint32_t seat,
    uint32_t *slot, qa_actor_id *actor, qa_q3_player *player, bool *found, qa_error *error)
{
    if (!slot || !actor || !player || !found || !qa_application_native_q3_presentation_current(app, view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 viewing seat requires its current physical GAME cut");
    *found = false;
    if (!app->players) return true;
    application_provider *provider = source(app, view->source_owner);
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = &app->players->records[i];
        if (row->seat != seat || row->retiring || row->remote || application_player_identity(row)->bot ||
            !qa_actors_get(qa_session_actors(app->session), row->actor)) continue;
        uint32_t physical;
        application_native_q3_wire_client_view wire;
        bool admitted;
        if (!qa_q3_native_client_slot(view->source_game, row->actor, &physical, error) ||
            physical != row->client_slot ||
            !application_native_q3_wire_client_read(provider, physical, &wire, &admitted, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 viewing seat differs from its physical source client");
        if (!admitted || !wire.begun || wire.bot || row->source_begin_pending) continue;
        if (!qa_actor_id_equal(wire.actor, row->actor) || wire.seat != seat || *found)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 viewing seat has ambiguous source admission");
        qa_application_native_q3_client client;
        if (!qa_application_native_q3_presentation_client(app, view, physical, &client, error)) return false;
        if (!client.present) continue;
        *slot = physical; *actor = row->actor; *player = client.player; *found = true;
    }
    return qa_application_native_q3_presentation_current(app, view) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 viewing seat changed during observation");
}

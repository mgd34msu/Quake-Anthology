#include "equipment_gear_world.h"

static bool state(qa_application *app, qa_actor_id actor, qa_equipment_state *out)
{
    return app && app->session && app->equipment && !app->destroy_requested &&
        qa_actors_get(qa_session_actors(app->session), actor) &&
        qa_equipment_read(app->equipment, actor, out) && out->selection.grapple == QA_GRAPPLE_Q3 &&
        application_equipment_runtime_owner_current(app->equipment_runtime, out->sources.grapple);
}
bool application_equipment_gear_world_current(qa_application *app,
    const application_equipment_gear_world_view *view)
{
    qa_equipment_state selected;
    if (!view || !state(app, view->source.actor, &selected) ||
        selected.sources.grapple != view->source.source.selected_owner ||
        view->offhand != (selected.selection.binding == QA_EQUIPMENT_OFFHAND) ||
        app->publication_generation != view->source.publication_generation || app->map_revision != view->source.map_revision ||
        !view->source.gear.tether.registry || !qa_actor_id_equal(view->source.actor, view->source.gear.actor) ||
        view->source.gear.definition != view->source.source.definition) return false;
    const qa_actor_record *tether = qa_actors_get(qa_session_actors(app->session), view->source.gear.tether);
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!tether || tether->owner != view->source.source.gear_owner || !primary || !primary->constructed ||
        !primary->attached || primary->close_pending || primary->owner != view->source.primary) return false;
    return view->source.source.gear &&
        application_equipment_runtime_source_current(app->equipment_runtime, &view->source.source);
}
bool application_equipment_gear_world_read(qa_application *app, qa_actor_id actor,
    application_equipment_gear_world_view *out, bool *visible, qa_error *error)
{
    if (!app || !out || !visible || !app->session || !app->world || app->destroy_requested ||
        !qa_actors_get(qa_session_actors(app->session), actor) || app->state != QA_APPLICATION_RUNNING ||
        !app->map_view_ready || app->routing_snapshot || app->q3_round_active || app->q3_world_restart ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear world observation requires its completed physical source world");
    *visible = false;
    qa_equipment_state selected;
    if (!app->equipment || !qa_equipment_read(app->equipment, actor, &selected) ||
        selected.selection.grapple != QA_GRAPPLE_Q3) return true;
    if (!state(app, actor, &selected))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear world observation lost its actual selected source");
    application_equipment_gear_world_view view = {.source = {.actor = actor,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision},
        .offhand = selected.selection.binding == QA_EQUIPMENT_OFFHAND};
    if (!application_equipment_runtime_source_read(app->equipment_runtime, selected.sources.grapple,
        &view.source.source, error)) return false;
    /* Native gear retains its separate native world presentation producer. */
    if (!view.source.source.gear) return true;
    if (!application_q3_gear_read(view.source.source.gear, actor, &view.source.gear, error)) return false;
    if (!view.source.gear.tether.registry) return true;
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_player_state control;
    if (!primary)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear world observation lost its authoritative primary owner");
    view.source.primary = primary->owner;
    if (!qa_application_control_read(app, actor, &control) ||
        !qa_world_body_read(app->world, actor, &view.player_body, error) ||
        !qa_world_body_read(app->world, view.source.gear.tether, &view.tether_body, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear world observation lost its real player or owned hook body");
    view.player_angles = control.view_angles; view.view_height = control.view_height;
    if (!application_equipment_gear_world_current(app, &view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear world observation changed its full actor or retained source");
    *out = view; *visible = true; return true;
}

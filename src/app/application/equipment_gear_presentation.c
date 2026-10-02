#include "equipment_gear_presentation.h"

static bool selected_state(const qa_application *app, qa_actor_id actor,
    qa_equipment_state *state)
{
    return app && app->session && app->equipment &&
        !app->destroy_requested && qa_actors_get(qa_session_actors(app->session), actor) &&
        qa_equipment_read(app->equipment, actor, state) &&
        state->selection.grapple == QA_GRAPPLE_Q3 &&
        state->selection.binding == QA_EQUIPMENT_WEAPON_SLOT &&
        (state->weapon_slot_present?qa_equipment_weapon_presented(app->equipment,actor,state->sources.grapple):state->slot_active);
}

bool application_equipment_gear_presentation_current(qa_application *app,
    const application_equipment_gear_presentation *view)
{
    qa_equipment_state state;
    if (!view || !selected_state(app, view->actor, &state) ||
        !application_equipment_runtime_owner_current(app->equipment_runtime, state.sources.grapple) ||
        state.sources.grapple != view->source.selected_owner ||
        app->publication_generation != view->publication_generation ||
        app->map_revision != view->map_revision || !view->source.gear || !view->source.definition ||
        !qa_actor_id_equal(view->actor, view->gear.actor) ||
        view->gear.definition != view->source.definition) return false;
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!primary || !primary->constructed || !primary->attached || primary->close_pending ||
        primary->owner != view->primary) return false;
    for (size_t i = 0; i < application_equipment_runtime_source_count(app->equipment_runtime); ++i) {
        application_equipment_runtime_source source;
        if (!application_equipment_runtime_source_at(app->equipment_runtime, i, &source, NULL)) return false;
        if (source.selected_owner == state.sources.grapple)
            return source.gear == view->source.gear && source.gear_owner == view->source.gear_owner &&
                source.weapon_item == view->source.weapon_item &&
                source.service_owner == view->source.service_owner && source.descriptor == view->source.descriptor &&
                source.definition == view->source.definition && source.artifact == view->source.artifact &&
                source.content == view->source.content && source.acquisition == view->source.acquisition;
    }
    return false;
}

bool application_equipment_gear_presentation_read(qa_application *app, qa_actor_id actor,
    application_equipment_gear_presentation *out, bool *selected, qa_error *error)
{
    if (!app || !out || !selected || !app->session || !app->world ||
        !qa_actors_get(qa_session_actors(app->session), actor) || app->destroy_requested ||
        app->state != QA_APPLICATION_RUNNING || !app->map_view_ready || app->routing_snapshot ||
        app->q3_round_active || app->q3_world_restart ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear presentation requires its actual completed source world");
    *selected = false;
    qa_equipment_state state;
    if (!selected_state(app, actor, &state)) return true;
    if (!application_equipment_runtime_owner_current(app->equipment_runtime, state.sources.grapple))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Active gear slot lost its actual retained source");
    application_equipment_gear_presentation view = {.actor = actor,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision};
    for (size_t i = 0; i < application_equipment_runtime_source_count(app->equipment_runtime); ++i) {
        application_equipment_runtime_source source;
        if (!application_equipment_runtime_source_at(app->equipment_runtime, i, &source, error)) return false;
        if (source.selected_owner == state.sources.grapple) { view.source = source; break; }
    }
    /* Native Q3 owns its genuine native weapon state and has no QVM profile. */
    if (!view.source.gear) return true;
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!primary || !primary->constructed || !primary->attached || primary->close_pending ||
        !view.source.definition || !view.source.artifact || !view.source.content || !view.source.acquisition)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear presentation lost its actual retained profile/content");
    view.primary = primary->owner;
    if (!application_q3_gear_read(view.source.gear, actor, &view.gear, error) ||
        !application_equipment_gear_presentation_current(app, &view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear presentation changed its full actor or source owner");
    *out = view; *selected = true; return true;
}

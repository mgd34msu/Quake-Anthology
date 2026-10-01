#include "source_effects.h"
#include "selected_effects.h"
#include "qa/application_character_selection.h"
#include "qa/ui_preferences.h"
#include "qa/world.h"
#include <stdlib.h>

struct frontend_source_effects {
    qa_frontend *frontend;
    qa_application *application;
    const qa_q3_host *host;
    const qa_qvm_call *call;
    qa_application_q3_client_context client;
    qa_q3_presentation *presentation;
    qa_q3_presentation_binding binding;
    const qa_q3_refdef *definition;
    qa_ui_preferences preferences;
    uint32_t seat, first, reserved;
    size_t groups;
    qa_scene_light *effect_lights, *lights, *projected_lights;
    size_t effect_light_count;
    bool admitted, ready, prepared;
};

bool frontend_source_effects_current(const frontend_source_effects *scope)
{
    return scope && scope->admitted && scope->frontend &&
        scope->application == scope->frontend->application &&
        scope->seat < scope->frontend->options.seats &&
        scope->binding.frame == &scope->frontend->frame &&
        scope->binding.world == scope->frontend->scene_world &&
        qa_application_q3_client_context_current(scope->application, &scope->client) &&
        qa_q3_host_render_scope_current(scope->host, scope->call, scope->client.frontend_lifetime,
            scope->client.service_owner, QA_QVM_CGAME, scope->presentation);
}

bool frontend_source_effects_primary_read(const frontend_source_effects *scope,
    const qa_frontend *frontend, qa_q3_presentation **presentation,
    uint32_t *seat, qa_ui_preferences *preferences)
{
    if (!presentation || !seat || !preferences || !frontend_source_effects_current(scope) ||
        scope->frontend != frontend) return false;
    *presentation = scope->presentation; *seat = scope->seat; *preferences = scope->preferences;
    return true;
}
bool frontend_source_effects_binding_read(const frontend_source_effects *scope,
    const qa_frontend *frontend, qa_q3_presentation_binding *out)
{
    if (!out || !frontend_source_effects_current(scope) || scope->frontend != frontend) return false;
    *out = scope->binding; return true;
}

typedef struct source_pose {
    frontend_source_effects *scope;
    qa_actor_id actor;
    qa_vec3 origin;
} source_pose;

static bool pose_origin(frontend_source_effects *scope, qa_actor_id actor,
    qa_vec3 *origin, bool *found, qa_error *error)
{
    *found = false;
    if (!frontend_source_effects_current(scope)) return false;
    qa_world *world = qa_application_world(scope->application);
    if (!world || !qa_actors_get(qa_world_actors(world), actor) ||
        !qa_world_body_storage_serial(world, actor)) return true;
    qa_body_state body;
    if (!qa_world_body_read(world, actor, &body, error) || !frontend_source_effects_current(scope)) return false;
    *origin = body.origin; *found = true; return true;
}

static bool pose_current(void *context)
{
    source_pose *pose = context; qa_vec3 origin; bool found;
    return pose_origin(pose->scope, pose->actor, &origin, &found, NULL) && found &&
        origin.x == pose->origin.x && origin.y == pose->origin.y && origin.z == pose->origin.z;
}

bool frontend_source_effects_begin(qa_frontend *frontend, const qa_q3_host *host,
    const qa_qvm_call *call, const qa_application_q3_client_context *client,
    qa_q3_presentation *presentation, uint32_t seat, const qa_q3_refdef *definition,
    frontend_source_effects **out, qa_error *error)
{
    if (!frontend || !client || !definition || !out || *out || seat >= frontend->options.seats ||
        (definition->flags & 1) || !client->source_owner || !client->source_actor.registry ||
        !qa_application_q3_client_context_current(frontend->application, client) ||
        !qa_q3_host_render_scope_current(host, call, client->frontend_lifetime,
            client->service_owner, QA_QVM_CGAME, presentation))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects need their actual entered local CGAME role");
    /* The real CGAME Init may render loading views before its successful
     * receipt. Its role bracket remains owned by the caller; no completed
     * world effects sample exists to borrow during that source constructor. */
    if (!client->initialized) return true;
    frontend_source_effects *scope = calloc(1, sizeof(*scope));
    if (!scope) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual source effects render scope");
    *out = scope;
    scope->frontend = frontend; scope->application = frontend->application;
    scope->host = host; scope->call = call; scope->client = *client;
    scope->presentation = presentation; scope->definition = definition; scope->seat = seat;
    uint32_t physical;
    if (!qa_application_constructor_seat_ordinal(scope->application, client->receiver, client->seat, &physical, error) ||
        physical != seat || !qa_q3_presentation_binding_read(presentation, &scope->binding, error) ||
        !scope->binding.world || !scope->binding.geometry || scope->binding.world != frontend->scene_world ||
        scope->binding.frame != &frontend->frame || scope->binding.options.seat != seat ||
        scope->binding.options.audio != frontend->audio ||
        !qa_ui_preferences_read(qa_application_cvars(scope->application), seat, &scope->preferences, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects lost their physical map, preferences or audio binding");
    scope->admitted = true;
    size_t count = qa_application_event_count(scope->application);
    for (size_t i = 0; i < count; ++i) {
        qa_builtin_event queued;
        if (!qa_application_event_at(scope->application, i, &queued) || !frontend_source_effects_current(scope))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects canonical event queue changed during render entry");
        if (queued.family != QA_GAME_Q3 || queued.kind != QA_BUILTIN_ANIMATION) continue;
        qa_application_effect_event event;
        if (!qa_application_effect_event_read(scope->application, i, &event, error)) return false;
        if (event.source.primary != client->source_owner) continue;
        source_pose actual = {.scope = scope, .actor = queued.actor}; bool found;
        if (!pose_origin(scope, queued.actor, &actual.origin, &found, error)) return false;
        if (!found) continue;
        frontend_selected_effects_pose pose = {queued.actor, actual.origin, &actual, pose_current};
        bool admitted;
        if (!frontend_selected_effects_source_event(frontend, scope, &event, &pose, &admitted, error)) return false;
    }
    if (qa_application_event_count(scope->application) != count ||
        !frontend_selected_effects_source_prepare(frontend, scope, error) ||
        !frontend_source_effects_current(scope)) return false;
    scope->groups = frontend_selected_effects_count(frontend);
    for (size_t i = 0; i < scope->groups; ++i) {
        if (!frontend_selected_effects_source_matches(frontend, scope, i)) continue;
        const qa_scene_light *lights; size_t count;
        if (!frontend_selected_effects_source_light_read(frontend, scope, i, &lights, &count, error)) return false;
        if (count > SIZE_MAX / sizeof(*lights) - scope->effect_light_count)
            return frontend_fail(error, QA_ERROR_MEMORY, "Source effect light extent is exhausted");
        if (!count) continue;
        size_t total = scope->effect_light_count + count;
        qa_scene_light *owned = realloc(scope->effect_lights, total * sizeof(*owned));
        if (!owned) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual source effect light packet");
        scope->effect_lights = owned;
        memcpy(owned + scope->effect_light_count, lights, count * sizeof(*lights));
        scope->effect_light_count = total;
    }
    scope->ready = frontend_source_effects_current(scope); return scope->ready;
}

bool frontend_source_effects_prepare(frontend_source_effects *scope,
    const qa_q3_refdef *definition, qa_q3_scene_options *options, qa_error *error)
{
    if (!frontend_source_effects_current(scope) || !scope->ready || scope->prepared || !options ||
        definition != scope->definition || frontend_selected_effects_count(scope->frontend) != scope->groups)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects preparation lost its actual entered definition or packet roster");
    scope->first = options->first_entity; scope->reserved = 0;
    if (!options->world.no_world) {
        if ((options->world.light_count && !options->world.lights) ||
            options->world.projected_light_count > 32 ||
            (options->world.projected_light_count && !options->world.projected_lights) ||
            options->world.light_count > SIZE_MAX / sizeof(*scope->lights) - scope->effect_light_count)
            return frontend_fail(error, QA_ERROR_FORMAT, "Source renderer lights leave their actual span");
        if (scope->effect_light_count) {
            size_t count = options->world.light_count + scope->effect_light_count;
            scope->lights = malloc(count * sizeof(*scope->lights));
            if (!scope->lights) return frontend_fail(error, QA_ERROR_MEMORY, "Combining scoped source renderer lights");
            if (options->world.light_count)
                memcpy(scope->lights, options->world.lights, options->world.light_count * sizeof(*scope->lights));
            memcpy(scope->lights + options->world.light_count, scope->effect_lights,
                scope->effect_light_count * sizeof(*scope->lights));
            size_t projected = options->world.projected_light_count;
            size_t added = scope->effect_light_count < 32 - projected ? scope->effect_light_count : 32 - projected;
            if (added) {
                scope->projected_lights = malloc((projected + added) * sizeof(*scope->projected_lights));
                if (!scope->projected_lights)
                    return frontend_fail(error, QA_ERROR_MEMORY, "Combining scoped source projected lights");
                if (projected) memcpy(scope->projected_lights, options->world.projected_lights,
                    projected * sizeof(*scope->projected_lights));
                memcpy(scope->projected_lights + projected, scope->effect_lights,
                    added * sizeof(*scope->projected_lights));
                options->world.projected_lights = scope->projected_lights;
                options->world.projected_light_count = projected + added;
            }
            options->world.lights = scope->lights; options->world.light_count = count;
        }
        for (size_t i = 0; i < scope->groups; ++i) {
            if (!frontend_selected_effects_source_matches(scope->frontend, scope, i)) continue;
            frontend_selected_effects_view view; qa_application_selected_effects source;
            const frontend_selected_effects_group *group = frontend_selected_effects_group_at(scope->frontend, i);
            if (!frontend_selected_effects_at(scope->frontend, i, &view, error) || !view.prepared ||
                !qa_application_effects_producer_read(scope->application, view.provider, &source, error) ||
                source.primary != scope->client.source_owner || view.sampled_application_frame != source.application_frame ||
                view.source_time_ms != source.sample_time_ms)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects have no completed actual source sample");
            size_t count = frontend_selected_effects_ref_count(group);
            if (options->first_entity >= 1022 || count > 1021u - options->first_entity)
                return frontend_fail(error, QA_ERROR_FORMAT, "Source effects exceed their actual scene entity reservation");
            options->first_entity += (uint32_t)count; scope->reserved += (uint32_t)count;
        }
    }
    scope->prepared = true; return frontend_source_effects_current(scope);
}

static bool output_current(frontend_source_effects *scope, size_t ordinal,
    const qa_q3_scene_options *options, qa_scene_frame *frame,
    const frontend_selected_effects_group *expected, const frontend_selected_effects_view *saved,
    size_t refs, size_t polygons, qa_error *error)
{
    const frontend_selected_effects_group *group; frontend_selected_effects_view view;
    return frontend_selected_effects_source_output_read(scope->frontend, scope, ordinal,
        options, frame, &group, &view, error) && group == expected && view.assets == saved->assets &&
        view.source_time_ms == saved->source_time_ms && frontend_selected_effects_ref_count(group) == refs &&
        frontend_selected_effects_poly_count(group) == polygons;
}

bool frontend_source_effects_submit(frontend_source_effects *scope,
    const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    qa_q3_presentation_binding binding;
    if (!frontend_source_effects_current(scope) || !scope->ready || !scope->prepared || !options ||
        !qa_q3_presentation_selected_binding_read(scope->presentation, options, frame, &binding, error) ||
        binding.world != scope->binding.world || binding.geometry != scope->binding.geometry ||
        binding.frame != scope->binding.frame || frontend_selected_effects_count(scope->frontend) != scope->groups)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects submission lost its actual renderer binding");
    if (options->world.no_world) return !scope->reserved;
    uint32_t order = scope->first;
    for (size_t i = 0; i < scope->groups; ++i) {
        if (!frontend_selected_effects_source_matches(scope->frontend, scope, i)) continue;
        const frontend_selected_effects_group *group; frontend_selected_effects_view view;
        if (!frontend_selected_effects_source_output_read(scope->frontend, scope, i,
            options, frame, &group, &view, error)) return false;
        size_t refs = frontend_selected_effects_ref_count(group), polygons = frontend_selected_effects_poly_count(group);
        if (order < scope->first || order > scope->first + scope->reserved || refs > scope->first + scope->reserved - order)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effects changed their reserved packet extent");
        for (size_t p = 0; p < polygons; ++p) {
            frontend_selected_effects_poly poly;
            if (!frontend_selected_effects_poly_at(group, p, &poly, error) ||
                !qa_q3_presentation_selected_poly(scope->presentation, view.assets, poly.shader,
                    poly.vertices, poly.count, view.source_time_ms, options, frame, error) ||
                !output_current(scope, i, options, frame, group, &view, refs, polygons, error)) return false;
        }
        for (size_t r = 0; r < refs; ++r) {
            const frontend_selected_effects_ref *captured = frontend_selected_effects_ref_at(group, r);
            if (!captured) return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effect refEntity lost its retained physical row");
            qa_q3_ref_entity ref = captured->ref;
            if (ref.kind == QA_Q3_REF_PORTAL || (ref.kind != QA_Q3_REF_MODEL &&
                qa_vec_length(qa_vec_sub(ref.origin, options->world.view.origin)) < captured->cull_radius)) continue;
            if (!qa_q3_presentation_selected_effect(scope->presentation, view.assets, &ref,
                view.source_time_ms, options, order + (uint32_t)r, frame, error) ||
                !output_current(scope, i, options, frame, group, &view, refs, polygons, error)) return false;
        }
        order += (uint32_t)refs;
    }
    return order == scope->first + scope->reserved &&
        frontend_selected_effects_count(scope->frontend) == scope->groups && frontend_source_effects_current(scope);
}

void frontend_source_effects_end(frontend_source_effects *scope)
{ if (scope) { free(scope->projected_lights); free(scope->lights); free(scope->effect_lights); free(scope); } }

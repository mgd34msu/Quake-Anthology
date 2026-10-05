#include "legacy_render_policy.h"
#include "particle_delivery.h"
#include "q1_sky.h"
#include "config_store.h"
#include "view_settings.h"
#include "remote_unified_private.h"
#include "remote_unified_presentation.h"
#include "qa/scene_effects.h"
#include "qa/ui_preferences.h"
#include <string.h>

static bool number(const qa_cvars *registry, const char *name, float *out, qa_error *error)
{
    const qa_cvar_view *row = qa_cvars_find(registry, name);
    if (!row || !isfinite(row->number)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Legacy renderer setting %s is absent or nonfinite in dialect %u",
            name, (unsigned)qa_cvars_dialect(registry));
        return false;
    }
    *out = row->number;
    return true;
}

bool frontend_legacy_render_policy_read(const qa_frontend *frontend, const qa_product *product,
    frontend_legacy_render_policy *out, qa_error *error)
{
    if (!frontend || !frontend->application || !product || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy rendering requires its actual selected product");
    const qa_cvars *registry = qa_application_cvars(frontend->application);
    return frontend_legacy_render_policy_read_registry(registry, product, out, error);
}

bool frontend_legacy_render_policy_read_registry(const qa_cvars *registry, const qa_product *product,
    frontend_legacy_render_policy *out, qa_error *error)
{
    if (!registry || !product || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy policy requires its actual cvar owner and product");
    frontend_legacy_render_policy value = {.family = product->family == QA_GAME_Q1 ? QA_SCENE_Q1 :
        product->family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3, .mirror_alpha = 1};
    if (value.family != QA_SCENE_Q3) {
        value.quakeworld = product->edition == QA_EDITION_QUAKEWORLD ||
            (value.family == QA_SCENE_Q1 && qa_cvars_dialect(registry) == QA_CONSOLE_QW);
        float flash = 0, eyes = 1, shadows, mirror = 1, texture_sort = 0;
        if (!number(registry, value.family == QA_SCENE_Q1 ? "r_shadows" : "gl_shadows", &shadows, error)) return false;
        if (!number(registry, "gl_flashblend", &flash, error)) return false;
        if (value.family == QA_SCENE_Q1 &&
            ((!value.quakeworld && !number(registry, "gl_doubleeys", &eyes, error)) ||
             !number(registry, "r_mirroralpha", &mirror, error) ||
             !number(registry, "gl_texsort", &texture_sort, error))) return false;
        value.flashblend = flash != 0;
        value.double_eyes = value.family == QA_SCENE_Q1 && (value.quakeworld || eyes != 0);
        value.planar_shadows = shadows != 0;
        value.texture_sort = value.family == QA_SCENE_Q1 && texture_sort != 0;
        value.mirror_alpha = value.family == QA_SCENE_Q1 && !value.quakeworld ? mirror : 1;
        float fullbright, lightmap, dynamic, polyblend, cull, clear;
        if (!number(registry, "r_fullbright", &fullbright, error) ||
            !number(registry, value.family == QA_SCENE_Q1 ? "r_lightmap" : "gl_lightmap", &lightmap, error) ||
            !number(registry, value.family == QA_SCENE_Q1 ? "r_dynamic" : "gl_dynamic", &dynamic, error) ||
            !number(registry, "gl_polyblend", &polyblend, error) ||
            !number(registry, "gl_cull", &cull, error) || !number(registry, "gl_clear", &clear, error)) return false;
        value.lighting = (qa_scene_legacy_policy){.source_family = value.family, .present = true,
            .fullbright = !value.quakeworld && fullbright != 0,
            .lightmap = !value.quakeworld && lightmap != 0, .dynamic = dynamic != 0,
            .polyblend = polyblend != 0, .cull = cull != 0, .clear = clear != 0,
            .planar_shadows = value.planar_shadows, .double_eyes = value.double_eyes,
            .flares = true, .modulate = 1, .monolightmap = '0'};
        if (value.family == QA_SCENE_Q2) {
            float saturate;
            const qa_cvar_view *mono = qa_cvars_find(registry, "gl_monolightmap");
            if (!number(registry, "gl_modulate", &value.lighting.modulate, error) ||
                !number(registry, "gl_saturatelighting", &saturate, error) || !mono || !mono->value) {
                frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 policy lost its reached lighting setting");
                return false;
            }
            value.lighting.saturate = saturate != 0;
            value.lighting.monolightmap = (uint8_t)mono->value[0];
            const qa_cvar_view *flares = qa_cvars_find(registry, "cl_flares");
            if (flares) {
                if (!isfinite(flares->number)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 flare setting is nonfinite");
                value.lighting.flares = flares->integer != 0;
            }
        }
    }
    *out = value;
    return true;
}

bool frontend_legacy_source_register(qa_cvars *registry, qa_console_dialect dialect,
    uint64_t owner, qa_error *error)
{
    if (!registry || !owner || qa_cvars_dialect(registry) != dialect)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy renderer requires its actual source registry");
    bool q1 = dialect == QA_CONSOLE_Q1 || dialect == QA_CONSOLE_QW;
    bool q2 = dialect == QA_CONSOLE_Q2 || dialect == QA_CONSOLE_Q2_RERELEASE;
    if (!q1 && !q2) return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy renderer requires a Q1/Q2 source dialect");
    static const struct { const char *name, *value; uint32_t flags; } shared[] = {
        {"r_fullbright", "0", 0}, {"gl_polyblend", "1", 0}, {"gl_cull", "1", 0}, {"gl_clear", "0", 0}};
    static const struct { const char *name, *value; uint32_t flags; } quake[] = {
        {"r_drawviewmodel", "1", 0},
        {"r_lightmap", "0", 0}, {"r_dynamic", "1", 0}, {"r_shadows", "0", 0},
        {"r_mirroralpha", "1", 0}, {"gl_texsort", "1", 0}, {"gl_flashblend", "1", 0}};
    static const struct { const char *name, *value; uint32_t flags; } quake2[] = {
        {"gl_lightmap", "0", 0}, {"gl_dynamic", "1", 0}, {"gl_shadows", "0", 0},
        {"gl_modulate", "1", QA_CVAR_ARCHIVE}, {"gl_monolightmap", "0", 0}, {"gl_saturatelighting", "0", 0},
        {"gl_flashblend", "0", 0}};
    for (size_t i = 0; i < sizeof(shared) / sizeof(*shared); ++i)
        if (!qa_cvars_register(registry, shared[i].name, shared[i].value, shared[i].flags,
            owner, "", error)) return false;
    if (q1) {
        if (!frontend_view_settings_q1_motion_register(registry, owner, dialect == QA_CONSOLE_QW, error)) return false;
        for (size_t i = 0; i < sizeof(quake) / sizeof(*quake); ++i)
            if (!qa_cvars_register(registry, quake[i].name, quake[i].value, quake[i].flags,
                owner, "", error)) return false;
        if (dialect == QA_CONSOLE_Q1 && !qa_cvars_register(registry, "gl_doubleeys", "1", 0, owner, "", error)) return false;
    } else {
        for (size_t i = 0; i < sizeof(quake2) / sizeof(*quake2); ++i)
            if (!qa_cvars_register(registry, quake2[i].name, quake2[i].value, quake2[i].flags,
                owner, "", error)) return false;
        if (dialect == QA_CONSOLE_Q2_RERELEASE &&
            !qa_cvars_register(registry, "cl_flares", "1", 0, owner, "", error)) return false;
    }
    return true;
}

bool frontend_legacy_source_owns(const qa_cvars *registry, const char *name)
{
    if (!registry || !name) return false;
    qa_console_dialect dialect = qa_cvars_dialect(registry);
    bool q1 = dialect == QA_CONSOLE_Q1 || dialect == QA_CONSOLE_QW;
    bool q2 = dialect == QA_CONSOLE_Q2 || dialect == QA_CONSOLE_Q2_RERELEASE;
    if (!q1 && !q2) return false;
    const qa_cvar_view *row = qa_cvars_find(registry, name);
    if (!row || !row->owner || row->console_created) return false;
    if (q1 && frontend_view_settings_q1_motion_owns(name,dialect == QA_CONSOLE_QW)) return true;
    const char *shared[] = {"r_fullbright", "gl_polyblend", "gl_cull", "gl_clear"};
    const char *quake[] = {"r_lightmap", "r_dynamic", "r_shadows", "r_mirroralpha", "gl_texsort", "gl_flashblend", "gl_doubleeys", "r_drawviewmodel"};
    const char *quake2[] = {"gl_lightmap", "gl_dynamic", "gl_shadows", "gl_modulate", "gl_monolightmap", "gl_saturatelighting", "cl_flares", "gl_flashblend"};
    for (size_t i = 0; i < sizeof(shared) / sizeof(*shared); ++i)
        if (!strcmp(name, shared[i])) return true;
    if (q1) {
        for (size_t i = 0; i < sizeof(quake) / sizeof(*quake); ++i)
            if (!strcmp(name, quake[i])) return true;
    } else {
        for (size_t i = 0; i < sizeof(quake2) / sizeof(*quake2); ++i)
            if (!strcmp(name, quake2[i])) return true;
    }
    return false;
}

bool frontend_legacy_model_input(const qa_scene_world *actual_world,
    const qa_scene_world_input *world, qa_scene_model_input *input, qa_error *error)
{
    if (!actual_world || !world || !input)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy model has no real world input");
    const qa_scene_legacy_policy *policy = &world->legacy_policy;
    input->q1_double_eyes = input->family == QA_SCENE_Q1 && policy->present && policy->double_eyes;
    if (policy->present) {
        input->no_cull = input->no_cull || !policy->cull;
        if (input->family == QA_SCENE_Q2) input->monochrome = policy->monolightmap != '0';
    }
    if (input->family != QA_SCENE_Q3) {
        qa_vec3 origin = qa_v3(input->transform.origin[0], input->transform.origin[1], input->transform.origin[2]);
        for (size_t i = 0; i < world->light_count; ++i) {
            const qa_scene_light *light = &world->lights[i];
            float amount = (light->radius - qa_vec_length(qa_vec_sub(origin, light->origin))) / 256;
            if (amount > 0) input->ambient = qa_vec_add(input->ambient, qa_vec_scale(light->color,
                amount * (input->family == QA_SCENE_Q2 && policy->present ? policy->modulate : 1)));
        }
    }
    if (policy->present && policy->planar_shadows && input->family != QA_SCENE_Q3 && !input->view_model) {
        qa_vec3 point;
        bool found = false;
        if (!qa_scene_world_sample_floor(actual_world,
            qa_v3(input->transform.origin[0], input->transform.origin[1], input->transform.origin[2]),
            &point, &found))
            return frontend_fail(error, QA_ERROR_FORMAT, "Legacy shadow floor traversal failed");
        input->planar_shadow = found;
        if (found) input->shadow_plane = point.z;
    }
    return true;
}

static void depth_range(qa_scene_frame *frame, size_t first, float near, float far)
{
    for (size_t i = first; i < frame->command_count; ++i) {
        qa_scene_command *command = &frame->commands[i];
        if (command->kind != QA_SCENE_COMMAND_DRAW) continue;
        qa_scene_state *state = &command->data.draw.state;
        state->depth_near = near + (far - near) * state->depth_near;
        state->depth_far = near + (far - near) * state->depth_far;
    }
}

static bool scene_current(const frontend_legacy_scene_services *services, qa_error *error)
{
    return services->current(services->context) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy scene lost its entered source");
}

static bool scene(qa_scene_world *actual_world, const frontend_legacy_scene_services *services,
    const qa_scene_world_input *input, const frontend_legacy_render_policy *policy,
    qa_scene_frame *frame, qa_scene_vec4 *blend, qa_error *error)
{
    if (services->view_blend && (!scene_current(services,error) ||
        !services->view_blend(services->context,input,blend,error) || !scene_current(services,error))) return false;
    qa_scene_world_input opaque = *input;
    if (policy->family == QA_SCENE_Q1) opaque.legacy_phase = QA_LEGACY_WORLD_OPAQUE;
    if (policy->family == QA_SCENE_Q1 && input->q1_sky_environment &&
        !qa_scene_world_q1_sky_begin(actual_world, &opaque, frame, &opaque.q1_sky, error)) return false;
    qa_scene_world_input visuals = *input;
    visuals.q1_sky = opaque.q1_sky;
    if (!scene_current(services, error) || !qa_scene_world_submit(actual_world, &opaque, frame, error) ||
        !scene_current(services, error) || !services->visuals(services->context, &visuals, frame, error) ||
        !scene_current(services, error) ||
        (opaque.q1_sky && !qa_scene_world_q1_sky_finish(opaque.q1_sky, frame, error))) return false;
    if (policy->flashblend && !qa_scene_legacy_dlights(frame, &input->view, policy->family,
        policy->quakeworld, input->lights, input->light_count, blend, error)) return false;
    if (services->dlights && (!scene_current(services, error) ||
        !services->dlights(services->context, input, frame, blend, error))) return false;
    if (!scene_current(services, error) || !services->particles(services->context, input, frame, error) ||
        !scene_current(services, error)) return false;
    if (policy->family == QA_SCENE_Q1) {
        qa_scene_world_input water = *input;
        water.legacy_phase = QA_LEGACY_WORLD_WATER;
        if (!qa_scene_world_submit(actual_world, &water, frame, error) ||
            !scene_current(services, error)) return false;
    }
    qa_scene_fog fog = input->fog;
    fog.sky_drawn = qa_scene_world_sky_drawn(actual_world);
    return qa_scene_frame_finish(frame, &input->view, &fog, error);
}

typedef struct native_scene_context {
    qa_frontend *frontend;
    qa_scene_world *world;
    uint32_t seat;
    qa_actor_owner exclude;
    frontend_config_legacy_view source;
    qa_application_map_view map;
    bool has_source;
} native_scene_context;

static bool native_current(void *context)
{
    native_scene_context *value = context;
    qa_application_map_view map;
    return value->frontend->scene_world == value->world && value->seat < value->frontend->options.seats &&
        qa_application_map_read(value->frontend->application, &map) && map.revision == value->map.revision &&
        map.resource == value->map.resource && map.geometry == value->map.geometry && map.presentation == value->map.presentation &&
        (!value->has_source || frontend_config_store_primary_legacy_current(value->frontend->config_store, &value->source));
}

static bool local_policy(const qa_frontend *frontend, const qa_product *product,
    const frontend_config_legacy_view *actual_source, frontend_legacy_render_policy *out, qa_error *error)
{
    if (actual_source) {
        if (!frontend_config_store_primary_legacy_current(frontend->config_store, actual_source) ||
            !product || !frontend_legacy_render_policy_read_registry(actual_source->registry,
                actual_source->product, out, error)) return false;
        /* The explicit WORLD presentation selects the sky/water traversal;
         * the entered Source owns its scalar renderer settings. */
        out->family = product->family == QA_GAME_Q1 ? QA_SCENE_Q1 :
            product->family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3;
        out->quakeworld = out->family == QA_SCENE_Q1 &&
            (product->edition == QA_EDITION_QUAKEWORLD || qa_cvars_dialect(actual_source->registry) == QA_CONSOLE_QW);
        if (out->family != QA_SCENE_Q1 || out->lighting.source_family != QA_SCENE_Q1) out->texture_sort = false;
        if (!out->texture_sort || out->quakeworld) out->mirror_alpha = 1;
        if (out->quakeworld) out->lighting.fullbright = out->lighting.lightmap = false;
        return frontend_config_store_primary_legacy_current(frontend->config_store, actual_source);
    }
    return frontend_legacy_render_policy_read(frontend, product, out, error);
}

bool frontend_legacy_local_policy_read(const qa_frontend *frontend, uint32_t seat,
    const qa_product *product, frontend_legacy_render_policy *out, qa_error *error)
{
    if (!frontend || seat >= frontend->options.seats) return false;
    uint32_t authored;
    frontend_config_legacy_view source; bool present;
    if (!frontend_seat_launch_id_read(frontend, seat, &authored) ||
        !frontend_config_store_primary_legacy_read(frontend->config_store, authored, &source, &present, error)) return false;
    return local_policy(frontend, product, present ? &source : NULL, out, error);
}

bool frontend_remote_unified_initial_clear(qa_frontend *frontend, uint32_t seat,
    bool *active, bool *clear, qa_error *error)
{
    if (!frontend || !active || !clear || seat >= frontend->options.seats) return false;
    *active = false; *clear = true;
    frontend_remote_unified *selected = NULL;
    for (frontend_remote_unified *row = frontend->remote_unified; row; row = row->next) {
        if (row->retired || row->options.domain.physical_seat != seat) continue;
        if (selected) return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy clear has multiple actual Unified receivers");
        selected = row;
    }
    if (!selected || !selected->frame || !selected->admitted) return true;
    if (selected->busy || !frontend_remote_unified_current(selected, error)) return false;
    frontend_unified_presentation_children children;
    if (!frontend_remote_unified_presentation_children_read(selected, &children, error)) return false;
    if (!children.media || !children.render || !frontend_unified_media_world(children.media)) return true;
    if (!frontend_unified_media_current(children.media) || !frontend_unified_render_idle(children.render))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy clear lost its actual received world and frame");
    qa_executable_recipe *recipe = frontend_remote_unified_recipe(selected);
    const qa_recipe_choices *choices = qa_executable_recipe_choices(recipe);
    const qa_product *product = choices ? qa_catalog_product(qa_executable_recipe_catalog(recipe), choices->world.presentation) : NULL;
    const frontend_remote_unified_domain *domain = frontend_remote_unified_domain_read(selected);
    frontend_legacy_render_policy policy;
    if (!domain || frontend_unified_media_recipe(children.media) != recipe ||
        !frontend_legacy_render_policy_read_registry(domain->cvars, product, &policy, error) ||
        !frontend_remote_unified_current(selected, error)) return false;
    if (policy.lighting.present) { *active = true; *clear = policy.lighting.clear; }
    return true;
}

static bool native_policy(void *context, const qa_product *product,
    frontend_legacy_render_policy *out, qa_error *error)
{
    native_scene_context *value = context;
    if (!native_current(value)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy policy lost its physical Source and world");
    if (!local_policy(value->frontend, product, value->has_source ? &value->source : NULL, out, error)) return false;
    return native_current(value);
}

static bool native_visuals(void *context, const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    native_scene_context *value = context;
    return frontend_visuals_submit(value->frontend, value->seat, value->exclude, input, frame, error);
}

static bool native_particles(void *context, const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    native_scene_context *value = context;
    if (frame != &value->frontend->frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy particles lost their physical frame");
    return frontend_particle_draw(value->frontend, value->seat, &input->view, error);
}

bool frontend_legacy_scene_submit(qa_frontend *frontend, uint32_t seat, qa_actor_owner exclude,
    const qa_scene_world_input *world, qa_scene_frame *frame, qa_error *error)
{
    if (!frontend || !world || frame != &frontend->frame || seat >= frontend->options.seats ||
        world->view.seat != seat || !frontend->scene_world)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy scene requires its actual physical frame");
    qa_application_map_view map;
    if (!qa_application_map_read(frontend->application, &map))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy scene lost its installed map");
    const qa_product *product = qa_catalog_product(qa_application_catalog(frontend->application), map.presentation);
    native_scene_context context = {.frontend = frontend, .world = frontend->scene_world,
        .seat = seat, .exclude = exclude, .map = map};
    uint32_t authored;
    if (!frontend_seat_launch_id_read(frontend, seat, &authored))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy scene has no actual authored seat");
    if (!frontend_config_store_primary_legacy_read(frontend->config_store, authored,
        &context.source, &context.has_source, error)) return false;
    frontend_legacy_render_policy policy;
    if (!native_policy(&context, product, &policy, error)) return false;
    qa_scene_world_input input = *world;
    qa_scene_q1_sky_environment sky = {0};
    if (policy.family == QA_SCENE_Q1) {
        qa_actor_id actor = {0};
        (void)frontend_seat_actor_read(frontend, seat, &actor);
        frontend_q1_sky_view selected;
        if (!frontend->q1_sky || !frontend_q1_sky_view_read(frontend->q1_sky, actor, &selected, error)) return false;
        if (selected.classic_q1) {
            if (selected.far_clip <= 4)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 far clip must exceed its actual near plane");
            float depth = selected.far_clip - 4;
            input.view.projection.m[10] = -(selected.far_clip + 4) / depth;
            input.view.projection.m[14] = -2 * selected.far_clip * 4 / depth;
            sky = (qa_scene_q1_sky_environment){.boxed = selected.boxed, .fast = selected.fast,
                .quality = selected.quality, .alpha = selected.alpha, .fog = selected.fog, .far_clip = selected.far_clip};
            memcpy(sky.images, selected.images, sizeof(sky.images));
            input.q1_sky_environment = &sky;
        }
    }
    frontend_legacy_scene_services services = {.context = &context, .current = native_current,
        .visuals = native_visuals, .particles = native_particles, .policy = native_policy};
    return frontend_legacy_scene_submit_product(frontend, frontend->scene_world, product,
        &input, frame, &services, error);
}

bool frontend_legacy_scene_submit_product(qa_frontend *frontend, qa_scene_world *actual_world,
    const qa_product *product, const qa_scene_world_input *world, qa_scene_frame *frame,
    const frontend_legacy_scene_services *services, qa_error *error)
{
    if (!frontend || !actual_world || !world || frame != &frontend->frame ||
        world->view.seat >= frontend->options.seats || !services || !services->current ||
        !services->visuals || !services->particles)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy scene requires its actual entered services");
    if (!scene_current(services, error)) return false;
    frontend_legacy_render_policy policy;
    if (!(services->policy ? services->policy(services->context, product, &policy, error) :
        frontend_legacy_render_policy_read(frontend, product, &policy, error)) ||
        !scene_current(services, error)) return false;
    qa_scene_world_input input = *world;
    if (policy.family == QA_SCENE_Q1 && input.q1_sky_environment) {
        float far_clip = input.q1_sky_environment->far_clip;
        if (!isfinite(far_clip) || far_clip <= 4)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 far clip must exceed its actual near plane");
        float depth = far_clip - 4;
        input.view.projection.m[10] = -(far_clip + 4) / depth;
        input.view.projection.m[14] = -2 * far_clip * 4 / depth;
    }
    input.legacy_flashblend = policy.flashblend;
    input.legacy_texture_sort = policy.texture_sort;
    input.legacy_policy = policy.lighting;
    if (policy.lighting.present) input.view.clear_color = policy.lighting.clear;
    qa_scene_view reflected;
    bool mirror = false;
    if (policy.texture_sort && policy.mirror_alpha != 1 && !qa_scene_world_q1_mirror(actual_world,
        &input, frame, &input.q1_mirror, &reflected, &mirror, error)) return false;
    qa_scene_vec4 blend = {0};
    size_t first = frame->command_count;
    size_t scene_first = first;
    if (!scene(actual_world, services, &input, &policy, frame, &blend, error)) return false;
    if (policy.mirror_alpha != 1) depth_range(frame, first, 0, .5f);
    if (mirror) {
        qa_scene_world_input child = input;
        child.view = reflected;
        /* R_MarkLeaves retains the parent PVS while the reflected camera is
         * active. Only frustum traversal changes for the mirror scene. */
        child.use_pvs_origin = true;
        child.pvs_origin = input.use_pvs_origin ? input.pvs_origin : input.view.origin;
        if (services->reflected_lights && (!scene_current(services,error) ||
            !services->reflected_lights(services->context,&child,frame,error) ||
            !scene_current(services,error))) return false;
        first = frame->command_count;
        blend = (qa_scene_vec4){0};
        if (!scene(actual_world, services, &child, &policy, frame, &blend, error)) return false;
        depth_range(frame, first, .5f, 1);
        if (!scene_current(services, error) || !qa_scene_world_q1_mirror_overlay(actual_world, &input,
            policy.mirror_alpha, frame, error) ||
            !qa_scene_frame_finish(frame, &input.view, NULL, error)) return false;
    }
    if ((policy.lighting.polyblend || !policy.lighting.present) && (blend.w > 0 || services->blend)) {
        qa_ui_preferences preferences;
        if (!qa_ui_preferences_read(qa_application_cvars(frontend->application), world->view.seat, &preferences, error)) return false;
        if (preferences.reduced_flashes) blend = (qa_scene_vec4){0};
        if (services->blend) {
            if (!scene_current(services, error) ||
                !services->blend(services->context, &input, blend, error) ||
                !scene_current(services, error)) return false;
        } else if (blend.w > 0 && !qa_scene_frame_picture(frame,
            qa_scene_white(frontend->ui_images), world->view.viewport, world->view.viewport,
            (qa_scene_vec4){0, 0, 1, 1}, blend, error)) return false;
    }
    if (policy.lighting.present && !policy.lighting.cull)
        for (size_t i = scene_first; i < frame->command_count; ++i)
            if (frame->commands[i].kind == QA_SCENE_COMMAND_DRAW)
                frame->commands[i].data.draw.state.cull = QA_CULL_NONE;
    return scene_current(services, error);
}

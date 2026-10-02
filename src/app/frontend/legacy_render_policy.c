#include "legacy_render_policy.h"
#include "particle_delivery.h"
#include "q1_sky.h"
#include "qa/scene_effects.h"
#include "qa/ui_preferences.h"
#include <string.h>

static bool number(const qa_cvars *registry, const char *name, float *out, qa_error *error)
{
    const qa_cvar_view *row = qa_cvars_find(registry, name);
    if (!row || !isfinite(row->number))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy renderer lost its canonical setting");
    *out = row->number;
    return true;
}

bool frontend_legacy_render_policy_read(const qa_frontend *frontend, const qa_product *product,
    frontend_legacy_render_policy *out, qa_error *error)
{
    if (!frontend || !frontend->application || !product || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy rendering requires its actual selected product");
    frontend_legacy_render_policy value = {.family = product->family == QA_GAME_Q1 ? QA_SCENE_Q1 :
        product->family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3, .mirror_alpha = 1};
    if (value.family != QA_SCENE_Q3) {
        qa_cvars *registry = qa_application_cvars(frontend->application);
        float flash, eyes, shadows, mirror, texture_sort = 0;
        if (!number(registry, "gl_flashblend", &flash, error) ||
            !number(registry, "gl_doubleeys", &eyes, error) ||
            !number(registry, value.family == QA_SCENE_Q1 ? "r_shadows" : "gl_shadows", &shadows, error) ||
            !number(registry, "r_mirroralpha", &mirror, error)) return false;
        if (value.family == QA_SCENE_Q1 && !number(registry, "gl_texsort", &texture_sort, error)) return false;
        value.quakeworld = product->edition == QA_EDITION_QUAKEWORLD;
        value.flashblend = flash != 0;
        value.double_eyes = value.family == QA_SCENE_Q1 && (value.quakeworld || eyes != 0);
        value.planar_shadows = shadows != 0;
        value.texture_sort = value.family == QA_SCENE_Q1 && texture_sort != 0;
        value.mirror_alpha = value.family == QA_SCENE_Q1 && !value.quakeworld ? mirror : 1;
    }
    *out = value;
    return true;
}

bool frontend_legacy_model_input(const qa_frontend *frontend, qa_product_id content,
    const qa_scene_world *actual_world,
    const qa_scene_world_input *world, qa_scene_model_input *input, qa_error *error)
{
    if (!frontend || !frontend->application)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy model has no actual catalog");
    return frontend_legacy_model_input_product(frontend,
        qa_catalog_product(qa_application_catalog(frontend->application), content),
        actual_world, world, input, error);
}

bool frontend_legacy_model_input_product(const qa_frontend *frontend, const qa_product *product,
    const qa_scene_world *actual_world, const qa_scene_world_input *world,
    qa_scene_model_input *input, qa_error *error)
{
    if (!frontend || !frontend->application || !actual_world || !world || !input)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Legacy model has no real world input");
    frontend_legacy_render_policy policy;
    if (!frontend_legacy_render_policy_read(frontend, product, &policy, error)) return false;
    input->q1_double_eyes = policy.double_eyes;
    if (policy.family != QA_SCENE_Q3) {
        qa_vec3 origin = qa_v3(input->transform.origin[0], input->transform.origin[1], input->transform.origin[2]);
        for (size_t i = 0; i < world->light_count; ++i) {
            const qa_scene_light *light = &world->lights[i];
            float amount = (light->radius - qa_vec_length(qa_vec_sub(origin, light->origin))) / 256;
            if (amount > 0) input->ambient = qa_vec_add(input->ambient, qa_vec_scale(light->color, amount));
        }
    }
    if (policy.planar_shadows && policy.family != QA_SCENE_Q3 && !input->view_model) {
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
} native_scene_context;

static bool native_current(void *context)
{
    native_scene_context *value = context;
    return value->frontend->scene_world == value->world && value->seat < value->frontend->options.seats;
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
    frontend_legacy_render_policy policy;
    if (!frontend_legacy_render_policy_read(frontend, product, &policy, error)) return false;
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
    native_scene_context context = {frontend, frontend->scene_world, seat, exclude};
    frontend_legacy_scene_services services = {&context, native_current, native_visuals, native_particles, NULL};
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
    if (!frontend_legacy_render_policy_read(frontend, product, &policy, error)) return false;
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
    qa_scene_view reflected;
    bool mirror = false;
    if (policy.texture_sort && policy.mirror_alpha != 1 && !qa_scene_world_q1_mirror(actual_world,
        &input, frame, &input.q1_mirror, &reflected, &mirror, error)) return false;
    qa_scene_vec4 blend = {0};
    size_t first = frame->command_count;
    if (!scene(actual_world, services, &input, &policy, frame, &blend, error)) return false;
    if (policy.mirror_alpha != 1) depth_range(frame, first, 0, .5f);
    if (mirror) {
        qa_scene_world_input child = input;
        child.view = reflected;
        /* R_MarkLeaves retains the parent PVS while the reflected camera is
         * active. Only frustum traversal changes for the mirror scene. */
        child.use_pvs_origin = true;
        child.pvs_origin = input.use_pvs_origin ? input.pvs_origin : input.view.origin;
        first = frame->command_count;
        blend = (qa_scene_vec4){0};
        if (!scene(actual_world, services, &child, &policy, frame, &blend, error)) return false;
        depth_range(frame, first, .5f, 1);
        if (!scene_current(services, error) || !qa_scene_world_q1_mirror_overlay(actual_world, &input,
            policy.mirror_alpha, frame, error) ||
            !qa_scene_frame_finish(frame, &input.view, NULL, error)) return false;
    }
    if (blend.w > 0 || services->blend) {
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
    return scene_current(services, error);
}

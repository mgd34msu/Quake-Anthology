#include "internal.h"
#include <stdio.h>

static qa_scene_family scene_family(qa_game_family family)
{
    return family == QA_GAME_Q3 ? QA_SCENE_Q3 : family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q1;
}
bool frontend_scene_sync(qa_frontend *frontend, qa_error *error)
{
    qa_application_map_view map;
    if (!qa_application_map_read(frontend->application, &map)) return true;
    uint64_t configuration = qa_application_configuration_generation(frontend->application);
    if (frontend->configuration == configuration && frontend->map_revision == map.revision) return true;
    const qa_launch_snapshot *snapshot = qa_application_launch(frontend->application);
    qa_vfs *mounts = qa_vfs_clone(qa_launch_snapshot_mounts(snapshot), error);
    if (!mounts) return false;
    qa_scene_resources *images = qa_scene_resources_create(mounts, error);
    qa_material_library *materials = images ? qa_material_library_create(images, frontend->order, error) : NULL;
    qa_scene_world *world = NULL;
    qa_audio_bank *sounds = NULL;
    qa_bsp_view bsp;
    const qa_product *product = qa_catalog_product(qa_launch_snapshot_catalog(snapshot), map.geometry);
    qa_scene_family family = product ? scene_family(product->family) : QA_SCENE_Q1;
    qa_scene_world_options options = {.images = {.family = family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = 255},
        .subdivisions = 64, .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 1};
    bool ok = images && materials && qa_bsp_open(qa_resource_bytes(map.resource), &bsp, error) &&
        qa_material_library_load_scripts(materials, mounts, &options.images, error) &&
        qa_scene_world_create(&bsp, images, materials, &options, &world, error) &&
        (!frontend->audio || qa_audio_bank_create(mounts, &sounds, error));
    if (!ok) {
        qa_scene_world_destroy(world); qa_audio_bank_destroy(sounds);
        qa_material_library_destroy(materials); qa_scene_resources_destroy(images); qa_vfs_destroy(mounts);
        return false;
    }
    size_t length = strlen(map.name);
    char *name = malloc(length + 1);
    if (!name) {
        qa_scene_world_destroy(world); qa_audio_bank_destroy(sounds);
        qa_material_library_destroy(materials); qa_scene_resources_destroy(images); qa_vfs_destroy(mounts);
        return frontend_fail(error, QA_ERROR_MEMORY, "retaining render map identity");
    }
    memcpy(name, map.name, length + 1);
    qa_resource *resource = map.resource;
    qa_resource_retain(resource);
    if (!frontend_source_retire_world(frontend, error)) {
        qa_resource_release(resource); free(name);
        qa_scene_world_destroy(world); qa_audio_bank_destroy(sounds);
        qa_material_library_destroy(materials); qa_scene_resources_destroy(images); qa_vfs_destroy(mounts);
        return false;
    }
    /* Rendering has finished with the previous publication before this swap. */
    qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
    qa_scene_world_destroy(frontend->scene_world); qa_audio_bank_destroy(frontend->sounds);
    qa_resource_release(frontend->map_resource);
    qa_material_library_destroy(frontend->materials); qa_scene_resources_destroy(frontend->images); qa_vfs_destroy(frontend->mounts);
    free(frontend->map_name);
    frontend->map_name = name; frontend->mounts = mounts; frontend->images = images;
    frontend->materials = materials; frontend->scene_world = world; frontend->sounds = sounds;
    frontend->map_resource = resource;
    frontend->configuration = configuration; frontend->map_revision = map.revision;
    return frontend_material_remaps(frontend, frontend->materials, error) && frontend_source_publish_world(frontend, error);
}
qa_scene_rect frontend_viewport(const qa_frontend *frontend, unsigned seat)
{
    unsigned columns = frontend->options.seats > 2 ? 2 : 1;
    unsigned rows = frontend->options.seats > 1 ? 2 : 1;
    uint32_t x = (seat % columns) * frontend->width / columns;
    uint32_t y = (seat / columns) * frontend->height / rows;
    uint32_t right = ((seat % columns) + 1) * frontend->width / columns;
    uint32_t bottom = ((seat / columns) + 1) * frontend->height / rows;
    return (qa_scene_rect){(int32_t)x, (int32_t)y, right - x, bottom - y};
}
void frontend_camera_axes(qa_vec3 angles, qa_vec3 axis[3])
{
    float yaw = angles.y * .017453292519943295f, pitch = angles.x * .017453292519943295f;
    float roll = angles.z * .017453292519943295f;
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch), sr = sinf(roll), cr = cosf(roll);
    axis[0] = qa_v3(cp * cy, cp * sy, -sp);
    axis[1] = qa_v3(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp);
    axis[2] = qa_v3(cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp);
}
bool frontend_present(qa_frontend *frontend, qa_error *error)
{
    qa_display_info display;
    if (!qa_display_info_get(frontend->display, &display, error)) return false;
    if (display.minimized || !display.drawable_width || !display.drawable_height) return true;
    if (frontend->width != display.drawable_width || frontend->height != display.drawable_height) {
        if (frontend->cpu && !qa_cpu_resize(frontend->cpu, display.drawable_width, display.drawable_height, error)) return false;
        frontend->width = display.drawable_width; frontend->height = display.drawable_height;
    }
    if (!frontend_scene_sync(frontend, error) || !frontend_shader_sync(frontend, error)) return false;
    qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
    if (!qa_scene_frame_material_order(&frontend->frame, frontend->order, error)) return false;
    qa_audio_listener listeners[4]; size_t listener_count = 0;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        qa_scene_rect rect = frontend_viewport(frontend, i);
        qa_ui_state ui;
        if (!qa_ui_tick(seat->ui, (double)frontend->time_ns / 1000000, error) || !qa_ui_state_read(seat->ui, &ui, error)) return false;
        qa_actor_id actor = {0}; qa_application_camera_view camera;
        bool live = qa_application_player_actor(frontend->application, i, &actor) && qa_application_control_camera(frontend->application, actor, &camera);
        qa_application_presentation_view source = {0};
        (void)qa_application_presentation_read(frontend->application, i, &source);
        qa_scene_view view = {.viewport = rect, .seat = i, .clear_color = true, .clear_depth = true,
            .color = {.015f, .02f, .03f, 1}, .depth = 1};
        if (live) {
            view.origin = qa_vec_add(camera.origin, camera.view_offset);
            qa_vec3 angles = camera.angles;
            if (!source.source_world && !camera.cutscene && seat->q2_view_ready &&
                    qa_actor_id_equal(actor, seat->q2_actor)) {
                qa_application_visual_view appearance;
                if (!qa_application_visual_read(frontend->application, actor, &appearance, error)) return false;
                const qa_product *character = qa_catalog_product(qa_application_catalog(frontend->application), appearance.character_content);
                view.origin = qa_vec_add(camera.origin, seat->q2_view.offset);
                if (character && character->edition == QA_EDITION_RERELEASE) view.origin.z += camera.view_height;
                angles = qa_vec_add(seat->q2_view.angles, seat->q2_view.kick_angles);
            }
            frontend_camera_axes(angles, view.axis);
        }
        else { view.axis[0] = qa_v3(1, 0, 0); view.axis[1] = qa_v3(0, 1, 0); view.axis[2] = qa_v3(0, 0, 1); }
        float fov_x = live && !source.source_world && seat->q2_view_ready &&
            qa_actor_id_equal(actor, seat->q2_actor) ? seat->q2_view.fov : 90;
        float fov_y = 2 * atanf(tanf(fov_x * .008726646259971648f) * (float)rect.height / (float)rect.width) * 57.29577951308232f;
        view.projection = qa_scene_projection(fov_x, fov_y, 4, 16384);
        if (!frontend_tools_camera(frontend, i, false, &view, error)) return false;
        qa_scene_command begin = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = view};
        if (!qa_scene_frame_emit(&frontend->frame, &begin, error)) return false;
        if (!frontend_source_frame(frontend, i, rect, error) ||
            !frontend_native_q2_frame(frontend, i, rect, error)) return false;
        if (live && !ui.fullscreen && !source.source_world && frontend->scene_world) {
            qa_scene_world_input world = {.view = view, .seconds = (double)frontend->time_ns / 1e9,
                .milliseconds = (int64_t)(frontend->time_ns / 1000000), .identity_light = 1, .curve_error = 4};
            if (!frontend_event_world(frontend, i, &world, error) ||
                !qa_scene_world_submit(frontend->scene_world, &world, &frontend->frame, error) ||
                !frontend_visuals_submit(frontend, i, 0, &world, &frontend->frame, error) ||
                !frontend_particle_draw(frontend, &view, error)) return false;
            world.fog.sky_drawn = qa_scene_world_sky_drawn(frontend->scene_world);
            if (!qa_scene_frame_finish(&frontend->frame, &view, &world.fog, error)) return false;
        }
        if (!source.source_world && !frontend_tools_debug(frontend, &view, error)) return false;
        uint32_t real_milliseconds = (uint32_t)((frontend->time_ns / 1000000) & UINT32_MAX);
        if (!qa_application_present(frontend->application, i, real_milliseconds,
                frontend_network_remote(frontend) ? frontend_network_client_time(frontend) :
                    real_milliseconds, error)) return false;
        if (!frontend_native_q2_world_text(frontend, i, &view, error)) return false;
        if (live && frontend->audio) {
            qa_audio_listener *listener = &listeners[listener_count++];
            *listener = (qa_audio_listener){.seat = i, .actor = frontend_audio_actor(frontend, actor, error),
                .origin = view.origin, .gain = 1.0f / (float)frontend->options.seats};
            if (listener->actor == QA_AUDIO_NO_ACTOR) return false;
            memcpy(listener->axis, view.axis, sizeof(view.axis));
            frontend_source_listener(frontend, i, listener);
        }
        if (live && !ui.fullscreen && !source.source_world && seat->q2_view_ready &&
                qa_actor_id_equal(actor, seat->q2_actor) && seat->q2_view.blend.w > 0) {
            qa_q2_blend blend = seat->q2_view.blend;
            if (!qa_scene_frame_picture(&frontend->frame, qa_scene_white(frontend->ui_images), rect, rect,
                    (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){blend.x, blend.y, blend.z, blend.w}, error)) return false;
        }
        if (live && !qa_hud_draw(seat->hud, &(qa_hud_frame){.seat = i, .actor = actor,
            .time_ns = frontend->time_ns, .viewport = rect, .safe_area = rect,
            .scale = 1, .show_scores = seat->scores || (seat->q2_view_ready && !seat->q2_help && (seat->q2_view.layouts & 1)),
            .show_inventory = seat->q2_inventory, .visible = !ui.fullscreen}, &frontend->frame, error)) return false;
        if (live && !ui.fullscreen && !qa_hud_wheel_draw(seat->wheel,
            &(qa_hud_wheel_draw_options){.viewport = rect, .fonts = seat->fonts,
                .white = qa_scene_white(frontend->ui_images), .text = {1, 1, 1, 1},
                .accent = {.9f, .7f, .3f, 1}, .disabled = {.4f, .4f, .4f, 1},
                .panel = {.05f, .05f, .05f, .85f}, .scale = 1}, &frontend->frame, error)) return false;
        if (!qa_ui_draw(seat->ui, &frontend->frame, rect, 1, false, error)) return false;
        if (qa_input_seat_focus(seat->input) == QA_INPUT_CONSOLE) {
            qa_field_view field = qa_text_field_read(qa_seat_console_field(seat->console, false));
            qa_console_draw_options console = {.target = rect, .font = &seat->fonts,
                .buffer = qa_seat_console_buffer(seat->console), .field = &field,
                .background = frontend->console_background, .now_milliseconds = (double)frontend->time_ns / 1000000,
                .height = (float)rect.height * .6f, .scale = 1};
            if (!qa_console_draw(&frontend->frame, &console, error)) return false;
        }
    }
    if (frontend->audio && !qa_audio_engine_listeners(frontend->audio, listeners, listener_count, error)) return false;
    const qa_cvar_view *gamma = qa_cvars_find(qa_application_cvars(frontend->application), "r_gamma");
    float brightness = gamma ? fmaxf(.5f, fminf(3, gamma->number)) : frontend->options.gamma;
    if (frontend->cpu) return qa_cpu_set_gamma(frontend->cpu, brightness, error) &&
        qa_cpu_execute(frontend->cpu, &frontend->frame, error) && qa_cpu_present_frame(frontend->cpu, error);
    return qa_gl_set_gamma(frontend->gl, brightness, error) && qa_gl_execute(frontend->gl, &frontend->frame, error) &&
        qa_gl_finish(frontend->gl, error) && qa_display_swap(frontend->display, error);
}

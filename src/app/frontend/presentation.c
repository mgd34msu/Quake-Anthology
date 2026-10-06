#include "source_prompt.h"
#include "source_companion_legacy.h"
#include "root_resources.h"
#include "qa/material_source_scratch.h"
#include "internal.h"
#include "accessibility.h"
#include "ui_features.h"
#include "menu_fonts.h"
#include "qc_rerelease_events.h"
#include "native_q3_client.h"
#include "native_composition.h"
#include "equipment_media.h"
#include "component_scene.h"
#include "qa/application_q3_components.h"
#include "network_session.h"
#include "remote_q2_client.h"
#include "remote_q1_client.h"
#include "remote_unified.h"
#include "remote_q1_camera.h"
#include "qa/network_q1.h"
#include "q3_render_policy.h"
#include "q3_color_policy.h"
#include "view_settings.h"
#include "q1_sky.h"
#include "shared_resource_policy.h"
#include "shared_render_controls.h"
#include "legacy_render_policy.h"
#include "particle_delivery.h"
#include "particle_clock.h"
#include "q2_client_lerp.h"
#include "qc_messages.h"
#include "config_store.h"
#include "qa/application_network.h"
#include "qa/application_network_qw.h"
#include <stdio.h>

static bool source_status_native(const qa_frontend *f,uint32_t physical,qa_actor_id viewer,bool *out,qa_error *error)
{
    *out=false;
    for (size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if (!frontend_component_scene_read(f,i,&row,error)) return false;
        if (row.retired || row.origin!=APPLICATION_Q3_COMPONENT_SCENE_LOCAL || !row.begun ||
            row.physical_seat!=physical || !qa_actor_id_equal(row.viewer,viewer)) continue;
        bool replaces=false;
        if (!qa_application_q3_component_scene_hud_read(f->application,row.identity,physical,viewer,row.sequence,&replaces,error)) return false;
        *out=*out || replaces;
    }
    return true;
}
bool frontend_scene_sync(qa_frontend *frontend,qa_error *error)
{ return frontend_root_resources_sync(frontend,error); }
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
bool frontend_view_background(qa_frontend *frontend, qa_scene_rect output,
    const qa_scene_view *view, qa_error *error)
{
    if (view->viewport.x == output.x && view->viewport.y == output.y &&
        view->viewport.width == output.width && view->viewport.height == output.height) return true;
    qa_scene_command clear = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = *view};
    clear.data.view.viewport = output;
    clear.data.view.clear_color = clear.data.view.clear_depth = true;
    clear.data.view.color = (qa_scene_vec4){0, 0, 0, 1};
    clear.data.view.depth = 1;
    return qa_scene_frame_emit(&frontend->frame, &clear, error);
}
static qa_scene_rect q1_view_rectangle(qa_scene_rect viewport,
    const frontend_q1_view_settings *settings, bool intermission)
{
    double size = intermission ? 120 : settings->size;
    uint32_t lines = size >= 120 ? 0 : size >= 110 ? 24 : 48;
    uint32_t reserved = settings->overlay_status && size >= 100 ? 0 : lines;
    uint32_t available = viewport.height > reserved ? viewport.height - reserved : 1;
    double fraction = fmin(size, 100) / 100;
    uint32_t width = (uint32_t)fmax(96, trunc(viewport.width * fraction));
    if (width > viewport.width) width = viewport.width;
    uint32_t height = (uint32_t)fmax(1, trunc(viewport.height * fraction));
    if (height > available) height = available;
    viewport.x += (int32_t)((viewport.width - width) / 2);
    if (size < 100) viewport.y += (int32_t)((available - height) / 2);
    viewport.width = width; viewport.height = height;
    return viewport;
}
static bool q1_view_projection(qa_frontend *frontend, qa_scene_view *view,
    double fov, qa_error *error)
{
    const qa_cvar_view *far_clip = qa_cvars_find(qa_application_cvars(frontend->application), "gl_farclip");
    if (!far_clip || !isfinite(far_clip->number) || far_clip->number <= 4)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 CLIENT camera lost its actual far clip declaration");
    float horizontal = (float)fov;
    float vertical = 2 * atanf(tanf(horizontal * .008726646259971648f) *
        (float)view->viewport.height / (float)view->viewport.width) * 57.29577951308232f;
    view->projection = qa_scene_projection(horizontal, vertical, 4, far_clip->number);
    return true;
}
static bool remote_q1_present(qa_frontend *f,unsigned seat,const qa_scene_view *fallback,
    qa_audio_listener *listener,bool *rendered,bool *hud_drawn,const qa_ui_preferences *preferences,bool visible,qa_error *error)
{
    *rendered=false; *hud_drawn=false;
    frontend_remote_q1 *selected=NULL;
    frontend_remote_q1_view source={0};
    for (size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(f,i);
        frontend_remote_q1_view actual;
        if (!frontend_remote_q1_metadata_read(row,&actual,error)) return false;
        if (actual.domain.physical_seat!=seat || actual.retired) continue;
        if (selected) return frontend_fail(error,QA_ERROR_ARGUMENT,"Two Q1 CLIENT receivers own one physical output");
        selected=row; source=actual;
    }
    if (!selected) return true;
    *rendered=true;
    *listener=(qa_audio_listener){.seat=seat,.actor=QA_AUDIO_NO_ACTOR};
    if (!source.bound) return true;
    frontend_remote_q1_player_view player; bool present=false;
    if (!frontend_remote_q1_player_read(selected,&player,&present,error)) return false;
    if (!present) return true;
    double fov; bool explicit_override;
    if (!frontend_view_settings_read(f->view_settings,&fov,&explicit_override))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 CLIENT camera lost its actual published view preference");
    (void)explicit_override;
    frontend_q1_view_settings settings;
    if (!frontend_view_settings_q1_sample(f->view_settings,
        qa_q1_is_qw(source.protocol)?QA_CONSOLE_QW:QA_CONSOLE_Q1,&settings,error)) return false;
    qa_scene_view view=*fallback;
    frontend_q1_view_pose pose;
    if (!frontend_remote_q1_view_pose_read(selected, &pose, error)) return false;
    view.origin=pose.origin;
    qa_vec3 angles=pose.angles;
    if (settings.chase && !player.intermission &&
        !frontend_remote_q1_chase_camera(selected,&settings,view.origin,player.angles,
            &view.origin,&angles,error)) return false;
    frontend_camera_axes(angles,view.axis);
    view.viewport = q1_view_rectangle(view.viewport, &settings, player.intermission);
    if (!q1_view_projection(f, &view, fov, error)) return false;
    if (!frontend_view_background(f, fallback->viewport, &view, error)) return false;
    if (!frontend_remote_q1_draw(selected,&view,listener,rendered,error)) return false;
    frontend_seat *physical=&f->seats[seat];
    bool component_status=false;
    if (!source_status_native(f,seat,player.actor,&component_status,error)) return false;
    if (!qa_hud_draw(physical->hud,&(qa_hud_frame){.seat=seat,.actor=player.actor,
        .source_status_native=component_status,
        .time_ns=f->time_ns,.viewport=view.viewport,.safe_area=fallback->viewport,
        .scale=preferences->hud_scale,.show_scores=physical->scores,.visible=visible},&f->frame,error)) return false;
    *hud_drawn=true; return true;
}
bool frontend_display_ready(qa_frontend *frontend, bool *ready, qa_error *error)
{
    *ready = false;
    qa_display_info display;
    if (!qa_display_info_get(frontend->display, &display, error)) return false;
    if (display.minimized || !display.drawable_width || !display.drawable_height) return true;
    if (frontend->width != display.drawable_width || frontend->height != display.drawable_height) {
        if (frontend->cpu && !qa_cpu_resize(frontend->cpu, display.drawable_width, display.drawable_height, error)) return false;
        frontend->width = display.drawable_width; frontend->height = display.drawable_height;
    }
    *ready = true;
    return true;
}
bool frontend_frame_present(qa_frontend *frontend, qa_error *error)
{
    const qa_cvar_view *gamma = qa_cvars_find(qa_application_cvars(frontend->application), "r_gamma");
    float brightness = gamma ? fmaxf(.5f, fminf(3, gamma->number)) : frontend->options.gamma;
    qa_profiler *profiler = qa_tools_profiler(frontend_tools_owner(frontend));
    bool profiling = qa_profiler_enabled(profiler);
    bool ok = frontend->cpu ? qa_cpu_set_gamma(frontend->cpu, brightness, error) :
        qa_gl_set_gamma(frontend->gl, brightness, error);
    if (ok && profiling) ok = qa_profiler_push(profiler, frontend->cpu ? "cpu_render" : "gl_submit", error);
    if (ok) {
        ok = frontend->cpu ? qa_cpu_execute(frontend->cpu, &frontend->frame, error) :
            qa_gl_execute(frontend->gl, &frontend->frame, error);
        if (profiling) ok = frontend_profiler_end(profiler, ok, error);
    }
    if (ok && frontend->gl && !frontend->frame.source_backend) {
        if (profiling) ok = qa_profiler_push(profiler, "gl_completion", error);
        if (ok) {
            ok = qa_gl_finish(frontend->gl, error);
            if (profiling) ok = frontend_profiler_end(profiler, ok, error);
        }
    }
    if (ok && profiling) ok = qa_profiler_push(profiler, "window_present", error);
    if (ok) {
        ok = frontend->cpu ? qa_cpu_present_frame(frontend->cpu, error) : qa_gl_swap(frontend->gl, error);
        if (profiling) ok = frontend_profiler_end(profiler, ok, error);
    }
    return ok;
}
static bool local_q1_view(qa_frontend *f, unsigned physical, qa_actor_id actor,
    const qa_application_camera_view *camera, const frontend_q1_view_settings *view,
    qa_scene_view *scene, qa_error *error)
{
    frontend_seat *seat = f->seats + physical;
    uint32_t logical; frontend_config_legacy_view source; bool present;
    if (!frontend_seat_launch_id_read(f, physical, &logical))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 view lost its actual authored seat");
    if (!frontend_config_store_primary_legacy_read(f->config_store, logical, &source, &present, error)) return false;
    if (!present || source.product->family != QA_GAME_Q1) return true;
    qa_application_control_view control; qa_body_state body;
    qa_application_equipment_view equipment;
    qa_q1_clientdata client;
    if (!qa_application_control_read(f->application, actor, &control))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 view lost its actual selected player motion");
    if (!qa_world_body_read(qa_application_world(f->application), actor, &body, error) ||
        !qa_application_equipment_read(f->application, actor, &equipment, error) ||
        !qa_application_network_q1_clientdata(f->application, actor, &client, error)) return false;
    bool qw = source.product->edition == QA_EDITION_QUAKEWORLD;
    double seconds;
    if (qw) {
        qa_application_network_qw_source clock;
        if (!qa_application_network_qw_source_read(f->application, &clock, error)) return false;
        seconds = (double)clock.source_time_ns / 1000000000.0;
    } else {
        qa_application_network_q1_world clock;
        if (!qa_application_network_q1_world_read(f->application, equipment.primary, &clock, error)) return false;
        seconds = clock.seconds;
    }
    frontend_q1_motion_settings settings;
    if (!frontend_view_settings_q1_motion_sample(source.registry, qw, &settings, error)) return false;
    if (!qa_actor_id_equal(seat->q1_view_actor, actor)) {
        seat->q1_view_motion = (frontend_q1_view_motion){0}; seat->q1_view_actor = actor;
    }
    if (!qw && source.product->program_kind == QA_PROGRAM_BUILTIN) {
        qa_application_network_q1_feedback feedback;
        if (!qa_application_network_q1_consume_feedback(f->application, actor, &feedback, error)) return false;
        if (feedback.damage) {
            qa_vec3 from;
            if (!frontend_view_q1_damage_origin(feedback.armor,feedback.blood,feedback.origin,&from,error)) return false;
            frontend_view_q1_damage(&settings, body.origin, body.angles,
                feedback.armor, feedback.blood, from, seconds, &seat->q1_view_motion);
        }
    }
    frontend_q1_motion_input input = {.origin = camera->origin, .angles = camera->angles,
        .entity_angles = qa_v3(-camera->angles.x, camera->angles.y, body.angles.z),
        .velocity = body.velocity, .punch = equipment.kick_angles, .seconds = seconds,
        .frame_seconds = (double)seat->client_frame_ns / 1000000000.0,
        .view_height = camera->view_height, .view_size = (float)view->size, .quakeworld = qw,
        .grounded = control.ground.hit != QA_TRACE_HIT_NONE,
        .dead = client.health <= 0, .intermission = camera->cutscene};
    frontend_view_q1_motion(&settings, &input, &seat->q1_view_motion, &seat->q1_view_pose);
    if (!frontend_config_store_primary_legacy_current(f->config_store, &source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 view changed its retained CLIENT settings");
    qa_actor_owner provider;
    if (!qa_application_provider_owner(f->application,source.descriptor->selection.instance,&provider) ||
        !frontend_equipment_media_q1_faces_prepare(f,provider,!strcmp(source.product->campaign,"rogue"),error)) return false;
    seat->q1_view_ready = true;
    scene->origin = seat->q1_view_pose.origin; qa_vec3 angles = seat->q1_view_pose.angles;
    seat->q1_chase = !qw && view->chase && !camera->cutscene;
    if (seat->q1_chase && !frontend_view_q1_chase(view,
        qa_world_geometry(qa_application_world(f->application)),scene->origin,camera->angles,
        &scene->origin,&angles,error)) return false;
    frontend_camera_axes(angles, scene->axis);
    int32_t contents;
    if (!qa_scene_world_q1_contents(f->scene_world,scene->origin,&contents,error)) return false;
    seat->q1_blend=frontend_view_q1_blend(&settings,&seat->q1_view_motion,contents,qw,client.items);
    return true;
}
static bool scene_build(qa_frontend *frontend, bool *render, qa_error *error)
{
    *render = false;
    bool ready;
    if (!frontend_display_ready(frontend, &ready, error)) return false;
    if (!ready) return true;
    if (!frontend_equipment_media_prune(frontend,error) || !frontend_ui_features_sync(frontend, error) || !frontend_scene_sync(frontend, error) || !frontend_shader_sync(frontend, error)) return false;
    frontend_native_q3_factory native_factory = {.context = frontend,
        .compose = frontend_native_composition_create};
    qa_application_map_view native_map;
    bool native_ready = qa_application_get_state(frontend->application) == QA_APPLICATION_RUNNING &&
        qa_application_map_read(frontend->application, &native_map);
    if (native_ready && !frontend_native_q3_sync(frontend, &native_factory, error)) return false;
    qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
    if (!frontend_q3_texture_mode_begin_frame(frontend,error)) return false;
    if (!qa_scene_frame_material_order(&frontend->frame, frontend->order, error)) return false;
    qa_audio_listener listeners[4]; size_t listener_count = 0;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        qa_ui_preferences preferences;
        if (!qa_ui_preferences_read(qa_application_cvars(frontend->application), i, &preferences, error)) return false;
        qa_scene_rect rect = frontend_viewport(frontend, i);
        if (!qa_scene_frame_output_domain(&frontend->frame,rect,false,error)) return false;
        qa_ui_state ui;
        if (!frontend_source_prompt_prepare(seat->source_prompt,error) || !qa_ui_tick(seat->ui, (double)frontend->time_ns / 1000000, error) || !qa_ui_state_read(seat->ui, &ui, error)) return false;
        seat->q1_view_ready = false; seat->q1_chase = false;
        qa_actor_id actor = {0}; qa_application_camera_view camera;
        uint32_t launch_seat;
        bool published=frontend_seat_launch_id_read(frontend,i,&launch_seat);
        if (published || frontend_network_remote(frontend)) ui.fullscreen = false;
        bool game_focus = qa_input_seat_focus(seat->input) == QA_INPUT_GAME;
        bool live = published && qa_application_player_actor(frontend->application, launch_seat, &actor) && qa_application_control_camera(frontend->application, actor, &camera);
        bool qc_status=false;
        if (live && frontend->qc_messages) {
            qa_application_qc_client_presentation qc;
            bool override=false;
            if (!frontend_qc_messages_client_vitals(frontend->qc_messages,actor,&qc,&qc_status,error) ||
                !frontend_qc_messages_client_camera(frontend->qc_messages,actor,&camera,&override,error)) return false;
        }
        qa_application_presentation_view source = {0};
        bool local_presentation = published &&
            qa_application_presentation_read(frontend->application, launch_seat, &source);
        bool source_weapon_status=false;
        if (live) {
            qa_application_equipment_view equipped; bool present=false;
            if (!qa_application_equipment_source_read(frontend->application,actor,&equipped,&present,error)) return false;
            source_weapon_status=present && equipped.selected;
            if (source_weapon_status) {
                frontend_equipment_media *media=NULL; const qa_material *icon=NULL;
                if (!frontend_equipment_media_prepare_source_icon(frontend,&equipped,&media,&icon,error)) return false;
            } else if (!present) {
                const qa_material *icon = NULL;
                if (!qa_application_equipment_read(frontend->application, actor, &equipped, error) ||
                    !frontend_equipment_media_native_icon_prepare(frontend, &equipped, &icon, error)) return false;
            }
        }
        qa_scene_view view = {.viewport = rect, .seat = i, .clear_color = true, .clear_depth = true,
            .color = {.015f, .02f, .03f, 1}, .depth = 1};
        bool legacy_receiver=false,legacy_clear=true;
        if (!frontend_remote_q1_initial_clear(frontend,i,&legacy_receiver,&legacy_clear,error)) return false;
        if (!legacy_receiver && !frontend_remote_q2_initial_clear(frontend,i,&legacy_receiver,&legacy_clear,error)) return false;
        if (!legacy_receiver && !frontend_remote_unified_initial_clear(frontend,i,&legacy_receiver,&legacy_clear,error)) return false;
        if (legacy_receiver) view.clear_color=legacy_clear;
        else if (live && !ui.fullscreen && !source.source_world && frontend->scene_world && native_ready) {
            const qa_product *product=qa_catalog_product(qa_application_catalog(frontend->application),native_map.presentation);
            if (product && (product->family==QA_GAME_Q1 || product->family==QA_GAME_Q2)) {
                frontend_legacy_render_policy policy;
                if (!frontend_legacy_local_policy_read(frontend,i,product,&policy,error)) return false;
                view.clear_color=policy.lighting.clear;
            }
        }
        qa_application_native_q2_player_sample q2_client_view={0};
        uint32_t old_gun_frame=0;
        float q2_back_lerp=0;
        bool q2_client_view_ready=false;
        if (live && !source.source_world && !camera.cutscene && seat->q2_view_ready &&
            qa_actor_id_equal(actor,seat->q2_actor) &&
            !frontend_particle_q2_player_sample(frontend,i,actor,&q2_client_view,&old_gun_frame,
                &q2_back_lerp,&q2_client_view_ready,error)) return false;
        if (live) {
            view.origin = qa_vec_add(camera.origin, camera.view_offset);
            qa_vec3 angles = camera.angles;
            if (!source.source_world && !camera.cutscene && seat->q2_view_ready &&
                    qa_actor_id_equal(actor, seat->q2_actor)) {
                qa_application_visual_view appearance;
                if (!qa_application_visual_read(frontend->application, actor, &appearance, error)) return false;
                const qa_product *character = qa_catalog_product(qa_application_catalog(frontend->application), appearance.character_content);
                qa_vec3 offset=q2_client_view_ready ? q2_client_view.view_offset : seat->q2_view.offset;
                qa_vec3 origin=camera.origin;
                if (q2_client_view_ready) {
                    uint32_t authored;
                    frontend_config_legacy_view input_source;
                    bool found;
                    if (!frontend_seat_launch_id_read(frontend,i,&authored) ||
                        !frontend_config_store_primary_legacy_read(frontend->config_store,authored,&input_source,&found,error)) return false;
                    const qa_cvar_view *predict=found ? qa_cvars_find(input_source.registry,"cl_predict") : NULL;
                    if ((predict && predict->number==0) || (q2_client_view.movement_flags&64u)) origin=q2_client_view.origin;
                    angles=frontend_q2_lerp_camera_angles(&q2_client_view,camera.angles);
                } else angles=qa_vec_add(seat->q2_view.health>0 && !seat->q2_view.spectator ?
                    camera.angles : seat->q2_view.angles,seat->q2_view.kick_angles);
                view.origin=qa_vec_add(origin,offset);
                if (character && character->edition == QA_EDITION_RERELEASE) view.origin.z += camera.view_height;
            }
            frontend_camera_axes(angles, view.axis);
        }
        else { view.axis[0] = qa_v3(1, 0, 0); view.axis[1] = qa_v3(0, 1, 0); view.axis[2] = qa_v3(0, 0, 1); }
        double ordinary_fov; bool explicit_override;
        if (!frontend_view_settings_read(frontend->view_settings, &ordinary_fov, &explicit_override))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Local camera lost its actual published view preference");
        (void)explicit_override;
        float fov_x = live && !source.source_world && seat->q2_view_ready &&
            qa_actor_id_equal(actor, seat->q2_actor) ?
            (q2_client_view_ready ? q2_client_view.fov : seat->q2_view.fov) : (float)ordinary_fov;
        float fov_y = 2 * atanf(tanf(fov_x * .008726646259971648f) * (float)rect.height / (float)rect.width) * 57.29577951308232f;
        view.projection = qa_scene_projection(fov_x, fov_y, 4, 16384);
        const qa_product *local_product = live && native_ready && !source.source_world ?
            qa_catalog_product(qa_application_catalog(frontend->application), native_map.presentation) : NULL;
        if (local_product && local_product->family == QA_GAME_Q1) {
            frontend_q1_view_settings settings;
            if (!frontend_view_settings_q1_sample(frontend->view_settings,
                local_product->edition == QA_EDITION_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1,
                &settings, error)) return false;
            if (!local_q1_view(frontend, i, actor, &camera, &settings, &view, error)) return false;
            view.viewport = q1_view_rectangle(rect, &settings, camera.cutscene);
            if (!q1_view_projection(frontend, &view, ordinary_fov, error)) return false;
        }
        if (!frontend_tools_camera(frontend, i, false, &view, error)) return false;
        if (!frontend_view_background(frontend, rect, &view, error)) return false;
        qa_scene_command begin = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = view};
        if (!qa_scene_frame_emit(&frontend->frame, &begin, error)) return false;
        if (!frontend_source_frame(frontend, i, rect, error) ||
            !frontend_native_q2_frame(frontend, i, rect, error)) return false;
        bool remote_rendered=false,remote_listener_present=false,native_rendered=false,common_hud_drawn=false;
        qa_audio_listener remote_listener;
        if (!frontend_network_client_draw(frontend,i,0,&remote_rendered,&remote_listener,
            &remote_listener_present,error)) return false;
        if (!remote_rendered) {
            if (!frontend_remote_q2_draw(frontend,i,0,&remote_listener,&remote_rendered,error)) return false;
            remote_listener_present=remote_rendered && remote_listener.actor!=QA_AUDIO_NO_ACTOR;
        }
        if (!remote_rendered) {
            if (!frontend_remote_unified_draw(frontend,i,0,&remote_listener,&remote_rendered,error)) return false;
            remote_listener_present=remote_rendered && remote_listener.actor!=QA_AUDIO_NO_ACTOR;
        }
        if (!remote_rendered) {
            if (!remote_q1_present(frontend,i,&view,&remote_listener,&remote_rendered,&common_hud_drawn,&preferences,!ui.fullscreen,error)) return false;
            remote_listener_present=remote_rendered && remote_listener.actor!=QA_AUDIO_NO_ACTOR;
        }
        if (remote_rendered) native_rendered=true;
        else if (native_ready && !frontend_native_q3_frame(frontend,i,rect,&native_rendered,error)) return false;
        if (live && !ui.fullscreen && !source.source_world && !native_rendered && frontend->scene_world) {
            qa_scene_world_input world = {.view = view, .seconds = (double)frontend->time_ns / 1e9,
                .milliseconds = (int64_t)(frontend->time_ns / 1000000), .identity_light = 1, .curve_error = 4,
                .video_frame=frontend_material_movies_frontend_resolve,.video_context=frontend};
            frontend_source_companion_legacy *companion=NULL;
            bool okay=frontend_event_world(frontend,i,&world,error);
            const qa_product *product=native_ready?
                qa_catalog_product(qa_application_catalog(frontend->application),native_map.presentation):NULL;
            if (okay && product && (product->family==QA_GAME_Q1 || product->family==QA_GAME_Q2))
                okay=frontend_source_companion_legacy_prepare(frontend,i,actor,frontend->scene_world,
                    product->family==QA_GAME_Q1?QA_SCENE_Q1:QA_SCENE_Q2,&world,&companion,error);
            if (okay) okay=frontend_legacy_scene_submit(frontend,i,0,&world,&frontend->frame,error);
            if (okay) okay=frontend_source_companion_legacy_submit(companion,&frontend->frame,error);
            frontend_source_companion_legacy_dispose(&companion);
            if (!okay) return false;
        }
        if (!source.source_world && !native_rendered && (!frontend_event_debug(frontend, &view, error) ||
            !frontend_tools_debug(frontend, &view, error))) return false;
        uint32_t real_milliseconds = (uint32_t)((frontend->time_ns / 1000000) & UINT32_MAX);
        if (local_presentation && !frontend_source_present(frontend, i, launch_seat, real_milliseconds,
                frontend_network_remote(frontend) ? frontend_network_client_time(frontend) :
                    real_milliseconds, error)) return false;
        if (!frontend_native_q2_world_text(frontend, i, &view, error) ||
            !frontend_qc_rerelease_draw(frontend, i, &view, error)) return false;
        if (remote_rendered && frontend->audio) {
            if (remote_listener_present) listeners[listener_count++]=remote_listener;
        } else if ((live || native_rendered) && frontend->audio) {
            qa_audio_listener *listener = &listeners[listener_count++];
            if (native_rendered) {
                if (!frontend_native_q3_listener(frontend, i, listener))
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Native draw did not publish its actual listener");
            } else {
                *listener = (qa_audio_listener){.seat = i, .actor = frontend_audio_actor(frontend, actor, error),
                    .origin = view.origin, .gain = 1.0f / (float)frontend->options.seats};
                if (listener->actor == QA_AUDIO_NO_ACTOR) return false;
                memcpy(listener->axis, view.axis, sizeof(view.axis));
                frontend_source_listener(frontend, i, listener);
            }
        }
        if (live && !ui.fullscreen && !source.source_world && !native_rendered && seat->q1_view_ready &&
                qa_actor_id_equal(actor,seat->q1_view_actor) && seat->q1_blend.w>0 && !preferences.reduced_flashes) {
            frontend_legacy_render_policy policy;
            if (!local_product || !frontend_legacy_local_policy_read(frontend,i,local_product,&policy,error)) return false;
            if (policy.lighting.polyblend && !qa_scene_frame_picture(&frontend->frame,
                qa_scene_white(frontend->ui_images),view.viewport,view.viewport,
                (qa_scene_vec4){0,0,1,1},seat->q1_blend,error)) return false;
        }
        if (live && !ui.fullscreen && !source.source_world && !native_rendered && seat->q2_view_ready &&
                qa_actor_id_equal(actor, seat->q2_actor) && seat->q2_view.blend.w > 0 && !preferences.reduced_flashes) {
            if (!native_ready) return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 blend lost its actual published map policy");
            const qa_product *product=qa_catalog_product(qa_application_catalog(frontend->application),native_map.presentation);
            frontend_legacy_render_policy policy;
            if (!product || !frontend_legacy_local_policy_read(frontend,i,product,&policy,error)) return false;
            if (policy.lighting.polyblend) {
                qa_q2_blend blend = seat->q2_view.blend;
                if (!qa_scene_frame_picture(&frontend->frame, qa_scene_white(frontend->ui_images), rect, rect,
                        (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){blend.x, blend.y, blend.z, blend.w}, error)) return false;
            }
        }
        if (!frontend_q3_generic_overlay_begin(frontend,rect,error)) return false;
        bool component_status=false;
        if (live && !source_status_native(frontend,i,actor,&component_status,error)) return false;
        if (live && !common_hud_drawn && (!native_rendered || source_weapon_status || qc_status) && !qa_hud_draw(seat->hud, &(qa_hud_frame){.seat = i, .actor = actor,
            .weapon_only=native_rendered && !qc_status,.source_status_native=component_status,
            .time_ns = frontend->time_ns, .viewport = view.viewport, .safe_area = rect,
            .scale = preferences.hud_scale, .show_scores = seat->scores || (seat->q2_view_ready && !seat->q2_help && (seat->q2_view.layouts & 1)),
            .show_inventory = seat->q2_inventory, .visible = !ui.fullscreen && game_focus}, &frontend->frame, error)) return false;
        if (live && !ui.fullscreen && game_focus && !qa_hud_wheel_draw(seat->wheel,
            &(qa_hud_wheel_draw_options){.viewport = rect, .fonts = seat->fonts,
                .white = qa_scene_white(frontend->ui_images), .text = {1, 1, 1, 1},
                .accent = {.9f, .7f, .3f, 1}, .disabled = {.4f, .4f, .4f, 1},
                .panel = {.05f, .05f, .05f, .85f}, .scale = preferences.hud_scale}, &frontend->frame, error)) return false;
        if (!qa_ui_draw(seat->ui, &frontend->frame, rect, preferences.menu_scale,
            preferences.high_contrast, !(live || native_rendered || remote_rendered || source.source_world), error)) return false;
        if (qa_input_seat_focus(seat->input) == QA_INPUT_CONSOLE) {
            qa_font_selection console_fonts;
            if (!frontend_console_font_selection(frontend, i, &console_fonts, error)) return false;
            qa_field_view field = qa_text_field_read(qa_seat_console_field(seat->console, false));
            qa_console_draw_options console = {.target = rect, .font = &console_fonts,
                .buffer = qa_seat_console_buffer(seat->console), .field = &field,
                .background = frontend->console_background, .now_milliseconds = (double)frontend->time_ns / 1000000,
                .height = (float)rect.height * .6f, .scale = preferences.text_scale};
            if (!qa_console_draw(&frontend->frame, &console, error)) return false;
        }
        if (!frontend_q3_generic_overlay_end(frontend,error)) return false;
    }
    if (frontend->audio && !qa_audio_engine_listeners(frontend->audio, listeners, listener_count, error)) return false;
    if (!frontend_render_controls_live(frontend,error)) return false;
    if (frontend->frame.source_backend && frontend->frame.source_skip_backend) {
        if (frontend->frame.source_pending &&
            !qa_material_source_frame_end(frontend->frame.source_pending,&frontend->frame,false,error)) return false;
        return true;
    }
    *render = true;
    return true;
}

bool frontend_present(qa_frontend *frontend, qa_error *error)
{
    qa_profiler *profiler = qa_tools_profiler(frontend_tools_owner(frontend));
    bool profiling = qa_profiler_enabled(profiler);
    bool render = false;
    if (profiling && !qa_profiler_push(profiler, "scene_build", error)) return false;
    bool ok = scene_build(frontend, &render, error);
    if (profiling) ok = frontend_profiler_end(profiler, ok, error);
    if (!ok || !render) return ok;
    return frontend_frame_present(frontend, error);
}

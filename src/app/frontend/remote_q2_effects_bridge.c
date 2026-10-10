#include "remote_q2_effects_bridge.h"
#include "remote_q2_private.h"
#include "remote_q2_restore.h"
#include "remote_q2_source.h"
#include "remote_q2_footsteps.h"
#include "remote_q2_material_movies_bridge.h"
#include "qa/console_cvar_observer.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static qa_vec3 vector(const float value[3]) { return qa_v3(value[0], value[1], value[2]); }
static bool model(void *, const char *, bool, qa_scene_model **, qa_error *);
static bool actor(void *, uint32_t, frontend_remote_q2_effects_pose *, qa_error *);
static bool actor_pose(void *, qa_actor_id, frontend_remote_q2_effects_pose *, qa_error *);
static bool hit_marker(void *, int32_t, qa_error *);
static bool controls(void *, frontend_remote_q2_effects_controls *, qa_error *);
static bool viewer(void *, qa_actor_id *, qa_error *);
static bool frame_milliseconds(void *, double *, qa_error *);
static bool render_clock(void *, uint64_t *, uint64_t *, qa_error *);
static bool source_current(void *context, const frontend_remote_q2_effects_source *source, qa_error *error)
{
    frontend_remote_q2 *row = context; frontend_remote_q2_view view;
    bool retained = row && row->importing ? frontend_remote_q2_import_read(row, &view, error) :
        frontend_remote_q2_metadata_read(row, &view, error);
    if (!retained || !source || source->context != row || row->frontend->application != row->options.domain.application ||
        source->profile != (remote_q2_rerelease_presentation(row) ? FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE : FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC) ||
        source->current != source_current || source->actor != actor || source->actor_pose != actor_pose || source->actor_live != NULL ||
        source->video_frame != frontend_material_movies_frontend_resolve || source->video_context != row->frontend ||
        source->model != model || source->sound != remote_q2_effect_sound || source->hit_marker != hit_marker ||
        source->controls != controls ||
        source->frame_milliseconds != frame_milliseconds ||
        source->render_clock != render_clock ||
        source->viewer != viewer ||
        source->footstep != remote_q2_footstep || source->trace != remote_q2_trace ||
        source->session != qa_application_session(row->options.domain.application) || source->identity != row->identity ||
        source->content_generation != row->content_generation || source->protocol.kind != row->options.domain.protocol.kind ||
        source->protocol.revision != row->options.domain.protocol.revision || source->protocol.flags != row->options.domain.protocol.flags ||
        source->map != row->map || source->files != row->content.mounts || source->images != row->images ||
        source->materials != row->materials || source->world != row->world || source->white != row->white || !row->selected || !row->map ||
        !qa_catalog_product_view_current(row->content.catalog, row->content.selected, row->content.mounts))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 effects left their actual private CLIENT resource tuple");
    if (row->importing) return row->frontend->source_restoring;
    if (row->frontend->resource_inventory) return !row->busy &&
        (!row->retiring || frontend_remote_q2_source_retirement_metadata_current(row, error));
    if (row->frontend->capture) return remote_q2_capture_owned(row);
    return remote_q2_live(row, error);
}
static bool model(void *context, const char *path, bool acquire, qa_scene_model **out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !path || !out) return false;
    *out = NULL;
    for (remote_q2_model *held = row->models; held; held = held->next)
        if (!strcmp(held->path, path)) { *out = held->scene; return held->scene != NULL; }
    for (remote_q2_missing_model *held = row->missing_models; held; held = held->next)
        if (!strcmp(held->path, path)) return true;
    if (!acquire) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 effect model has no retained acquisition admission");
    remote_q2_model *held = NULL; qa_error issue = {0};
    if (remote_q2_model_read(row, path, &held, &issue)) { *out = held->scene; return true; }
    if (issue.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = issue;
    return false;
}
static bool actor(void *context, uint32_t number, frontend_remote_q2_effects_pose *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !out || !row->options.entity_actor || !frontend_remote_q2_entity_received(row, number))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 effect entity has no genuine received CLIENT observer");
    frontend_remote_q2_effects_pose pose = {.number = number};
    if (!row->options.entity_actor(row->options.context, &row->options.domain, number, &pose.actor, error) ||
        !qa_actors_get(qa_session_actor_registry(qa_application_session(row->options.domain.application)), pose.actor)) return false;
    const qa_q2_entity *entity=remote_q2_frame_entity(&row->frame,number);
    if (entity) {
        pose.origin = vector(entity->origin); pose.angles = vector(entity->angles);
        pose.frame = (int32_t)entity->frame; pose.effects = entity->effects; pose.event = entity->event;
        pose.model_index = entity->modelindex;
        pose.scale = entity->scale != 0 ? entity->scale : 1;
        pose.bounds_present = true;
        if (entity->solid && entity->solid != 31) {
            pose.bounds = remote_q2_solid_bounds(row, entity->solid);
            pose.radius = qa_vec_length(qa_vec_sub(pose.bounds.maxs, pose.bounds.mins)) * .5f;
        }
        if (entity->modelindex && entity->modelindex < row->layout.max_models) {
            const char *path = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + entity->modelindex));
            for (remote_q2_model *held = row->models; held; held = held->next) if (!strcmp(held->path, path) && held->source) {
                pose.model_present = true; break;
            }
        }
        *out = pose; return true;
    }
    for (size_t i = 0; i < row->frame.player_count; ++i) {
        const qa_q2_player *player = &row->frame.players[i].player;
        int32_t player_number;
        if (frontend_remote_q2_player_number(row, &row->frame, i, &player_number) &&
            player_number >= 0 && (uint32_t)player_number + 1 == number) {
            pose.origin = remote_q2_float_movement(row) ? vector(player->pmove.origin_f) :
                qa_v3((float)player->pmove.origin[0] * .125f, (float)player->pmove.origin[1] * .125f,
                    (float)player->pmove.origin[2] * .125f);
            pose.angles = vector(player->viewangles); pose.scale = 1; *out = pose; return true;
        }
    }
    return false;
}
static bool viewer(void *context, qa_actor_id *out, qa_error *error)
{
    frontend_remote_q2 *row = context; uint32_t index;
    if (!row || !out || !remote_q2_live(row, error) || !row->frame.valid ||
        !frontend_remote_q2_wire_seat(row, &index, error) || index >= row->frame.player_count) return false;
    int32_t number;
    if (!frontend_remote_q2_player_number(row, &row->frame, index, &number) || number < 0) return false;
    frontend_remote_q2_effects_pose pose;
    if (!actor(row, (uint32_t)number + 1, &pose, error)) return false;
    *out = pose.actor; return true;
}
bool remote_q2_sound_asset(frontend_remote_q2 *row, const char *path, uint32_t entity,
    qa_audio_asset **out, qa_error *error)
{
    if (path[0] != '*') return qa_audio_bank_register(row->sounds, path, QA_GAME_Q2, out, error);
    const char *info = entity && entity <= 256 ?
        frontend_remote_q2_config(row, (uint16_t)(row->layout.players + entity - 1)) : "";
    const char *appearance = strchr(info, '\\');
    return qa_audio_bank_sexed(row->sounds, path, appearance ? appearance + 1 : "", out, error);
}

bool remote_q2_effect_sound(void *context, const char *path, qa_vec3 origin, qa_actor_id actor_id,
    double time, int32_t channel, float volume, float attenuation, double delay, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !row->sounds || !path || !qa_vec_finite(origin) || !isfinite(time) || !isfinite(delay)) return false;
    uint64_t actor_number = QA_AUDIO_NO_ACTOR;
    if (actor_id.registry) {
        const qa_actor_record *record = qa_actors_get(qa_session_actor_registry(
            qa_application_session(row->options.domain.application)), actor_id);
        if (!record || !record->has_source || !frontend_remote_q2_entity_received(row, record->source_slot)) return false;
        qa_actor_id actual;
        if (!row->options.entity_actor || !row->options.entity_actor(row->options.context, &row->options.domain,
            record->source_slot, &actual, error) || !qa_actor_id_equal(actual, actor_id)) return false;
        actor_number = record->source_slot;
    }
    if (!row->frontend->audio) return true;
    qa_audio_asset *asset = NULL;
    if (!remote_q2_sound_asset(row, path, (uint32_t)actor_number, &asset, error)) return false;
    if (!asset) return true;
    qa_audio_play play = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = path, .family = QA_GAME_Q2,
        .actor = actor_number, .owner = row->identity, .audience = row->options.domain.physical_seat,
        .origin_kind = QA_AUDIO_FIXED, .origin = origin, .channel = channel, .volume = volume,
        .attenuation = attenuation, .delay_seconds = delay, .server_milliseconds = time, .has_server_time = true};
    bool ok = qa_audio_engine_play(row->frontend->audio, &play, (int32_t)(row->sample_ns / 1000000), error);
    qa_audio_asset_release(asset); return ok;
}
static bool actor_pose(void *context, qa_actor_id id, frontend_remote_q2_effects_pose *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actor_registry(
        qa_application_session(row->options.domain.application)), id);
    return record && record->has_source && actor(row, record->source_slot, out, error) &&
        qa_actor_id_equal(out->actor, id);
}
static bool hit_marker(void *context, int32_t damage, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || damage <= 0 || !remote_q2_live(row, error)) return false;
    if (row->hit_marker_set && row->hit_marker_frame == row->frame.server_frame) return true;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(row->options.domain.runtime),
        row->options.domain.client);
    if (!client || qa_network_epoch(row->options.domain.runtime, client->id) != row->options.domain.epoch) return false;
    row->hit_marker_set = true; row->hit_marker_frame = row->frame.server_frame;
    row->hit_marker_ns = client->received_ns > row->sample_ns ? client->received_ns : row->sample_ns;
    if (row->hit_marker_count < UINT32_MAX) ++row->hit_marker_count;
    const qa_cvar_view *setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_hit_markers);
    if (setting && setting->number > 1) {
        qa_vec3 position = qa_v3(0, 0, 0); uint32_t index;
        if (!frontend_remote_q2_wire_seat(row, &index, error)) return false;
        if (row->frame.valid && index < row->frame.player_count) {
            const qa_q2_player *player = &row->frame.players[index].player;
            position = remote_q2_float_movement(row) ? vector(player->pmove.origin_f) :
                qa_v3((float)player->pmove.origin[0] * .125f, (float)player->pmove.origin[1] * .125f, (float)player->pmove.origin[2] * .125f);
            position = qa_vec_add(position, vector(player->viewoffset));
        }
        return remote_q2_effect_sound(row, "weapons/marker.wav", position, (qa_actor_id){0},
            ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms, 257, 1, 0, 0, error);
    }
    return true;
}
static bool controls_current(void *context, qa_error *error)
{ return remote_q2_live(context, error); }
static bool controls(void *context, frontend_remote_q2_effects_controls *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row) return false;
    frontend_remote_q2_effects_control_source source = {
        .cvars = row->options.domain.cvars, .handles = &row->cvar_handles.effects,
        .gun = row->cvar_handles.legacy.cl_gun, .console = row->options.domain.console,
        .command_context = &row->options.domain.command_context,
        .context = row, .current = controls_current};
    return frontend_remote_q2_effects_controls_read(&source, out, error);
}
static bool frame_milliseconds(void *context, double *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!out || !row || !remote_q2_live(row, error) || !isfinite(row->frame_ms) || row->frame_ms <= 0) return false;
    *out = row->frame_ms; return true;
}
static bool render_clock(void *context, uint64_t *wall, uint64_t *sequence, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !wall || !sequence || !remote_q2_live(row, error)) return false;
    *wall = row->frontend->wall_time_ns / UINT64_C(1000000);
    *sequence = row->frontend->frame_number; return true;
}
bool remote_q2_hit_marker_sample(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !remote_q2_live(row, error)) return false;
    if (!remote_q2_rerelease_presentation(row) || !row->media_ready || !row->frame.valid) return true;
    const qa_cvar_view *setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_hit_markers);
    if (!setting || !setting->integer) return true;
    uint32_t seat;
    if (!frontend_remote_q2_wire_seat(row, &seat, error) || seat >= row->frame.player_count) return false;
    int32_t damage = row->frame.players[seat].player.stats[50];
    return !damage || hit_marker(row, damage < 0 ? -damage : damage, error);
}
bool remote_q2_effects_source_read(frontend_remote_q2 *row, frontend_remote_q2_effects_source *out, qa_error *error)
{
    if (!row || !out) return false;
    *out = (frontend_remote_q2_effects_source){.session = qa_application_session(row->options.domain.application),
        .actor_capacity = qa_actors_capacity(qa_session_actor_registry(qa_application_session(row->options.domain.application))),
        .identity = row->identity, .content_generation = row->content_generation, .protocol = row->options.domain.protocol,
        .profile = remote_q2_rerelease_presentation(row) ? FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE : FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC,
        .map = row->map, .files = row->content.mounts, .images = row->images, .materials = row->materials, .world = row->world,
        .white = row->white, .context = row, .current = source_current, .actor = actor, .actor_pose = actor_pose, .viewer = viewer,
        .video_frame = frontend_material_movies_frontend_resolve, .video_context = row->frontend,
        .model = model, .sound = remote_q2_effect_sound, .hit_marker = hit_marker,
        .controls = controls,
        .frame_milliseconds = frame_milliseconds,
        .render_clock = render_clock,
        .footstep = remote_q2_footstep, .trace = remote_q2_trace};
    return source_current(row, out, error);
}
bool remote_q2_effects_create(frontend_remote_q2 *row, qa_error *error)
{
    frontend_remote_q2_effects_source source;
    return remote_q2_effects_source_read(row, &source, error) &&
        frontend_remote_q2_effects_create(&source, &row->effects, error);
}
const qa_scene_image *frontend_remote_q2_effects_image(const frontend_remote_q2 *row)
{
    frontend_remote_q2_view view; qa_error error = {0};
    bool retained = row && row->importing ? frontend_remote_q2_import_read(row, &view, &error) :
        frontend_remote_q2_metadata_read(row, &view, &error);
    return retained ? frontend_remote_q2_effects_particle_image(row->effects) : NULL;
}
bool frontend_remote_q2_effects_records(frontend_remote_q2 *row,
    const qa_q2_server_record *records, size_t count, qa_error *error)
{
    uint32_t seat;
    if (!row || (count && !records) || !remote_q2_live(row, error) ||
        !frontend_remote_q2_wire_seat(row, &seat, error)) return false;
    double server = row->frame.valid ? (double)row->frame.server_frame * row->frame_ms : 0;
    double time = row->frame.valid ? ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms : 0;
    for (size_t i = 0; i < count; ++i) {
        const qa_q2_server_record *record = records + i;
        if (record->seat && record->seat != seat + 1) continue;
        if (record->event.kind != QA_Q2_SVC_TEMP_ENTITY && record->event.kind != QA_Q2_SVC_MUZZLEFLASH) continue;
        if (!row->effects) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 service effects arrived before their real media owner");
        if (record->event.kind == QA_Q2_SVC_TEMP_ENTITY) {
            if (!frontend_remote_q2_effects_temporary(row->effects, &record->event.data.temporary, NULL, time, server, error)) return false;
        } else if (!frontend_remote_q2_effects_muzzle(row->effects, record->event.data.muzzle.entity,
            record->event.data.muzzle.flash, record->event.data.muzzle.monster, record->event.data.muzzle.silenced,
            time, server, error)) return false;
    }
    return remote_q2_live(row, error);
}
static bool effect_poses_reserve(frontend_remote_q2 *row,qa_error *error)
{
    if (row->frame.entity_count<=row->effect_pose_capacity) return true;
    size_t capacity=row->effect_pose_capacity?row->effect_pose_capacity:128;
    while (capacity<row->frame.entity_count) {
        if (capacity>SIZE_MAX/2) { capacity=row->frame.entity_count;break; }
        capacity*=2;
    }
    if (capacity>SIZE_MAX/sizeof(*row->effect_poses))
        return remote_q2_fail(error,QA_ERROR_MEMORY,"Q2 effect pose extent overflows");
    frontend_remote_q2_effects_pose *poses=realloc(row->effect_poses,capacity*sizeof(*poses));
    if (!poses) return remote_q2_fail(error,QA_ERROR_MEMORY,"Retaining Q2 effect pose capacity");
    row->effect_poses=poses;row->effect_pose_capacity=capacity;return true;
}
bool remote_q2_effects_frame(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !row->effects || !row->frame.valid || !remote_q2_live(row, error) ||
        row->frame.entity_count > SIZE_MAX / sizeof(frontend_remote_q2_effects_pose)) return false;
    if (!effect_poses_reserve(row,error)) return false;
    frontend_remote_q2_effects_pose *poses=row->effect_poses;
    bool ok = true;
    for (size_t i = 0; ok && i < row->frame.entity_count; ++i) ok = actor(row, row->frame.entities[i].number, poses + i, error);
    const qa_cvar_view *footsteps = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.legacy.cl_footsteps);
    frontend_remote_q2_effects_sample sample = {
        .milliseconds = ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms,
        .server_milliseconds = (double)row->frame.server_frame * row->frame_ms,
        .frame_sequence = (uint64_t)(uint32_t)row->frame.server_frame, .fraction = row->fraction,
        .entities = poses, .entity_count = row->frame.entity_count, .footsteps = footsteps ? (float)footsteps->number : 1};
    if (ok) ok = frontend_remote_q2_effects_frame(row->effects, &sample, error);
    return ok;
}
bool remote_q2_effects_sample_prepare(frontend_remote_q2 *row, const qa_scene_view *view, float player_fov, qa_vec3 viewer_origin, qa_vec3 gun_offset,
    int32_t viewer_number, frontend_remote_q2_effects_sample *sample,
    const qa_scene_light **lights, size_t *count, qa_error *error)
{
    if (!row || !row->effects || !view || !isfinite(player_fov) || player_fov <= 0 || player_fov >= 180 ||
        !qa_vec_finite(viewer_origin) || !sample || !lights || !count) return false;
    if (!effect_poses_reserve(row,error)) return false;
    frontend_remote_q2_effects_pose *poses=row->effect_poses;
    bool ok = true;size_t previous_cursor=0;
    for (size_t i = 0; ok && i < row->frame.entity_count; ++i) {
        const qa_q2_entity *entity = row->frame.entities + i;
        ok = actor(row, entity->number, poses + i, error);
        while (row->previous.valid && previous_cursor<row->previous.entity_count &&
            row->previous.entities[previous_cursor].number<entity->number) ++previous_cursor;
        if (ok && row->previous.valid && previous_cursor<row->previous.entity_count &&
            row->previous.entities[previous_cursor].number==entity->number) {
            const qa_q2_entity *previous=row->previous.entities+previous_cursor;
            qa_vec3 delta = qa_vec_sub(vector(previous->origin), poses[i].origin);
            if (previous->modelindex == entity->modelindex && entity->event != 6 && entity->event != 7 &&
                fabsf(delta.x) <= 512 && fabsf(delta.y) <= 512 && fabsf(delta.z) <= 512)
                poses[i].origin = qa_vec_lerp(vector(previous->origin), poses[i].origin, row->fraction);
        }
    }
    frontend_remote_q2_effects_pose viewer = {0};
    if (ok && viewer_number >= 0) ok = actor(row, (uint32_t)viewer_number + 1, &viewer, error);
    const qa_cvar_view *hand = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.legacy.hand);
    *sample = (frontend_remote_q2_effects_sample){
        .milliseconds = ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms,
        .server_milliseconds = (double)row->frame.server_frame * row->frame_ms,
        .fraction = row->fraction, .frame_sequence = (uint64_t)(uint32_t)row->frame.server_frame,
        .entities = poses, .entity_count = row->frame.entity_count, .view = *view, .viewer = viewer.actor,
        .viewer_origin = viewer_origin, .viewer_origin_present = true,
        .player_fov = player_fov,
        .gun_offset = gun_offset, .hand = hand && isfinite(hand->number) && hand->number >= 0 && hand->number <= 2 ?
            (int32_t)hand->number : 0, .hardware = row->frontend->gl != NULL,
        .frame_seconds = (float)row->sample_frame_seconds, .per_pixel_lighting = false};
    if (ok) ok = frontend_remote_q2_effects_prepare(row->effects, sample, lights, count, error);
    return ok;
}

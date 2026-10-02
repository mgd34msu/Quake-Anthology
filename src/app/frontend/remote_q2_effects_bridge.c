#include "remote_q2_effects_bridge.h"
#include "remote_q2_private.h"
#include "remote_q2_restore.h"
#include "remote_q2_footsteps.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static qa_vec3 vector(const float value[3]) { return qa_v3(value[0], value[1], value[2]); }
static bool model(void *, const char *, bool, qa_scene_model **, qa_error *);
static bool actor(void *, uint32_t, frontend_remote_q2_effects_pose *, qa_error *);
static bool actor_pose(void *, qa_actor_id, frontend_remote_q2_effects_pose *, qa_error *);
static bool hit_marker(void *, int32_t, qa_error *);
static bool controls(void *, frontend_remote_q2_effects_controls *, qa_error *);
static bool viewer(void *, qa_actor_id *, qa_error *);
static bool source_current(void *context, const frontend_remote_q2_effects_source *source, qa_error *error)
{
    frontend_remote_q2 *row = context; frontend_remote_q2_view view;
    bool retained = row && row->importing ? frontend_remote_q2_import_read(row, &view, error) :
        frontend_remote_q2_metadata_read(row, &view, error);
    if (!retained || !source || source->context != row || row->frontend->application != row->options.domain.application ||
        source->profile != (row->layout.max_models == 8192 ? FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE : FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC) ||
        source->current != source_current || source->actor != actor || source->actor_pose != actor_pose ||
        source->model != model || source->sound != remote_q2_effect_sound || source->hit_marker != hit_marker ||
        source->controls != controls ||
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
    if (row->frontend->resource_inventory) return !row->busy;
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
    for (size_t i = 0; i < row->frame.entity_count; ++i) if (row->frame.entities[i].number == number) {
        const qa_q2_entity *entity = row->frame.entities + i;
        pose.origin = vector(entity->origin); pose.angles = vector(entity->angles);
        pose.frame = (int32_t)entity->frame; pose.effects = entity->effects; pose.event = entity->event;
        pose.model_index = entity->modelindex;
        pose.scale = entity->scale ? entity->scale : 1;
        if (entity->modelindex && entity->modelindex < row->layout.max_models) {
            const char *path = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + entity->modelindex));
            for (remote_q2_model *held = row->models; held; held = held->next) if (!strcmp(held->path, path) && held->source) {
                pose.bounds = (qa_bounds){vector(held->source->bounds.min), vector(held->source->bounds.max)};
                pose.radius = held->source->radius; pose.bounds_present = true; pose.model_present = true; break;
            }
        }
        *out = pose; return true;
    }
    for (size_t i = 0; i < row->frame.player_count; ++i) {
        const qa_q2_player *player = &row->frame.players[i].player;
        if (player->clientnum >= 0 && (uint32_t)player->clientnum + 1 == number) {
            pose.origin = row->layout.max_models == 8192 ? vector(player->pmove.origin_f) :
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
    int32_t number = row->frame.players[index].player.clientnum;
    if (number < 0) return false;
    frontend_remote_q2_effects_pose pose;
    if (!actor(row, (uint32_t)number + 1, &pose, error)) return false;
    *out = pose.actor; return true;
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
    if (path[0] == '*') {
        const char *info = actor_number && actor_number <= 256 ?
            frontend_remote_q2_config(row, (uint16_t)(row->layout.players + actor_number - 1)) : "";
        const char *appearance = strchr(info, '\\'); appearance = appearance ? appearance + 1 : info;
        const char *slash = strchr(appearance, '/'); char model_name[1024];
        size_t length = slash ? (size_t)(slash - appearance) : 0;
        if (!length || length >= sizeof(model_name)) strcpy(model_name, "male");
        else { memcpy(model_name, appearance, length); model_name[length] = 0; }
        if (!qa_audio_bank_sexed(row->sounds, path, model_name, &asset, error)) return false;
    } else if (!qa_audio_bank_register(row->sounds, path, QA_AUDIO_Q2, &asset, error)) return false;
    if (!asset) return true;
    qa_audio_play play = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = path, .family = QA_AUDIO_Q2,
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
    const qa_cvar_view *setting = qa_cvars_find(row->options.domain.cvars, "cl_hit_markers");
    if (setting && setting->number > 1) {
        qa_vec3 position = qa_v3(0, 0, 0); uint32_t index;
        if (!frontend_remote_q2_wire_seat(row, &index, error)) return false;
        if (row->frame.valid && index < row->frame.player_count) {
            const qa_q2_player *player = &row->frame.players[index].player;
            position = row->layout.max_models == 8192 ? vector(player->pmove.origin_f) :
                qa_v3(player->pmove.origin[0] * .125f, player->pmove.origin[1] * .125f, player->pmove.origin[2] * .125f);
            position = qa_vec_add(position, vector(player->viewoffset));
        }
        return remote_q2_effect_sound(row, "weapons/marker.wav", position, (qa_actor_id){0},
            ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms, 257, 1, 0, 0, error);
    }
    return true;
}
static bool controls(void *context, frontend_remote_q2_effects_controls *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !out || !remote_q2_live(row, error)) return false;
    const qa_cvar_view *time = qa_cvars_find(row->options.domain.cvars, "cl_muzzlelight_time");
    const qa_cvar_view *effects = qa_cvars_find(row->options.domain.cvars, "cl_rerelease_effects");
    const qa_cvar_view *flashes = qa_cvars_find(row->options.domain.cvars, "cl_muzzleflashes");
    const qa_cvar_view *hacks = qa_cvars_find(row->options.domain.cvars, "cl_dlight_hacks");
    const qa_cvar_view *particles = qa_cvars_find(row->options.domain.cvars, "cl_disable_particles");
    const qa_cvar_view *explosions = qa_cvars_find(row->options.domain.cvars, "cl_disable_explosions");
    if (!time || !effects || !flashes || !hacks || !particles || !explosions) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 effects have no actual canonical CLIENT controls");
    *out = (frontend_remote_q2_effects_controls){.muzzlelight_milliseconds = time->integer,
        .rerelease_effects = effects->integer != 0, .muzzleflashes = flashes->integer != 0,
        .dlight_hacks = (uint32_t)hacks->integer, .disable_particles = (uint32_t)particles->integer,
        .disable_explosions = (uint32_t)explosions->integer};
    return true;
}
bool remote_q2_effects_source_read(frontend_remote_q2 *row, frontend_remote_q2_effects_source *out, qa_error *error)
{
    if (!row || !out) return false;
    *out = (frontend_remote_q2_effects_source){.session = qa_application_session(row->options.domain.application),
        .identity = row->identity, .content_generation = row->content_generation, .protocol = row->options.domain.protocol,
        .profile = row->layout.max_models == 8192 ? FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE : FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC,
        .map = row->map, .files = row->content.mounts, .images = row->images, .materials = row->materials, .world = row->world,
        .white = row->white, .context = row, .current = source_current, .actor = actor, .actor_pose = actor_pose, .viewer = viewer,
        .model = model, .sound = remote_q2_effect_sound, .hit_marker = hit_marker,
        .controls = controls,
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
bool remote_q2_effects_frame(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !row->effects || !row->frame.valid || !remote_q2_live(row, error) ||
        row->frame.entity_count > SIZE_MAX / sizeof(frontend_remote_q2_effects_pose)) return false;
    frontend_remote_q2_effects_pose *poses = row->frame.entity_count ? calloc(row->frame.entity_count, sizeof(*poses)) : NULL;
    if (row->frame.entity_count && !poses) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 received frame event poses");
    bool ok = true;
    for (size_t i = 0; ok && i < row->frame.entity_count; ++i) ok = actor(row, row->frame.entities[i].number, poses + i, error);
    const qa_cvar_view *footsteps = qa_cvars_find(row->options.domain.cvars, "cl_footsteps");
    frontend_remote_q2_effects_sample sample = {
        .milliseconds = ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms,
        .server_milliseconds = (double)row->frame.server_frame * row->frame_ms,
        .frame_sequence = (uint64_t)(uint32_t)row->frame.server_frame, .fraction = row->fraction,
        .entities = poses, .entity_count = row->frame.entity_count, .footsteps = footsteps ? (float)footsteps->number : 1};
    if (ok) ok = frontend_remote_q2_effects_frame(row->effects, &sample, error);
    free(poses); return ok;
}
bool remote_q2_effects_sample_prepare(frontend_remote_q2 *row, const qa_scene_view *view, qa_vec3 gun_offset,
    int32_t viewer_number, frontend_remote_q2_effects_sample *sample, frontend_remote_q2_effects_pose **owned,
    const qa_scene_light **lights, size_t *count, qa_error *error)
{
    if (!row || !row->effects || !view || !sample || !owned || *owned || !lights || !count ||
        row->frame.entity_count > SIZE_MAX / sizeof(**owned)) return false;
    frontend_remote_q2_effects_pose *poses = row->frame.entity_count ? calloc(row->frame.entity_count, sizeof(*poses)) : NULL;
    if (row->frame.entity_count && !poses) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 effects view poses");
    bool ok = true;
    for (size_t i = 0; ok && i < row->frame.entity_count; ++i) {
        const qa_q2_entity *entity = row->frame.entities + i;
        ok = actor(row, entity->number, poses + i, error);
        for (size_t j = 0; ok && row->previous.valid && j < row->previous.entity_count; ++j) {
            const qa_q2_entity *previous = row->previous.entities + j;
            if (previous->number != entity->number) continue;
            qa_vec3 delta = qa_vec_sub(vector(previous->origin), poses[i].origin);
            if (previous->modelindex == entity->modelindex && entity->event != 6 && entity->event != 7 &&
                fabsf(delta.x) <= 512 && fabsf(delta.y) <= 512 && fabsf(delta.z) <= 512)
                poses[i].origin = qa_vec_lerp(vector(previous->origin), poses[i].origin, row->fraction);
            break;
        }
    }
    frontend_remote_q2_effects_pose viewer = {0};
    if (ok && viewer_number >= 0) ok = actor(row, (uint32_t)viewer_number + 1, &viewer, error);
    const qa_cvar_view *hand = qa_cvars_find(row->options.domain.cvars, "hand");
    *sample = (frontend_remote_q2_effects_sample){
        .milliseconds = ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms,
        .server_milliseconds = (double)row->frame.server_frame * row->frame_ms,
        .fraction = row->fraction, .frame_sequence = (uint64_t)(uint32_t)row->frame.server_frame,
        .entities = poses, .entity_count = row->frame.entity_count, .view = *view, .viewer = viewer.actor,
        .gun_offset = gun_offset, .hand = hand && isfinite(hand->number) && hand->number >= 0 && hand->number <= 2 ?
            (int32_t)hand->number : 0, .hardware = row->frontend->gl != NULL,
        .frame_seconds = (float)row->sample_frame_seconds, .per_pixel_lighting = false};
    if (ok) ok = frontend_remote_q2_effects_prepare(row->effects, sample, lights, count, error);
    if (!ok) { free(poses); return false; }
    *owned = poses; return true;
}

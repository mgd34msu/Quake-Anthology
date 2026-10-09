#include "remote_q2_private.h"
#include "q2_client_lerp.h"
#include "remote_q2_effects_bridge.h"
#include "remote_q2_clientinfo.h"
#include "remote_q2_material_movies_bridge.h"
#include "legacy_render_policy.h"
#include "qa/material.h"
#include "qa/scene_effects.h"
#include "qa/game_q2.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static qa_vec3 vector(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static qa_vec3 angles_lerp(const float a[3], const float b[3], float t)
{
    return frontend_q2_lerp_angles(vector(a),vector(b),t);
}
static void axes(qa_vec3 angles, qa_vec3 out[3])
{
    float pitch = angles.x * 0.017453292519943295f, yaw = angles.y * 0.017453292519943295f, roll = angles.z * 0.017453292519943295f;
    float sp = sinf(pitch), cp = cosf(pitch), sy = sinf(yaw), cy = cosf(yaw), sr = sinf(roll), cr = cosf(roll);
    out[0] = qa_v3(cp * cy, cp * sy, -sp);
    out[1] = qa_v3(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp);
    out[2] = qa_v3(cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp);
}
static frontend_remote_q2 *seat_owner(qa_frontend *f, uint32_t seat, qa_error *error)
{
    frontend_remote_q2 *found = NULL;
    for (frontend_remote_q2 *row = f->remote_q2; row; row = row->next) {
        if (row->retired || row->retiring || row->options.domain.physical_seat != seat) continue;
        if (found) { remote_q2_fail(error, QA_ERROR_ARGUMENT, "Physical seat has multiple live Q2 CLIENT sources"); return NULL; }
        found = row;
    }
    return found;
}
static const qa_q2_frame_player *frame_player(const frontend_remote_q2 *row, const qa_q2_wire_frame *frame)
{
    uint32_t index; qa_error error = {0};
    if (!frontend_remote_q2_wire_seat(row, &index, &error)) return NULL;
    return frame->valid && index < frame->player_count ? &frame->players[index] : NULL;
}
static int32_t client_time(const frontend_remote_q2 *row)
{
    uint32_t bits = (uint32_t)(int64_t)(((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms);
    int32_t milliseconds; memcpy(&milliseconds, &bits, sizeof(milliseconds)); return milliseconds;
}
bool frontend_remote_q2_initial_clear(qa_frontend *frontend, uint32_t seat,
    bool *active, bool *clear, qa_error *error)
{
    if (!frontend || !active || !clear || seat >= frontend->options.seats) return false;
    *active = false; *clear = true;
    frontend_remote_q2 *row = seat_owner(frontend, seat, error);
    if (!row) return !error || error->code == QA_OK;
    if (!row->bound || !row->media_ready || !row->world) return true;
    if (row->busy || !remote_q2_live(row, error)) return false;
    if (!frame_player(row, &row->frame)) return true;
    const qa_product *product = qa_catalog_product(row->content.catalog, row->content.selected);
    frontend_legacy_render_policy policy;
    if (!frontend_legacy_render_policy_read_controls(row->options.domain.cvars, &row->cvar_handles.legacy, product, &policy, error) ||
        !remote_q2_live(row, error)) return false;
    *active = true; *clear = policy.lighting.clear;
    return true;
}
static const qa_q2_entity *entity(const qa_q2_wire_frame *frame, uint32_t number)
{
    return remote_q2_frame_entity(frame,number);
}
static qa_vec3 player_origin(const frontend_remote_q2 *row, const qa_q2_player *player)
{
    if (remote_q2_float_movement(row)) return vector(player->pmove.origin_f);
    return qa_v3((float)player->pmove.origin[0] * 0.125f, (float)player->pmove.origin[1] * 0.125f, (float)player->pmove.origin[2] * 0.125f);
}
static bool near(qa_vec3 a, qa_vec3 b, float limit)
{ return frontend_q2_lerp_near(a,b,limit); }
static void fog_receive(frontend_remote_q2 *row, const qa_q2_wire_fog *wire)
{
    row->fog_duration_ms = wire->bits & 16u ? wire->time : 0;
    if (row->fog_duration_ms) {
        row->fog_start = row->fog_end;
        row->fog_started_ms = row->frame.valid ? ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms : 0;
    }
    qa_scene_fog *target = &row->fog_end;
    if (wire->bits & 1u) { target->density = wire->density; target->sky_factor = (float)wire->sky_factor / 255; }
    if (wire->bits & 2u) target->color.x = (float)wire->color[0] / 255;
    if (wire->bits & 4u) target->color.y = (float)wire->color[1] / 255;
    if (wire->bits & 8u) target->color.z = (float)wire->color[2] / 255;
    if (wire->bits & 32u) target->height_falloff = wire->height_falloff;
    if (wire->bits & 64u) target->height_density = wire->height_density;
    if (wire->bits & 256u) target->height_color.x = (float)wire->height_start_color[0] / 255;
    if (wire->bits & 512u) target->height_color.y = (float)wire->height_start_color[1] / 255;
    if (wire->bits & 1024u) target->height_color.z = (float)wire->height_start_color[2] / 255;
    if (wire->bits & 2048u) target->height_start = (float)wire->height_start_distance;
    if (wire->bits & 4096u) target->height_end_color.x = (float)wire->height_end_color[0] / 255;
    if (wire->bits & 8192u) target->height_end_color.y = (float)wire->height_end_color[1] / 255;
    if (wire->bits & 16384u) target->height_end_color.z = (float)wire->height_end_color[2] / 255;
    if (wire->bits & 32768u) target->height_end = (float)wire->height_end_distance;
    row->fog_received = true;
}
static qa_scene_fog fog_sample(const frontend_remote_q2 *row, double milliseconds)
{
    double elapsed = milliseconds - row->fog_started_ms;
    float fraction = row->fog_duration_ms && elapsed <= row->fog_duration_ms ?
        (float)(elapsed / row->fog_duration_ms) : 1;
    const qa_scene_fog *a = &row->fog_start, *b = &row->fog_end;
    qa_scene_fog value = {.kind = QA_FOG_Q2, .effect = QA_FOG_OVERLAY, .far_depth = 1 - 1e-6f};
    value.color = qa_vec_lerp(a->color, b->color, fraction);
    value.height_color = qa_vec_lerp(a->height_color, b->height_color, fraction);
    value.height_end_color = qa_vec_lerp(a->height_end_color, b->height_end_color, fraction);
#define MIX(field) value.field = a->field + (b->field - a->field) * fraction
    MIX(density); MIX(sky_factor); MIX(height_density); MIX(height_start); MIX(height_end); MIX(height_falloff);
#undef MIX
    return value;
}
bool remote_q2_player_fog_receive(frontend_remote_q2 *row, qa_error *error)
{
    if (row->options.domain.protocol.kind != QA_NET_Q2PRO_36 || row->data.protocol_revision < 1026) return true;
    uint32_t seat;
    if (!frontend_remote_q2_wire_seat(row, &seat, error) || seat >= row->frame.player_count) return false;
    const qa_q2_player_fog zero = {0};
    const qa_q2_player_fog *fog = &row->frame.players[seat].player.fog;
    const qa_q2_player_fog *before = row->previous.valid && seat < row->previous.player_count ?
        &row->previous.players[seat].player.fog : &zero;
    qa_scene_fog *target = &row->fog_end; bool changed = false;
    if (fog->density != before->density || fog->sky_factor != before->sky_factor) {
        target->density = (float)fog->density / 65535; target->sky_factor = (float)fog->sky_factor / 65535; changed = true;
    }
    if (fog->height_density != before->height_density) { target->height_density = (float)fog->height_density / 65535; changed = true; }
    if (fog->height_falloff != before->height_falloff) { target->height_falloff = (float)fog->height_falloff / 65535; changed = true; }
    if (memcmp(fog->color, before->color, 3)) {
        target->color = qa_v3((float)fog->color[0] / 255, (float)fog->color[1] / 255, (float)fog->color[2] / 255); changed = true;
    }
    if (memcmp(fog->height_start_color, before->height_start_color, 3)) {
        target->height_color = qa_v3((float)fog->height_start_color[0] / 255,
            (float)fog->height_start_color[1] / 255, (float)fog->height_start_color[2] / 255); changed = true;
    }
    if (memcmp(fog->height_end_color, before->height_end_color, 3)) {
        target->height_end_color = qa_v3((float)fog->height_end_color[0] / 255,
            (float)fog->height_end_color[1] / 255, (float)fog->height_end_color[2] / 255); changed = true;
    }
    if (fog->height_start_distance != before->height_start_distance) {
        target->height_start = (float)fog->height_start_distance * .125f; changed = true;
    }
    if (fog->height_end_distance != before->height_end_distance) {
        target->height_end = (float)fog->height_end_distance * .125f; changed = true;
    }
    if (changed) { row->fog_duration_ms = 0; row->fog_received = true; }
    return true;
}
static qa_scene_vec4 player_blend(const float current[4], const float *previous, float fraction)
{
    qa_scene_vec4 value = {current[0], current[1], current[2], current[3]};
    if (previous && previous[3] != 0) {
        value.x = previous[0] + (current[0] - previous[0]) * fraction;
        value.y = previous[1] + (current[1] - previous[1]) * fraction;
        value.z = previous[2] + (current[2] - previous[2]) * fraction;
        value.w = previous[3] + (current[3] - previous[3]) * fraction;
    }
    return value;
}
static bool damage_blend_draw(frontend_remote_q2 *row, qa_scene_rect viewport,
    qa_scene_vec4 color, qa_error *error)
{
    if (color.w <= 0) return true;
    const qa_cvar_view *setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.gl_damageblend_frac);
    if (!setting || !isfinite(setting->number))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 damage blend has no actual CLIENT fraction control");
    float fraction = (float)fmin(.5, fmax(0, setting->number));
    if (fraction == 0) return qa_scene_frame_picture(&row->frontend->frame, row->white, viewport, viewport,
        (qa_scene_vec4){0, 0, 1, 1}, color, error);
    qa_scene_frame *frame = &row->frontend->frame;
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, 8 * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    uint32_t *indices = qa_arena_alloc(&frame->storage, 24 * sizeof(*indices), _Alignof(uint32_t), error);
    if (!vertices || !indices) return false;
    static const uint32_t order[24] = {0, 5, 4, 0, 1, 5, 1, 6, 5, 1, 2, 6, 6, 2, 3, 6, 3, 7, 0, 7, 3, 0, 4, 7};
    memcpy(indices, order, sizeof(order));
    float distance = truncf(fminf((float)viewport.width, (float)viewport.height) * fraction);
    float x = 2 * distance / (float)viewport.width, y = 2 * distance / (float)viewport.height;
    qa_vec3 positions[8] = {{-1, 1, 0}, {1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
        {-1 + x, 1 - y, 0}, {1 - x, 1 - y, 0}, {1 - x, -1 + y, 0}, {-1 + x, -1 + y, 0}};
    for (size_t i = 0; i < 8; ++i) {
        vertices[i] = (qa_scene_vertex){.position = positions[i], .color = color};
        if (i >= 4) vertices[i].color.w = 0;
    }
    qa_scene_draw draw = {.mesh = {.vertices = vertices, .indices = indices, .vertex_count = 8,
        .index_count = 24, .primitive = QA_SCENE_TRIANGLES,
        .bounds = {{-1, -1, 0}, {1, 1, 0}}}, .textures = {row->white, NULL}, .texture_count = 1};
    qa_scene_matrix_identity(&draw.model); qa_scene_matrix_identity(&draw.mvp);
    qa_scene_state_default(&draw.state);
    draw.state.blend_source = QA_BLEND_SRC_ALPHA; draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test = QA_DEPTH_ALWAYS; draw.state.depth_write = false;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = {.viewport = viewport}};
    return qa_scene_frame_emit(frame, &view, error) && qa_scene_frame_draw(frame, &draw, error);
}
static bool flare_standard_image(const char *path)
{
    static const char *const names[] = {"misc/flare.tga", "sprites/psx_flare"};
    for (size_t n = 0; n < 2; ++n) {
        size_t i = 0;
        while (names[n][i] && path[i]) {
            unsigned char c = (unsigned char)path[i];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
            if (c == '\\') c = '/';
            if (c != (unsigned char)names[n][i]) break;
            ++i;
        }
        if (!names[n][i] && (n == 1 || !path[i])) return true;
    }
    return false;
}
static bool flare_draw(frontend_remote_q2 *row, const qa_q2_entity *packet,
    const qa_scene_view *view, qa_vec3 origin, qa_error *error)
{
    const char *path = "misc/flare.tga";
    if ((packet->renderfx & 256u) && packet->frame < row->layout.max_images) {
        const char *selected = frontend_remote_q2_config(row, (uint16_t)(row->layout.images + packet->frame));
        if (*selected) path = selected[0] == '/' || selected[0] == '\\' ? selected + 1 : selected;
    }
    const qa_scene_image *image = NULL;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        qa_error issue = {0}; image = remote_q2_sprite_read(row, path, &issue);
        if (image) break;
        if (issue.code != QA_ERROR_NOT_FOUND) { if (error) *error = issue; return false; }
        if (!strcmp(path, "misc/flare.tga")) return true;
        path = "misc/flare.tga";
    }
    if (!image) return true;
    uint32_t color = packet->skinnum;
    qa_scene_flare_options options = {.scale = packet->scale != 0 ? packet->scale : 1,
        .fade_start = (float)packet->modelindex2, .fade_end = (float)packet->modelindex3,
        .color = color ? qa_v3((float)(color >> 24) / 255, (float)((color >> 16) & 255u) / 255,
            (float)((color >> 8) & 255u) / 255) : qa_v3(1, 1, 1),
        .lock_angle = (packet->renderfx & 1u) != 0,
        .separate_rim = (packet->renderfx & (1024u | 2048u | 4096u)) != 0,
        .rim_color = {(packet->renderfx & 1024u) ? 1.f : 0.f, (packet->renderfx & 2048u) ? 1.f : 0.f,
            (packet->renderfx & 4096u) ? 1.f : 0.f},
        .standard_image = flare_standard_image(path)};
    return qa_scene_flare(&row->frontend->frame, view, origin, &options, image, error);
}
static bool hit_marker_draw(frontend_remote_q2 *row, const qa_q2_player *player,
    qa_scene_rect viewport, qa_error *error)
{
    if (!row->hit_marker_count) return true;
    const qa_cvar_view *crosshair = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.crosshair);
    if (!crosshair || crosshair->number == 0 || (player->stats[13] & (4 | 32))) return true;
    const qa_cvar_view *duration = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.scr_hit_marker_time);
    const qa_scene_image *image = NULL;
    for (remote_q2_picture *picture = row->pictures; picture; picture = picture->next)
        if (!strcmp(picture->name, "marker")) { image = picture->image; break; }
    double elapsed = row->sample_ns >= row->hit_marker_ns ? (double)(row->sample_ns - row->hit_marker_ns) / 1000000 : 0;
    double limit = duration && isfinite(duration->number) ? trunc(duration->number) : 0;
    if (!image || limit <= 0 || elapsed > limit) { row->hit_marker_count = 0; return true; }
    float fraction = (float)(elapsed / limit), scale = fmaxf(1, 1.5f * (1 - fraction));
    const qa_cvar_view *size = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.ch_scale);
    if (size && isfinite(size->number)) scale *= (float)fmax(.1, fmin(9, size->number));
    const qa_cvar_view *alpha = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.ch_alpha);
    float opacity = alpha && isfinite(alpha->number) ? (float)fmax(0, fmin(1, alpha->number)) : 1;
    const qa_cvar_view *x = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.ch_x);
    const qa_cvar_view *y = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.ch_y);
    float dx = x && isfinite(x->number) ? (float)fmax(INT32_MIN, fmin(INT32_MAX, trunc(x->number))) : 0;
    float dy = y && isfinite(y->number) ? (float)fmax(INT32_MIN, fmin(INT32_MAX, trunc(y->number))) : 0;
    float width = truncf((float)image->logical_width * scale), height = truncf((float)image->logical_height * scale);
    qa_scene_rect_f rect = {(float)viewport.x + truncf(((float)viewport.width - width) * .5f) + dx,
        (float)viewport.y + truncf(((float)viewport.height - height) * .5f) + dy, width, height};
    return qa_scene_frame_picture_f(&row->frontend->frame, image, viewport, rect,
        (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){1, 0, 0, opacity * (1 - fraction * fraction)}, error);
}
static const char *hud_config(void *context, int32_t index)
{ return index >= 0 && index <= UINT16_MAX ? frontend_remote_q2_config(context, (uint16_t)index) : ""; }
static bool sound(frontend_remote_q2 *row, const qa_q2_kex_sound *event, qa_error *error)
{
    if (!row->media_ready || !row->frontend->audio) return true;
    if (event->index >= row->layout.max_sounds) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 sound leaves its actual protocol config range");
    const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.sounds + event->index));
    if (!*name) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 sound has no actual configstring");
    const qa_q2_entity *source = entity(&row->frame, event->entity);
    qa_vec3 origin = event->has_position ? vector(event->position) : source ? vector(source->origin) : qa_v3(0, 0, 0);
    qa_audio_asset *asset = NULL;
    bool ok = remote_q2_sound_asset(row, name, event->entity, &asset, error);
    if (!ok || !asset) return ok;
    qa_audio_play play = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = name, .family = QA_AUDIO_Q2,
        .actor = event->entity ? event->entity : QA_AUDIO_NO_ACTOR, .owner = row->identity,
        .audience = row->options.domain.physical_seat, .origin_kind = QA_AUDIO_FIXED, .origin = origin,
        .channel = event->channel, .volume = event->volume, .attenuation = event->attenuation,
        .delay_seconds = event->time_offset, .server_milliseconds = (float)row->frame.server_frame * row->frame_ms,
        .has_server_time = row->frame.valid};
    ok = qa_audio_engine_play(row->frontend->audio, &play, (int32_t)(row->sample_ns / 1000000), error);
    qa_audio_asset_release(asset); return ok;
}
bool remote_q2_records(frontend_remote_q2 *row, const qa_q2_server_record *records, size_t count, qa_error *error)
{
    uint32_t remote_index;
    if (!frontend_remote_q2_wire_seat(row, &remote_index, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_q2_server_record *record = &records[i];
        if (record->seat && record->seat != remote_index + 1) continue;
        const qa_q2_server_event *event = &record->event;
        switch (event->kind) {
        case QA_Q2_SVC_CONFIGSTRING:
            if (!remote_q2_config_set(row, event->data.config.index, event->data.config.value, error)) return false;
            break;
        case QA_Q2_SVC_INVENTORY:
            if (event->data.inventory.count > 256) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 inventory leaves its source item table");
            memset(row->inventory, 0, sizeof(row->inventory));
            for (size_t j = 0; j < event->data.inventory.count; ++j) row->inventory[j] = event->data.inventory.counts[j];
            break;
        case QA_Q2_SVC_LAYOUT: {
            const char *text = event->data.print.text; if (!text) return false;
            char *copy = malloc(strlen(text) + 1); if (!copy) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 server layout");
            strcpy(copy, text); free(row->overlay); row->overlay = copy; break;
        }
        case QA_Q2_SVC_SOUND: if (!sound(row, &event->data.sound, error)) return false; break;
        case QA_Q2_SVC_FOG: fog_receive(row, &event->data.fog); break;
        default: break;
        }
    }
    return true;
}
static bool loops(frontend_remote_q2 *row, qa_error *error)
{
    if (!row->frontend->audio || !row->media_ready || !row->frame.valid) return true;
    const qa_cvar_view *paused = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.paused);
    if (paused && paused->number != 0) return true;
    for (size_t i = 0; i < row->frame.entity_count; ++i) {
        const qa_q2_entity *entity = row->frame.entities + i;
        if (!entity->sound) continue;
        if (entity->sound >= row->layout.max_sounds) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 looping sound leaves its received config range");
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.sounds + entity->sound));
        if (!*name) continue;
        qa_audio_asset *asset = NULL;
        if (!qa_audio_bank_register(row->sounds, name, QA_AUDIO_Q2, &asset, error)) return false;
        if (!asset) continue;
        qa_audio_loop loop = {.sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
            .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = name, .family = QA_AUDIO_Q2,
            .actor = entity->number, .owner = row->identity, .audience = row->options.domain.physical_seat,
            .origin_kind = QA_AUDIO_FIXED, .origin = vector(entity->origin), .channel = 0,
            .volume = entity->loop_volume != 0 ? entity->loop_volume : 1,
            .attenuation = entity->loop_attenuation == -1 ? 0 : entity->loop_attenuation != 0 ? entity->loop_attenuation : 1},
            .frame_number = row->frame.server_frame};
        bool ok = qa_audio_engine_loop(row->frontend->audio, &loop, error);
        qa_audio_asset_release(asset);
        if (!ok) return false;
    }
    return true;
}
bool frontend_remote_q2_sample(qa_frontend *f, uint64_t now, qa_error *error)
{
    if (!f || f->capture || f->resource_inventory || f->source_restoring) return false;
    for (frontend_remote_q2 *row = f->remote_q2; row; row = row->next) {
        if (row->retired || row->retiring || !row->bound) continue;
        if (row->busy || row->image_policy) return false;
        if (!remote_q2_live(row, error)) return false;
        row->sample_frame_seconds = row->sample_ns && now >= row->sample_ns ?
            (double)(now - row->sample_ns) / 1000000000.0 : 0;
        row->sample_ns = now;
        double elapsed = now >= row->received_ns ? (double)(now - row->received_ns) / 1000000.0 : 0;
        if (row->options.demo && row->frame.valid)
            elapsed = row->demo_ms - ((double)row->frame.server_frame - 1) * row->frame_ms;
        row->fraction = (float)fmin(1, fmax(0, elapsed / row->frame_ms));
        ++row->busy; bool ok = remote_q2_hit_marker_sample(row, error) && loops(row, error); --row->busy;
        if (!ok || !remote_q2_live(row, error)) return false;
    }
    return true;
}
bool frontend_remote_q2_input(qa_frontend *f, uint32_t seat, const qa_seat_input_sample *sample,
    uint64_t sequence, qa_q2_usercmd *out, bool *handled, bool *command_ready, qa_error *error)
{
    if (!f || !sample || !out || !handled || !command_ready) return false;
    *handled = false; *command_ready = false;
    frontend_remote_q2 *row = seat_owner(f, seat, error);
    if (!row) return !error || error->code == QA_OK;
    *handled = true;
    if (!row->bound || row->options.demo) return true;
    const qa_q2_frame_player *frame = frame_player(row, &row->frame);
    if (!remote_q2_live(row, error)) return false;
    if (!row->media_ready || !frame) return true;
    qa_movement_kind kind = remote_q2_float_movement(row) ? QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC;
    qa_input_command_tuning tuning;
    if (!qa_input_settings_read(row->options.domain.cvars, row->options.domain.cvars, &row->input_tuning, kind, &tuning, error)) return false;
    qa_vec3 delta = frame->player.pmove.float_delta_angles ? vector(frame->player.pmove.delta_angles_f) :
        qa_v3(frame->player.pmove.delta_angles[0] * (360.0f / 65536), frame->player.pmove.delta_angles[1] * (360.0f / 65536),
            frame->player.pmove.delta_angles[2] * (360.0f / 65536));
    if (!row->input_set || row->input.kind != kind) {
        qa_input_command_clear(&row->input); row->input.kind = kind;
        if (!qa_input_command_angles(&row->input, qa_vec_sub(vector(frame->player.viewangles), delta), error)) return false;
    }
    qa_input_command_frame basis = {.kind = kind, .sequence = sequence, .server_frame = row->frame.server_frame,
        .server_time_ms = client_time(row),
        .acknowledged_server_seconds = (double)row->frame.server_frame * row->frame_ms * 0.001,
        .delta_angles = delta, .sensitivity = 1, .attack_allowed = true};
    qa_movement_command command;
    if (!qa_input_command_build(&row->input, &tuning, sample, &basis, sample->frame_ms, &command, error)) return false;
    bool kex = row->options.domain.protocol.kind == QA_NET_Q2KEX_2023 ||
        row->options.domain.protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    *out = (qa_q2_usercmd){.server_frame = kex ? row->frame.server_frame : 0,
        .msec = command.milliseconds > 255 ? 255 : (uint8_t)command.milliseconds,
        .buttons = (uint8_t)command.buttons, .impulse = command.impulse, .lightlevel = command.light_level,
        .forwardmove = command.forward_move, .sidemove = command.side_move, .upmove = command.up_move};
    if (kind == QA_MOVEMENT_Q2_RERELEASE) {
        float values[3] = {command.angles.x, command.angles.y, command.angles.z};
        for (size_t i = 0; i < 3; ++i) {
            uint16_t bits = qa_angle_to_word(values[i]);
            memcpy(out->angles + i, &bits, sizeof(bits));
        }
    } else for (size_t i = 0; i < 3; ++i) out->angles[i] = (int16_t)command.angle_words[i];
    if (!remote_q2_live(row, error)) return false;
    row->input_set = true;
    *command_ready = true;
    return true;
}
static bool submit_model(frontend_remote_q2 *row, const char *path, const char *skin_path,
    const qa_scene_view *view, const qa_scene_world_input *world, const qa_q2_entity *current,
    const qa_q2_entity *previous, bool view_model, qa_vec3 origin, qa_vec3 angles, qa_error *error)
{
    if (!*path || path[0] == '#') return true;
    qa_model_transform transform; qa_model_transform_identity(&transform);
    qa_vec3 basis[3]; axes(angles, basis);
    transform.origin[0] = origin.x; transform.origin[1] = origin.y; transform.origin[2] = origin.z;
    for (size_t i = 0; i < 3; ++i) {
        transform.axes[i][0] = basis[i].x; transform.axes[i][1] = basis[i].y; transform.axes[i][2] = basis[i].z;
        transform.scale[i] = current->scale != 0 ? current->scale : 1;
    }
    qa_scene_vec4 color = {1, 1, 1, current->renderfx & 32 ? 0.30f : 1};
    uint32_t flags = current->renderfx;
    if (current->alpha != 0) {
        color.w = previous && previous->alpha != 0 ? previous->alpha + row->fraction * (current->alpha - previous->alpha) : current->alpha;
        if (color.w != 1) flags |= 32;
        else flags &= ~UINT32_C(32);
    }
    if (path[0] == '*') {
        char *end; unsigned long index = strtoul(path + 1, &end, 10);
        if (end == path + 1 || *end || index > UINT32_MAX) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 inline model has no actual model index");
        return qa_scene_world_submit_model(row->world, (uint32_t)index, &transform, world, current->number, color, &row->frontend->frame, error);
    }
    remote_q2_model *model = NULL; qa_error issue = {0};
    if (!remote_q2_model_read(row, path, &model, &issue)) {
        if (issue.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = issue;
        return false;
    }
    const qa_material *skin = NULL;
    if (skin_path && *skin_path) {
        qa_scene_image_options options = {.family = QA_SCENE_Q2, .usage = QA_IMAGE_USAGE_SKIN,
            .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = 255};
        if (!qa_material_register(row->materials, skin_path, &options, false, &skin, error)) return false;
    }
    const qa_scene_image_options *model_options = qa_scene_model_image_options(model->scene);
    qa_scene_model_input input = {.view = *view, .transform = transform,
        .previous_origin = current->renderfx & (64u | 128u) ? vector(current->old_origin) : origin,
        .model_beam = qa_q2_model_beam(remote_q2_rerelease_presentation(row) ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
            current->renderfx, current->modelindex > 1),
        .beam_segment_length = (float)current->frame,
        .color = color, .family = QA_SCENE_Q2,
        .frame = current->frame, .old_frame = previous ? previous->frame : current->frame,
        .skin = current->modelindex == 255 ? 0 : current->skinnum, .flags = flags,
        .entity = current->number, .back_lerp = previous ? 1 - row->fraction : 0,
        .seconds = world->seconds, .view_model = view_model, .player = current->modelindex == 255,
        .material_library = model_options && model_options->family == QA_SCENE_Q3 ? row->materials : NULL,
        .custom_material = skin, .source_path = path,
        .video_frame = frontend_material_movies_frontend_resolve, .video_context = row->frontend};
    const qa_cvar_view *hand = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.legacy.hand);
    if (view_model && hand && isfinite(hand->number) && hand->number >= 0 && hand->number <= 2)
        input.left_hand = (uint8_t)hand->number;
    if (remote_q2_rerelease_presentation(row)) {
        const frontend_q2_animation *retained=NULL;
        uint32_t source_flags=current->renderfx,source_old_frame=current->old_frame;
        double window=100;
        if (view_model) {
            const qa_q2_frame_player *player=frame_player(row,&row->frame);
            retained=&row->gun_animation;
            window=1000.0/(player->player.gunrate?player->player.gunrate:10);
        } else if ((size_t)current->number<row->entity_animation_capacity) {
            retained=&row->entity_animations[current->number].animation;
            const qa_q2_entity *source=remote_q2_frame_entity(&row->frame,current->number);
            if (source) { source_flags=source->renderfx;source_old_frame=source->old_frame; }
        }
        if (retained) {
            frontend_q2_animation_sample animation=frontend_q2_animation_lerp(retained,true,view_model,
                input.frame,source_flags&(UINT32_C(1)<<22)?source_old_frame:input.old_frame,source_flags,
                world->seconds*1000,row->frame_ms,window,input.back_lerp);
            input.frame=animation.frame;input.old_frame=animation.old_frame;input.back_lerp=animation.back_lerp;
        }
    }
    qa_vec3 ambient = qa_v3(1, 1, 1), directed = qa_v3(0, 0, 0), direction = qa_v3(0, 0, 1);
    if (!world->no_world) {
        if (!qa_scene_world_sample_light_input(row->world, world, origin, &ambient, &directed, &direction, error)) return false;
        ambient = qa_vec_add(ambient, directed);
    }
    input.ambient = ambient; input.light_direction = direction;
    if (!remote_q2_live(row, error) || !frontend_legacy_model_input(row->world, world, &input, error) || !remote_q2_live(row, error)) return false;
    if (!qa_scene_model_submit(model->scene, &input, &row->frontend->frame, error)) return false;
    if (view_model && row->effects && remote_q2_rerelease_presentation(row)) {
        qa_actor_id viewer;
        if (!row->options.entity_actor || !row->options.entity_actor(row->options.context, &row->options.domain,
            current->number, &viewer, error)) return false;
        return frontend_remote_q2_effects_weapon_draw(row->effects, viewer, &input, &row->frontend->frame, error);
    }
    return true;
}
bool frontend_remote_q2_draw(qa_frontend *f, uint32_t seat, float stereo,
    qa_audio_listener *listener, bool *rendered, qa_error *error)
{
    if (!f || !listener || !rendered || !isfinite(stereo)) return false;
    *rendered = false; frontend_remote_q2 *row = seat_owner(f, seat, error);
    if (!row) return !error || error->code == QA_OK;
    *rendered = true;
    if (!row->bound) { *listener = (qa_audio_listener){.seat = seat, .actor = QA_AUDIO_NO_ACTOR}; return true; }
    if (!remote_q2_live(row, error)) return false;
    const qa_q2_frame_player *frame = frame_player(row, &row->frame);
    if (!row->media_ready || !frame) { *listener = (qa_audio_listener){.seat = seat, .actor = QA_AUDIO_NO_ACTOR}; return true; }
    uint32_t player_index; int32_t player_number;
    if (!frontend_remote_q2_wire_seat(row, &player_index, error) ||
        !frontend_remote_q2_player_number(row, &row->frame, player_index, &player_number))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 camera has no negotiated player identity receipt");
    uint32_t player_entity_number = player_number >= 0 ? (uint32_t)player_number + 1 : 0;
    const qa_q2_frame_player *before = frame_player(row, &row->previous);
    const qa_product *product = qa_catalog_product(row->content.catalog, row->content.selected);
    frontend_legacy_render_policy policy;
    if (!frontend_legacy_render_policy_read_controls(row->options.domain.cvars, &row->cvar_handles.legacy, product, &policy, error) ||
        !remote_q2_live(row, error)) return false;
    size_t scene_first = f->frame.command_count;
    qa_vec3 origin = player_origin(row, &frame->player), offset = vector(frame->player.viewoffset), angles = vector(frame->player.viewangles);
    bool continuous = before && near(player_origin(row, &before->player), origin, 256);
    if (continuous && remote_q2_rerelease_presentation(row)) {
        const qa_q2_entity *player_entity = entity(&row->frame, player_entity_number);
        continuous = row->frame.server_frame == row->previous.server_frame + 1 &&
            (!player_entity || (player_entity->event != 6 && player_entity->event != 7)) &&
            !((before->player.rdflags ^ frame->player.rdflags) & 16);
    }
    if (continuous) {
        origin = qa_vec_lerp(player_origin(row, &before->player), origin, row->fraction);
        offset = qa_vec_lerp(vector(before->player.viewoffset), offset, row->fraction);
        angles = angles_lerp(before->player.viewangles, frame->player.viewangles, row->fraction);
    }
    if (row->predicted) {
        origin = qa_vec_sub(row->prediction_origin, qa_vec_scale(row->prediction_error, 1 - row->fraction));
        double elapsed = row->sample_ns >= row->prediction_step_ns ?
            (double)(row->sample_ns - row->prediction_step_ns) / 1000000 : 100;
        if (elapsed < 100) origin.z -= row->prediction_step * (1 - (float)elapsed * .01f);
    }
    bool angular=frontend_q2_lerp_live_angles(remote_q2_float_movement(row) ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
        frame->player.pmove.type,(uint32_t)frame->player.pmove.flags);
    qa_vec3 local = row->input.angles; bool local_set = row->input_set;
    if (!local_set && row->sent_set) {
        const remote_q2_sent_command *sent = &row->sent[row->last_sent & 63];
        if (sent->valid && sent->packet_sequence == row->last_sent) {
            local = qa_v3((float)(uint16_t)sent->command.angles[0] * (360.0f / 65536),
                (float)(uint16_t)sent->command.angles[1] * (360.0f / 65536),
                (float)(uint16_t)sent->command.angles[2] * (360.0f / 65536));
            local_set = true;
        }
    }
    if (angular && local_set) {
        qa_vec3 delta = frame->player.pmove.float_delta_angles ? vector(frame->player.pmove.delta_angles_f) :
            qa_v3(frame->player.pmove.delta_angles[0] * (360.0f / 65536),
                frame->player.pmove.delta_angles[1] * (360.0f / 65536),
                frame->player.pmove.delta_angles[2] * (360.0f / 65536));
        angles = qa_vec_add(local, delta);
    }
    angles = qa_vec_add(angles, continuous ?
        qa_vec_lerp(vector(before->player.kick_angles), vector(frame->player.kick_angles), row->fraction) :
        vector(frame->player.kick_angles));
    qa_vec3 viewer_origin = origin;
    origin = qa_vec_add(origin, offset);
    double time = ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms;
    int32_t milliseconds = client_time(row), doubled;
    uint32_t time_bits = (uint32_t)milliseconds * 2u; memcpy(&doubled, &time_bits, sizeof(doubled));
    uint32_t auto_frame = (uint32_t)(remote_q2_rerelease_presentation(row) ? milliseconds / 500 : doubled / 1000);
    if (remote_q2_float_movement(row)) {
        float height = (float)frame->player.pmove.viewheight;
        if (!row->height_set) { row->height_previous = row->height_current = height; row->height_changed_ms = time; row->height_set = true; }
        else if (row->height_current != height) { row->height_previous = row->height_current; row->height_current = height; row->height_changed_ms = time; }
        float elapsed = (float)fmin(100, fmax(0, time - row->height_changed_ms));
        origin.z += row->height_current + (row->height_previous - row->height_current) * (100 - elapsed) * 0.01f;
    }
    qa_scene_view view = {.viewport = frontend_viewport(f, seat), .origin = origin,
        .clear_color = policy.lighting.clear, .clear_depth = true, .depth = 1, .seat = seat};
    axes(angles, view.axis); view.origin = qa_vec_add(view.origin, qa_vec_scale(view.axis[1], stereo));
    float fov = continuous ? before->player.fov + (frame->player.fov - before->player.fov) * row->fraction : frame->player.fov;
    if (!(fov > 0 && fov < 180) || !view.viewport.width || !view.viewport.height) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 decoded camera has no valid projection");
    float fovy = atan2f((float)view.viewport.height, (float)view.viewport.width / tanf(fov * 0.008726646259971648f)) * 114.59155902616465f;
    view.projection = qa_scene_projection(fov, fovy, 4, 4096);
    qa_scene_command begin = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = view};
    ++row->busy; bool ok = qa_scene_frame_emit(&f->frame, &begin, error);
    qa_vec3 styles[256];
    for (size_t i = 0; i < 256; ++i) {
        const char *style = frontend_remote_q2_config(row, (uint16_t)(row->layout.lights + i)); size_t length = strlen(style);
        float value = length ? (style[((size_t)fmax(0, floor(time * 0.01))) % length] - 'a') / 12.0f : 1;
        styles[i] = qa_v3(value, value, value);
    }
    qa_scene_world_input world = {.view = view, .seconds = time * 0.001, .milliseconds = (int64_t)time,
        .visible_areas = frame->area_bits.data, .visible_area_bytes = frame->area_bits.size,
        .q2_styles = styles, .style_count = 256, .no_world = (frame->player.rdflags & 2) != 0,
        .legacy_policy = policy.lighting,
        .video_frame = frontend_material_movies_frontend_resolve, .video_context = f};
    if (row->fog_received && !world.no_world) world.fog = fog_sample(row, time);
    if (ok && row->shader_movies) ok = frontend_material_movies_frame(row->shader_movies, &f->frame, error);
    bool sky_auto = true; char *sky_end;
    world.sky_rotation = strtof(frontend_remote_q2_config(row, 3), &sky_end);
    if (remote_q2_rerelease_presentation(row)) {
        char *auto_end; long automatic = strtol(sky_end, &auto_end, 10);
        if (auto_end != sky_end) sky_auto = automatic != 0;
    }
    if (sscanf(frontend_remote_q2_config(row, 4), "%f %f %f", &world.sky_axis.x,
        &world.sky_axis.y, &world.sky_axis.z) != 3) world.sky_axis = qa_v3(0, 0, 0);
    if (!isfinite(world.sky_rotation) || !qa_vec_finite(world.sky_axis))
        ok = remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 sky configuration has nonfinite received parameters");
    world.sky_auto_rotate = sky_auto;
    frontend_remote_q2_effects_sample effects_sample = {0};
    if (ok) ok = remote_q2_effects_sample_prepare(row, &view, fov, viewer_origin, vector(frame->player.gunoffset), player_number,
        &effects_sample, &world.lights, &world.light_count, error);
    effects_sample.world_input = &world;
    const qa_cvar_view *light_setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_lights);
    const qa_cvar_view *entities_setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_entities);
    bool entities_enabled = !entities_setting || entities_setting->number != 0;
    if (light_setting && light_setting->number == 0) world.light_count = 0;
    if (ok) ok = qa_scene_world_boxed_sky_begin(row->world, &world, &f->frame, &world.boxed_sky, error);
    if (ok) ok = qa_scene_world_q2_alpha_begin(&world, &f->frame, &world.q2_alpha, error);
    if (ok) ok = qa_scene_world_submit(row->world, &world, &f->frame, error);
    for (size_t i = 0; ok && entities_enabled && i < row->frame.entity_count; ++i) {
        const qa_q2_entity *current = &row->frame.entities[i];
        if (remote_q2_rerelease_presentation(row) && (current->renderfx & (UINT32_C(1) << 21))) {
            if (policy.lighting.flares) {
                const qa_q2_entity *prior = entity(&row->previous, current->number);
                qa_vec3 position = prior && near(vector(prior->origin), vector(current->origin), 512) ?
                    qa_vec_lerp(vector(prior->origin), vector(current->origin), row->fraction) : vector(current->origin);
                ok = flare_draw(row, current, &view, position, error);
            }
            continue;
        }
        if (current->renderfx & 128) {
            if (qa_q2_model_beam(remote_q2_rerelease_presentation(row) ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
                current->renderfx, current->modelindex > 1)) {
                if (current->modelindex < row->layout.max_models) {
                    const char *path = frontend_remote_q2_config(row,
                        (uint16_t)(row->layout.models + current->modelindex));
                    ok = submit_model(row, path, NULL, &view, &world, current, NULL, false,
                        vector(current->origin), vector(current->angles), error);
                }
                continue;
            }
            if (current->frame > INT32_MAX) { ok = remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 beam width leaves its native integer range"); break; }
            ok = frontend_remote_q2_effects_entity_beam(row->effects, &view, vector(current->origin),
                vector(current->old_origin), current->skinnum, (int32_t)current->frame, &f->frame, error);
            continue;
        }
        if (!current->modelindex || current->modelindex >= row->layout.max_models ||
            current->number == player_entity_number) continue;
        const qa_q2_entity *prior = entity(&row->previous, current->number);
        if (!prior || prior->modelindex != current->modelindex || current->event == 6 || current->event == 7 ||
            !near(vector(prior->origin), vector(current->origin), 512)) prior = NULL;
        qa_vec3 position = prior && !(current->renderfx & 64) ?
            qa_vec_lerp(vector(prior->origin), vector(current->origin), row->fraction) : vector(current->origin);
        qa_vec3 direction = prior ? angles_lerp(prior->angles, current->angles, row->fraction) : vector(current->angles);
        qa_q2_entity packet = *current;
        if (current->effects & (UINT64_C(1) << 10)) packet.frame = auto_frame & 1;
        else if (current->effects & (UINT64_C(1) << 11)) packet.frame = 2 + (auto_frame & 1);
        else if (current->effects & (UINT64_C(1) << 12)) packet.frame = auto_frame;
        else if (current->effects & (UINT64_C(1) << 13)) packet.frame = (uint32_t)(milliseconds / 100);
        if (current->effects & 1) direction = frontend_legacy_entity_angles(QA_SCENE_Q2,
            remote_q2_rerelease_presentation(row) ? QA_EDITION_RERELEASE : QA_EDITION_CLASSIC,
            NULL, current->effects, direction, world.seconds, milliseconds);
        else if (current->effects & (UINT64_C(1) << 23)) direction = qa_v3(0, (float)fmod(time * .5, 360) + current->angles[1], 180);
        uint32_t shell_flags = packet.renderfx;
        bool shell = (current->effects & 256) != 0;
        if (current->effects & (UINT64_C(1) << 16)) { shell = true; shell_flags |= 1024; }
        if (current->effects & (UINT64_C(1) << 15)) { shell = true; shell_flags |= 4096; }
        if (current->effects & (UINT64_C(1) << 27)) { shell = true; shell_flags |= 65536; }
        if (current->effects & (UINT64_C(1) << 30)) { shell = true; shell_flags |= 131072; }
        if (remote_q2_rerelease_presentation(row) && (current->effects & (UINT64_C(1) << 32))) { shell = true; shell_flags |= 524288; }
        if (shell) packet.renderfx = 0;
        if (packet.alpha == 0) {
            if (current->renderfx == 32) packet.alpha = .7f;
            if (current->effects & 128) { packet.renderfx |= 32; packet.alpha = .3f; }
            if (current->effects & (UINT64_C(1) << 24)) { packet.renderfx |= 32; packet.alpha = .6f; }
            if (current->effects & (UINT64_C(1) << 28)) {
                packet.renderfx |= 32; packet.alpha = current->effects & (UINT64_C(1) << 31) ? .6f : .3f;
            }
        }
        const char *path = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + current->modelindex));
        remote_q2_clientinfo info = {0}; const char *skin = NULL;
        if (current->modelindex == 255) {
            ok = remote_q2_clientinfo_read(row, current->skinnum & 255, current->skinnum >> 8, &info, error);
            if (!ok) break;
            if (!info.valid) continue;
            path = info.model; skin = info.skin;
        }
        ok = submit_model(row, path, skin, &view, &world, &packet, prior, false, position, direction, error);
        if (ok && shell) {
            if (!strcmp(row->data.gamedir, "rogue")) {
                if ((shell_flags & 131072) && (shell_flags & (1024 | 4096 | 65536))) shell_flags &= ~UINT32_C(131072);
                if (shell_flags & 65536) {
                    if (shell_flags & (1024 | 4096 | 2048)) shell_flags &= ~UINT32_C(65536);
                    if (shell_flags & 1024) shell_flags |= 4096;
                    else if (shell_flags & 4096) {
                        if (shell_flags & 2048) shell_flags &= ~UINT32_C(4096);
                        else shell_flags |= 2048;
                    }
                }
            }
            packet.renderfx = shell_flags | 32; packet.alpha = current->alpha != 0 ? current->alpha : .3f;
            ok = submit_model(row, path, skin, &view, &world, &packet, prior, false, position, direction, error);
        }
        uint32_t linked[] = {current->modelindex2, current->modelindex3, current->modelindex4};
        for (size_t j = 0; ok && j < 3; ++j) if (linked[j] && linked[j] < row->layout.max_models) {
            const char *linked_path;
            if (!j && linked[j] == 255) {
                if (current->modelindex != 255 && !remote_q2_clientinfo_read(row,
                    current->skinnum & 255, current->skinnum >> 8, &info, error)) { ok = false; break; }
                linked_path = info.weapon;
            } else linked_path = frontend_remote_q2_config(row, (uint16_t)(row->layout.models +
                ((!j && !remote_q2_rerelease_presentation(row) && (linked[j] & 128)) ? linked[j] & 127 : linked[j])));
            qa_q2_entity attachment = packet; attachment.skinnum = 0; attachment.alpha = 0; attachment.renderfx = 0; attachment.modelindex = 0;
            if (!j && linked[j] != 255 && !remote_q2_rerelease_presentation(row) && (linked[j] & 128)) {
                attachment.alpha = .32f; attachment.renderfx = 32;
            }
            qa_q2_entity attachment_old = prior ? *prior : attachment; attachment_old.alpha = 0;
            ok = submit_model(row, linked_path, NULL, &view, &world, &attachment,
                prior ? &attachment_old : NULL, false, position, direction, error);
        }
    }
    const qa_cvar_view *gun_setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.legacy.cl_gun);
    if (ok && entities_enabled && (!gun_setting || gun_setting->number != 0) &&
        (remote_q2_rerelease_presentation(row) || frame->player.fov <= 90) &&
        frame->player.gunindex && frame->player.gunindex < row->layout.max_models) {
        qa_q2_entity gun = {.number = player_entity_number, .frame = frame->player.gunframe,
            .skinnum = frame->player.gunskin, .renderfx = 1 | 4 | 16, .scale = 1};
        qa_q2_entity old = gun; old.frame = before && before->player.gunindex == frame->player.gunindex ? before->player.gunframe : gun.frame;
        if (!gun.frame) old.frame = 0;
        qa_vec3 gun_offset = continuous ? qa_vec_lerp(vector(before->player.gunoffset), vector(frame->player.gunoffset), row->fraction) :
            vector(frame->player.gunoffset);
        qa_vec3 gun_angles = continuous ? angles_lerp(before->player.gunangles, frame->player.gunangles, row->fraction) :
            vector(frame->player.gunangles);
        ok = submit_model(row, frontend_remote_q2_config(row, (uint16_t)(row->layout.models + frame->player.gunindex)), NULL,
            &view, &world, &gun, &old, true, qa_vec_add(origin, gun_offset), qa_vec_add(angles, gun_angles), error);
    }
    const qa_cvar_view *particles_setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_particles);
    if (ok) ok = frontend_remote_q2_effects_draw(row->effects, &effects_sample,
        !particles_setting || particles_setting->number != 0, entities_enabled,
        &f->frame, error);
    if (ok && world.boxed_sky) ok = qa_scene_world_boxed_sky_finish(world.boxed_sky, &f->frame, error);
    if (ok && world.q2_alpha) {
        qa_scene_world_input alpha = world;
        alpha.legacy_phase = QA_LEGACY_WORLD_ALPHA;
        ok = qa_scene_world_submit(row->world, &alpha, &f->frame, error);
    }
    world.fog.sky_drawn = !world.no_world && qa_scene_world_sky_drawn(row->world);
    if (ok) ok = qa_scene_frame_finish(&f->frame, &view, &world.fog, error);
    const qa_cvar_view *blend_setting = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.cl_blend);
    if (ok && policy.lighting.polyblend && (!blend_setting || blend_setting->number != 0)) {
        bool extended = remote_q2_rerelease_presentation(row) || (row->options.domain.protocol.kind == QA_NET_Q2PRO_36 &&
            row->data.protocol_revision >= 1025 && (row->data.wire_flags & 16u));
        qa_scene_vec4 blend = player_blend(frame->player.blend,
            extended && continuous ? before->player.blend : NULL, row->fraction);
        if (blend.w > 0) ok = qa_scene_frame_picture(&f->frame, row->white, view.viewport, view.viewport,
            (qa_scene_vec4){0, 0, 1, 1}, blend, error);
        if (ok && extended) ok = damage_blend_draw(row, view.viewport,
            player_blend(frame->player.damage_blend, continuous ? before->player.damage_blend : NULL, row->fraction), error);
    }
    if (!policy.lighting.cull)
        for (size_t i = scene_first; i < f->frame.command_count; ++i)
            if (f->frame.commands[i].kind == QA_SCENE_COMMAND_DRAW)
                f->frame.commands[i].data.draw.state.cull = QA_CULL_NONE;
    if (ok && row->classic) {
        qa_hud_q2_options options = {.viewport = view.viewport, .scale = 1, .font_line_height = 8, .white = row->white,
            .fonts = f->seats[seat].fonts, .table = &row->hud_table, .context = row,
            .configstring = hud_config, .picture = remote_q2_picture_read};
        options.fonts.classic = row->classic;
        qa_hud_q2_frame hud = {.protocol = row->options.domain.protocol, .stats = frame->player.stats,
            .stat_count = QA_Q2_MAX_STATS, .inventory = row->inventory, .inventory_count = 256,
            .layout = row->overlay ? row->overlay : "", .player_number = player_number,
            .server_frame = row->frame.server_frame, .time_ns = row->sample_ns, .frame_ns = (uint64_t)(row->frame_ms * 1000000)};
        ok = qa_hud_q2_draw(&options, &hud, false, &f->frame, error);
    }
    if (ok) ok = hit_marker_draw(row, &frame->player, view.viewport, error);
    --row->busy;
    *listener = (qa_audio_listener){.seat = seat, .actor = player_entity_number ? player_entity_number : QA_AUDIO_NO_ACTOR,
        .origin = view.origin, .gain = 1};
    memcpy(listener->axis, view.axis, sizeof(listener->axis));
    return ok && remote_q2_live(row, error);
}

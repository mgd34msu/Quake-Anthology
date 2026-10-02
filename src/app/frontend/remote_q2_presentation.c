#include "remote_q2_private.h"
#include "qa/material.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static qa_vec3 vector(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static qa_vec3 angles_lerp(const float a[3], const float b[3], float t)
{
    float out[3];
    for (size_t i = 0; i < 3; ++i) { float delta = fmodf(b[i] - a[i], 360); if (delta > 180) delta -= 360; if (delta < -180) delta += 360; out[i] = a[i] + t * delta; }
    return vector(out);
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
        if (row->retired || row->options.domain.physical_seat != seat) continue;
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
static const qa_q2_entity *entity(const qa_q2_wire_frame *frame, uint32_t number)
{
    for (size_t i = 0; i < frame->entity_count; ++i) if (frame->entities[i].number == number) return &frame->entities[i];
    return NULL;
}
static qa_vec3 player_origin(const frontend_remote_q2 *row, const qa_q2_player *player)
{
    if (row->layout.max_models == 8192) return vector(player->pmove.origin_f);
    return qa_v3((float)player->pmove.origin[0] * 0.125f, (float)player->pmove.origin[1] * 0.125f, (float)player->pmove.origin[2] * 0.125f);
}
static bool near(qa_vec3 a, qa_vec3 b, float limit)
{ return fabsf(a.x - b.x) <= limit && fabsf(a.y - b.y) <= limit && fabsf(a.z - b.z) <= limit; }
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
    qa_audio_asset *asset = NULL; bool ok;
    if (name[0] == '*') {
        const char *value = event->entity && event->entity <= 256 ?
            frontend_remote_q2_config(row, (uint16_t)(row->layout.players + event->entity - 1)) : "";
        const char *appearance = strchr(value, '\\'); appearance = appearance ? appearance + 1 : value;
        const char *slash = strchr(appearance, '/'); char model[1024];
        size_t length = slash ? (size_t)(slash - appearance) : 0;
        if (!length || length >= sizeof(model)) strcpy(model, "male");
        else { memcpy(model, appearance, length); model[length] = 0; }
        ok = qa_audio_bank_sexed(row->sounds, name, model, &asset, error);
    } else ok = qa_audio_bank_register(row->sounds, name, QA_AUDIO_Q2, &asset, error);
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
        default: break;
        }
    }
    return true;
}
bool frontend_remote_q2_sample(qa_frontend *f, uint64_t now, qa_error *error)
{
    if (!f) return false;
    for (frontend_remote_q2 *row = f->remote_q2; row; row = row->next) {
        if (row->retired || !row->bound) continue;
        if (!remote_q2_live(row, error)) return false;
        row->sample_ns = now;
        double elapsed = now >= row->received_ns ? (double)(now - row->received_ns) / 1000000.0 : 0;
        row->fraction = (float)fmin(1, fmax(0, elapsed / row->frame_ms));
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
    if (!row->bound) return true;
    const qa_q2_frame_player *frame = frame_player(row, &row->frame);
    if (!remote_q2_live(row, error)) return false;
    if (!row->media_ready || !frame) return true;
    qa_movement_kind kind = row->layout.max_models == 8192 ? QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC;
    qa_input_command_tuning tuning;
    if (!qa_input_settings_read(row->options.domain.cvars, kind, &tuning, error)) return false;
    qa_vec3 delta = frame->player.pmove.float_delta_angles ? vector(frame->player.pmove.delta_angles_f) :
        qa_v3(frame->player.pmove.delta_angles[0] * (360.0f / 65536), frame->player.pmove.delta_angles[1] * (360.0f / 65536),
            frame->player.pmove.delta_angles[2] * (360.0f / 65536));
    if (!row->input_set || row->input.kind != kind) {
        qa_input_command_clear(&row->input); row->input.kind = kind;
        if (!qa_input_command_angles(&row->input, qa_vec_sub(vector(frame->player.viewangles), delta), error)) return false;
    }
    qa_input_command_frame basis = {.kind = kind, .sequence = sequence, .server_frame = row->frame.server_frame,
        .server_time_ms = (int32_t)(((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms),
        .acknowledged_server_seconds = (double)row->frame.server_frame * row->frame_ms * 0.001,
        .delta_angles = delta, .sensitivity = 1, .attack_allowed = true};
    qa_movement_command command;
    if (!qa_input_command_build(&row->input, &tuning, sample, &basis, sample->frame_ms, &command, error)) return false;
    *out = (qa_q2_usercmd){.server_frame = row->frame.server_frame,
        .msec = command.milliseconds > 255 ? 255 : (uint8_t)command.milliseconds,
        .buttons = (uint8_t)command.buttons, .impulse = command.impulse, .lightlevel = command.light_level,
        .forwardmove = command.forward_move, .sidemove = command.side_move, .upmove = command.up_move};
    if (kind == QA_MOVEMENT_Q2_RERELEASE) {
        float values[3] = {command.angles.x, command.angles.y, command.angles.z};
        for (size_t i = 0; i < 3; ++i) out->angles[i] = (int16_t)(int32_t)fmodf(truncf(values[i] * (65536.0f / 360)), 65536);
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
        transform.scale[i] = current->scale ? current->scale : 1;
    }
    qa_scene_vec4 color = {1, 1, 1, current->renderfx & 32 ? 0.30f : 1};
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
    qa_scene_model_input input = {.view = *view, .transform = transform,
        .previous_origin = previous ? vector(previous->origin) : origin, .color = color, .family = QA_SCENE_Q2,
        .frame = current->frame, .old_frame = previous ? previous->frame : current->frame,
        .skin = current->modelindex == 255 ? 0 : current->skinnum, .flags = current->renderfx,
        .entity = current->number, .back_lerp = previous ? 1 - row->fraction : 0,
        .seconds = world->seconds, .view_model = view_model, .player = current->modelindex == 255,
        .material_library = row->materials, .custom_material = skin, .source_path = path};
    qa_vec3 ambient = qa_v3(1, 1, 1), directed = qa_v3(0, 0, 0), direction = qa_v3(0, 0, 1);
    if (!world->no_world && qa_scene_world_sample_light(row->world, origin, &ambient, &directed, &direction))
        ambient = qa_vec_add(ambient, directed);
    input.ambient = ambient; input.light_direction = direction;
    return qa_scene_model_submit(model->scene, &input, &row->frontend->frame, error);
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
    const qa_q2_frame_player *before = frame_player(row, &row->previous);
    qa_vec3 origin = player_origin(row, &frame->player), offset = vector(frame->player.viewoffset), angles = vector(frame->player.viewangles);
    bool continuous = before && near(player_origin(row, &before->player), origin, 256);
    if (continuous && row->layout.max_models == 8192) {
        const qa_q2_entity *player_entity = entity(&row->frame, (uint32_t)frame->player.clientnum + 1);
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
    bool angular = frame->player.pmove.type < (row->layout.max_models == 8192 ? 4 : 2) &&
        !(row->layout.max_models == 8192 && (frame->player.pmove.flags & 256));
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
    origin = qa_vec_add(origin, offset);
    double time = ((double)row->frame.server_frame - 1 + row->fraction) * row->frame_ms;
    if (row->layout.max_models == 8192) {
        float height = (float)frame->player.pmove.viewheight;
        if (!row->height_set) { row->height_previous = row->height_current = height; row->height_changed_ms = time; row->height_set = true; }
        else if (row->height_current != height) { row->height_previous = row->height_current; row->height_current = height; row->height_changed_ms = time; }
        float elapsed = (float)fmin(100, fmax(0, time - row->height_changed_ms));
        origin.z += row->height_current + (row->height_previous - row->height_current) * (100 - elapsed) * 0.01f;
    }
    qa_scene_view view = {.viewport = frontend_viewport(f, seat), .origin = origin,
        .clear_depth = true, .depth = 1, .seat = seat};
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
        .q2_styles = styles, .style_count = 256, .no_world = (frame->player.rdflags & 2) != 0};
    if (ok) ok = qa_scene_world_submit(row->world, &world, &f->frame, error);
    for (size_t i = 0; ok && i < row->frame.entity_count; ++i) {
        const qa_q2_entity *current = &row->frame.entities[i];
        if (!current->modelindex || current->modelindex >= row->layout.max_models ||
            current->number == (uint32_t)frame->player.clientnum + 1) continue;
        const qa_q2_entity *prior = entity(&row->previous, current->number);
        if (!prior || prior->modelindex != current->modelindex || current->event == 6 || current->event == 7 ||
            !near(vector(prior->origin), vector(current->origin), 512)) prior = NULL;
        qa_vec3 position = prior ? qa_vec_lerp(vector(prior->origin), vector(current->origin), row->fraction) : vector(current->origin);
        qa_vec3 direction = prior ? angles_lerp(prior->angles, current->angles, row->fraction) : vector(current->angles);
        const char *path = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + current->modelindex));
        char player_path[2048], skin_path[2048]; const char *skin = NULL;
        if (current->modelindex == 255) {
            const char *value = frontend_remote_q2_config(row, (uint16_t)(row->layout.players + (current->skinnum & 255)));
            const char *appearance = strchr(value, '\\'); appearance = appearance ? appearance + 1 : "male/grunt";
            const char *slash = strchr(appearance, '/'); size_t length = slash ? (size_t)(slash - appearance) : 0;
            if (!length || length > 900) { appearance = "male/grunt"; slash = appearance + 4; length = 4; }
            snprintf(player_path, sizeof(player_path), "players/%.*s/tris.md2", (int)length, appearance);
            snprintf(skin_path, sizeof(skin_path), "players/%.*s/%s.pcx", (int)length, appearance, slash + 1);
            path = player_path; skin = skin_path;
        }
        ok = submit_model(row, path, skin, &view, &world, current, prior, false, position, direction, error);
        uint32_t linked[] = {current->modelindex2, current->modelindex3, current->modelindex4};
        for (size_t j = 0; ok && j < 3; ++j) if (linked[j] && linked[j] != 255 && linked[j] < row->layout.max_models)
            ok = submit_model(row, frontend_remote_q2_config(row, (uint16_t)(row->layout.models + linked[j])), NULL,
                &view, &world, current, prior, false, position, direction, error);
    }
    if (ok && frame->player.gunindex && frame->player.gunindex < row->layout.max_models) {
        qa_q2_entity gun = {.number = (uint32_t)frame->player.clientnum + 1, .frame = frame->player.gunframe, .skinnum = frame->player.gunskin, .scale = 1};
        qa_q2_entity old = gun; old.frame = before && before->player.gunindex == frame->player.gunindex ? before->player.gunframe : gun.frame;
        ok = submit_model(row, frontend_remote_q2_config(row, (uint16_t)(row->layout.models + frame->player.gunindex)), NULL,
            &view, &world, &gun, &old, true, qa_vec_add(origin, vector(frame->player.gunoffset)),
            qa_vec_add(angles, vector(frame->player.gunangles)), error);
    }
    if (ok) ok = qa_scene_frame_finish(&f->frame, &view, &world.fog, error);
    if (ok && row->classic) {
        qa_hud_q2_options options = {.viewport = view.viewport, .scale = 1, .font_line_height = 8, .white = row->white,
            .fonts = f->seats[seat].fonts, .table = &row->hud_table, .context = row,
            .configstring = hud_config, .picture = remote_q2_picture_read};
        options.fonts.classic = row->classic;
        qa_hud_q2_frame hud = {.protocol = row->options.domain.protocol, .stats = frame->player.stats,
            .stat_count = QA_Q2_MAX_STATS, .inventory = row->inventory, .inventory_count = 256,
            .layout = row->overlay ? row->overlay : "", .player_number = frame->player.clientnum,
            .server_frame = row->frame.server_frame, .time_ns = row->sample_ns, .frame_ns = (uint64_t)(row->frame_ms * 1000000)};
        ok = qa_hud_q2_draw(&options, &hud, false, &f->frame, error);
    }
    --row->busy;
    *listener = (qa_audio_listener){.seat = seat, .actor = (uint32_t)frame->player.clientnum + 1,
        .origin = view.origin, .gain = 1};
    memcpy(listener->axis, view.axis, sizeof(listener->axis));
    return ok && remote_q2_live(row, error);
}

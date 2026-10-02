#include "remote_q1_private.h"
#include "remote_q1_prediction.h"
#include <math.h>

static qa_q1_entity sampled(const frontend_remote_q1 *row, qa_q1_entity value)
{
    const qa_q1_entity *old = NULL;
    for (size_t i = 0; i < row->previous.count; ++i) if (row->previous.rows[i].number == value.number) { old = row->previous.rows + i; break; }
    if (value.step || !old || fabsf(value.origin[0] - old->origin[0]) > 100 ||
        fabsf(value.origin[1] - old->origin[1]) > 100 || fabsf(value.origin[2] - old->origin[2]) > 100) return value;
    for (unsigned i = 0; i < 3; ++i) {
        value.origin[i] = (float)(old->origin[i] + (value.origin[i] - old->origin[i]) * row->fraction);
        double delta = fmod((double)value.angles[i] - old->angles[i] + 540, 360) - 180;
        value.angles[i] = (float)(old->angles[i] + delta * row->fraction);
    }
    return value;
}
size_t frontend_remote_q1_entity_count(const frontend_remote_q1 *row)
{ return row ? row->current.count + row->statics.count + (row->view_entity && row->has_data && row->data.weapon_model ? 1 : 0) : 0; }
bool frontend_remote_q1_entity_at(frontend_remote_q1 *row, size_t index, frontend_remote_q1_entity_view *out, qa_error *error)
{
    if (!out || !remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error) || !row->loaded) return false;
    bool weapon = false; qa_q1_entity value;
    if (index < row->current.count) value = sampled(row, row->current.rows[index]);
    else if ((index -= row->current.count) < row->statics.count) value = row->statics.rows[index];
    else {
        index -= row->statics.count;
        if (index || !row->view_entity || !row->has_data || !row->data.weapon_model)
            return remote_q1_fail(error, QA_ERROR_NOT_FOUND, "Q1 presentation entity ordinal is absent");
        qa_q1_entity_init(&value); value.number = row->view_entity; value.model = row->data.weapon_model;
        value.frame = row->data.weapon_frame; value.alpha = row->data.weapon_alpha;
        for (size_t i = 0; i < row->current.count; ++i) if (row->current.rows[i].number == row->view_entity) {
            qa_q1_entity source = sampled(row, row->current.rows[i]);
            for (unsigned j = 0; j < 3; ++j) value.origin[j] = source.origin[j];
            break;
        }
        value.origin[2] += row->data.viewheight;
        value.angles[0] = row->view_angles.x; value.angles[1] = row->view_angles.y; value.angles[2] = row->view_angles.z;
        frontend_remote_q1_player_view player; bool present;
        if (!frontend_remote_q1_player_read(row, &player, &present, error) || !present) return false;
        value.origin[0] = player.origin.x; value.origin[1] = player.origin.y; value.origin[2] = player.origin.z + player.view_height;
        value.angles[0] = player.angles.x; value.angles[1] = player.angles.y; value.angles[2] = player.angles.z;
        weapon = true;
    }
    const char *model = "";
    if (value.model) {
        if (value.model > row->model_count) return remote_q1_fail(error, QA_ERROR_FORMAT, "Unknown received Q1 model index");
        model = row->models[value.model - 1];
    }
    qa_actor_id actor;
    ++row->busy; bool ok = remote_q1_actor_read(row, value.number, &actor, error); --row->busy;
    if (!ok || !remote_q1_live(row, error)) return false;
    *out = (frontend_remote_q1_entity_view){.actor = actor, .entity = value, .model = model,
        .alpha = value.alpha == 0 ? 1 : (float)(value.alpha - 1) / 254,
        .scale = (float)value.scale / 16, .visible = weapon ? row->data.health > 0 && !(row->data.items & 524288) : value.number != row->view_entity,
        .view_weapon = weapon};
    if (weapon) out->scale = 1;
    if (value.colormap && value.colormap <= row->max_clients) {
        uint8_t colors = row->clients[value.colormap - 1].colors;
        out->has_colors = row->clients[value.colormap - 1].present;
        out->top_color = (colors >> 4) > 13 ? 13 : colors >> 4;
        out->bottom_color = (colors & 15) > 13 ? 13 : colors & 15;
    }
    if (qa_q1_is_qw(row->options.domain.protocol) && row->qw_intermission && weapon) out->visible = false;
    const remote_q1_camera_view *camera = remote_q1_camera_read(row);
    if (qa_q1_is_qw(row->options.domain.protocol) && row->qw.spectator) {
        if (weapon) {
            out->visible = out->visible && camera && camera->chase;
            if (camera && camera->chase) out->entity.frame = camera->target_weapon_frame;
        } else if (camera && camera->chase && value.number == (uint32_t)camera->target_slot+1) out->visible = false;
    }
    return true;
}
bool frontend_remote_q1_client_at(const frontend_remote_q1 *row, uint32_t slot, frontend_remote_q1_client_row *out, qa_error *error)
{
    if (!row || !out || row->busy || slot >= 256)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 client metadata requires its actual native slot");
    const remote_q1_client *value = row->clients + slot;
    *out = (frontend_remote_q1_client_row){value->name ? value->name : "", value->social ? value->social : "",
        value->player_info ? value->player_info : "", value->frags, value->ping, value->colors,
        value->present, value->has_ping, value->has_social, value->has_player_info}; return true;
}
const char *frontend_remote_q1_light_style(const frontend_remote_q1 *row, uint32_t style)
{ return row && style < 256 && row->styles[style] ? row->styles[style] : ""; }
bool frontend_remote_q1_player_read(frontend_remote_q1 *row, frontend_remote_q1_player_view *out, bool *present, qa_error *error)
{
    if (!remote_q1_mutable(row) || !out || !present || row->busy || !remote_q1_live(row, error)) return false;
    *present = row->loaded && row->view_entity && row->has_data;
    if (!*present) return true;
    qa_q1_entity value; qa_q1_entity_init(&value);
    for (size_t i = 0; i < row->current.count; ++i) if (row->current.rows[i].number == row->view_entity) { value = sampled(row, row->current.rows[i]); break; }
    qa_actor_id actor;
    ++row->busy; bool ok = remote_q1_actor_read(row, row->view_entity, &actor, error); --row->busy;
    if (!ok) return false;
    *out = (frontend_remote_q1_player_view){.actor = actor,
        .origin = qa_v3(value.origin[0], value.origin[1], value.origin[2]), .angles = row->view_angles,
        .kick_angles = qa_v3(row->data.punch[0], row->data.punch[1], row->data.punch[2]),
        .velocity = qa_v3(row->data.velocity[0], row->data.velocity[1], row->data.velocity[2]),
        .view_height = row->data.viewheight, .ideal_pitch = row->data.idealpitch, .grounded = row->data.onground,
        .intermission = row->intermission || row->qw_intermission};
    if (row->qw_intermission) {
        out->origin = row->qw_intermission_origin; out->angles = row->qw_intermission_angles;
        out->view_height = 0; out->kick_angles = qa_v3(0, 0, 0); out->pitch_drift_disabled = true; out->grounded = false;
    } else if (qa_q1_is_qw(row->options.domain.protocol) && row->qw_player_valid[row->qw.player_slot] &&
        (row->qw_players[row->qw.player_slot].flags & QA_QW_PF_DEAD)) out->angles.z = 80;
    qa_qw_movement_state predicted; float height; bool predicted_present;
    if (!remote_q1_prediction_read(row, &predicted, &height, &predicted_present)) return false;
    if (predicted_present) {
        out->origin = qa_qw_origin_to_vec3(predicted.origin); out->velocity = predicted.velocity;
        out->view_height = height; out->grounded = predicted.ground.hit != QA_TRACE_HIT_NONE; out->ideal_pitch = 0;
    }
    if (qa_q1_is_qw(row->options.domain.protocol) && row->qw.spectator) out->pitch_drift_disabled = true;
    const remote_q1_camera_view *camera = remote_q1_camera_read(row);
    if (!row->qw_intermission && row->qw.spectator && camera) {
        out->origin = camera->origin; out->angles = camera->angles;
        out->view_height = camera->chase && (camera->target_flags & QA_QW_PF_DEAD) ? -16 : 22;
        out->kick_angles = qa_v3(0,0,0); out->grounded = false; out->ideal_pitch = 0;
        out->pitch_drift_disabled = true;
    }
    return remote_q1_live(row, error);
}
bool frontend_remote_q1_receive_end(frontend_remote_q1 *row, uint64_t received, qa_error *error)
{
    if (!remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error)) return false;
    bool own_changed = false;
    if (qa_q1_is_qw(row->options.domain.protocol)) {
        for (size_t i = 0; i < row->qw_batch_players.count; ++i)
            if (row->qw_batch_players.rows[i].number == (uint32_t)row->qw.player_slot + 1) own_changed = true;
        bool frame = row->qw_frame && row->qw_player_valid[row->qw.player_slot];
        if (frame) {
            if (row->frame_number == UINT64_MAX || row->revision == UINT64_MAX)
                return remote_q1_fail(error, QA_ERROR_FORMAT, "QW frame continuation is exhausted");
            /* QW projects the actual local receive clock as binary64. It has
             * no NQ wire float-time service to round through. */
            remote_q1_time_advance(row, (double)received / 1000000000.0, received);
            ++row->revision;
            row->qw_frame = false;
        }
        while (row->qw_pending_cursor < row->qw_pending_count) {
            const qa_nq_message *message = &row->qw_pending[row->qw_pending_cursor++].message;
            if (!frontend_remote_q1_receive_nq(row, message, received, error)) return false;
        }
        remote_q1_qw_queue_clear(row);
        if (frame) {
            qa_nq_message message = {0};
            const remote_q1_entities *tables[] = {&row->qw_entities, &row->qw_nails, &row->qw_batch_players};
            for (unsigned i = 0; i < 3; ++i) for (size_t j = 0; j < tables[i]->count; ++j) {
                message = (qa_nq_message){.op = QA_NQ_ENTITY, .data.entity = tables[i]->rows[j]};
                message.data.entity.step = true;
                if (!frontend_remote_q1_receive_nq(row, &message, received, error)) return false;
            }
            const qa_qw_player *own = row->qw_players + row->qw.player_slot;
            message = (qa_nq_message){.op = QA_NQ_CLIENTDATA, .data.clientdata = {
                .viewheight = own->flags & QA_QW_PF_GIB ? 8 : own->flags & QA_QW_PF_DEAD ? -16 : 22,
                .items = (uint32_t)row->qw_stats[15], .weapon_frame = own->weapon_frame,
                .armor = (uint32_t)row->qw_stats[4], .weapon_model = (uint32_t)row->qw_stats[2], .health = row->qw_stats[0],
                .ammo = (uint32_t)row->qw_stats[3], .shells = (uint32_t)row->qw_stats[6], .nails = (uint32_t)row->qw_stats[7],
                .rockets = (uint32_t)row->qw_stats[8], .cells = (uint32_t)row->qw_stats[9], .weapon = (uint32_t)row->qw_stats[10]}};
            message.data.clientdata.punch[0] = row->qw_kick;
            for (unsigned i = 0; i < 3; ++i) message.data.clientdata.velocity[i] = own->velocity[i];
            if (!frontend_remote_q1_receive_nq(row, &message, received, error)) return false;
            row->qw_kick = 0;
        }
        row->qw_frame = false; row->qw_batch_players.count = row->qw_nails.count = 0;
    }
    ++row->busy; bool ok = true; qa_actor_id actor;
    for (size_t i = 0; ok && i < row->current.count; ++i) ok = remote_q1_actor_read(row, row->current.rows[i].number, &actor, error);
    if (ok && row->view_entity) ok = remote_q1_actor_read(row, row->view_entity, &actor, error);
    if (ok && own_changed) ok = remote_q1_prediction_receive(row, error);
    --row->busy; return ok && remote_q1_live(row, error);
}

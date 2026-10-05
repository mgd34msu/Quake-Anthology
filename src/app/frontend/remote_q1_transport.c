#include "remote_q1_private.h"
#include "internal.h"
#include "remote_q1_prediction.h"
#include "remote_q1_skins.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const struct remote_weapon { uint32_t bit, ammo; const char *name; } weapons[] = {
    {4096,0,"axe"},{1,1,"shotgun"},{2,1,"supershotgun"},{4,2,"nailgun"},
    {8,2,"supernailgun"},{16,3,"grenadelauncher"},{32,3,"rocketlauncher"},{64,4,"lightning"}
};
static bool current(frontend_remote_q1 *row, qa_net_client_id id, qa_error *error)
{ return remote_q1_mutable(row) && qa_net_client_id_equal(row->options.domain.client, id) && remote_q1_live(row, error); }
static bool actor_current(frontend_remote_q1 *row, qa_actor_id actor, qa_error *error)
{
    if (!remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error) || !row->loaded || !row->view_entity || !row->has_data) return false;
    qa_actor_id actual;
    return remote_q1_actor_read(row, row->view_entity, &actual, error) && qa_actor_id_equal(actual, actor);
}
static size_t active(const frontend_remote_q1 *row)
{
    for (size_t i = 0; i < sizeof(weapons) / sizeof(*weapons); ++i)
        if (row->data.weapon == weapons[i].bit || (!i && !row->data.weapon && row->data.weapon_model &&
            row->data.weapon_model <= row->model_count && !strcmp(row->models[row->data.weapon_model - 1], "progs/v_axe.mdl"))) return i;
    return SIZE_MAX;
}
static bool selected(frontend_remote_q1 *row, qa_bytes name, qa_error *error)
{
    if (name.size && !name.data) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Native Q1 weapon name is absent");
    const char *prefix = "q1:weapon/";
    for (size_t i = 0; i < sizeof(weapons) / sizeof(*weapons); ++i) {
        size_t length = strlen(weapons[i].name);
        bool match = (name.size == length && !memcmp(name.data, weapons[i].name, length)) ||
            (name.size == length + 10 && !memcmp(name.data, prefix, 10) && !memcmp(name.data + 10, weapons[i].name, length));
        if (!match) continue;
        if (!(row->data.items & weapons[i].bit)) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Native Q1 weapon is not owned");
        row->pending_impulse = (uint8_t)(i + 1); return true;
    }
    return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Native Q1 weapon selection is unknown");
}
static int16_t source_short(float input)
{
    uint16_t word = (uint16_t)(uint32_t)qa_source_float_to_i32(input);
    int16_t result; memcpy(&result, &word, sizeof(result)); return result;
}
static bool command(frontend_remote_q1 *row, const qa_network_command *source, qa_error *error)
{
    if (!source || !current(row, source->client, error) || source->seat.owner != row->options.domain.seat.owner ||
        source->seat.index != row->options.domain.seat.index || source->epoch != row->options.domain.epoch ||
        !actor_current(row, source->actor, error)) return false;
    if (row->revision == UINT64_MAX) return remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 command revision is exhausted");
    if (source->has_arsenal && source->arsenal.weapon.size) {
        size_t i = active(row); qa_bytes requested = source->arsenal.weapon;
        bool same = false;
        if (i != SIZE_MAX) {
            size_t length = strlen(weapons[i].name);
            same = (requested.size == length && !memcmp(requested.data, weapons[i].name, length)) ||
                (requested.size == length + 10 && !memcmp(requested.data, "q1:weapon/", 10) &&
                    !memcmp(requested.data + 10, weapons[i].name, length));
        }
        if (!same && !selected(row, requested, error)) return false;
    }
    row->view_angles = source->movement.angles; ++row->revision; return true;
}
static bool nq_command(void *context, const qa_network_command *source, qa_q1_command *out, qa_error *error)
{
    frontend_remote_q1 *row = context;
    if (!out || !source || source->movement.kind != QA_MOVEMENT_NETQUAKE || !command(row, source, error)) return false;
    const qa_movement_command *m = &source->movement;
    *out = (qa_q1_command){.time = (float)m->acknowledged_server_seconds,
        .angles = {m->angles.x, m->angles.y, m->angles.z}, .forward = source_short(m->forward_move),
        .side = source_short(m->side_move), .up = source_short(m->up_move), .buttons = (uint8_t)m->buttons,
        .impulse = m->impulse ? m->impulse : row->pending_impulse};
    row->pending_impulse = 0; return true;
}
static bool qw_command(void *context, const qa_network_command *source, uint64_t now, qa_qw_command *out, qa_error *error)
{
    frontend_remote_q1 *row = context;
    if (!out || !source || source->movement.kind != QA_MOVEMENT_QUAKEWORLD || !command(row, source, error)) return false;
    const qa_movement_command *m = &source->movement;
    *out = (qa_qw_command){.angles = {m->angles.x, m->angles.y, m->angles.z},
        .forward = source_short(m->forward_move), .side = source_short(m->side_move), .up = source_short(m->up_move),
        .msec = (uint8_t)m->milliseconds, .buttons = (uint8_t)m->buttons,
        .impulse = m->impulse ? m->impulse : row->pending_impulse};
    row->pending_impulse = 0;
    if (!remote_q1_camera_command(row,out,now,error)) return false;
    row->view_angles=qa_v3(out->angles[0],out->angles[1],out->angles[2]);
    return true;
}
static bool teleport(void *context,qa_net_client_id id,qa_vec3 *out,bool *present,qa_error *error)
{ frontend_remote_q1 *row=context; return current(row,id,error) && remote_q1_camera_take_teleport(row,out,present,error); }
static bool negotiated(frontend_remote_q1 *row, qa_net_client_id id, qa_net_protocol_id protocol, qa_error *error)
{
    if (!current(row, id, error) || !qa_q1_profile_valid(protocol, error) ||
        qa_q1_is_qw(protocol) != qa_q1_is_qw(row->options.domain.protocol)) return false;
    if (protocol.kind != row->protocol.kind || protocol.revision != row->protocol.revision || protocol.flags != row->protocol.flags) {
        if (row->revision == UINT64_MAX) return remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 protocol revision is exhausted");
        ++row->revision;
    }
    row->protocol = protocol;
    return true;
}
static bool nq(void *context, qa_net_client_id id, qa_net_protocol_id protocol,
    const qa_nq_message *message, uint64_t received, qa_error *error)
{ frontend_remote_q1 *row = context; return negotiated(row, id, protocol, error) && frontend_remote_q1_receive_nq(row, message, received, error); }
static bool qw(void *context, qa_net_client_id id, qa_net_protocol_id protocol,
    const qa_qw_service *service, uint64_t received, qa_error *error)
{
    frontend_remote_q1 *row = context;
    if (!service || !negotiated(row, id, protocol, error)) return false;
    if (service->kind == QA_QW_SERVER_DATA) remote_q1_demo_clear(row);
    return service->kind == QA_QW_SERVER_DATA ? frontend_remote_q1_serverdata_qw(row, &service->data.server, error) :
        frontend_remote_q1_receive_qw(row, service, received, error);
}
static bool game_state(void *context, qa_net_client_id id, const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count, uint32_t *checksum, qa_error *error)
{
    frontend_remote_q1 *row = context;
    return current(row, id, error) && frontend_remote_q1_gamestate_qw(row, models, model_count, sounds, sound_count, checksum, error);
}
static bool skins(void *context, qa_net_client_id id, bool *ready, qa_error *error)
{
    frontend_remote_q1 *row = context;
    return ready && current(row, id, error) && row->skins &&
        frontend_remote_q1_skins_refresh(row->skins,ready,error) && current(row,id,error);
}
static bool end(void *context, qa_net_client_id id, uint64_t received, qa_error *error)
{ frontend_remote_q1 *row = context; return current(row, id, error) && frontend_remote_q1_receive_end(row, received, error); }
static bool sent(void *context, qa_net_client_id id, uint32_t sequence, const qa_q1_command *nq_command_value,
    const qa_qw_command *qw_command_value, uint64_t ns, qa_error *error)
{
    frontend_remote_q1 *row = context; (void)nq_command_value;
    bool ok = current(row, id, error) && (!qa_q1_is_qw(row->options.domain.protocol) ||
        (qw_command_value && remote_q1_prediction_sent(row, sequence, qw_command_value, ns, error)));
    if (ok && qw_command_value && row->demo_sink.append) {
        frontend_demo_packet packet = {.format = FRONTEND_DEMO_QW, .value.qw = {
            .kind = QA_QW_DEMO_COMMAND, .seconds = (float)((double)(ns >= row->demo_record_start ?
                ns - row->demo_record_start : 0) / 1e9)}};
        packet.value.qw.data.input.command = *qw_command_value;
        packet.value.qw.data.input.angles[0] = row->view_angles.x;
        packet.value.qw.data.input.angles[1] = row->view_angles.y;
        packet.value.qw.data.input.angles[2] = row->view_angles.z;
        qa_error recording = {0}; (void)row->demo_sink.append(row->demo_sink.owner, &packet, &recording);
    }
    return ok;
}
static bool acknowledged(void *context, qa_net_client_id id, uint32_t sequence, uint64_t ns, qa_error *error)
{
    frontend_remote_q1 *row = context;
    return current(row, id, error) && (!qa_q1_is_qw(row->options.domain.protocol) ||
        remote_q1_prediction_receipt(row, sequence, ns, error));
}
static bool loss(void *context, qa_net_client_id id, uint32_t outgoing, uint8_t *out, qa_error *error)
{ frontend_remote_q1 *row = context; return current(row, id, error) && remote_q1_prediction_loss(row, outgoing, out, error); }
static bool drop(void *context, qa_net_client_id id, const char *reason, qa_error *error)
{
    frontend_remote_q1 *row = context;
    return row && qa_net_client_id_equal(id, row->options.domain.client) && frontend_remote_q1_disconnected(row, reason, error);
}
static bool batch(void *context, qa_net_client_id id, qa_net_protocol_id protocol,
    qa_bytes bytes, qa_bytes prefix, uint32_t sequence, uint32_t acknowledged, uint64_t received, qa_error *error)
{
    frontend_remote_q1 *row = context;
    return current(row, id, error) && row->protocol.kind == protocol.kind &&
        row->protocol.revision == protocol.revision && row->protocol.flags == protocol.flags &&
        remote_q1_demo_batch(row, bytes, prefix, sequence, acknowledged, received, error);
}
bool frontend_remote_q1_hooks(frontend_remote_q1 *row, qa_network_q1_client_hooks *out, qa_error *error)
{
    if (!row || !out || row->busy || row->retired ||
        (row->bound || !remote_q1_mutable(row)))
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 hooks require their genuine pending CLIENT owner");
    *out = (qa_network_q1_client_hooks){.context = row, .nq = nq, .qw = qw, .qw_game_state = game_state,
        .qw_skins = skins, .end = end, .command_nq = nq_command, .command_qw = qw_command, .qw_teleport = teleport,
        .qw_loss = loss, .sent = sent, .acknowledged = acknowledged, .drop = drop, .batch = batch}; return true;
}
bool frontend_remote_q1_player_command(frontend_remote_q1 *row, qa_actor_id actor,
    const char *name, const char *const *args, size_t count, qa_error *error)
{
    if (!name || (count && !args) || !actor_current(row, actor, error) || row->revision == UINT64_MAX) return false;
    if (strpbrk(name, "\"\n\r;")) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Invalid native Q1 command delimiter");
    size_t size = strlen(name) + 1;
    for (size_t i = 0; i < count; ++i) {
        if (!args[i] || strpbrk(args[i], "\"\n\r;") || size > SIZE_MAX - 3 || strlen(args[i]) > SIZE_MAX - size - 3) return false;
        size += strlen(args[i]) + 3;
    }
    if (!strcmp(name, "use")) {
        char *selection = malloc(size); if (!selection) return false;
        size_t at = 0;
        for (size_t i = 0; i < count; ++i) for (const unsigned char *p = (const unsigned char *)args[i]; *p; ++p)
            if (*p != ' ') selection[at++] = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        bool ok = selected(row, (qa_bytes){(const uint8_t *)selection, at}, error);
        if (ok) ++row->revision;
        free(selection); return ok;
    }
    if (!strcmp(name, "weapnext") || !strcmp(name, "weapprev")) {
        size_t owned[8], available = 0, chosen = active(row), position = SIZE_MAX;
        uint32_t ammo[] = {1, row->data.shells, row->data.nails, row->data.rockets, row->data.cells};
        for (size_t i = 0; i < 8; ++i) if ((row->data.items & weapons[i].bit) && ammo[weapons[i].ammo]) {
            if (i == chosen) position = available;
            owned[available++] = i;
        }
        if (available) {
            size_t next = !strcmp(name, "weapnext") ? (position == SIZE_MAX ? 0 : (position + 1) % available) :
                (position == SIZE_MAX ? (available + available - 2) % available : (position + available - 1) % available);
            row->pending_impulse = (uint8_t)(owned[next] + 1);
            ++row->revision;
        }
        return true;
    }
    char *text = malloc(size); if (!text) return false;
    size_t at = strlen(name); memcpy(text, name, at);
    for (size_t i = 0; i < count; ++i) {
        text[at++] = ' '; text[at++] = '"'; size_t n = strlen(args[i]); memcpy(text + at, args[i], n); at += n; text[at++] = '"';
    }
    text[at] = 0;
    bool ok = qa_network_q1_client_command(row->options.domain.runtime, row->options.domain.client, text, error);
    free(text); return ok;
}

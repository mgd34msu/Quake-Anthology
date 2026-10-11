#include "remote_q1_private.h"
#include "remote_q1_prediction.h"
#include "remote_q1_skins.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int32_t source_atoi(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p == ' ' || (*p >= 9 && *p <= 13)) ++p;
    bool negative = *p == '-'; if (*p == '+' || *p == '-') ++p;
    uint32_t limit = negative ? 2147483648u : 2147483647u, value = 0;
    while (*p >= '0' && *p <= '9') {
        unsigned digit = *p++ - '0'; value = value > (limit - digit) / 10 ? limit : value * 10 + digit;
    }
    if (negative && value == 2147483648u) return INT32_MIN;
    return negative ? -(int32_t)value : (int32_t)value;
}
static bool info_value(const char *info, const char *key, char **out, qa_error *error)
{
    const char *value = ""; size_t length = 0, key_size = strlen(key);
    const char *p = info ? info : ""; if (*p == '\\') ++p;
    while (*p) {
        const char *start = p; while (*p && *p != '\\') ++p;
        if (!*p) break;
        size_t name_size = (size_t)(p - start); const char *next = ++p;
        while (*p && *p != '\\') ++p;
        if (name_size == key_size && !memcmp(start, key, key_size)) value = next, length = (size_t)(p - next);
        if (*p) ++p;
    }
    char *copy = malloc(length + 1);
    if (!copy) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining QW userinfo value");
    memcpy(copy, value, length); copy[length] = 0; free(*out); *out = copy; return true;
}
static bool info_set(char **info, const char *key, const char *value, qa_error *error)
{
    if (!key || !value) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "QW setinfo lacks its actual key/value");
    const char *old = *info ? *info : "";
    size_t old_size = strlen(old), key_size = strlen(key), value_size = strlen(value);
    if (key_size > SIZE_MAX - 3 || value_size > SIZE_MAX - key_size - 3 || old_size > SIZE_MAX - key_size - value_size - 3) return false;
    char *copy = malloc(old_size + key_size + value_size + 3);
    if (!copy) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining QW setinfo map");
    size_t used = 0; const char *p = old; if (*p == '\\') ++p;
    while (*p) {
        const char *name = p; while (*p && *p != '\\') ++p; if (!*p) break;
        size_t length = (size_t)(p - name); const char *text = ++p;
        while (*p && *p != '\\') ++p;
        if (length != key_size || memcmp(name, key, length)) {
            copy[used++] = '\\'; memcpy(copy + used, name, length); used += length;
            copy[used++] = '\\'; length = (size_t)(p - text); memcpy(copy + used, text, length); used += length;
        }
        if (*p) ++p;
    }
    copy[used++] = '\\'; memcpy(copy + used, key, key_size); used += key_size;
    copy[used++] = '\\'; memcpy(copy + used, value, value_size); used += value_size; copy[used] = 0;
    free(*info); *info = copy; return true;
}
static bool scoreboard(frontend_remote_q1 *row, uint8_t slot, qa_error *error)
{
    if (slot >= 32) return remote_q1_fail(error, QA_ERROR_FORMAT, "Invalid QW userinfo slot");
    char *name = NULL, *top = NULL, *bottom = NULL;
    bool ok = info_value(row->clients[slot].userinfo, "name", &name, error) &&
        info_value(row->clients[slot].userinfo, "topcolor", &top, error) &&
        info_value(row->clients[slot].userinfo, "bottomcolor", &bottom, error);
    if (ok) {
        if (strlen(name) > 15) name[15] = 0;
        int32_t a = source_atoi(top), b = source_atoi(bottom);
        if (a < 0 || a > 13) a = 13;
        if (b < 0 || b > 13) b = 13;
        qa_nq_message message = {.op = QA_NQ_NAME, .data.indexed_text = {slot, name}};
        ok = remote_q1_event_admit(row, &message, row->received_ns, error);
        message = (qa_nq_message){.op = QA_NQ_COLORS, .data.indexed = {slot, a * 16 + b}};
        if (ok) ok = remote_q1_event_admit(row, &message, row->received_ns, error);
    }
    free(name); free(top); free(bottom); return ok;
}
bool frontend_remote_q1_serverdata_qw(frontend_remote_q1 *row, const qa_qw_serverdata *data, qa_error *error)
{
    if (!remote_q1_mutable(row) || !data || row->busy || !remote_q1_live(row, error) || !qa_q1_is_qw(row->options.domain.protocol) ||
        data->player_slot >= 32 || data->protocol.kind != row->protocol.kind ||
        data->protocol.revision != row->protocol.revision || data->protocol.flags != row->protocol.flags)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "QW serverdata requires its real native CLIENT dialect");
    if (!remote_q1_string(&row->qw_directory, data->game_directory, error) || !remote_q1_string(&row->level_name, data->level, error)) return false;
    row->qw = *data; row->qw.game_directory = row->qw_directory; row->qw.level = row->level_name;
    row->event_cursor = qa_event_ring_next(row->events);
    qa_event_ring_retire(row->events, row->event_cursor);
    row->qw_ready = false; ++row->revision; return true;
}
bool frontend_remote_q1_gamestate_qw(frontend_remote_q1 *row, const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count, uint32_t *checksum, qa_error *error)
{
    if (!remote_q1_mutable(row) || row->busy || !checksum || !remote_q1_live(row, error) || !row->qw_directory || !qa_q1_is_qw(row->options.domain.protocol)) return false;
    qa_nq_message message = {.op = QA_NQ_SERVERINFO, .data.serverinfo = {
        .protocol = {QA_NET_NQ15, 15, 0}, .max_clients = 32, .game_type = 1, .level = row->qw.level,
        .models = models, .sounds = sounds, .model_count = model_count, .sound_count = sound_count}};
    if (!frontend_remote_q1_receive_nq(row, &message, 0, error)) return false;
    message = (qa_nq_message){.op = QA_NQ_SETVIEW, .data.value = (uint32_t)row->qw.player_slot + 1};
    if (!frontend_remote_q1_receive_nq(row, &message, 0, error)) return false;
    if (!qa_qw_map_checksum2(qa_resource_bytes(row->map), checksum, error)) return false;
    row->qw_ready = true; ++row->revision; return true;
}
bool frontend_remote_q1_receive_qw(frontend_remote_q1 *row, const qa_qw_service *service, uint64_t now, qa_error *error)
{
    row->received_ns = now;
    if (!remote_q1_mutable(row) || !service || row->busy || !remote_q1_live(row, error) || !qa_q1_is_qw(row->options.domain.protocol)) return false;
    if (row->revision == UINT64_MAX) return remote_q1_fail(error, QA_ERROR_FORMAT, "QW presentation revision is exhausted");
    ++row->revision;
    if (service->kind == QA_QW_DOWNLOAD) {
        bool completed=false;
        if (!row->skins ||
            !frontend_remote_q1_skins_receive(row->skins,service,&completed,error)) return false;
        if (!completed) return true;
        qa_network_q1_client_state actual;
        return qa_network_q1_client_state_read(row->options.domain.runtime,row->options.domain.client,&actual,error) &&
            (!actual.waiting_skins || qa_network_q1_client_skins_ready(row->options.domain.runtime,row->options.domain.client,error));
    }
    if (!row->qw_ready) {
        if (service->kind == QA_QW_CD_TRACK) {
            qa_nq_message message = {.op = QA_NQ_CDTRACK,
                .data.cd = {service->data.byte, service->data.byte}};
            return remote_q1_event_admit(row, &message, now, error);
        }
        return true;
    }
    qa_nq_message message = {0};
    switch (service->kind) {
    case QA_QW_PLAYER: {
        const qa_qw_player *p = &service->data.player;
        if (p->slot >= 32) return remote_q1_fail(error, QA_ERROR_FORMAT, "QW playerinfo slot exceeds native players");
        row->qw_players[p->slot] = *p; row->qw_player_valid[p->slot] = true;
        if (p->slot == row->qw.player_slot) row->camera.self_present = false;
        qa_q1_entity entity; qa_q1_entity_init(&entity);
        entity.number = (uint32_t)p->slot + 1; entity.model = p->model; entity.frame = p->frame; entity.colormap = entity.number;
        entity.skin = p->skin; entity.effects = p->effects; entity.step = true;
        memcpy(entity.origin, p->origin, sizeof(entity.origin)); entity.angles[0] = -p->command.angles[0] / 3; entity.angles[1] = p->command.angles[1];
        return remote_q1_entity_set(&row->qw_batch_players, &entity, error);
    }
    case QA_QW_PACKET_ENTITIES:
        row->qw_entities.count = 0;
        for (size_t i = 0; i < service->data.packet.frame->count; ++i)
            if (!remote_q1_entity_set(&row->qw_entities, service->data.packet.frame->entities + i, error)) return false;
        row->qw_frame = true; return true;
    case QA_QW_NAILS: {
        uint32_t model = 0;
        for (size_t i = 0; i < row->model_count; ++i) if (!strcmp(row->models[i], "progs/spike.mdl")) { model = (uint32_t)i + 1; break; }
        if (!model) return true;
        for (size_t i = 0; i < service->data.nails.count; ++i) {
            qa_q1_entity entity; qa_q1_entity_init(&entity);
            entity.number = 131072u + (uint32_t)i; entity.model = model; entity.step = true;
            memcpy(entity.origin, service->data.nails.items[i].origin, sizeof(entity.origin));
            entity.angles[0] = service->data.nails.items[i].pitch; entity.angles[1] = service->data.nails.items[i].yaw;
            if (!remote_q1_entity_set(&row->qw_nails, &entity, error)) return false;
        }
        return true;
    }
    case QA_QW_STAT: message.op = QA_NQ_STAT; message.data.indexed.index = service->data.stat.index; message.data.indexed.value = service->data.stat.value; break;
    case QA_QW_KILLED_MONSTER: message.op = QA_NQ_KILLEDMONSTER; break;
    case QA_QW_FOUND_SECRET: message.op = QA_NQ_FOUNDSECRET; break;
    case QA_QW_CHOKE_COUNT: return remote_q1_prediction_choked(row, service->data.byte, error);
    case QA_QW_INVALID_DELTA: return remote_q1_prediction_invalid_delta(row, error);
    case QA_QW_KICK: row->qw_kick = service->data.kick; return true;
    case QA_QW_MAX_SPEED: row->qw.movement.max_speed = service->data.scalar; return true;
    case QA_QW_ENTITY_GRAVITY: row->qw.movement.entity_gravity = service->data.scalar; return true;
    case QA_QW_USERINFO:
        if (service->data.userinfo.slot >= 32 || !remote_q1_string(&row->clients[service->data.userinfo.slot].userinfo, service->data.userinfo.value, error)) return false;
        return scoreboard(row, service->data.userinfo.slot, error);
    case QA_QW_SET_INFO:
        if (service->data.info.slot >= 32 || !info_set(&row->clients[service->data.info.slot].userinfo, service->data.info.key, service->data.info.value, error)) return false;
        return scoreboard(row, service->data.info.slot, error);
    case QA_QW_PRINT: message.op = QA_NQ_PRINT; message.data.text = service->data.text.value; break;
    case QA_QW_INTERMISSION:
        row->qw_intermission = true;
        row->qw_intermission_origin = qa_v3(service->data.intermission.origin[0], service->data.intermission.origin[1], service->data.intermission.origin[2]);
        row->qw_intermission_angles = qa_v3(service->data.intermission.angles[0], service->data.intermission.angles[1], service->data.intermission.angles[2]);
        message.op = QA_NQ_SETANGLE; memcpy(message.data.angles, service->data.intermission.angles, sizeof(message.data.angles));
        if (!remote_q1_event_admit(row, &message, row->received_ns, error)) return false;
        message.op = QA_NQ_INTERMISSION; break;
    case QA_QW_CD_TRACK: message.op = QA_NQ_CDTRACK; message.data.cd.track = message.data.cd.loop = service->data.byte; break;
    case QA_QW_SELL_SCREEN: message.op = QA_NQ_SELLSCREEN; break;
    case QA_QW_SOUND: case QA_QW_STATIC_SOUND:
        if (!service->data.sound.index || service->data.sound.index > row->sound_count)
            return remote_q1_fail(error, QA_ERROR_FORMAT, "Invalid QW sound index");
        if (!row->sound_available || !row->sound_available[service->data.sound.index - 1]) return true;
        message.op = service->kind == QA_QW_SOUND ? QA_NQ_SOUND : QA_NQ_STATICSOUND; message.data.sound = service->data.sound; break;
    case QA_QW_SET_VIEW: message.op = QA_NQ_SETVIEW; message.data.value = service->data.entity; break;
    case QA_QW_FRAGS: message.op = QA_NQ_FRAGS; message.data.indexed.index = service->data.score.slot; message.data.indexed.value = service->data.score.value; break;
    case QA_QW_BASELINE: case QA_QW_STATIC: message.op = service->kind == QA_QW_STATIC ? QA_NQ_STATIC : QA_NQ_BASELINE; message.data.entity = service->data.baseline; break;
    case QA_QW_SET_ANGLE: message.op = QA_NQ_SETANGLE; memcpy(message.data.angles, service->data.angles, sizeof(message.data.angles)); break;
    case QA_QW_LIGHT_STYLE: message.op = QA_NQ_LIGHTSTYLE; message.data.indexed_text.index = service->data.light_style.index; message.data.indexed_text.text = service->data.light_style.value; break;
    case QA_QW_STOP_SOUND: message.op = QA_NQ_STOPSOUND; message.data.stop_sound.entity = service->data.stop_sound.entity; message.data.stop_sound.channel = service->data.stop_sound.channel; break;
    case QA_QW_DAMAGE: message.op = QA_NQ_DAMAGE; message.data.damage.armor = service->data.damage.armor; message.data.damage.blood = service->data.damage.blood; memcpy(message.data.damage.origin, service->data.damage.origin, sizeof(message.data.damage.origin)); break;
    case QA_QW_TEMPORARY_ENTITY: message.op = QA_NQ_TEMPENTITY; message.data.temporary = service->data.temporary; break;
    case QA_QW_PAUSE: message.op = QA_NQ_PAUSE; message.data.value = service->data.paused; break;
    case QA_QW_CENTER_PRINT: case QA_QW_FINALE: message.op = service->kind == QA_QW_FINALE ? QA_NQ_FINALE : QA_NQ_CENTERPRINT; message.data.text = service->data.text.value; break;
    default: return true;
    }
    return remote_q1_event_admit(row, &message, row->received_ns, error);
}

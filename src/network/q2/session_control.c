#include "session_internal.h"
#include "q2pro_internal.h"
#include <limits.h>

enum { Q2_NOGUN = 0, Q2_NOBLEND = 1, Q2_RECORDING = 2, Q2_FPS = 4,
    Q2_NOGIBS = 10, Q2_NOFOOTSTEPS = 11, Q2_NOPREDICT = 12, Q2_NOFLARES = 13 };

bool q2_server_userinfo(q2_session *session, const char *text, qa_error *error)
{
    q2_server *server = &session->state.server;
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client || !text || strlen(text) >= sizeof(server->userinfo))
        return q2_fail(error, QA_ERROR_FORMAT, "Q2 userinfo exceeds its actual connection extent");
    char normalized[8193];
    memcpy(normalized, text, strlen(text) + 1);
    for (size_t i = 0; i < session->seats; ++i) {
        char selected[8193]; const char *value = text;
        if (session->codec.protocol.kind == QA_NET_Q2KEX_2023) {
            if (!qa_q2_kex_seat_userinfo(text, (unsigned)i, selected, sizeof(selected), error)) return false;
            value = selected;
        }
        qa_buffer returned = {0};
        bool ok = server->hooks.userinfo(server->hooks.context, session->id, client->seats[i].seat, value, &returned, error);
        if (ok && (!returned.data || !returned.size || returned.size > sizeof(normalized) ||
            returned.data[returned.size - 1] || memchr(returned.data, 0, returned.size - 1)))
            ok = q2_fail(error, QA_ERROR_ARGUMENT, "Q2 userinfo callback lost its actual returned Source dictionary");
        if (ok && session->codec.protocol.kind != QA_NET_Q2KEX_2023) memcpy(normalized, returned.data, returned.size);
        qa_buffer_free(&returned); if (!ok) return false;
    }
    memcpy(server->userinfo, normalized, strlen(normalized) + 1); return true;
}
bool qa_network_q2_server_userinfo(qa_network_runtime *runtime, qa_net_client_id id,
    const char *text, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, true, error);
    return session && q2_server_userinfo(session, text, error);
}
/* q2repro Info_SetValueForKey filters high bits after validating the raw
 * lengths, removes every matching key, and keeps the native 512-byte extent. */
static bool info_part(const char *text, size_t *length)
{
    *length = 0;
    while (*text) {
        unsigned c = (unsigned char)*text++ & 127u;
        if (c == '\\' || c == '"' || c == ';' || ++*length >= 64) return false;
    }
    return true;
}
static void info_remove(char *text, const char *key, size_t length)
{
    char *cursor = text;
    while (*cursor) {
        char *start = cursor;
        if (*cursor == '\\') ++cursor;
        char *name = cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        if (!*cursor) return;
        size_t count = (size_t)(cursor - name); ++cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        if (count == length && !memcmp(name, key, length)) {
            memmove(start, cursor, strlen(cursor) + 1); cursor = start;
        }
    }
}
bool q2_server_userinfo_delta(q2_session *session, const char *key, const char *value, qa_error *error)
{
    size_t kl, vl;
    if (!key || !value || !info_part(key, &kl) || !info_part(value, &vl) ||
        strlen(session->state.server.userinfo) >= 512)
        return q2_fail(error, QA_ERROR_FORMAT, "Malformed Q2 userinfo delta");
    char result[512]; memcpy(result, session->state.server.userinfo, strlen(session->state.server.userinfo) + 1);
    info_remove(result, key, kl);
    size_t used = strlen(result);
    if (vl) {
        if (used + kl + vl + 2 >= sizeof(result)) return q2_fail(error, QA_ERROR_FORMAT, "Q2 userinfo delta exceeds native capacity");
        result[used++] = '\\';
        for (size_t i = 0; i < kl; ++i) { unsigned c = (unsigned char)key[i] & 127u; if (c >= 32 && c <= 126) result[used++] = (char)c; }
        result[used++] = '\\';
        for (size_t i = 0; i < vl; ++i) { unsigned c = (unsigned char)value[i] & 127u; if (c >= 32 && c <= 126) result[used++] = (char)c; }
        result[used] = 0;
    }
    return q2_server_userinfo(session, result, error);
}
static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b) { uint32_t next = a % b; a = b; b = next; } return a;
}
bool q2_server_align(q2_session *session, uint64_t source_frame, qa_error *error)
{
    q2_server *server = &session->state.server;
    uint32_t source_divisor = (uint32_t)(UINT64_C(1000000000) / server->policy.source_interval_ns) / 10;
    if (!source_divisor) source_divisor = 1;
    uint32_t divisor = server->settings.frame_divisor, key_divisor = source_divisor / divisor;
    uint64_t number = source_frame / divisor + (source_frame % divisor != 0);
    uint64_t offset = number % key_divisor;
    uint64_t aligned = offset + ((uint64_t)server->wire_frame + key_divisor - 1) / key_divisor * key_divisor;
    if (aligned > INT32_MAX) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 FPS keyframe alignment exceeds its wire counter");
    server->wire_frame = (int32_t)aligned; return true;
}
bool q2_server_setting(q2_session *session, int16_t index, int16_t value, qa_error *error)
{
    if (index < 0 || index >= 14) return true;
    q2_server *server = &session->state.server;
    if (index != Q2_FPS || session->codec.protocol.kind != QA_NET_Q2PRO_36) {
        server->settings.values[index] = value; return true;
    }
    uint32_t source_fps = (uint32_t)(UINT64_C(1000000000) / server->policy.source_interval_ns);
    int32_t requested = value ? value : (int32_t)source_fps;
    int32_t requested_divisor = requested / 10;
    if (requested_divisor < 1) requested_divisor = 1;
    if (requested_divisor > 6) requested_divisor = 6;
    uint32_t source_divisor = source_fps / 10;
    if (!source_divisor) source_divisor = 1;
    uint32_t divisor = source_divisor / gcd(source_divisor, (uint32_t)requested_divisor);
    uint32_t fps = source_fps / divisor;
    qa_q2_server_settings previous = server->settings; int32_t previous_frame = server->wire_frame;
    server->settings.frame_divisor = divisor;
    if (!q2_server_align(session, server->has_source_frame ? server->last_source_frame : 0, error)) {
        server->settings = previous; return false;
    }
    qa_q2_server_event response = {.kind = QA_Q2_SVC_SETTING, .data.setting = {.index = 1, .value = (int32_t)fps}};
    if (!q2_queue_event(session, &response, 0, true, error)) {
        server->settings = previous; server->wire_frame = previous_frame; return false;
    }
    server->settings.values[index] = (int32_t)fps; server->settings.fps = fps;
    return true;
}
bool qa_network_q2_server_settings(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_server_settings **out, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, true, error);
    if (!session || !out) return q2_fail(error, QA_ERROR_ARGUMENT, "Missing actual Q2 connection settings receipt");
    *out = &session->state.server.settings; return true;
}
static void player_projection(const qa_q2_server_settings *settings, const qa_q2_player *old,
    qa_q2_player *player, bool enhanced, bool rerelease)
{
    if (!settings->values[Q2_RECORDING]) {
        if (settings->values[Q2_NOGUN]) {
            player->gunframe = old->gunframe;
            memcpy(player->gunangles, old->gunangles, sizeof(player->gunangles));
            memcpy(player->gunoffset, old->gunoffset, sizeof(player->gunoffset));
            if (settings->values[Q2_NOGUN] != 2) { player->gunindex = old->gunindex; player->gunskin = old->gunskin; }
        }
        if (settings->values[Q2_NOBLEND]) {
            memcpy(player->blend, old->blend, sizeof(player->blend));
            memcpy(player->damage_blend, old->damage_blend, sizeof(player->damage_blend));
        }
        if (enhanced) {
            int32_t dead = rerelease ? 4 : 2;
            if (player->pmove.type < dead) {
                if (!(player->pmove.flags & 64)) memcpy(player->viewangles, old->viewangles, sizeof(player->viewangles));
            } else {
                memcpy(player->pmove.delta_angles, old->pmove.delta_angles, sizeof(player->pmove.delta_angles));
                memcpy(player->pmove.delta_angles_f, old->pmove.delta_angles_f, sizeof(player->pmove.delta_angles_f));
                player->pmove.float_delta_angles = old->pmove.float_delta_angles;
            }
        }
    }
    if (settings->values[Q2_NOPREDICT]) {
        memcpy(player->pmove.velocity, old->pmove.velocity, sizeof(player->pmove.velocity));
        memcpy(player->pmove.velocity_f, old->pmove.velocity_f, sizeof(player->pmove.velocity_f));
        player->pmove.time = old->pmove.time; player->pmove.flags = old->pmove.flags; player->pmove.gravity = old->pmove.gravity;
    }
}
static bool motion_valid(const qa_q2_source_motion *motion, int32_t frame, qa_error *error)
{
    if (!motion || !motion->source_owner || (uint32_t)motion->source_frame != (uint32_t)frame ||
        (motion->count && !motion->rows))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 slower client frame lacks its actual full Source motion receipt");
    for (size_t i = 0; i < motion->count; ++i) {
        const qa_q2_source_entity_motion *row = motion->rows + i;
        if (!row->actor.registry || row->source_frame != motion->source_frame ||
            (i && motion->rows[i - 1].source_slot >= row->source_slot) ||
            !qa_vec_finite(row->origin) || (row->creation_present &&
                (row->creation_frame > motion->source_frame || !qa_vec_finite(row->creation_origin))))
            return q2_fail(error, QA_ERROR_FORMAT, "Q2 Source motion row lost its physical incarnation or frame");
        for (size_t j = 0; j < 8; ++j)
            if (row->origins[j].present && (row->origins[j].source_frame > motion->source_frame ||
                !qa_vec_finite(row->origins[j].origin)))
                return q2_fail(error, QA_ERROR_FORMAT, "Q2 Source motion contains an invalid link-time origin");
    }
    return true;
}
static bool old_origin(const qa_q2_server_settings *settings, const qa_q2_source_motion *motion,
    qa_q2_entity *entity, qa_error *error)
{
    if (entity->renderfx & 128u) return true; /* RF_BEAM owns its endpoints. */
    size_t low = 0, high = motion->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (motion->rows[mid].source_slot < entity->number) low = mid + 1;
        else high = mid;
    }
    if (low == motion->count || motion->rows[low].source_slot != entity->number)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 visible entity has no genuine Source motion row");
    const qa_q2_source_entity_motion *row = motion->rows + low;
    if (!row->link_count) return true;
    if (!row->creation_present)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 linked Source entity lost its actual creation receipt");
    if (row->creation_frame >= motion->source_frame) return true;
    qa_vec3 value;
    bool found = false;
    if (entity->event == 6 && settings->values[Q2_RECORDING]) {
        value = qa_v3(entity->origin[0], entity->origin[1], entity->origin[2]); found = true;
    } else if (motion->source_frame < settings->frame_divisor ||
        row->creation_frame > motion->source_frame - settings->frame_divisor) {
        value = row->creation_origin; found = true;
    } else {
        for (uint32_t i = 0; i + 1 < settings->frame_divisor; ++i) {
            uint64_t frame = motion->source_frame - (settings->frame_divisor - i);
            const qa_q2_source_origin *origin = &row->origins[frame & 7u];
            if (origin->present && origin->source_frame == frame) {
                value = origin->origin; found = true; break;
            }
        }
    }
    if (found) {
        entity->old_origin[0] = value.x; entity->old_origin[1] = value.y; entity->old_origin[2] = value.z;
    }
    return true;
}
bool q2_server_projection(q2_session *session, const qa_q2_wire_frame *source,
    const qa_q2_wire_frame *old, const qa_q2_source_motion *motion, qa_q2_wire_frame *out, qa_error *error)
{
    const qa_q2_server_settings *settings = &session->state.server.settings;
    bool slower = settings->frame_divisor > 1;
    if (slower && settings->frame_divisor > 6)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source frame divisor exceeds its native link-history profile");
    if (slower && !motion_valid(motion, source->server_frame, error)) return false;
    if (!qa_q2_frame_clone(source, out, error)) return false;
    bool rerelease = session->codec.protocol.kind == QA_NET_Q2KEX_2023 ||
        session->codec.protocol.kind == QA_NET_Q2REPRO_1038 || session->codec.protocol.kind == QA_NET_Q2PRIVATE_4038;
    bool extended = rerelease ||
        (session->codec.protocol.kind == QA_NET_Q2PRO_36 && qa_q2pro_extensions(&session->codec));
    bool enhanced = session->codec.protocol.kind == QA_NET_R1Q2_35 || session->codec.protocol.kind == QA_NET_Q2PRO_36 ||
        session->codec.protocol.kind == QA_NET_Q2REPRO_1038 || session->codec.protocol.kind == QA_NET_Q2PRIVATE_4038;
    static const qa_q2_player zero;
    for (size_t i = 0; i < out->player_count; ++i)
        player_projection(settings, old && i < old->player_count ? &old->players[i].player : &zero,
            &out->players[i].player, enhanced, rerelease);
    size_t count = 0;
    for (size_t i = 0; i < out->entity_count; ++i) {
        qa_q2_entity entity = out->entities[i];
        if (settings->values[Q2_NOGIBS] && ((entity.effects & 2u && !(extended && entity.effects & 16u)) ||
            entity.effects & UINT64_C(2097152))) continue;
        if (settings->values[Q2_NOFLARES] && extended && entity.renderfx & UINT32_C(2097152)) continue;
        if (slower && !old_origin(settings, motion, &entity, error)) { qa_q2_frame_free(out); return false; }
        if (settings->values[Q2_NOFOOTSTEPS] && (entity.event == 2 || entity.event == 8 || entity.event == 9)) entity.event = 0;
        out->entities[count++] = entity;
    }
    out->entity_count = count; return true;
}

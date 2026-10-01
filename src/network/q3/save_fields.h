#ifndef QA_Q3_SAVE_FIELDS_H
#define QA_Q3_SAVE_FIELDS_H
#include "peer_internal.h"
#include <math.h>

#define Q3_SAVE_ENTITY_I32(X) \
    X(number) X(eType) X(eFlags) X(time) X(time2) X(otherEntityNum) X(otherEntityNum2) \
    X(groundEntityNum) X(constantLight) X(loopSound) X(modelindex) X(modelindex2) \
    X(clientNum) X(frame) X(solid) X(event) X(eventParm) X(powerups) X(weapon) \
    X(legsAnim) X(torsoAnim) X(generic1)
#define Q3_SAVE_PLAYER_I32(X) \
    X(commandTime) X(pmType) X(bobCycle) X(pmFlags) X(pmTime) X(weaponTime) X(gravity) X(speed) \
    X(groundEntityNum) X(legsTimer) X(legsAnim) X(torsoTimer) X(torsoAnim) X(movementDir) \
    X(eFlags) X(eventSequence) X(externalEvent) X(externalEventParm) X(externalEventTime) \
    X(clientNum) X(weapon) X(weaponState) X(viewheight) X(damageEvent) X(damageYaw) X(damagePitch) \
    X(damageCount) X(generic1) X(loopSound) X(jumppadEnt) X(ping) X(pmoveFramecount) X(jumppadFrame) X(entityEventSequence)

#define Q3_ENTITY_FIELD_BYTES(field) + 4U
enum { Q3_SAVE_ENTITY_BYTES = 0 Q3_SAVE_ENTITY_I32(Q3_ENTITY_FIELD_BYTES) + 2U * 9U * 4U + 12U * 4U };
#undef Q3_ENTITY_FIELD_BYTES

static inline bool q3_save_bool(qa_net_reader *r)
{
    uint8_t value = qa_net_read_u8(r);
    if (value > 1) qa_net_reader_fail(r, "Invalid Q3 checkpoint boolean");
    return value != 0;
}
static inline bool q3_save_floats(qa_net_writer *w, const float *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (!isfinite(values[i]) || !qa_net_write_f32(w, values[i]))
            return qa_net_writer_fail(w, "Invalid Q3 checkpoint floating field");
    return true;
}
static inline bool q3_restore_floats(qa_net_reader *r, float *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        values[i] = qa_net_read_f32(r);
        if (!isfinite(values[i])) return qa_net_reader_fail(r, "Invalid Q3 checkpoint floating field");
    }
    return !r->failed;
}
static inline bool q3_save_ints(qa_net_writer *w, const int32_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (!qa_net_write_i32(w, values[i])) return false;
    return true;
}
static inline bool q3_restore_ints(qa_net_reader *r, int32_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) values[i] = qa_net_read_i32(r);
    return !r->failed;
}
static inline bool q3_save_trajectory(qa_net_writer *w, const qa_q3_trajectory *v)
{
    return qa_net_write_i32(w, v->type) && qa_net_write_i32(w, v->time) && qa_net_write_i32(w, v->duration) &&
        q3_save_floats(w, v->base, 3) && q3_save_floats(w, v->delta, 3);
}
static inline bool q3_restore_trajectory(qa_net_reader *r, qa_q3_trajectory *v)
{
    v->type = qa_net_read_i32(r); v->time = qa_net_read_i32(r); v->duration = qa_net_read_i32(r);
    return q3_restore_floats(r, v->base, 3) && q3_restore_floats(r, v->delta, 3);
}
static inline bool q3_save_entity(qa_net_writer *w, const qa_q3_entity *v)
{
#define PUT(field) if (!qa_net_write_i32(w, v->field)) return false;
    Q3_SAVE_ENTITY_I32(PUT)
#undef PUT
    return q3_save_trajectory(w, &v->pos) && q3_save_trajectory(w, &v->apos) &&
        q3_save_floats(w, v->origin, 3) && q3_save_floats(w, v->origin2, 3) &&
        q3_save_floats(w, v->angles, 3) && q3_save_floats(w, v->angles2, 3);
}
static inline bool q3_restore_entity(qa_net_reader *r, qa_q3_entity *v)
{
#define GET(field) v->field = qa_net_read_i32(r);
    Q3_SAVE_ENTITY_I32(GET)
#undef GET
    return q3_restore_trajectory(r, &v->pos) && q3_restore_trajectory(r, &v->apos) &&
        q3_restore_floats(r, v->origin, 3) && q3_restore_floats(r, v->origin2, 3) &&
        q3_restore_floats(r, v->angles, 3) && q3_restore_floats(r, v->angles2, 3);
}
static inline bool q3_save_player(qa_net_writer *w, const qa_q3_player *v)
{
    if (!qa_net_write_u32(w, v->product)) return false;
#define PUT(field) if (!qa_net_write_i32(w, v->field)) return false;
    Q3_SAVE_PLAYER_I32(PUT)
#undef PUT
    return q3_save_floats(w, v->origin, 3) && q3_save_floats(w, v->velocity, 3) &&
        q3_save_ints(w, v->deltaAngles, 3) && q3_save_floats(w, v->grapplePoint, 3) &&
        q3_save_ints(w, v->events, 2) && q3_save_ints(w, v->eventParms, 2) &&
        q3_save_floats(w, v->viewangles, 3) && q3_save_ints(w, v->stats, 16) &&
        q3_save_ints(w, v->persistant, 16) && q3_save_ints(w, v->powerups, 16) && q3_save_ints(w, v->ammo, 16);
}
static inline bool q3_restore_player(qa_net_reader *r, qa_q3_player *v, qa_q3_product product)
{
    v->product = (qa_q3_product)qa_net_read_u32(r);
    if (v->product != product) return qa_net_reader_fail(r, "Q3 checkpoint player product differs");
#define GET(field) v->field = qa_net_read_i32(r);
    Q3_SAVE_PLAYER_I32(GET)
#undef GET
    return q3_restore_floats(r, v->origin, 3) && q3_restore_floats(r, v->velocity, 3) &&
        q3_restore_ints(r, v->deltaAngles, 3) && q3_restore_floats(r, v->grapplePoint, 3) &&
        q3_restore_ints(r, v->events, 2) && q3_restore_ints(r, v->eventParms, 2) &&
        q3_restore_floats(r, v->viewangles, 3) && q3_restore_ints(r, v->stats, 16) &&
        q3_restore_ints(r, v->persistant, 16) && q3_restore_ints(r, v->powerups, 16) && q3_restore_ints(r, v->ammo, 16);
}
static inline bool q3_save_usercmd(qa_net_writer *w, const qa_q3_usercmd *v)
{
    return qa_net_write_i32(w, v->serverTime) && q3_save_ints(w, v->angles, 3) &&
        qa_net_write_i8(w, v->forwardmove) && qa_net_write_i8(w, v->rightmove) && qa_net_write_i8(w, v->upmove) &&
        qa_net_write_i32(w, v->buttons) && qa_net_write_u8(w, v->weapon);
}
static inline bool q3_restore_usercmd(qa_net_reader *r, qa_q3_usercmd *v)
{
    v->serverTime = qa_net_read_i32(r); if (!q3_restore_ints(r, v->angles, 3)) return false;
    v->forwardmove = qa_net_read_i8(r); v->rightmove = qa_net_read_i8(r); v->upmove = qa_net_read_i8(r);
    v->buttons = qa_net_read_i32(r); v->weapon = qa_net_read_u8(r); return !r->failed;
}
static inline bool q3_save_text(qa_net_writer *w, const char *text, size_t capacity)
{
    const char *end = memchr(text, 0, capacity);
    return end ? qa_net_write_string(w, text) : qa_net_writer_fail(w, "Unterminated Q3 checkpoint text");
}
static inline bool q3_save_address(qa_net_writer *w, const qa_net_address *v)
{
    if (!qa_net_write_u32(w, v->kind) || !qa_net_write_u16(w, v->port)) return false;
    switch (v->kind) {
    case QA_NET_IPV4: return qa_net_write_data(w, v->host.ipv4, 4);
    case QA_NET_IPV6: return qa_net_write_data(w, v->host.ipv6.bytes, 16) && qa_net_write_u32(w, v->host.ipv6.scope);
    case QA_NET_LOOPBACK: return q3_save_text(w, v->host.loopback, sizeof(v->host.loopback));
    case QA_NET_IPX: return qa_net_write_u32(w, v->host.ipx.network) && qa_net_write_data(w, v->host.ipx.node, 6);
    }
    return qa_net_writer_fail(w, "Invalid Q3 checkpoint address family");
}
static inline bool q3_restore_address(qa_net_reader *r, qa_net_address *v)
{
    v->kind = (qa_net_address_kind)qa_net_read_u32(r); v->port = qa_net_read_u16(r);
    switch (v->kind) {
    case QA_NET_IPV4: return qa_net_read_data(r, v->host.ipv4, 4);
    case QA_NET_IPV6:
        if (!qa_net_read_data(r, v->host.ipv6.bytes, 16)) return false;
        v->host.ipv6.scope = qa_net_read_u32(r); return !r->failed;
    case QA_NET_LOOPBACK: return qa_net_read_string(r, v->host.loopback, sizeof(v->host.loopback));
    case QA_NET_IPX:
        v->host.ipx.network = qa_net_read_u32(r); return qa_net_read_data(r, v->host.ipx.node, 6);
    }
    return qa_net_reader_fail(r, "Invalid Q3 checkpoint address family");
}
static inline bool q3_save_gamestate(qa_net_writer *w, const qa_q3_gamestate *v)
{
    if (!v->string_bytes || v->string_bytes > QA_Q3_GAMESTATE_CHARS || v->strings[0])
        return qa_net_writer_fail(w, "Invalid Q3 checkpoint gamestate string storage");
    if (!qa_net_write_i32(w, v->command_sequence) || !qa_net_write_i32(w, v->client_number) ||
        !qa_net_write_i32(w, v->checksum_feed) || !qa_net_write_u32(w, (uint32_t)v->string_bytes) ||
        !qa_net_write_data(w, v->strings, v->string_bytes)) return false;
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        if (v->config_offsets[i] >= v->string_bytes ||
            !memchr(v->strings + v->config_offsets[i], 0, v->string_bytes - v->config_offsets[i]))
            return qa_net_writer_fail(w, "Invalid Q3 checkpoint configstring offset");
        if (!qa_net_write_u16(w, v->config_offsets[i])) return false;
    }
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i)
        if (!qa_net_write_u8(w, v->baseline_present[i]) || !q3_save_entity(w, &v->baselines[i])) return false;
    return true;
}
static inline bool q3_restore_gamestate(qa_net_reader *r, qa_q3_gamestate *v)
{
    v->command_sequence = qa_net_read_i32(r); v->client_number = qa_net_read_i32(r);
    v->checksum_feed = qa_net_read_i32(r); v->string_bytes = qa_net_read_u32(r);
    if (!v->string_bytes || v->string_bytes > QA_Q3_GAMESTATE_CHARS)
        return qa_net_reader_fail(r, "Invalid Q3 checkpoint gamestate string extent");
    if (!qa_net_read_data(r, v->strings, v->string_bytes)) return false;
    if (v->strings[0]) return qa_net_reader_fail(r, "Q3 checkpoint empty configstring is missing");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        v->config_offsets[i] = qa_net_read_u16(r);
        if (v->config_offsets[i] >= v->string_bytes ||
            !memchr(v->strings + v->config_offsets[i], 0, v->string_bytes - v->config_offsets[i]))
            return qa_net_reader_fail(r, "Invalid Q3 checkpoint configstring offset");
    }
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i) {
        v->baseline_present[i] = q3_save_bool(r);
        if (!q3_restore_entity(r, &v->baselines[i])) return false;
        if (v->baseline_present[i] && v->baselines[i].number != (int32_t)i &&
            v->baselines[i].number != QA_Q3_ENTITY_NONE)
            return qa_net_reader_fail(r, "Invalid Q3 checkpoint baseline identity");
    }
    return !r->failed;
}
static inline bool q3_save_snapshot(qa_net_writer *w, const qa_q3_snapshot_slot *slot)
{
    const qa_q3_snapshot *v = &slot->value;
    if (v->entity_count > UINT32_MAX || (v->entity_count && !v->entities) || v->area_bytes > 32)
        return qa_net_writer_fail(w, "Invalid Q3 checkpoint snapshot extent");
    if (!qa_net_write_u8(w, v->valid) || !qa_net_write_i32(w, v->message_number) ||
        !qa_net_write_i32(w, v->server_time) || !qa_net_write_i32(w, v->delta_number) ||
        !qa_net_write_i32(w, v->server_command_number) || !qa_net_write_u64(w, v->parse_entities_number) ||
        !qa_net_write_u8(w, v->flags) || !qa_net_write_u8(w, v->area_bytes) ||
        !qa_net_write_data(w, v->area_mask, 32) || !q3_save_player(w, &v->player) ||
        !qa_net_write_u32(w, (uint32_t)v->entity_count)) return false;
    for (size_t i = 0; i < v->entity_count; ++i) if (!q3_save_entity(w, &v->entities[i])) return false;
    return qa_net_write_i32(w, slot->sent_time) && qa_net_write_i32(w, slot->ack_time) &&
        qa_net_write_u64(w, slot->message_size);
}
static inline bool q3_restore_snapshot(qa_net_reader *r, qa_q3_snapshot_slot *slot, qa_q3_product product)
{
    qa_q3_snapshot *v = &slot->value;
    v->valid = q3_save_bool(r); v->message_number = qa_net_read_i32(r); v->server_time = qa_net_read_i32(r);
    v->delta_number = qa_net_read_i32(r); v->server_command_number = qa_net_read_i32(r);
    v->parse_entities_number = qa_net_read_u64(r); v->flags = qa_net_read_u8(r); v->area_bytes = qa_net_read_u8(r);
    if (v->area_bytes > 32) return qa_net_reader_fail(r, "Invalid Q3 checkpoint area mask extent");
    if (!qa_net_read_data(r, v->area_mask, 32) || !q3_restore_player(r, &v->player, product)) return false;
    v->entity_count = qa_net_read_u32(r);
    if (v->entity_count > SIZE_MAX / sizeof(*slot->entities) ||
        r->bytes.size > SIZE_MAX / 8 || r->bit > r->bytes.size * 8 ||
        v->entity_count > (r->bytes.size - r->bit / 8) / Q3_SAVE_ENTITY_BYTES)
        return qa_net_reader_fail(r, "Invalid Q3 checkpoint entity count");
    if (v->entity_count) {
        slot->entities = calloc(v->entity_count, sizeof(*slot->entities));
        if (!slot->entities) {
            qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Restoring Q3 snapshot entity history"); r->failed = true; return false;
        }
        slot->capacity = v->entity_count;
    }
    v->entities = slot->entities;
    for (size_t i = 0; i < v->entity_count; ++i) {
        if (!q3_restore_entity(r, &slot->entities[i])) return false;
        int32_t number = slot->entities[i].number;
        if (number < 0 || number >= QA_Q3_ENTITY_NONE)
            return qa_net_reader_fail(r, "Invalid Q3 checkpoint snapshot entity number");
    }
    slot->sent_time = qa_net_read_i32(r); slot->ack_time = qa_net_read_i32(r);
    uint64_t size = qa_net_read_u64(r);
    if (size > QA_Q3_MESSAGE_BYTES) return qa_net_reader_fail(r, "Invalid Q3 checkpoint retained message size");
    slot->message_size = (size_t)size; return !r->failed;
}
#endif

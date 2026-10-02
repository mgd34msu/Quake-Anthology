#include "qa/network_q2_wire_save.h"
#include <string.h>
#include <stdlib.h>

#define U8(f) if (!qa_source_save_u8(io, &(f))) return false
#define U16(f) if (!qa_source_save_u16(io, &(f))) return false
#define U32(f) if (!qa_source_save_u32(io, &(f))) return false
#define I32(f) if (!qa_source_save_i32(io, &(f))) return false
#define U64(f) if (!qa_source_save_u64(io, &(f))) return false
#define F32(f) if (!qa_source_save_f32(io, &(f))) return false
#define BOOL(f) if (!qa_source_save_bool(io, &(f))) return false
#define RAW(f) if (!qa_source_save_bytes(io, (f), sizeof(f))) return false
#define COUNT(f,n) if (!qa_source_save_count(io, &(f), (n))) return false
static bool invalid(qa_source_save_io *io, const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message); io->failed = true; return false;
}
static bool i16(qa_source_save_io *io, int16_t *value)
{
    uint16_t bits; memcpy(&bits, value, sizeof(bits));
    if (!qa_source_save_u16(io, &bits)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) memcpy(value, &bits, sizeof(bits));
    return true;
}
bool qa_q2_save_protocol(qa_source_save_io *io, qa_net_protocol_id *value)
{
    uint32_t kind = (uint32_t)value->kind; U32(kind); U32(value->revision); U32(value->flags);
    if (io->direction == QA_SOURCE_SAVE_READ) value->kind = (qa_net_protocol)kind;
    return (qa_q2_protocol_version(*value) && qa_net_protocol_valid(*value, io->error) &&
        !(value->kind == QA_NET_Q2PRO_36 && value->revision == 1016)) || invalid(io, "Invalid saved Q2 wire identity");
}
bool qa_q2_save_channel_options(qa_source_save_io *io, qa_q2_channel_options *value)
{
    if (!qa_q2_save_protocol(io, &value->protocol)) return false;
    BOOL(value->server); BOOL(value->new_channel); BOOL(value->compress); U16(value->qport);
    COUNT(value->payload_bytes, SIZE_MAX); COUNT(value->message_bytes, SIZE_MAX); COUNT(value->datagram_bytes, SIZE_MAX);
    uint32_t recording = (uint32_t)value->sequence_recording; U32(recording);
    if (recording > QA_Q2_SEQUENCE_Q2PRO) return invalid(io, "Invalid saved Q2 sequence recording");
    if (io->direction == QA_SOURCE_SAVE_READ) value->sequence_recording = (qa_q2_sequence_recording)recording;
    return true;
}
bool qa_q2_save_codec(qa_source_save_io *io, qa_q2_codec *value)
{
    uint32_t kind = (uint32_t)value->protocol.kind;
    U32(kind); U32(value->protocol.revision); U32(value->protocol.flags);
    if (io->direction == QA_SOURCE_SAVE_READ) value->protocol.kind = (qa_net_protocol)kind;
    qa_net_protocol_id admitted = value->protocol;
    if (admitted.kind == QA_NET_Q2PRO_36) admitted.flags = 0;
    qa_q2_codec probe;
    if (!qa_q2_codec_init(&probe, admitted, io->error) ||
        (value->protocol.kind == QA_NET_Q2PRO_36 && value->protocol.flags >
            (value->protocol.revision >= 1024 ? UINT16_MAX : 7u)))
        return invalid(io, "Invalid saved Q2 mutable codec identity");
    U32(value->wire_flags); U32(value->frame_extra); BOOL(value->demo26); BOOL(value->frame_player_pending);
    COUNT(value->split_players, QA_Q2_MAX_SEATS); RAW(value->kex_nonzero_solid);
    return (value->split_players && (value->protocol.kind != QA_NET_Q2PRO_36 ||
        value->protocol.flags == value->wire_flags)) || invalid(io, "Saved Q2 codec state differs from its negotiated flags");
}
bool qa_q2_save_usercmd(qa_source_save_io *io, qa_q2_usercmd *value)
{
    I32(value->server_frame); U8(value->msec); U8(value->buttons); U8(value->impulse); U8(value->lightlevel);
    for (size_t i = 0; i < 3; ++i) if (!i16(io, &value->angles[i])) return false;
    F32(value->forwardmove); F32(value->sidemove); F32(value->upmove); return true;
}
bool qa_q2_save_entity(qa_source_save_io *io, qa_q2_entity *value)
{
    U32(value->number);
    for (size_t i = 0; i < 3; ++i) { F32(value->origin[i]); F32(value->angles[i]); F32(value->old_origin[i]); }
    U32(value->modelindex); U32(value->modelindex2); U32(value->modelindex3); U32(value->modelindex4);
    U32(value->frame); U32(value->skinnum); U32(value->renderfx); U32(value->solid); U32(value->sound); U32(value->event);
    U64(value->effects); F32(value->alpha); F32(value->scale); F32(value->loop_volume); F32(value->loop_attenuation);
    U32(value->instance_bits); U32(value->owner); U32(value->old_frame); U32(value->morefx); return true;
}
static bool pmove(qa_source_save_io *io, qa_q2_pmove *value)
{
    I32(value->type);
    for (size_t i = 0; i < 3; ++i) { I32(value->origin[i]); I32(value->velocity[i]); }
    I32(value->flags); I32(value->time); I32(value->gravity);
    for (size_t i = 0; i < 3; ++i) {
        if (!i16(io, &value->delta_angles[i])) return false;
        F32(value->origin_f[i]); F32(value->velocity_f[i]); F32(value->delta_angles_f[i]);
    }
    BOOL(value->float_delta_angles); I32(value->viewheight); return true;
}
bool qa_q2_save_player(qa_source_save_io *io, qa_q2_player *value)
{
    I32(value->clientnum); RAW(value->fog.color); RAW(value->fog.height_start_color); RAW(value->fog.height_end_color);
    U16(value->fog.density); U16(value->fog.sky_factor); U16(value->fog.height_density); U16(value->fog.height_falloff);
    I32(value->fog.height_start_distance); I32(value->fog.height_end_distance);
    if (!pmove(io, &value->pmove)) return false;
    for (size_t i = 0; i < 3; ++i) {
        F32(value->viewangles[i]); F32(value->viewoffset[i]); F32(value->kick_angles[i]); F32(value->gunangles[i]); F32(value->gunoffset[i]);
    }
    U32(value->gunindex); U32(value->gunskin); U32(value->gunframe); U32(value->gunrate);
    for (size_t i = 0; i < 4; ++i) { F32(value->blend[i]); F32(value->damage_blend[i]); }
    F32(value->fov); U32(value->rdflags);
    for (size_t i = 0; i < QA_Q2_MAX_STATS; ++i) if (!i16(io, &value->stats[i])) return false;
    U8(value->team_id); return true;
}
bool qa_q2_save_serverdata(qa_source_save_io *io, qa_q2_serverdata *value)
{
    I32(value->servercount); BOOL(value->attractloop); RAW(value->gamedir); RAW(value->levelname);
    I32(value->clientnum); COUNT(value->client_count, QA_Q2_MAX_SEATS);
    for (size_t i = 0; i < QA_Q2_MAX_SEATS; ++i) { I32(value->clientnums[i]); }
    U32(value->server_state); U32(value->server_fps); U32(value->wire_flags); U32(value->protocol_revision);
    BOOL(value->strafejump_hack); BOOL(value->qw_mode); BOOL(value->waterjump_hack);
    return (memchr(value->gamedir, 0, sizeof(value->gamedir)) && memchr(value->levelname, 0, sizeof(value->levelname))) ||
        invalid(io, "Saved Q2 server strings lack termination");
}
bool qa_q2_save_frame(qa_source_save_io *io, qa_q2_wire_frame *value)
{
    BOOL(value->valid); I32(value->server_frame); I32(value->delta_frame); U8(value->suppressed_count);
    COUNT(value->player_count, QA_Q2_MAX_SEATS);
    if (!value->player_count) return invalid(io, "Saved Q2 frame has no actual players");
    for (size_t i = 0; i < value->player_count; ++i) {
        if (!qa_q2_save_player(io, &value->players[i].player)) return false;
        size_t size = value->players[i].area_bits.size; COUNT(size, QA_Q2_MAX_AREABITS);
        if (io->direction == QA_SOURCE_SAVE_READ) {
            uint8_t *data = size ? malloc(size) : NULL;
            if (size && !data) return invalid(io, "Cannot restore Q2 frame area bits");
            value->players[i].area_bits = (qa_bytes){data, size};
        }
        if (size && (!value->players[i].area_bits.data || !qa_source_save_bytes(io, (void *)value->players[i].area_bits.data, size))) return false;
    }
    COUNT(value->entity_count, UINT16_MAX);
    if (io->direction == QA_SOURCE_SAVE_READ && value->entity_count) {
        value->entities = calloc(value->entity_count, sizeof(*value->entities));
        if (!value->entities) return invalid(io, "Cannot restore Q2 frame entities");
    }
    uint32_t previous = 0;
    for (size_t i = 0; i < value->entity_count; ++i) {
        if (!value->entities || !qa_q2_save_entity(io, value->entities + i)) return false;
        if (value->entities[i].number <= previous || value->entities[i].number > UINT16_MAX) return invalid(io, "Saved Q2 frame entity order differs");
        previous = value->entities[i].number;
    }
    return true;
}

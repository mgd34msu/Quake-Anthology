#include "internal.h"
#include "qa/q3_abi.h"
#include "qa/network_q3.h"
#include "qa/collision.h"

#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct record_field { uint16_t source, count; size_t native; } record_field;
#define E(member,source,count) {source,count,offsetof(qa_q3_entity,member)}
static const record_field entity_fields[] = {
    E(number,0,1),E(eType,4,1),E(eFlags,8,1),
    E(pos.type,12,1),E(pos.time,16,1),E(pos.duration,20,1),E(pos.base,24,3),E(pos.delta,36,3),
    E(apos.type,48,1),E(apos.time,52,1),E(apos.duration,56,1),E(apos.base,60,3),E(apos.delta,72,3),
    E(time,84,1),E(time2,88,1),E(origin,92,3),E(origin2,104,3),E(angles,116,3),E(angles2,128,3),
    E(otherEntityNum,140,1),E(otherEntityNum2,144,1),E(groundEntityNum,148,1),E(constantLight,152,1),
    E(loopSound,156,1),E(modelindex,160,1),E(modelindex2,164,1),E(clientNum,168,1),E(frame,172,1),E(solid,176,1),
    E(event,180,1),E(eventParm,184,1),E(powerups,188,1),E(weapon,192,1),E(legsAnim,196,1),E(torsoAnim,200,1),E(generic1,204,1)
};
#undef E
#define P(member,source,count) {source,count,offsetof(qa_q3_player,member)}
static const record_field player_fields[] = {
    P(commandTime,0,1),P(pmType,4,1),P(bobCycle,8,1),P(pmFlags,12,1),P(pmTime,16,1),
    P(origin,20,3),P(velocity,32,3),P(weaponTime,44,1),P(gravity,48,1),P(speed,52,1),P(deltaAngles,56,3),
    P(groundEntityNum,68,1),P(legsTimer,72,1),P(legsAnim,76,1),P(torsoTimer,80,1),P(torsoAnim,84,1),P(movementDir,88,1),
    P(grapplePoint,92,3),P(eFlags,104,1),P(eventSequence,108,1),P(events,112,2),P(eventParms,120,2),
    P(externalEvent,128,1),P(externalEventParm,132,1),P(externalEventTime,136,1),P(clientNum,140,1),
    P(weapon,144,1),P(weaponState,148,1),P(viewangles,152,3),P(viewheight,164,1),P(damageEvent,168,1),
    P(damageYaw,172,1),P(damagePitch,176,1),P(damageCount,180,1),P(stats,184,16),P(persistant,248,16),
    P(powerups,312,16),P(ammo,376,16),P(generic1,440,1),P(loopSound,444,1),P(jumppadEnt,448,1),
    P(ping,452,1),P(pmoveFramecount,456,1),P(jumppadFrame,460,1),P(entityEventSequence,464,1)
};
#undef P
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

size_t qa_qvm_entity_bytes(qa_qvm_abi abi) { return abi == QA_QVM_Q3_MODERN ? 208u : 204u; }
size_t qa_qvm_player_bytes(qa_qvm_abi abi) { return abi == QA_QVM_Q3_MODERN ? 468u : 444u; }
size_t qa_qvm_shared_entity_bytes(qa_qvm_abi abi) { return abi == QA_QVM_Q3_MODERN ? 516u : 504u; }
size_t qa_qvm_snapshot_bytes(qa_qvm_abi abi) { return abi == QA_QVM_Q3_MODERN ? 53772u : 52724u; }

static bool record_live(const qa_q3_abi_record *record, qa_error *error)
{
    return (record && (unsigned)record->abi <= QA_QVM_Q3_116N &&
            (record->bytes.data || !record->bytes.size)) ||
           qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 source record view");
}

static bool offset(qa_q3_abi_record *record, size_t pointer, size_t size,
                    size_t *out, qa_error *error)
{
    if (!record_live(record, error)) return false;
    if (pointer > record->bytes.size || size > record->bytes.size - pointer) {
        qa_qvm_error(error, QA_ERROR_ARGUMENT, pointer,
                     "Q3 source record exceeds admitted bytes");
        return false;
    }
    *out = pointer;
    return true;
}

static bool record_write(qa_q3_abi_record *record, size_t address, qa_bytes bytes,
                          qa_error *error)
{
    size_t checked;
    if (!offset(record, address, bytes.size, &checked, error)) return false;
    if (!record->write)
        return qa_qvm_error(error, QA_ERROR_ARGUMENT, address,
                            "Q3 source record has no writer");
    return record->write(record->context, checked, bytes, error);
}
static bool store(qa_q3_abi_record *record, size_t address, uint32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes,value);
    return record_write(record,address,(qa_bytes){bytes,4},error);
}
static bool store_float(qa_q3_abi_record *record, size_t address, float value, qa_error *error)
{
    uint32_t word; memcpy(&word,&value,4);
    return store(record,address,word,error);
}
static qa_vec3 read_vector(const qa_q3_abi_record *record, size_t address)
{
    return qa_v3(qa_load_f32le(record->bytes.data + address),qa_load_f32le(record->bytes.data + address + 4),qa_load_f32le(record->bytes.data + address + 8));
}
static bool store_vector(qa_q3_abi_record *record, size_t address, qa_vec3 value, qa_error *error)
{
    return store_float(record,address,value.x,error) && store_float(record,address + 4,value.y,error) && store_float(record,address + 8,value.z,error);
}
static void read_fields(const qa_q3_abi_record *record, size_t address, const record_field *fields, size_t count, size_t extent, void *out)
{
    for (size_t n = 0; n < count; ++n) {
        if (fields[n].source >= extent) continue;
        for (size_t i = 0; i < fields[n].count; ++i) {
            uint32_t word = qa_load_u32le(record->bytes.data + address + fields[n].source + i * 4);
            memcpy((uint8_t *)out + fields[n].native + i * 4,&word,4);
        }
    }
}
static bool write_fields(qa_q3_abi_record *record, size_t address, const record_field *fields, size_t count,
                         size_t extent, const void *source, qa_error *error)
{
    for (size_t n = 0; n < count; ++n) {
        if (fields[n].source >= extent) continue;
        for (size_t i = 0; i < fields[n].count; ++i) {
            uint32_t word;
            memcpy(&word,(const uint8_t *)source + fields[n].native + i * 4,4);
            if (!store(record,address + fields[n].source + (uint32_t)i * 4,word,error)) return false;
        }
    }
    return true;
}

bool qa_qvm_event_tag(qa_qvm_abi abi, int32_t value, bool to_source, int32_t *out, qa_error *error)
{
    if (out == NULL || (unsigned)abi > QA_QVM_Q3_116N) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM event mapping");
    if (abi == QA_QVM_Q3_MODERN) { *out = value; return true; }
    static const uint8_t events[65] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
        32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,48,49,50,51,53,54,55,56,57,58,59,60,61,62,63,64,66,68
    };
    uint32_t event = (uint32_t)value & 255u, mapped = UINT32_MAX;
    if (to_source) { for (uint32_t i = 0; i < COUNT(events); ++i) if (events[i] == event) { mapped = i; break; } }
    else if (event < COUNT(events)) mapped = events[event];
    if (mapped == UINT32_MAX) return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,event,"event is not represented by legacy QVM ABI");
    uint32_t bits = mapped | ((uint32_t)value & ~UINT32_C(255)); memcpy(out,&bits,4);
    return true;
}

bool qa_qvm_entity_tag(qa_qvm_abi abi, int32_t value, bool to_source, int32_t *out, qa_error *error)
{
    if (out == NULL || (unsigned)abi > QA_QVM_Q3_116N) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM entity type mapping");
    if (abi == QA_QVM_Q3_MODERN) { *out = value; return true; }
    int32_t first = to_source ? 13 : 12, target = to_source ? 12 : 13;
    if (value >= first) {
        int32_t event;
        if (!qa_qvm_event_tag(abi,value - first,to_source,&event,error)) return false;
        uint32_t bits = (uint32_t)target + (uint32_t)event; memcpy(out,&bits,4);
    } else {
        if (to_source && value == 12) return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,12,"legacy QVM has no team entity type");
        *out = value;
    }
    return true;
}

bool qa_qvm_configstring_tag(qa_qvm_abi abi, int32_t value, int32_t *out, qa_error *error)
{
    if (out == NULL || (unsigned)abi > QA_QVM_Q3_116N) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM configstring mapping");
    if (abi != QA_QVM_Q3_MODERN && value >= 16 && value <= 26)
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,(size_t)(uint32_t)value,"legacy private configstring has no modern mapping");
    *out = abi != QA_QVM_Q3_MODERN && value >= 12 && value <= 15 ? value + 8 : value;
    return true;
}
bool qa_qvm_client_configstring_argument(qa_qvm_abi abi, const char *text,
    int32_t *out, bool *mapped, qa_error *error)
{
    if ((unsigned)abi > QA_QVM_Q3_116N || !text || !out || !mapped)
        return qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "invalid reached configstring argument");
    *mapped = false; *out = 0;
    if (abi == QA_QVM_Q3_MODERN) return true;
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    char *end = NULL;
    double value = *text ? strtod(text, &end) : 0;
    if (!*text) end = (char *)text;
    if (end == text && *text)
        return qa_qvm_error(error, QA_ERROR_FORMAT, 0, "invalid reached configstring command index");
    while (*end == ' ' || (*end >= '\t' && *end <= '\r')) ++end;
    if (*end || !isfinite(value) || trunc(value) != value)
        return qa_qvm_error(error, QA_ERROR_FORMAT, 0, "invalid reached configstring command index");
    if (value >= 20 && value <= 23) { *out = (int32_t)value - 8; *mapped = true; }
    else if (value >= 12 && value <= 26)
        return qa_qvm_error(error, QA_ERROR_UNSUPPORTED, (size_t)value,
            "canonical configstring has no legacy client mapping");
    return true;
}

static bool powerups(qa_qvm_abi abi, const int32_t values[16], qa_error *error)
{
    if (abi != QA_QVM_Q3_MODERN)
        for (size_t i = 9; i < 16; ++i)
            if (values[i] != 0) return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,i,"legacy private powerup has no modern mapping");
    return true;
}
static bool entity_translate(qa_qvm_abi abi, qa_q3_entity *entity, bool to_source, qa_error *error)
{
    if (abi != QA_QVM_Q3_MODERN && ((uint32_t)entity->powerups & ~UINT32_C(0x1ff)) != 0)
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,188,"legacy private powerup has no modern mapping");
    return qa_qvm_entity_tag(abi,entity->eType,to_source,&entity->eType,error)
        && qa_qvm_event_tag(abi,entity->event,to_source,&entity->event,error);
}
static bool read_entity_at(qa_q3_abi_record *record, size_t address, bool source_tags, qa_q3_entity *out, qa_error *error)
{
    qa_q3_entity value = {0};
    read_fields(record,address,entity_fields,COUNT(entity_fields),qa_qvm_entity_bytes(record->abi),&value);
    if (!source_tags && !entity_translate(record->abi,&value,false,error)) return false;
    *out = value;
    return true;
}
static bool write_entity_at(qa_q3_abi_record *record, size_t address, bool source_tags, const qa_q3_entity *source, qa_error *error)
{
    qa_q3_entity value = *source;
    if (record->abi != QA_QVM_Q3_MODERN && value.generic1 != 0)
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,address,"legacy QVM entity has no generic1 field");
    if (!source_tags && !entity_translate(record->abi,&value,true,error)) return false;
    return write_fields(record,address,entity_fields,COUNT(entity_fields),qa_qvm_entity_bytes(record->abi),&value,error);
}
bool qa_q3_abi_read_entity(qa_q3_abi_record *record, size_t pointer, bool source_tags, qa_q3_entity *out, qa_error *error)
{
    size_t address;
    if (out == NULL || !record_live(record,error)) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM entity read");
    return offset(record,pointer,qa_qvm_entity_bytes(record->abi),&address,error) && read_entity_at(record,address,source_tags,out,error);
}
bool qa_q3_abi_write_entity(qa_q3_abi_record *record, size_t pointer, bool source_tags, const qa_q3_entity *source, qa_error *error)
{
    size_t address;
    if (source == NULL || !record_live(record,error)) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM entity write");
    return offset(record,pointer,qa_qvm_entity_bytes(record->abi),&address,error) && write_entity_at(record,address,source_tags,source,error);
}

bool qa_q3_abi_read_player(qa_q3_abi_record *record, size_t pointer, bool source_tags, qa_q3_player *out, qa_error *error)
{
    size_t address;
    if (out == NULL || !record_live(record,error)) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM player read");
    if (!offset(record,pointer,qa_qvm_player_bytes(record->abi),&address,error)) return false;
    qa_q3_player value = {0};
    read_fields(record,address,player_fields,COUNT(player_fields),record->abi == QA_QVM_Q3_MODERN ? 468 : 440,&value);
    if (record->abi != QA_QVM_Q3_MODERN) value.ping = qa_load_i32le(record->bytes.data + address + 440);
    if (!source_tags) {
        if (!powerups(record->abi,value.powerups,error)
            || !qa_qvm_event_tag(record->abi,value.events[0],false,&value.events[0],error)
            || !qa_qvm_event_tag(record->abi,value.events[1],false,&value.events[1],error)
            || !qa_qvm_event_tag(record->abi,value.externalEvent,false,&value.externalEvent,error)) return false;
        if (record->abi != QA_QVM_Q3_MODERN) {
            int32_t source[16]; memcpy(source,value.persistant,sizeof(source)); memset(value.persistant,0,sizeof(value.persistant));
            const size_t same[] = {0,1,2,3,4,8,9,10};
            for (size_t i = 0; i < COUNT(same); ++i) value.persistant[same[i]] = source[same[i]];
            value.persistant[6] = source[7]; value.persistant[13] = source[11];
        }
    }
    *out = value;
    return true;
}
static bool write_player_at(qa_q3_abi_record *record, size_t address, bool source_tags, bool preserve_private, const qa_q3_player *source, qa_error *error)
{
    qa_q3_player value = *source;
    if (!source_tags) {
        if (!powerups(record->abi,value.powerups,error)
            || !qa_qvm_event_tag(record->abi,value.events[0],true,&value.events[0],error)
            || !qa_qvm_event_tag(record->abi,value.events[1],true,&value.events[1],error)
            || !qa_qvm_event_tag(record->abi,value.externalEvent,true,&value.externalEvent,error)) return false;
        if (record->abi != QA_QVM_Q3_MODERN) {
            for (size_t i = 0; i < 16; ++i) value.persistant[i] = preserve_private ? qa_load_i32le(record->bytes.data + address + 248 + i * 4) : 0;
            const size_t same[] = {0,1,2,3,4,8,9,10};
            for (size_t i = 0; i < COUNT(same); ++i) value.persistant[same[i]] = source->persistant[same[i]];
            value.persistant[7] = source->persistant[6]; value.persistant[11] = source->persistant[13];
        }
    }
    if (!write_fields(record,address,player_fields,COUNT(player_fields),record->abi == QA_QVM_Q3_MODERN ? 468 : 440,&value,error)) return false;
    return record->abi == QA_QVM_Q3_MODERN || store(record,address + 440,(uint32_t)value.ping,error);
}
bool qa_q3_abi_write_player(qa_q3_abi_record *record, size_t pointer, bool source_tags, bool preserve_private, const qa_q3_player *source, qa_error *error)
{
    size_t address;
    if (source == NULL || !record_live(record,error)) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM player write");
    return offset(record,pointer,qa_qvm_player_bytes(record->abi),&address,error) && write_player_at(record,address,source_tags,preserve_private,source,error);
}

bool qa_q3_abi_read_usercmd(qa_q3_abi_record *record, size_t pointer, qa_q3_usercmd *out, qa_error *error)
{
    size_t address;
    if (out == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM user command output");
    if (!offset(record,pointer,24,&address,error)) return false;
    bool modern = record->abi == QA_QVM_Q3_MODERN;
    const uint8_t *bytes = record->bytes.data + address;
    qa_q3_usercmd value = {0};
    value.serverTime = qa_load_i32le(bytes);
    for (size_t i = 0; i < 3; ++i) value.angles[i] = qa_load_i32le(bytes + (modern ? 4 : 8) + i * 4);
    value.buttons = modern ? qa_load_i32le(bytes + 16) : (bytes[4] & 31) | ((bytes[4] & 128) != 0 ? 2048 : 0);
    value.weapon = bytes[modern ? 20 : 5];
    memcpy(&value.forwardmove,bytes + (modern ? 21 : 20),1);
    memcpy(&value.rightmove,bytes + (modern ? 22 : 21),1);
    memcpy(&value.upmove,bytes + (modern ? 23 : 22),1);
    *out = value;
    return true;
}
bool qa_q3_abi_write_usercmd(qa_q3_abi_record *record, size_t pointer, bool preserve_private, const qa_q3_usercmd *value, qa_error *error)
{
    size_t address;
    if (value == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM user command");
    if (!offset(record,pointer,24,&address,error)) return false;
    bool modern = record->abi == QA_QVM_Q3_MODERN;
    if (!store(record,address,(uint32_t)value->serverTime,error)) return false;
    if (modern) { if (!store(record,address + 16,(uint32_t)value->buttons,error)) return false; }
    else {
        uint8_t buttons = (uint8_t)((preserve_private ? record->bytes.data[address + 4] & 96 : 0) | (value->buttons & 31) | ((value->buttons & 2048) != 0 ? 128 : 0));
        if (!record_write(record,address + 4,(qa_bytes){&buttons,1},error)) return false;
    }
    for (uint32_t i = 0; i < 3; ++i) if (!store(record,address + (modern ? 4u : 8u) + i * 4,(uint32_t)value->angles[i],error)) return false;
    if (!record_write(record,address + (modern ? 20u : 5u),(qa_bytes){&value->weapon,1},error)) return false;
    size_t movement = address + (modern ? 21u : 20u);
    return record_write(record,movement,(qa_bytes){(const uint8_t *)&value->forwardmove,1},error)
        && record_write(record,movement + 1,(qa_bytes){(const uint8_t *)&value->rightmove,1},error)
        && record_write(record,movement + 2,(qa_bytes){(const uint8_t *)&value->upmove,1},error);
}

bool qa_q3_abi_write_gamestate(qa_q3_abi_record *record, size_t pointer, bool source_tags, const qa_q3_gamestate *state, qa_error *error)
{
    size_t address;
    if (state == NULL || state->string_bytes > QA_Q3_GAMESTATE_CHARS)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM game state");
    if (!offset(record,pointer,20100,&address,error)) return false;
    for (uint32_t i = 0; i < 1024; ++i) {
        int32_t source = (int32_t)i;
        uint32_t value = 0;
        if (!source_tags && record->abi != QA_QVM_Q3_MODERN && i >= 16 && i <= 26) source = -1;
        else if (!source_tags && !qa_qvm_configstring_tag(record->abi,(int32_t)i,&source,error)) return false;
        if (source >= 0) value = state->config_offsets[(uint32_t)source];
        if (!store(record,address + i * 4,value,error)) return false;
    }
    return record_write(record,address + 4096,(qa_bytes){(const uint8_t *)state->strings,16000},error)
        && store(record,address + 20096,(uint32_t)state->string_bytes,error);
}

bool qa_q3_abi_write_snapshot(qa_q3_abi_record *record, size_t pointer, bool source_tags, const qa_q3_snapshot *snapshot, int32_t ping, qa_error *error)
{
    size_t address;
    if (snapshot == NULL || snapshot->entity_count > 256 || (snapshot->entity_count > 0 && snapshot->entities == NULL) || snapshot->area_bytes > 32)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM snapshot");
    if (!record_live(record,error) || !offset(record,pointer,qa_qvm_snapshot_bytes(record->abi),&address,error)) return false;
    uint32_t ps_bytes = (uint32_t)qa_qvm_player_bytes(record->abi), entity_bytes = (uint32_t)qa_qvm_entity_bytes(record->abi);
    if (!store(record,address,snapshot->flags,error) || !store(record,address + 4,(uint32_t)ping,error)
        || !store(record,address + 8,(uint32_t)snapshot->server_time,error)
        || !record_write(record,address + 12,(qa_bytes){snapshot->area_mask,32},error)
        || !write_player_at(record,address + 44,source_tags,false,&snapshot->player,error)
        || !store(record,address + 44 + ps_bytes,(uint32_t)snapshot->entity_count,error)) return false;
    for (size_t i = 0; i < snapshot->entity_count; ++i)
        if (!write_entity_at(record,address + 48 + ps_bytes + (uint32_t)i * entity_bytes,source_tags,&snapshot->entities[i],error)) return false;
    return store(record,address + (uint32_t)qa_qvm_snapshot_bytes(record->abi) - 4,(uint32_t)snapshot->server_command_number,error);
}

bool qa_q3_abi_write_trace(qa_q3_abi_record *record, size_t pointer, const qa_trace_result *trace, int32_t entity_number, qa_error *error)
{
    size_t address;
    if (trace == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM trace");
    if (!offset(record,pointer,56,&address,error)) return false;
    if (!store(record,address,trace->all_solid ? 1u : 0u,error) || !store(record,address + 4,trace->start_solid ? 1u : 0u,error)
        || !store_float(record,address + 8,trace->fraction,error) || !store_vector(record,address + 12,trace->end,error)
        || !store_vector(record,address + 24,trace->plane.normal,error) || !store_float(record,address + 36,trace->plane.distance,error)) return false;
    uint8_t type = (uint8_t)trace->plane.type, zero[2] = {0,0};
    return record_write(record,address + 40,(qa_bytes){&type,1},error)
        && record_write(record,address + 41,(qa_bytes){&trace->plane.signbits,1},error)
        && record_write(record,address + 42,(qa_bytes){zero,2},error)
        && store(record,address + 44,(uint32_t)trace->surface_flags,error)
        && store(record,address + 48,(uint32_t)trace->contents,error)
        && store(record,address + 52,(uint32_t)entity_number,error);
}

static uint32_t shared_offset(const qa_q3_abi_record *record, uint32_t modern)
{
    return record->abi == QA_QVM_Q3_MODERN ? modern : modern < 428 ? modern - 8 : modern - 12;
}
bool qa_q3_abi_read_shared_entity(qa_q3_abi_record *record, size_t pointer, qa_qvm_entity_shared *out, qa_error *error)
{
    size_t address;
    if (out == NULL || !record_live(record,error)) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM shared entity read");
    if (!offset(record,pointer,qa_qvm_shared_entity_bytes(record->abi),&address,error)) return false;
    qa_qvm_entity_shared value = {0};
#define I(at) qa_load_i32le(record->bytes.data + address + shared_offset(record,at))
#define V(at) read_vector(record,address + shared_offset(record,at))
    value.linked = I(416) != 0; value.linkcount = I(420); value.server_flags = I(424);
    value.single_client = record->abi == QA_QVM_Q3_MODERN ? I(428) : 0;
    value.inline_model = I(432) != 0;
    value.local_bounds = (qa_bounds){V(436),V(448)}; value.contents = I(460);
    value.absolute_bounds = (qa_bounds){V(464),V(476)};
    value.origin = V(488); value.angles = V(500); value.owner_number = I(512);
#undef I
#undef V
    *out = value;
    return true;
}
bool qa_q3_abi_write_shared_entity(qa_q3_abi_record *record, size_t pointer, const qa_qvm_entity_shared *source, qa_error *error)
{
    size_t address;
    if (source == NULL || !record_live(record,error)) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM shared entity write");
    if (!offset(record,pointer,qa_qvm_shared_entity_bytes(record->abi),&address,error)) return false;
    if (record->abi != QA_QVM_Q3_MODERN && source->single_client != 0)
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,address,"legacy QVM entity has no singleClient field");
#define I(at,value) store(record,address + shared_offset(record,at),(uint32_t)(value),error)
#define V(at,value) store_vector(record,address + shared_offset(record,at),value,error)
    return I(416,source->linked ? 1 : 0) && I(420,source->linkcount) && I(424,source->server_flags)
        && (record->abi != QA_QVM_Q3_MODERN || I(428,source->single_client)) && I(432,source->inline_model ? 1 : 0)
        && V(436,source->local_bounds.mins) && V(448,source->local_bounds.maxs) && I(460,source->contents)
        && V(464,source->absolute_bounds.mins) && V(476,source->absolute_bounds.maxs)
        && V(488,source->origin) && V(500,source->angles) && I(512,source->owner_number);
#undef I
#undef V
}

static bool qvm_record_write(void *context, size_t address, qa_bytes bytes, qa_error *error)
{
    if (address > UINT32_MAX)
        return qa_qvm_error(error, QA_ERROR_ARGUMENT, address,
                            "QVM source record address exceeds its word width");
    return qa_qvm_write(context, (uint32_t)address, bytes, error);
}

static bool qvm_record(qa_qvm *vm, int32_t pointer, size_t size,
                       qa_q3_abi_record *record, size_t *address, qa_error *error)
{
    qa_bytes admitted;
    if (!qa_qvm_span(vm, pointer, 0, size, &admitted, error)) return false;
    *address = (size_t)(admitted.data - vm->data);
    *record = (qa_q3_abi_record){.abi = vm->options.abi,
                                .bytes = {vm->data, vm->data_size},
                                .context = vm, .write = qvm_record_write};
    return true;
}

bool qa_qvm_read_entity(qa_qvm *vm, int32_t pointer, bool source_tags,
                         qa_q3_entity *out, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_entity_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_read_entity(&record, address, source_tags, out, error);
}

bool qa_qvm_write_entity(qa_qvm *vm, int32_t pointer, bool source_tags,
                          const qa_q3_entity *value, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_entity_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_write_entity(&record, address, source_tags, value, error);
}

bool qa_qvm_read_player(qa_qvm *vm, int32_t pointer, bool source_tags,
                         qa_q3_player *out, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_player_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_read_player(&record, address, source_tags, out, error);
}

bool qa_qvm_write_player(qa_qvm *vm, int32_t pointer, bool source_tags,
                          bool preserve_private, const qa_q3_player *value, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_player_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_write_player(&record, address, source_tags, preserve_private, value, error);
}

bool qa_qvm_read_usercmd(qa_qvm *vm, int32_t pointer, qa_q3_usercmd *out, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, 24, &record, &address, error) &&
           qa_q3_abi_read_usercmd(&record, address, out, error);
}

bool qa_qvm_write_usercmd(qa_qvm *vm, int32_t pointer, bool preserve_private,
                           const qa_q3_usercmd *value, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, 24, &record, &address, error) &&
           qa_q3_abi_write_usercmd(&record, address, preserve_private, value, error);
}

bool qa_qvm_write_gamestate(qa_qvm *vm, int32_t pointer, bool source_tags,
                             const qa_q3_gamestate *value, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, 20100, &record, &address, error) &&
           qa_q3_abi_write_gamestate(&record, address, source_tags, value, error);
}

bool qa_qvm_write_snapshot(qa_qvm *vm, int32_t pointer, bool source_tags,
                            const qa_q3_snapshot *value, int32_t ping, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_snapshot_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_write_snapshot(&record, address, source_tags, value, ping, error);
}

bool qa_qvm_write_trace(qa_qvm *vm, int32_t pointer, const qa_trace_result *value,
                         int32_t entity_number, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, 56, &record, &address, error) &&
           qa_q3_abi_write_trace(&record, address, value, entity_number, error);
}

bool qa_qvm_read_shared_entity(qa_qvm *vm, int32_t pointer,
                                qa_qvm_entity_shared *out, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_shared_entity_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_read_shared_entity(&record, address, out, error);
}

bool qa_qvm_write_shared_entity(qa_qvm *vm, int32_t pointer,
                                 const qa_qvm_entity_shared *value, qa_error *error)
{
    qa_q3_abi_record record; size_t address;
    return qvm_record(vm, pointer, qa_qvm_shared_entity_bytes(qa_qvm_get_abi(vm)),
                       &record, &address, error) &&
           qa_q3_abi_write_shared_entity(&record, address, value, error);
}

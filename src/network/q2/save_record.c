#include "qa/network_q2_wire_save.h"
#include <stdlib.h>
#include <string.h>

#define U8(f) if (!qa_source_save_u8(io, &(f))) return false
#define U16(f) if (!qa_source_save_u16(io, &(f))) return false
#define U32(f) if (!qa_source_save_u32(io, &(f))) return false
#define I32(f) if (!qa_source_save_i32(io, &(f))) return false
#define F32(f) if (!qa_source_save_f32(io, &(f))) return false
#define BOOL(f) if (!qa_source_save_bool(io, &(f))) return false
#define RAW(f) if (!qa_source_save_bytes(io, (f), sizeof(f))) return false
#define COUNT(f,n) if (!qa_source_save_count(io, &(f), (n))) return false
static bool invalid(qa_source_save_io *io, const char *message)
{ qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message); io->failed = true; return false; }
static bool bytes(qa_source_save_io *io, qa_bytes *value)
{
    size_t length = value->size; COUNT(length, SIZE_MAX);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset) return invalid(io, "Saved Q2 record bytes exceed their document");
        uint8_t *data = length ? malloc(length) : NULL;
        if (length && !data) return invalid(io, "Cannot restore Q2 record bytes");
        *value = (qa_bytes){data, length};
    }
    if (length && !value->data) return invalid(io, "Saved Q2 record has absent bytes");
    return qa_source_save_bytes(io, (void *)value->data, length);
}
static bool text(qa_source_save_io *io, const char **value)
{
    if (io->direction != QA_SOURCE_SAVE_READ && !*value) return invalid(io, "Saved Q2 service text is absent");
    size_t length = io->direction == QA_SOURCE_SAVE_READ ? 0 : strlen(*value); COUNT(length, SIZE_MAX - 1);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset) return invalid(io, "Saved Q2 text exceeds its document");
        char *data = malloc(length + 1); if (!data) return invalid(io, "Cannot restore Q2 service text");
        data[length] = 0; *value = data;
    }
    if (!qa_source_save_bytes(io, (void *)*value, length)) return false;
    return !memchr(*value, 0, length) || invalid(io, "Saved Q2 service text contains a terminator");
}
static bool sound(qa_source_save_io *io, qa_q2_kex_sound *value)
{
    U8(value->flags); U16(value->index); F32(value->volume); F32(value->attenuation); F32(value->time_offset);
    U32(value->entity); U8(value->channel); BOOL(value->has_position);
    for (size_t i = 0; i < 3; ++i) { F32(value->position[i]); } return true;
}
static bool temporary(qa_source_save_io *io, qa_q2_temp_entity *value)
{
    U8(value->type); COUNT(value->field_count, 7);
    for (size_t i = 0; i < 7; ++i) {
        qa_q2_temp_field *field = value->fields + i;
        uint32_t kind = (uint32_t)field->kind, name = (uint32_t)field->name; U32(kind); U32(name);
        if (kind > QA_Q2_TEMP_VECTOR || name > QA_Q2_TEMP_OFFSET) return invalid(io, "Saved Q2 temporary field is invalid");
        if (io->direction == QA_SOURCE_SAVE_READ) { field->kind = (qa_q2_temp_field_kind)kind; field->name = (qa_q2_temp_field_name)name; }
        if (kind == QA_Q2_TEMP_VECTOR) for (size_t axis = 0; axis < 3; ++axis) { F32(field->value.vector[axis]); }
        else { I32(field->value.integer); }
    }
    return bytes(io, &value->raw);
}
static bool fog(qa_source_save_io *io, qa_q2_wire_fog *value)
{
    U16(value->bits); F32(value->density); F32(value->height_falloff); F32(value->height_density);
    U8(value->sky_factor); RAW(value->color); RAW(value->height_start_color); RAW(value->height_end_color);
    U16(value->time); I32(value->height_start_distance); I32(value->height_end_distance); return true;
}
static bool event(qa_source_save_io *io, qa_q2_server_event *value)
{
    uint32_t kind = (uint32_t)value->kind; U32(kind);
    if (kind > QA_Q2_SVC_PRIVATE) return invalid(io, "Unknown saved Q2 service kind");
    if (io->direction == QA_SOURCE_SAVE_READ) value->kind = (qa_q2_server_event_kind)kind;
    switch (value->kind) {
    case QA_Q2_SVC_NOP: case QA_Q2_SVC_DISCONNECT: case QA_Q2_SVC_RECONNECT: case QA_Q2_SVC_LEVEL_RESTART: return true;
    case QA_Q2_SVC_SERVERDATA: return qa_q2_save_serverdata(io, &value->data.serverdata);
    case QA_Q2_SVC_PRINT: case QA_Q2_SVC_CENTERPRINT: case QA_Q2_SVC_COMMAND: case QA_Q2_SVC_LAYOUT: case QA_Q2_SVC_ACHIEVEMENT:
        U8(value->data.print.level); return text(io, &value->data.print.text);
    case QA_Q2_SVC_CONFIGSTRING: U16(value->data.config.index); return text(io, &value->data.config.value);
    case QA_Q2_SVC_BASELINE: return qa_q2_save_entity(io, &value->data.baseline);
    case QA_Q2_SVC_FRAME: {
        if (io->direction == QA_SOURCE_SAVE_READ) {
            value->data.frame = calloc(1, sizeof(*value->data.frame));
            if (!value->data.frame) return invalid(io, "Cannot restore a retained Q2 Source frame");
        }
        return value->data.frame && qa_q2_save_frame(io, (qa_q2_wire_frame *)value->data.frame);
    }
    case QA_Q2_SVC_SOUND: return sound(io, &value->data.sound);
    case QA_Q2_SVC_TEMP_ENTITY: return temporary(io, &value->data.temporary);
    case QA_Q2_SVC_MUZZLEFLASH:
        U32(value->data.muzzle.entity); U32(value->data.muzzle.flash); BOOL(value->data.muzzle.monster); BOOL(value->data.muzzle.silenced); return true;
    case QA_Q2_SVC_INVENTORY: {
        COUNT(value->data.inventory.count, 32768);
        if (io->direction == QA_SOURCE_SAVE_READ && value->data.inventory.count) {
            value->data.inventory.counts = calloc(value->data.inventory.count, sizeof(*value->data.inventory.counts));
            if (!value->data.inventory.counts) return invalid(io, "Cannot restore Q2 inventory");
        }
        for (size_t i = 0; i < value->data.inventory.count; ++i) {
            uint16_t bits; memcpy(&bits, value->data.inventory.counts + i, sizeof(bits)); U16(bits);
            if (io->direction == QA_SOURCE_SAVE_READ) memcpy((int16_t *)value->data.inventory.counts + i, &bits, sizeof(bits));
        }
        return true;
    }
    case QA_Q2_SVC_DOWNLOAD:
        U8(value->data.download.percent); BOOL(value->data.download.missing);
        return bytes(io, &value->data.download.bytes);
    case QA_Q2_SVC_SETTING: I32(value->data.setting.index); I32(value->data.setting.value); return true;
    case QA_Q2_SVC_SEAT: U8(value->data.seat); return value->data.seat <= QA_Q2_MAX_SEATS || invalid(io, "Saved Q2 recipient marker is invalid");
    case QA_Q2_SVC_DAMAGE:
        COUNT(value->data.damage.count, 4);
        for (size_t i = 0; i < 4; ++i) {
            qa_q2_kex_damage *damage = value->data.damage.indicators + i;
            U8(damage->damage); BOOL(damage->health); BOOL(damage->armor); BOOL(damage->shield);
            for (size_t axis = 0; axis < 3; ++axis) { F32(damage->direction[axis]); }
        }
        return true;
    case QA_Q2_SVC_LOCALIZED_PRINT: {
        qa_q2_kex_locprint *print = &value->data.localized;
        U8(print->flags); COUNT(print->arg_count, 8); RAW(print->base); RAW(print->args);
        if (!memchr(print->base, 0, sizeof(print->base))) return invalid(io, "Saved Q2 localization base is unterminated");
        for (size_t i = 0; i < print->arg_count; ++i)
            if (!memchr(print->args[i], 0, sizeof(print->args[i]))) return invalid(io, "Saved Q2 localization argument is unterminated");
        return true;
    }
    case QA_Q2_SVC_FOG: return fog(io, &value->data.fog);
    case QA_Q2_SVC_POI: {
        qa_q2_kex_poi *poi = &value->data.poi; U16(poi->key); U16(poi->time); U16(poi->image);
        for (size_t i = 0; i < 3; ++i) { F32(poi->position[i]); }
        U8(poi->color); U8(poi->flags); return true;
    }
    case QA_Q2_SVC_HELP_PATH:
        BOOL(value->data.help_path.start);
        for (size_t i = 0; i < 3; ++i) { F32(value->data.help_path.position[i]); F32(value->data.help_path.direction[i]); }
        return true;
    case QA_Q2_SVC_PRIVATE:
        return text(io, &value->data.private_message.name) && bytes(io, &value->data.private_message.payload);
    }
    return invalid(io, "Unknown saved Q2 service");
}
void qa_q2_saved_record_free(qa_q2_server_record *record)
{
    free((void *)record->raw.data);
    qa_q2_server_event *value = &record->event;
    switch (value->kind) {
    case QA_Q2_SVC_PRINT: case QA_Q2_SVC_CENTERPRINT: case QA_Q2_SVC_COMMAND: case QA_Q2_SVC_LAYOUT: case QA_Q2_SVC_ACHIEVEMENT:
        free((void *)value->data.print.text); break;
    case QA_Q2_SVC_CONFIGSTRING: free((void *)value->data.config.value); break;
    case QA_Q2_SVC_FRAME:
        if (value->data.frame) { qa_q2_frame_free((qa_q2_wire_frame *)value->data.frame); free((void *)value->data.frame); } break;
    case QA_Q2_SVC_INVENTORY: free((void *)value->data.inventory.counts); break;
    case QA_Q2_SVC_DOWNLOAD: free((void *)value->data.download.bytes.data); break;
    case QA_Q2_SVC_TEMP_ENTITY: free((void *)value->data.temporary.raw.data); break;
    case QA_Q2_SVC_PRIVATE: free((void *)value->data.private_message.name); free((void *)value->data.private_message.payload.data); break;
    default: break;
    }
    memset(record, 0, sizeof(*record));
}
bool qa_q2_save_record(qa_source_save_io *io, qa_q2_server_record *record)
{
    U8(record->seat); U8(record->opcode);
    if (record->seat > QA_Q2_MAX_SEATS) return invalid(io, "Saved Q2 record recipient marker is invalid");
    return bytes(io, &record->raw) && event(io, &record->event);
}

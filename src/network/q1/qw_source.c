/* Donor: network/q1/codecs/qw28.ts and bootstrap/network/qw-server.ts. */
#include "qa/network_qw_source.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    U_ANGLE1 = 1, U_ANGLE3 = 2, U_MODEL = 4, U_COLORMAP = 8,
    U_SKIN = 16, U_EFFECTS = 32, U_SOLID = 64,
    U_ORIGIN1 = 512, U_ORIGIN2 = 1024, U_ORIGIN3 = 2048,
    U_ANGLE2 = 4096, U_FRAME = 8192, U_REMOVE = 16384, U_MOREBITS = 32768,
    SOURCE_ENTITY_BYTES = 69
};
static const unsigned angle_bits[3] = {U_ANGLE1, U_ANGLE2, U_ANGLE3};
typedef struct source_baseline {
    bool present;
    qa_qw_source_entity value;
} source_baseline;
typedef struct source_frame {
    bool present;
    qa_qw_source_frame value;
} source_frame;
struct qa_qw_source_history {
    source_baseline baselines[512];
    source_frame frames[QA_QW_UPDATE_BACKUP];
};
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool number_valid(double value)
{ return isfinite(value) && value == trunc(value); }
static bool vector_valid(const float value[3])
{ return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]); }
static bool entity_valid(const qa_qw_source_entity *entity)
{
    return entity && entity->number > 0 && entity->number < 512 &&
        number_valid(entity->model) && number_valid(entity->frame) &&
        number_valid(entity->colormap) && number_valid(entity->skin) && number_valid(entity->effects) &&
        vector_valid(entity->origin) && vector_valid(entity->angles);
}
static bool frame_valid(const qa_qw_source_frame *frame)
{
    if (!frame || !frame->sequence || frame->sequence > INT32_MAX ||
        frame->count > QA_QW_MAX_PACKET_ENTITIES) return false;
    uint32_t previous = 0;
    for (size_t i = 0; i < frame->count; ++i) {
        if (!entity_valid(frame->entities + i) || frame->entities[i].number <= previous) return false;
        previous = frame->entities[i].number;
    }
    return true;
}
/* Integer Source fields are widened here without rounding through float. */
static int32_t source_integer(double value)
{ return value >= INT32_MIN && value <= INT32_MAX ? (int32_t)value : INT32_MIN; }
static bool byte(qa_net_writer *writer, double value)
{ return qa_net_write_u8(writer, (uint8_t)(uint32_t)source_integer(value)); }
static bool short_word(qa_net_writer *writer, int32_t value)
{ return qa_net_write_u16(writer, (uint16_t)(uint32_t)value); }
static bool coord(qa_net_writer *writer, float value)
{ return short_word(writer, qa_source_float_to_i32(value * 8.0f)); }
static bool angle(qa_net_writer *writer, float value)
{ return byte(writer, qa_source_float_to_i32(value * 256.0f / 360.0f)); }

qa_qw_source_history *qa_qw_source_history_create(qa_error *error)
{
    qa_qw_source_history *history = calloc(1, sizeof(*history));
    if (!history) fail(error, QA_ERROR_MEMORY, "Allocating QuakeWorld source Number history");
    return history;
}
void qa_qw_source_history_destroy(qa_qw_source_history *history)
{ free(history); }
void qa_qw_source_history_reset(qa_qw_source_history *history)
{ if (history) memset(history, 0, sizeof(*history)); }
bool qa_qw_source_history_baselines(qa_qw_source_history *history,
    const qa_qw_source_entity *entities, size_t count, qa_error *error)
{
    if (!history || count > 511 || (count && !entities))
        return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld source baseline inventory");
    for (size_t i = 0; i < count; ++i)
        if (!entity_valid(entities + i) || (i && entities[i].number <= entities[i - 1].number))
            return fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeWorld source baseline Numbers or order");
    qa_qw_source_entity *copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy)
        return fail(error, QA_ERROR_MEMORY, "Retaining complete QuakeWorld source baseline replacement");
    if (count) memcpy(copy, entities, count * sizeof(*copy));
    qa_qw_source_history_reset(history);
    for (size_t i = 0; i < count; ++i)
        history->baselines[copy[i].number] = (source_baseline){true, copy[i]};
    free(copy);
    return true;
}
const qa_qw_source_frame *qa_qw_source_history_frame(const qa_qw_source_history *history, uint32_t sequence)
{
    if (!history) return NULL;
    const source_frame *frame = history->frames + (sequence & (QA_QW_UPDATE_BACKUP - 1));
    return frame->present && frame->value.sequence == sequence ? &frame->value : NULL;
}
bool qa_qw_source_history_store(qa_qw_source_history *history,
    const qa_qw_source_frame *frame, qa_error *error)
{
    if (!history || !frame_valid(frame))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid transmitted QuakeWorld source frame");
    for (size_t i = 0; i < QA_QW_UPDATE_BACKUP; ++i)
        if (history->frames[i].present && history->frames[i].value.sequence >= frame->sequence)
            return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld source frame sequence did not advance");
    history->frames[frame->sequence & (QA_QW_UPDATE_BACKUP - 1)] = (source_frame){true, *frame};
    for (size_t i = 0; i < QA_QW_UPDATE_BACKUP; ++i)
        if (history->frames[i].present &&
            frame->sequence - history->frames[i].value.sequence >= QA_QW_UPDATE_BACKUP)
            history->frames[i].present = false;
    return true;
}
bool qa_qw_source_history_cut(const qa_qw_source_history *history, uint32_t outgoing, qa_error *error)
{
    if (!history || !outgoing || outgoing > (uint32_t)INT32_MAX + 1u)
        return fail(error, QA_ERROR_FORMAT, "QuakeWorld source history lacks its actual outgoing channel cut");
    if (history->baselines[0].present)
        return fail(error, QA_ERROR_FORMAT, "QuakeWorld source baseline zero is a wire terminator");
    for (size_t i = 1; i < 512; ++i)
        if (history->baselines[i].present &&
            (!entity_valid(&history->baselines[i].value) || history->baselines[i].value.number != i))
            return fail(error, QA_ERROR_FORMAT, "QuakeWorld source baseline physical identity differs");
    for (size_t i = 0; i < QA_QW_UPDATE_BACKUP; ++i) {
        const source_frame *frame = history->frames + i;
        if (frame->present && (!frame_valid(&frame->value) || frame->value.sequence >= outgoing ||
            (frame->value.sequence & (QA_QW_UPDATE_BACKUP - 1)) != i))
            return fail(error, QA_ERROR_FORMAT, "QuakeWorld source frame exceeds its actual transmitted channel cut");
    }
    return true;
}

bool qa_qw_source_write_baseline(qa_net_writer *writer, const qa_qw_source_entity *entity)
{
    if (!entity_valid(entity)) return qa_net_writer_fail(writer, "Invalid QuakeWorld source baseline");
    qa_net_write_u8(writer, 22); qa_net_write_u16(writer, (uint16_t)entity->number);
    byte(writer, entity->model); byte(writer, entity->frame);
    byte(writer, entity->colormap); byte(writer, entity->skin);
    for (size_t i = 0; i < 3; ++i) { coord(writer, entity->origin[i]); angle(writer, entity->angles[i]); }
    return !writer->failed;
}
bool qa_qw_source_write_stat(qa_net_writer *writer, uint8_t index, double value)
{
    if (!number_valid(value)) return qa_net_writer_fail(writer, "Invalid QuakeWorld source stat Number");
    bool narrow = value >= 0 && value <= 255;
    qa_net_write_u8(writer, narrow ? 3 : 38); qa_net_write_u8(writer, index);
    if (narrow) byte(writer, value); else qa_net_write_u32(writer, (uint32_t)source_integer(value));
    return !writer->failed;
}
static bool command(qa_net_writer *writer, const qa_qw_command *value)
{
    if (!vector_valid(value->angles)) return qa_net_writer_fail(writer, "Invalid QuakeWorld source player command");
    unsigned bits = (value->angles[0] != 0 ? 1u : 0u) | (value->angles[1] != 0 ? 128u : 0u) |
        (value->angles[2] != 0 ? 2u : 0u) | (value->forward != 0 ? 4u : 0u) |
        (value->side != 0 ? 8u : 0u) | (value->up != 0 ? 16u : 0u) |
        (value->buttons != 0 ? 32u : 0u) | (value->impulse != 0 ? 64u : 0u);
    qa_net_write_u8(writer, (uint8_t)bits);
    for (size_t i = 0; i < 3; ++i)
        if (value->angles[i] != 0)
            short_word(writer, qa_source_float_to_i32(value->angles[i] * 65536.0f / 360.0f));
    if (bits & 4) qa_net_write_i16(writer, value->forward);
    if (bits & 8) qa_net_write_i16(writer, value->side);
    if (bits & 16) qa_net_write_i16(writer, value->up);
    if (bits & 32) qa_net_write_u8(writer, value->buttons);
    if (bits & 64) qa_net_write_u8(writer, value->impulse);
    qa_net_write_u8(writer, value->msec);
    return !writer->failed;
}
bool qa_qw_source_write_player(qa_net_writer *writer, const qa_qw_source_player *player)
{
    if (!player || player->slot >= 32 || (player->flags & ~4095u) ||
        !vector_valid(player->origin) || !vector_valid(player->velocity) ||
        !number_valid(player->frame) || !number_valid(player->model) || !number_valid(player->skin) ||
        !number_valid(player->effects) || !number_valid(player->weapon_frame))
        return qa_net_writer_fail(writer, "Invalid QuakeWorld source player Numbers");
    qa_net_write_u8(writer, 42); qa_net_write_u8(writer, player->slot);
    qa_net_write_u16(writer, player->flags);
    for (size_t i = 0; i < 3; ++i) coord(writer, player->origin[i]);
    byte(writer, player->frame);
    if (player->flags & QA_QW_PF_MSEC) qa_net_write_u8(writer, player->msec);
    if ((player->flags & QA_QW_PF_COMMAND) && !command(writer, &player->command)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (player->flags & (QA_QW_PF_VELOCITY1 << i))
            short_word(writer, qa_source_float_to_i32(player->velocity[i]));
    if (player->flags & QA_QW_PF_MODEL) byte(writer, player->model);
    if (player->flags & QA_QW_PF_SKIN) byte(writer, player->skin);
    if (player->flags & QA_QW_PF_EFFECTS) byte(writer, player->effects);
    if (player->flags & QA_QW_PF_WEAPONFRAME) byte(writer, player->weapon_frame);
    return !writer->failed;
}
bool qa_qw_source_write_nails(qa_net_writer *writer, const qa_qw_nail *nails, size_t count)
{
    if (count > QA_QW_MAX_NAILS || (count && !nails))
        return qa_net_writer_fail(writer, "Invalid QuakeWorld source nail inventory");
    for (size_t i = 0; i < count; ++i)
        if (!vector_valid(nails[i].origin) || !isfinite(nails[i].pitch) || !isfinite(nails[i].yaw))
            return qa_net_writer_fail(writer, "Nonfinite QuakeWorld source nail vector");
    qa_net_write_u8(writer, 43); qa_net_write_u8(writer, (uint8_t)count);
    for (size_t i = 0; i < count; ++i) {
        const qa_qw_nail *nail = nails + i;
        uint32_t x = (uint32_t)(qa_source_float_to_i32(nail->origin[0] + 4096.0f) >> 1);
        uint32_t y = (uint32_t)(qa_source_float_to_i32(nail->origin[1] + 4096.0f) >> 1);
        uint32_t z = (uint32_t)(qa_source_float_to_i32(nail->origin[2] + 4096.0f) >> 1);
        uint32_t pitch = (uint32_t)qa_source_float_to_i32(nail->pitch * 16.0f / 360.0f) & 15u;
        uint32_t yaw = (uint32_t)qa_source_float_to_i32(nail->yaw * 256.0f / 360.0f) & 255u;
        uint8_t packed[6] = {(uint8_t)x, (uint8_t)((x >> 8) | (y << 4)),
            (uint8_t)(y >> 4), (uint8_t)z, (uint8_t)((z >> 8) | (pitch << 4)), (uint8_t)yaw};
        qa_net_write_data(writer, packed, sizeof(packed));
    }
    return !writer->failed;
}
static bool delta_entity(qa_net_writer *writer, const qa_qw_source_entity *from,
    const qa_qw_source_entity *to, bool force)
{
    qa_qw_source_entity empty = {0};
    if (!from) from = &empty;
    unsigned bits = 0;
    for (size_t i = 0; i < 3; ++i) {
        double difference = (double)to->origin[i] - (double)from->origin[i];
        if (difference < -0.1 || difference > 0.1) bits |= (unsigned)U_ORIGIN1 << i;
        if (to->angles[i] != from->angles[i]) bits |= angle_bits[i];
    }
    if (to->model != from->model) bits |= U_MODEL;
    if (to->frame != from->frame) bits |= U_FRAME;
    if (to->colormap != from->colormap) bits |= U_COLORMAP;
    if (to->skin != from->skin) bits |= U_SKIN;
    if (to->effects != from->effects) bits |= U_EFFECTS;
    if (bits & 511) bits |= U_MOREBITS;
    if (to->solid) bits |= U_SOLID;
    if (!bits && !force) return true;
    qa_net_write_u16(writer, (uint16_t)(to->number | (bits & ~511u)));
    if (bits & U_MOREBITS) qa_net_write_u8(writer, (uint8_t)bits);
    if (bits & U_MODEL) byte(writer, to->model);
    if (bits & U_FRAME) byte(writer, to->frame);
    if (bits & U_COLORMAP) byte(writer, to->colormap);
    if (bits & U_SKIN) byte(writer, to->skin);
    if (bits & U_EFFECTS) byte(writer, to->effects);
    for (size_t i = 0; i < 3; ++i) {
        if (bits & ((unsigned)U_ORIGIN1 << i)) coord(writer, to->origin[i]);
        if (bits & angle_bits[i]) angle(writer, to->angles[i]);
    }
    return !writer->failed;
}
bool qa_qw_source_write_entities(qa_net_writer *writer, const qa_qw_source_history *history,
    const qa_qw_source_frame *frame, const qa_qw_source_frame *previous)
{
    if (!history || !frame_valid(frame) || (previous && (!frame_valid(previous) ||
        previous->sequence >= frame->sequence || frame->sequence - previous->sequence >= QA_QW_UPDATE_BACKUP ||
        qa_qw_source_history_frame(history, previous->sequence) != previous)))
        return qa_net_writer_fail(writer, "Invalid QuakeWorld source frame or actual delta history");
    qa_net_write_u8(writer, previous ? 48 : 47);
    if (previous) qa_net_write_u8(writer, (uint8_t)previous->sequence);
    size_t current = 0, old = 0;
    while (current < frame->count || (previous && old < previous->count)) {
        const qa_qw_source_entity *to = current < frame->count ? frame->entities + current : NULL;
        const qa_qw_source_entity *from = previous && old < previous->count ? previous->entities + old : NULL;
        if (from && (!to || from->number < to->number)) {
            qa_net_write_u16(writer, (uint16_t)(from->number | U_REMOVE)); ++old;
        } else if (to && from && to->number == from->number) {
            delta_entity(writer, from, to, false); ++current; ++old;
        } else {
            const source_baseline *baseline = history->baselines + to->number;
            delta_entity(writer, baseline->present ? &baseline->value : NULL, to, true); ++current;
        }
        if (writer->failed) return false;
    }
    return qa_net_write_u16(writer, 0);
}

static bool save_entity(qa_net_writer *writer, const qa_qw_source_entity *entity)
{
    qa_net_write_u32(writer, entity->number);
    qa_net_write_f64(writer, entity->model); qa_net_write_f64(writer, entity->frame);
    qa_net_write_f64(writer, entity->colormap); qa_net_write_f64(writer, entity->skin);
    qa_net_write_f64(writer, entity->effects);
    for (size_t i = 0; i < 3; ++i) qa_net_write_f32(writer, entity->origin[i]);
    for (size_t i = 0; i < 3; ++i) qa_net_write_f32(writer, entity->angles[i]);
    qa_net_write_u8(writer, entity->solid);
    return !writer->failed;
}
static bool restore_entity(qa_net_reader *reader, qa_qw_source_entity *entity)
{
    *entity = (qa_qw_source_entity){.number = qa_net_read_u32(reader)};
    entity->model = qa_net_read_f64(reader); entity->frame = qa_net_read_f64(reader);
    entity->colormap = qa_net_read_f64(reader); entity->skin = qa_net_read_f64(reader);
    entity->effects = qa_net_read_f64(reader);
    for (size_t i = 0; i < 3; ++i) entity->origin[i] = qa_net_read_f32(reader);
    for (size_t i = 0; i < 3; ++i) entity->angles[i] = qa_net_read_f32(reader);
    uint8_t solid = qa_net_read_u8(reader); entity->solid = solid != 0;
    return (!reader->failed && solid <= 1 && entity_valid(entity)) ||
        qa_net_reader_fail(reader, "Invalid saved QuakeWorld source Number entity");
}
bool qa_qw_source_history_checkpoint(const qa_qw_source_history *history, uint32_t outgoing,
    qa_buffer *out, qa_error *error)
{
    if (!out || !qa_qw_source_history_cut(history, outgoing, error)) return false;
    size_t capacity = 10 + 511 * SOURCE_ENTITY_BYTES +
        QA_QW_UPDATE_BACKUP * (7 + QA_QW_MAX_PACKET_ENTITIES * SOURCE_ENTITY_BYTES);
    qa_buffer bytes = {malloc(capacity), 0};
    if (!bytes.data) return fail(error, QA_ERROR_MEMORY, "Encoding QuakeWorld source Number history");
    qa_net_writer writer; qa_net_writer_init(&writer, bytes.data, capacity, error);
    uint16_t baselines = 0;
    for (size_t i = 1; i < 512; ++i)
        if (history->baselines[i].present) ++baselines;
    qa_net_write_u32(&writer, UINT32_C(0x48535751));
    qa_net_write_u16(&writer, baselines);
    for (size_t i = 1; i < 512; ++i)
        if (history->baselines[i].present) save_entity(&writer, &history->baselines[i].value);
    for (size_t i = 0; i < QA_QW_UPDATE_BACKUP; ++i) {
        const source_frame *frame = history->frames + i;
        qa_net_write_u8(&writer, frame->present);
        if (!frame->present) continue;
        qa_net_write_u32(&writer, frame->value.sequence);
        qa_net_write_u16(&writer, (uint16_t)frame->value.count);
        for (size_t j = 0; j < frame->value.count; ++j) save_entity(&writer, frame->value.entities + j);
    }
    if (writer.failed) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&writer); *out = bytes; return true;
}
bool qa_qw_source_history_restore(qa_bytes bytes, uint32_t outgoing,
    qa_qw_source_history **out, qa_error *error)
{
    if (!out || *out || !bytes.data)
        return fail(error, QA_ERROR_ARGUMENT, "Source Number restore requires an empty actual history owner");
    qa_qw_source_history *history = qa_qw_source_history_create(error);
    if (!history) return false;
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint32_t magic = qa_net_read_u32(&reader);
    uint16_t count = qa_net_read_u16(&reader); uint32_t previous = 0;
    bool ok = !reader.failed && magic == UINT32_C(0x48535751) && count <= 511;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_qw_source_entity entity;
        ok = restore_entity(&reader, &entity) && entity.number > previous;
        if (ok) { previous = entity.number; history->baselines[entity.number] = (source_baseline){true, entity}; }
    }
    for (size_t i = 0; ok && i < QA_QW_UPDATE_BACKUP; ++i) {
        uint8_t present = qa_net_read_u8(&reader);
        ok = !reader.failed && present <= 1;
        if (!ok || !present) continue;
        source_frame *frame = history->frames + i;
        frame->present = true; frame->value.sequence = qa_net_read_u32(&reader);
        frame->value.count = qa_net_read_u16(&reader);
        ok = !reader.failed && frame->value.count <= QA_QW_MAX_PACKET_ENTITIES;
        for (size_t j = 0; ok && j < frame->value.count; ++j)
            ok = restore_entity(&reader, frame->value.entities + j);
    }
    if (ok) ok = qa_net_reader_finish(&reader) && qa_qw_source_history_cut(history, outgoing, error);
    if (!ok) {
        qa_qw_source_history_destroy(history);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid QuakeWorld source Number history");
        return false;
    }
    *out = history; return true;
}

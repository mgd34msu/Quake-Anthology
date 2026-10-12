/* Donor: network/q1/codecs/qw28.ts and bootstrap/network/qw-server.ts. */
#include "qa/network_qw_source.h"
#include "qa/text.h"
#include "qw_output.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { SOURCE_ENTITY_BYTES = 69 };
static const qa_net_protocol_id source_protocol = {.kind = QA_NET_QW28};
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
    qw_output_entity value=qw_output_entity_source(entity);
    return qw_output_write_baseline(writer,source_protocol,QW_OUTPUT_SOURCE,&value);
}
bool qa_qw_source_write_stat(qa_net_writer *writer, uint8_t index, double value)
{
    if (!number_valid(value)) return qa_net_writer_fail(writer, "Invalid QuakeWorld source stat Number");
    return qw_output_write_stat(writer,index,value);
}
bool qa_qw_source_write_player(qa_net_writer *writer, const qa_qw_source_player *player)
{
    if (!player || player->slot >= 32 || (player->flags & ~4095u) ||
        !vector_valid(player->origin) || !vector_valid(player->velocity) ||
        !number_valid(player->frame) || !number_valid(player->model) || !number_valid(player->skin) ||
        !number_valid(player->effects) || !number_valid(player->weapon_frame))
        return qa_net_writer_fail(writer, "Invalid QuakeWorld source player Numbers");
    qa_net_write_u8(writer, 42);
    qw_output_player value={.slot=player->slot,.msec=player->msec,.flags=player->flags,
        .origin=player->origin,.velocity=player->velocity,.frame=player->frame,.model=player->model,
        .skin=player->skin,.effects=player->effects,.weapon_frame=player->weapon_frame,.command=&player->command};
    return qw_output_write_player(writer,source_protocol,QW_OUTPUT_SOURCE,&value);
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
        uint32_t x = (uint32_t)(qa_source_to_i32(nail->origin[0] + 4096.0f) >> 1);
        uint32_t y = (uint32_t)(qa_source_to_i32(nail->origin[1] + 4096.0f) >> 1);
        uint32_t z = (uint32_t)(qa_source_to_i32(nail->origin[2] + 4096.0f) >> 1);
        uint32_t pitch = (uint32_t)qa_source_to_i32(nail->pitch * 16.0f / 360.0f) & 15u;
        uint32_t yaw = (uint32_t)qa_source_to_i32(nail->yaw * 256.0f / 360.0f) & 255u;
        uint8_t packed[6] = {(uint8_t)x, (uint8_t)((x >> 8) | (y << 4)),
            (uint8_t)(y >> 4), (uint8_t)z, (uint8_t)((z >> 8) | (pitch << 4)), (uint8_t)yaw};
        qa_net_write_data(writer, packed, sizeof(packed));
    }
    return !writer->failed;
}
static void output_entity_read(const void *rows, size_t index, qw_output_entity *out)
{ *out=qw_output_entity_source((const qa_qw_source_entity *)rows+index); }
static bool output_baseline_read(const void *context, uint32_t number, qw_output_entity *out)
{
    const qa_qw_source_history *history=context;
    const source_baseline *row=history->baselines+number;
    if (!row->present) return false;
    *out=qw_output_entity_source(&row->value); return true;
}
bool qa_qw_source_write_entities(qa_net_writer *writer, const qa_qw_source_history *history,
    const qa_qw_source_frame *frame, const qa_qw_source_frame *previous)
{
    if (!history || !frame_valid(frame) || (previous && (!frame_valid(previous) ||
        previous->sequence >= frame->sequence || frame->sequence - previous->sequence >= QA_QW_UPDATE_BACKUP ||
        qa_qw_source_history_frame(history, previous->sequence) != previous)))
        return qa_net_writer_fail(writer, "Invalid QuakeWorld source frame or actual delta history");
    qw_output_packet packet={.current=frame->entities,.count=frame->count,
        .previous=previous ? previous->entities : NULL,.previous_count=previous ? previous->count : 0,
        .baselines=history,.read=output_entity_read,.baseline=output_baseline_read,
        .delta=previous!=NULL,.previous_sequence=previous ? previous->sequence : 0};
    return qw_output_write_entities(writer,source_protocol,QW_OUTPUT_SOURCE,&packet);
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

#include "bsp_private.h"
#include "../save_fields.h"
#include "qa/bot_bsp_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'B', 'S', 'P', 0, 0};

static bool capacity_valid(size_t count, size_t capacity, size_t width)
{
    return capacity >= count && capacity <= SIZE_MAX / width &&
        (capacity ? count && capacity >= 64 && !(capacity & (capacity - 1)) : !count);
}

static bool span_fields(qa_source_save_io *io, qa_bytes source, size_t parsed_size, qa_bytes *span)
{
    size_t offset = 0, size = span->size;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        uintptr_t base = (uintptr_t)source.data, address = (uintptr_t)span->data;
        if (!source.data || !span->data || address < base || address - base > parsed_size)
            return bot_save_fail(io, QA_ERROR_FORMAT, "Bot BSP span is outside its immutable source");
        offset = (size_t)(address - base);
    }
    if (!qa_source_save_count(io, &offset, parsed_size) || !qa_source_save_count(io, &size, parsed_size) ||
        !offset || offset >= parsed_size || size >= parsed_size - offset ||
        source.data[offset - 1] != '"' || source.data[offset + size] != '"')
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid source bot BSP quoted span");
    if (io->direction == QA_SOURCE_SAVE_READ)
        *span = (qa_bytes){source.data + offset, size};
    return true;
}

static bool bsp_fields(qa_source_save_io *io, qa_bot_bsp *bsp, qa_bytes source)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t source_size = source.size;
    if (!qa_source_save_count(io, &source_size, SIZE_MAX) || source_size != source.size)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Bot BSP immutable source identity differs");
    size_t parsed_size = source.size;
    const uint8_t *zero = source.size ? memchr(source.data, 0, source.size) : NULL;
    if (zero)
        parsed_size = (size_t)(zero - source.data);
    if (!qa_source_save_count(io, &bsp->entities.count, 2047) ||
        !qa_source_save_count(io, &bsp->record_capacity, 2048) ||
        !qa_source_save_count(io, &bsp->entities.property_count, SIZE_MAX) ||
        !qa_source_save_count(io, &bsp->property_capacity, SIZE_MAX) ||
        !capacity_valid(bsp->entities.count, bsp->record_capacity, sizeof(qa_entity_record)) ||
        !capacity_valid(bsp->entities.property_count, bsp->property_capacity, sizeof(qa_entity_property)))
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot BSP table capacities");
    if (reading) {
        size_t remaining = io->input.size - io->offset;
        if (bsp->entities.count > remaining / 16 ||
            bsp->entities.property_count > (remaining - bsp->entities.count * 16) / 32)
            return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot BSP tables");
        if (bsp->record_capacity)
            bsp->entities.records = calloc(bsp->record_capacity, sizeof(*bsp->entities.records));
        if (bsp->property_capacity)
            bsp->entities.properties = calloc(bsp->property_capacity, sizeof(*bsp->entities.properties));
        if ((bsp->record_capacity && !bsp->entities.records) ||
            (bsp->property_capacity && !bsp->entities.properties))
            return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot BSP tables");
    }
    size_t first = 0;
    for (size_t i = 0; i < bsp->entities.count; ++i) {
        qa_entity_record *record = &bsp->entities.records[i];
        if (!qa_source_save_count(io, &record->first_property, bsp->entities.property_count) ||
            !qa_source_save_count(io, &record->property_count, bsp->entities.property_count) ||
            record->first_property != first || record->property_count > bsp->entities.property_count - first)
            return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot BSP epair partition");
        first += record->property_count;
    }
    if (first != bsp->entities.property_count)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Bot BSP epairs have no source record");
    for (size_t i = 0; i < bsp->entities.property_count; ++i)
        if (!span_fields(io, source, parsed_size, &bsp->entities.properties[i].key) ||
            !span_fields(io, source, parsed_size, &bsp->entities.properties[i].value))
            return false;
    return true;
}

bool qa_bot_bsp_capture(const qa_bot_bsp *source_bsp, qa_bytes source, qa_buffer *out, qa_error *error)
{
    if (!source_bsp || !out || (source.size && !source.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot BSP owner, immutable bytes or output");
        return false;
    }
    qa_bot_bsp view = *source_bsp;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) &&
        bsp_fields(&io, &view, source) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_bsp_restore(qa_bytes bytes, qa_bytes source, qa_bot_bsp **out, qa_error *error)
{
    if (!out || *out || (source.size && !source.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot BSP restore requires empty output and qualified source");
        return false;
    }
    qa_bot_bsp *bsp = calloc(1, sizeof(*bsp));
    if (!bsp) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring bot BSP owner");
        return false;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        bsp_fields(&io, bsp, source) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        qa_bot_bsp_close(bsp);
        return false;
    }
    *out = bsp;
    return true;
}

#include "internal.h"

#define ACTOR_RECORD_BYTES 24u

bool qa_save_actors_encode(const qa_actor_checkpoint *value, qa_buffer *out, qa_error *error)
{
    if (!value || !out || !value->capacity || value->count > value->capacity ||
        (value->count && !value->slots) ||
        (value->count && (SIZE_MAX - 12u) / value->count < ACTOR_RECORD_BYTES))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid actor checkpoint encoding");
    uint32_t players = 0;
    for (uint32_t i = 0; i < value->count; ++i) players += value->slots[i].player.present ? 1u : 0u;
    size_t size = 12u + (size_t)value->count * ACTOR_RECORD_BYTES;
    if (players) {
        if (size > SIZE_MAX - 8u || (SIZE_MAX - size - 8u) / players < 24u)
            return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid player checkpoint extent");
        size += 8u + (size_t)players * 24u;
    }
    qa_buffer buffer = {malloc(size), size};
    if (!buffer.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating actor checkpoint codec");
    qa_net_writer writer;
    qa_net_writer_init(&writer, buffer.data, size, error);
    qa_net_write_data(&writer, "QAAR", 4);
    qa_net_write_u32(&writer, value->count);
    qa_net_write_u32(&writer, value->capacity);
    for (uint32_t i = 0; i < value->count; ++i) {
        const qa_actor_slot_checkpoint *slot = value->slots + i;
        qa_net_write_u64(&writer, slot->generation);
        qa_net_write_u32(&writer, slot->owner);
        qa_net_write_u32(&writer, slot->definition);
        qa_net_write_u32(&writer, slot->source_slot);
        qa_net_write_u8(&writer, slot->active ? 1 : 0);
        qa_net_write_u8(&writer, slot->has_source ? 1 : 0);
        qa_net_write_u16(&writer, 0);
    }
    if (players) {
        qa_net_write_data(&writer, "QAPL", 4);
        qa_net_write_u32(&writer, players);
        for (uint32_t i = 0; i < value->count; ++i) {
            const qa_actor_player *player = &value->slots[i].player;
            if (!player->present) continue;
            qa_net_write_u32(&writer, i);
            qa_net_write_u32(&writer, player->name);
            qa_net_write_u32(&writer, player->team);
            qa_net_write_u32(&writer, player->skin);
            qa_net_write_i32(&writer, player->ping);
            qa_net_write_u8(&writer, (player->bot ? 1u : 0u) | (player->spectator ? 2u : 0u));
            qa_net_write_u8(&writer, 0); qa_net_write_u16(&writer, 0);
        }
    }
    if (writer.failed) { qa_buffer_free(&buffer); return false; }
    *out = buffer;
    return true;
}

bool qa_save_actors_decode(qa_bytes bytes, qa_actor_checkpoint *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < 12 || memcmp(bytes.data, "QAAR", 4))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid actor checkpoint signature");
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error);
    reader.bit = 32;
    qa_actor_checkpoint value = {0};
    value.count = qa_net_read_u32(&reader);
    value.capacity = qa_net_read_u32(&reader);
    if (!value.capacity || value.count > value.capacity ||
        value.count > qa_net_reader_remaining(&reader) / ACTOR_RECORD_BYTES ||
        qa_net_reader_remaining(&reader) < (size_t)value.count * ACTOR_RECORD_BYTES)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid actor checkpoint extent");
    value.slots = value.count ? calloc(value.count, sizeof(*value.slots)) : NULL;
    if (value.count && !value.slots)
        return persistence_fail(error, QA_ERROR_MEMORY, "Allocating decoded actor slots");
    for (uint32_t i = 0; i < value.count && !reader.failed; ++i) {
        qa_actor_slot_checkpoint *slot = value.slots + i;
        slot->generation = qa_net_read_u64(&reader);
        slot->owner = qa_net_read_u32(&reader);
        slot->definition = qa_net_read_u32(&reader);
        slot->source_slot = qa_net_read_u32(&reader);
        uint8_t active = qa_net_read_u8(&reader), source = qa_net_read_u8(&reader);
        uint16_t reserved = qa_net_read_u16(&reader);
        slot->active = active != 0;
        slot->has_source = source != 0;
        if (active > 1 || source > 1 || reserved ||
            (slot->active && slot->generation == UINT64_MAX) || (!slot->active && slot->has_source))
            qa_net_reader_fail(&reader, "Invalid saved actor generation or flags");
    }
    if (!reader.failed && qa_net_reader_remaining(&reader)) {
        qa_bytes tag;
        if (!qa_net_read_bytes(&reader, 4, &tag) || memcmp(tag.data, "QAPL", 4))
            qa_net_reader_fail(&reader, "Invalid saved player component");
        uint32_t players = qa_net_read_u32(&reader);
        if (players > value.count || qa_net_reader_remaining(&reader) != (size_t)players * 24u)
            qa_net_reader_fail(&reader, "Invalid saved player component extent");
        for (uint32_t i = 0; i < players && !reader.failed; ++i) {
            uint32_t slot = qa_net_read_u32(&reader);
            qa_actor_player player = {.present = true};
            player.name = qa_net_read_u32(&reader); player.team = qa_net_read_u32(&reader);
            player.skin = qa_net_read_u32(&reader); player.ping = qa_net_read_i32(&reader);
            uint8_t flags = qa_net_read_u8(&reader), pad = qa_net_read_u8(&reader);
            uint16_t reserved = qa_net_read_u16(&reader);
            player.bot = (flags & 1u) != 0; player.spectator = (flags & 2u) != 0;
            if (slot >= value.count || flags > 3 || pad || reserved ||
                !value.slots[slot].active || value.slots[slot].player.present)
                qa_net_reader_fail(&reader, "Invalid saved player component slot");
            if (!reader.failed) value.slots[slot].player = player;
        }
    }
    if (!qa_net_reader_finish(&reader)) { qa_actor_checkpoint_free(&value); return false; }
    *out = value;
    return true;
}

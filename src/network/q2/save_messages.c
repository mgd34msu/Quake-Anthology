#include "messages_internal.h"
#include "qa/network_q2_wire_save.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static bool invalid(qa_source_save_io *io, const char *message)
{ qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message); io->failed = true; return false; }
static bool text(qa_source_save_io *io, char **value)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    size_t length = io->direction == QA_SOURCE_SAVE_READ ? 0 : strlen(*value);
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset) return invalid(io, "Saved Q2 text exceeds its source record");
        *value = malloc(length + 1); if (!*value) return invalid(io, "Cannot restore Q2 text");
        (*value)[length] = 0;
    }
    return qa_source_save_bytes(io, *value, length) &&
        (!memchr(*value, 0, length) || invalid(io, "Saved Q2 text contains a terminator"));
}
static bool options(qa_source_save_io *io, qa_q2_message_options *value)
{
    if (!qa_source_save_count(io, &value->config_strings, UINT16_MAX) ||
        !qa_source_save_count(io, &value->inventory_slots, 32768) ||
        !qa_source_save_count(io, &value->history_capacity, SIZE_MAX / sizeof(qa_q2_wire_frame)) ||
        !qa_source_save_count(io, &value->max_inflated_bytes, SIZE_MAX) ||
        !qa_source_save_bool(io, &value->demo) || !qa_source_save_bool(io, &value->override_extended_temps) ||
        !qa_source_save_bool(io, &value->extended_temps) ||
        !qa_source_save_bytes(io, value->private_opcodes, sizeof(value->private_opcodes))) return false;
    bool bound = value->private_read != NULL;
    if (!qa_source_save_bool(io, &bound)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && bound != (value->private_read != NULL))
        return invalid(io, "Saved Q2 private services lack their actual candidate reader");
    return value->config_strings && value->inventory_slots && value->history_capacity && value->max_inflated_bytes;
}
static bool same_options(const qa_q2_message_options *a, const qa_q2_message_options *b)
{
    size_t history = b->history_capacity ? b->history_capacity : 16;
    size_t inflated = b->max_inflated_bytes ? b->max_inflated_bytes : 64u * 1024u * 1024u;
    return a->config_strings == b->config_strings && a->inventory_slots == b->inventory_slots &&
        a->history_capacity == history && a->max_inflated_bytes == inflated && a->demo == b->demo &&
        a->override_extended_temps == b->override_extended_temps && a->extended_temps == b->extended_temps &&
        !memcmp(a->private_opcodes, b->private_opcodes, sizeof(a->private_opcodes)) &&
        (a->private_read != NULL) == (b->private_read != NULL);
}
bool qa_q2_save_messages(qa_source_save_io *io, const qa_q2_message_options *candidate,
    qa_q2_messages **owner)
{
    if (!io || !owner || (io->direction == QA_SOURCE_SAVE_READ ? *owner != NULL || !candidate : *owner == NULL || (*owner)->reading)) return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_q2_codec codec = reading ? (qa_q2_codec){0} : (*owner)->codec;
    qa_q2_message_options policy = reading ? *candidate : (*owner)->options;
    if (!qa_q2_save_codec(io, &codec) || !options(io, &policy)) return false;
    if (reading && !same_options(&policy, candidate)) return invalid(io, "Q2 decoder candidate layout differs from its saved Source owner");
    qa_q2_messages *messages = reading ? NULL : *owner;
    qa_net_protocol_id admitted = codec.protocol;
    if (admitted.kind == QA_NET_Q2PRO_36) admitted.flags = 0;
    if (reading && !qa_q2_messages_create(admitted, &policy, &messages, io->error)) return false;
    if (reading) messages->codec = codec;
    bool ok = qa_source_save_u8(io, &messages->seat);
    uint32_t stream = (uint32_t)messages->stream;
    if (ok) ok = qa_source_save_u32(io, &stream);
    if (ok && (stream > STREAM_GAMESTATE || messages->seat > QA_Q2_MAX_SEATS ||
        (messages->seat == QA_Q2_MAX_SEATS && codec.protocol.kind != QA_NET_Q2KEX_2023 && codec.protocol.kind != QA_NET_Q2KEX_DEMO_2022)))
        ok = invalid(io, "Saved Q2 stream or recipient marker is invalid");
    if (ok && reading) messages->stream = stream;
    if (ok) ok = qa_source_save_count(io, &messages->inflated_this_read, messages->options.max_inflated_bytes);
    for (size_t i = 0; ok && i < policy.config_strings; ++i) ok = text(io, &messages->configs[i]);
    if (ok) ok = qa_source_save_count(io, &messages->baseline_capacity, UINT16_MAX) &&
        qa_source_save_count(io, &messages->baseline_count, messages->baseline_capacity);
    if (ok && reading && messages->baseline_capacity) {
        messages->baselines = calloc(messages->baseline_capacity, sizeof(*messages->baselines));
        if (!messages->baselines) ok = invalid(io, "Cannot restore Q2 baseline allocation");
    }
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < messages->baseline_count; ++i) {
        ok = qa_q2_save_entity(io, messages->baselines + i);
        if (ok && (messages->baselines[i].number <= previous || messages->baselines[i].number > UINT16_MAX))
            ok = invalid(io, "Saved Q2 baseline order differs");
        previous = messages->baselines[i].number;
    }
    for (size_t i = 0; ok && i < QA_Q2_MAX_SEATS; ++i) {
        bool present = messages->histories[i] != NULL;
        ok = qa_source_save_bool(io, &present);
        if (ok && present) ok = qa_q2_save_history(io, &messages->histories[i]);
    }
    bool open = messages->download_open;
    if (ok) ok = qa_source_save_bool(io, &open);
    size_t count = 0; const qa_q2_inflate_segment *last = NULL;
    if (!reading) for (const qa_q2_inflate_segment *segment = messages->download_first; segment; segment = segment->next) {
        if (count == SIZE_MAX) { ok = invalid(io, "Q2 deflate receipt inventory overflows"); break; }
        ++count; last = segment;
    }
    if (ok && !reading && ((messages->download_last != last) || (open != (count != 0))))
        ok = invalid(io, "Q2 deflate continuation differs from its accepted receipt inventory");
    if (ok) ok = qa_source_save_count(io, &count, SIZE_MAX);
    if (ok && open != (count != 0)) ok = invalid(io, "Saved Q2 deflate receipts lack their actual stream");
    const qa_q2_inflate_segment *segment = messages->download_first;
    for (size_t i = 0; ok && i < count; ++i) {
        size_t length = reading ? 0 : segment->compressed.size, output = reading ? 0 : segment->output_size;
        ok = qa_source_save_count(io, &length, UINT_MAX) && qa_source_save_count(io, &output, policy.max_inflated_bytes);
        if (!ok) break;
        if (reading) {
            if (length > io->input.size - io->offset) { ok = invalid(io, "Saved Q2 deflate segment exceeds its record"); break; }
            qa_bytes bytes = {length ? io->input.data + io->offset : NULL, length}; io->offset += length;
            ok = qa_q2_messages_restore_download_segment(messages, bytes, output, io->error);
        } else { ok = qa_source_save_bytes(io, segment->compressed.data, length); segment = segment->next; }
    }
    if (reading) { if (!ok) qa_q2_messages_destroy(messages); else *owner = messages; }
    return ok;
}

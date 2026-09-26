#include "roq_internal.h"

#include <string.h>

bool roq_fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text);
    return false;
}
static void header(roq_stream *stream, size_t offset) {
    const uint8_t *bytes = stream->file + offset;
    stream->state.next_id = qa_load_u16le(bytes);
    stream->state.next_size = qa_load_u32le(bytes + 2) & UINT32_C(0xffffff);
    stream->state.next_flags = qa_load_u16le(bytes + 6);
    stream->state.has_next = true;
}
static bool read_file(roq_stream *stream, size_t wanted, size_t *count, qa_error *error) {
    if (wanted > ROQ_FILE_BYTES)
        return roq_fail(error, "RoQ chunk exceeds retained scratch");
    uint64_t length = qa_media_input_size(stream->input);
    if (stream->state.position > length)
        return roq_fail(error, "RoQ cursor exceeds input");
    uint64_t available = length - stream->state.position;
    *count = available < wanted ? (size_t)available : wanted;
    if (!qa_media_input_read(stream->input, stream->state.position, stream->file, *count, error))
        return false;
    stream->state.position += *count;
    return true;
}
bool roq_stream_init(roq_stream *stream, bool reset, qa_error *error) {
    uint16_t remaining = reset ? stream->state.packet_remaining : 0;
    stream->state = (qa_roq_stream_checkpoint){.played = 24, .packet_remaining = remaining};
    stream->pending = false;
    size_t count;
    if (!read_file(stream, 16, &count, error))
        return false;
    bool header_only =
        count == 8 && qa_media_input_size(stream->input) == 8 && stream->policy == QA_ROQ_COMPLETE;
    if (!reset && count != 16 && !header_only)
        return roq_fail(error, "Truncated RoQ header");
    if (!reset && qa_load_u16le(stream->file) != 0x1084)
        return roq_fail(error, "Invalid RoQ magic");
    stream->state.buffered_length = reset ? 16 : (uint32_t)count;
    memcpy(stream->state.header, stream->file, 8);
    if (!header_only)
        header(stream, 8);
    if (!reset && stream->state.has_next && stream->state.next_size > 65536)
        return roq_fail(error, "RoQ chunk exceeds 65536 bytes");
    return true;
}
bool roq_stream_next(roq_stream *stream, roq_chunk *out, qa_error *error) {
    qa_roq_stream_checkpoint *state = &stream->state;
    if (stream->pending)
        return roq_fail(error, "RoQ dispatch is already pending");
    if (state->invalid || !state->has_next) {
        *out = (roq_chunk){.ended = true};
        return true;
    }
    if (!state->buffered_next) {
        uint64_t offset = state->position;
        size_t count;
        if (!read_file(stream, (size_t)state->next_size + 8, &count, error))
            return false;
        if (stream->policy == QA_ROQ_CINEMATIC &&
            state->played >= qa_media_input_size(stream->input)) {
            *out = (roq_chunk){.ended = true};
            return true;
        }
        if (stream->policy == QA_ROQ_COMPLETE) {
            if (count < state->next_size)
                return roq_fail(error, "Truncated RoQ payload");
            state->buffered_length = (uint32_t)count;
        } else {
            if (count < (size_t)state->next_size + 8) {
                if (count == state->next_size &&
                    state->position == qa_media_input_size(stream->input))
                    state->retained_eof = true;
                else if (count != 0 || !state->retained_eof)
                    return roq_fail(error, "Truncated RoQ payload or lookahead");
            }
            if (count == (size_t)state->next_size + 8 &&
                state->position == qa_media_input_size(stream->input))
                state->retained_eof = true;
            state->buffered_length = state->next_size + 8;
        }
        state->buffer_offset = offset;
        state->chunk_offset = 0;
    }
    size_t offset = state->chunk_offset;
    size_t decoded = state->next_id == 0x1030 || state->next_id == 0x1013 ? 0 : state->next_size;
    size_t limit = state->buffered_next ? 65536 : state->buffered_length;
    if (offset > limit || decoded > limit - offset)
        return roq_fail(error, "RoQ packet exceeds retained scratch");
    size_t end =
        state->next_id == 0x1002 && stream->policy == QA_ROQ_CINEMATIC ? 65536 : offset + decoded;
    if (end < offset)
        return roq_fail(error, "RoQ codebook starts beyond scratch");
    *out = (roq_chunk){.id = state->next_id,
                       .flags = state->next_flags,
                       .bytes = {stream->file + offset, end - offset}};
    stream->pending = true;
    return true;
}
bool roq_stream_complete(roq_stream *stream, bool ended, qa_error *error) {
    qa_roq_stream_checkpoint *state = &stream->state;
    if (!stream->pending)
        return roq_fail(error, "RoQ has no pending dispatch");
    if (state->next_id == 0x1030)
        state->packet_remaining = state->next_flags;
    size_t following =
        (size_t)state->chunk_offset +
        (state->next_id == 0x1030 || state->next_id == 0x1013 ? 0 : state->next_size);
    if (stream->policy == QA_ROQ_COMPLETE && following == state->buffered_length &&
        !state->packet_remaining) {
        state->has_next = false;
        state->buffered_next = false;
        stream->pending = false;
        return true;
    }
    size_t limit = state->buffered_next ? 65536 : state->buffered_length;
    if (following > limit || limit - following < 8)
        return roq_fail(error, "Truncated RoQ next chunk header");
    header(stream, following);
    state->invalid = state->next_size > 65536 || state->next_id == 0x1084;
    stream->pending = false;
    state->buffered_next = false;
    if (state->invalid)
        return true;
    if (state->packet_remaining && !ended) {
        --state->packet_remaining;
        state->chunk_offset = (uint32_t)(following + 8);
        state->buffered_next = true;
    } else {
        if (state->played > UINT64_MAX - state->next_size - 8)
            return roq_fail(error, "RoQ playback position overflow");
        state->played += state->next_size + 8;
    }
    return true;
}

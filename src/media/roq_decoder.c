#include "roq_internal.h"

#include <stdlib.h>
#include <string.h>

struct qa_roq_decoder {
    roq_stream stream;
    qa_roq_scratch *scratch;
    int16_t *audio;
    size_t audio_capacity;
    uint32_t width, height;
    uint16_t rate;
    int64_t index;
    uint8_t unknown;
    bool silent, busy, faulted;
};
typedef struct vq_reader {
    qa_bytes bytes;
    size_t cursor;
    uint16_t codes;
    unsigned remaining;
} vq_reader;

bool qa_roq_scratch_create(qa_roq_scratch **out, qa_error *error) {
    if (!out)
        return roq_fail(error, "Missing RoQ scratch output");
    qa_roq_scratch *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ retained buffers");
        return false;
    }
    scratch->references = 1;
    *out = scratch;
    return true;
}
void qa_roq_scratch_retain(qa_roq_scratch *scratch) {
    if (scratch)
        ++scratch->references;
}
void qa_roq_scratch_release(qa_roq_scratch *scratch) {
    if (scratch && !--scratch->references)
        free(scratch);
}
void qa_roq_scratch_clear(qa_roq_scratch *scratch, bool books) {
    if (!scratch)
        return;
    memset(scratch->file, 0, sizeof(scratch->file));
    memset(scratch->frames, 0, sizeof(scratch->frames));
    if (books)
        memset(&scratch->books, 0, sizeof(scratch->books));
}
bool qa_roq_scratch_capture(const qa_roq_scratch *scratch, qa_buffer *out, qa_error *error) {
    if (!scratch || !scratch->references || !out || out->data || out->size)
        return roq_fail(error, "Missing retained RoQ scratch checkpoint owner");
    qa_buffer saved = {.size = ROQ_SAVED_BYTES + 8u};
    saved.data = malloc(saved.size);
    if (!saved.data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Saving shared RoQ physical buffers");
        return false;
    }
    memcpy(saved.data, "QRSB", 4);
    qa_store_u32le(saved.data + 4, 1);
    uint8_t *bytes = saved.data + 8;
    memcpy(bytes, scratch->file, ROQ_FILE_BYTES);
    bytes += ROQ_FILE_BYTES;
    memcpy(bytes, &scratch->books, sizeof(scratch->books));
    bytes += sizeof(scratch->books);
    memcpy(bytes, scratch->frames, ROQ_FRAME_BYTES);
    *out = saved;
    return true;
}
bool qa_roq_scratch_restore(qa_roq_scratch *scratch, qa_bytes saved, qa_error *error) {
    if (!scratch || !scratch->references || !saved.data || saved.size != ROQ_SAVED_BYTES + 8u ||
        memcmp(saved.data, "QRSB", 4) || qa_load_u32le(saved.data + 4) != 1)
        return roq_fail(error, "Invalid shared RoQ physical-buffer checkpoint");
    const uint8_t *bytes = saved.data + 8;
    memcpy(scratch->file, bytes, ROQ_FILE_BYTES);
    bytes += ROQ_FILE_BYTES;
    memcpy(&scratch->books, bytes, sizeof(scratch->books));
    bytes += sizeof(scratch->books);
    memcpy(scratch->frames, bytes, ROQ_FRAME_BYTES);
    return true;
}
bool qa_roq_decoder_create(qa_media_input *input, const qa_roq_decoder_options *options,
                           qa_roq_decoder **out, qa_error *error) {
    if (!input || !options || !out ||
        (options->end_policy != QA_ROQ_COMPLETE && options->end_policy != QA_ROQ_CINEMATIC))
        return roq_fail(error, "Invalid RoQ decoder configuration");
    qa_roq_decoder *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ decoder");
        return false;
    }
    decoder->scratch = options->scratch;
    if (decoder->scratch)
        qa_roq_scratch_retain(decoder->scratch);
    else if (!qa_roq_scratch_create(&decoder->scratch, error)) {
        free(decoder);
        return false;
    }
    qa_media_input_retain(input);
    decoder->stream =
        (roq_stream){.input = input, .file = decoder->scratch->file, .policy = options->end_policy};
    decoder->silent = options->silent;
    decoder->index = -1;
    if (!roq_stream_init(&decoder->stream, false, error)) {
        qa_roq_decoder_destroy(decoder);
        return false;
    }
    decoder->rate = qa_load_u16le(decoder->stream.state.header + 6);
    if (!decoder->rate)
        decoder->rate = 30;
    *out = decoder;
    return true;
}
void qa_roq_decoder_destroy(qa_roq_decoder *decoder) {
    if (!decoder || decoder->busy)
        return;
    qa_media_input_release(decoder->stream.input);
    qa_roq_scratch_release(decoder->scratch);
    free(decoder->audio);
    free(decoder);
}
static bool code(vq_reader *reader, unsigned *out, qa_error *error) {
    if (!reader->remaining) {
        if (reader->bytes.size - reader->cursor < 2) {
            roq_fail(error, "Truncated RoQ control word");
            return false;
        }
        reader->codes = qa_load_u16le(reader->bytes.data + reader->cursor);
        reader->cursor += 2;
        reader->remaining = 8;
    }
    *out = reader->codes >> 14;
    reader->codes = (uint16_t)(reader->codes << 2);
    --reader->remaining;
    return true;
}
static bool byte(vq_reader *reader, uint8_t *out, qa_error *error) {
    if (reader->cursor == reader->bytes.size) {
        roq_fail(error, "Truncated RoQ block operand");
        return false;
    }
    *out = reader->bytes.data[reader->cursor++];
    return true;
}
static uint8_t *front(qa_roq_decoder *decoder) {
    return decoder->scratch->frames +
           ((uint64_t)decoder->index & 1u) * (size_t)decoder->width * decoder->height * 4;
}
static void blit(qa_roq_decoder *decoder, const uint8_t *book, uint8_t index, unsigned size,
                 unsigned x, unsigned y) {
    uint8_t *destination = front(decoder);
    for (unsigned row = 0; row < size; ++row)
        memcpy(destination + ((size_t)(y + row) * decoder->width + x) * 4,
               book + ((size_t)index * size * size + row * size) * 4, size * 4);
}
static bool block(qa_roq_decoder *decoder, vq_reader *reader, unsigned x, unsigned y, unsigned size,
                  uint16_t flags, qa_error *error) {
    unsigned action;
    uint8_t operand;
    if (!code(reader, &action, error))
        return false;
    switch (action) {
    case 0:
        return true;
    case 1: {
        if (!byte(reader, &operand, error))
            return false;
        int32_t mean_x = flags >> 8, mean_y = flags & 255;
        if (mean_x >= 128)
            mean_x -= 256;
        if (mean_y >= 128)
            mean_y -= 256;
        int32_t scale = decoder->width == decoder->height * 4 ? 2 : 1;
        int64_t reference =
            (decoder->index & 1) ? 0 : (int64_t)decoder->width * decoder->height * 4;
        int64_t sx = (int64_t)x + (8 - (operand >> 4) - mean_x) * scale;
        int64_t sy = (int64_t)y + (8 - (operand & 15) - mean_y) * scale;
        int64_t offset = reference + (sy * decoder->width + sx) * 4;
        size_t length = ((size_t)(size - 1) * decoder->width + size) * 4;
        if (offset < 0 || (uint64_t)offset > ROQ_FRAME_BYTES ||
            length > ROQ_FRAME_BYTES - (size_t)offset)
            return roq_fail(error, "RoQ motion exceeds physical image buffers");
        uint8_t *destination = front(decoder);
        const uint8_t *source = decoder->scratch->frames + offset;
        for (unsigned row = 0; row < size; ++row)
            for (unsigned pair = 0; pair < size * 4; pair += 8)
                memmove(destination + ((size_t)(y + row) * decoder->width + x) * 4 + pair,
                        source + (size_t)row * decoder->width * 4 + pair, 8);
        return true;
    }
    case 2:
        if (!byte(reader, &operand, error))
            return false;
        blit(decoder, size == 8 ? decoder->scratch->books.book8 : decoder->scratch->books.book4,
             operand, size, x, y);
        return true;
    case 3:
        for (unsigned quadrant = 0; quadrant < 4; ++quadrant) {
            unsigned nx = x + (quadrant & 1) * (size / 2), ny = y + (quadrant >> 1) * (size / 2);
            if (size == 8) {
                if (!block(decoder, reader, nx, ny, 4, flags, error))
                    return false;
            } else {
                if (!byte(reader, &operand, error))
                    return false;
                blit(decoder, decoder->scratch->books.book2, operand, 2, nx, ny);
            }
        }
        return true;
    }
    return roq_fail(error, "Invalid RoQ block code");
}
static bool read_info(qa_roq_decoder *decoder, qa_bytes bytes, qa_error *error) {
    if (decoder->index != -1) {
        if (decoder->index != 1)
            decoder->index = 0;
        return true;
    }
    if (bytes.size < 8)
        return roq_fail(error, "Truncated RoQ image dimensions");
    uint32_t width = qa_load_u16le(bytes.data), height = qa_load_u16le(bytes.data + 2);
    if (!width || !height || width % 8 || height % 8 || (uint64_t)width * height > 512u * 512u)
        return roq_fail(error, "Invalid RoQ image dimensions");
    decoder->width = width;
    decoder->height = height;
    decoder->index = 0;
    return true;
}
static bool read_frame(qa_roq_decoder *decoder, const roq_chunk *chunk, qa_roq_event *out,
                       qa_error *error) {
    if (!decoder->width || decoder->index < 0 || decoder->index == INT64_MAX)
        return roq_fail(error, "RoQ frame lacks valid image state");
    vq_reader reader = {.bytes = chunk->bytes};
    for (unsigned y = 0; y < decoder->height; y += 16)
        for (unsigned x = 0; x < decoder->width; x += 16)
            for (unsigned quadrant = 0; quadrant < 4; ++quadrant) {
                unsigned bx = x + (quadrant & 1) * 8, by = y + (quadrant >> 1) * 8;
                if (bx + 8 <= decoder->width && by + 8 <= decoder->height &&
                    !block(decoder, &reader, bx, by, 8, chunk->flags, error))
                    return false;
            }
    size_t length = (size_t)decoder->width * decoder->height * 4;
    uint8_t *pixels = front(decoder);
    *out = (qa_roq_event){
        .kind = QA_ROQ_FRAME,
        .data.frame = {.rgba = {pixels, length},
                       .index = (uint64_t)decoder->index,
                       .time_ms = (double)decoder->index * 1000 / decoder->rate,
                       .physical_offset = (size_t)(pixels - decoder->scratch->frames)}};
    if (!decoder->index)
        memcpy(decoder->scratch->frames + length, pixels, length);
    ++decoder->index;
    return true;
}
static bool read_audio(qa_roq_decoder *decoder, const roq_chunk *chunk, uint8_t channels,
                       qa_roq_event *out, qa_error *error) {
    size_t count = chunk->bytes.size, capacity = channels == 1 ? count * 2 : count;
    if (capacity > decoder->audio_capacity) {
        int16_t *grown = realloc(decoder->audio, capacity * sizeof(*grown));
        if (!grown) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ audio scratch");
            return false;
        }
        decoder->audio = grown;
        decoder->audio_capacity = capacity;
    }
    size_t frames;
    if (!qa_roq_audio_decode(chunk->bytes, count, chunk->flags,
                             channels == 1 ? QA_ROQ_MONO_TO_STEREO : QA_ROQ_STEREO_TO_STEREO, false,
                             decoder->audio, decoder->audio_capacity, &frames, error))
        return false;
    /* The original mono path publishes the first half of duplicated samples. */
    *out = (qa_roq_event){.kind = QA_ROQ_AUDIO,
                          .data.audio = {decoder->audio, count / channels, channels}};
    return true;
}
static bool dispatch(qa_roq_decoder *decoder, const qa_roq_decode_hooks *hooks, qa_roq_event *out,
                     qa_error *error) {
    if (decoder->unknown) {
        *out = (qa_roq_event){.kind = QA_ROQ_END};
        return true;
    }
    roq_chunk chunk;
    if (!roq_stream_next(&decoder->stream, &chunk, error))
        return false;
    if (chunk.ended) {
        *out = (qa_roq_event){.kind = QA_ROQ_END};
        return true;
    }
    qa_roq_event event = {.kind = QA_ROQ_METADATA};
    switch (chunk.id) {
    case 0x1001:
        if (!read_info(decoder, chunk.bytes, error))
            return false;
        if (hooks && hooks->info &&
            !hooks->info(hooks->context, decoder->width, decoder->height, error))
            return false;
        event = (qa_roq_event){.kind = QA_ROQ_INFO, .data.info = {decoder->width, decoder->height}};
        break;
    case 0x1002: {
        size_t consumed;
        if (!qa_roq_codebook_decode(&decoder->scratch->books, chunk.bytes, chunk.flags,
                                    QA_ROQ_BOOK_NORMAL, 4, (qa_bytes){0},
                                    decoder->stream.policy == QA_ROQ_COMPLETE, &consumed, error))
            return false;
        break;
    }
    case 0x1011:
        if (!read_frame(decoder, &chunk, &event, error))
            return false;
        break;
    case 0x1012:
    case 0x1013:
    case 0x1030:
        break;
    case 0x1020:
    case 0x1021:
        if (decoder->silent)
            break;
        if (chunk.id == 0x1021 && hooks && hooks->before_stereo &&
            !hooks->before_stereo(hooks->context, error))
            return false;
        if (!read_audio(decoder, &chunk, chunk.id == 0x1020 ? 1 : 2, &event, error))
            return false;
        break;
    default:
        if (decoder->stream.policy == QA_ROQ_COMPLETE)
            return roq_fail(error, "Unsupported RoQ chunk");
        event.kind = QA_ROQ_END;
        break;
    }
    if (event.kind == QA_ROQ_AUDIO && hooks && hooks->audio &&
        !hooks->audio(hooks->context, &event, error))
        return false;
    if (!roq_stream_complete(&decoder->stream, event.kind == QA_ROQ_END, error))
        return false;
    if (event.kind == QA_ROQ_END)
        decoder->unknown = decoder->stream.state.invalid ? 1 : 2;
    *out = event;
    return true;
}
bool qa_roq_decoder_chunk(qa_roq_decoder *decoder, const qa_roq_decode_hooks *hooks,
                          qa_roq_event *out, qa_error *error) {
    if (!decoder || !out || decoder->busy || decoder->faulted)
        return roq_fail(error, "RoQ decoder is not available for dispatch");
    decoder->busy = true;
    bool ok = dispatch(decoder, hooks, out, error);
    decoder->busy = false;
    decoder->faulted = !ok;
    return ok;
}
bool qa_roq_decoder_next(qa_roq_decoder *decoder, qa_roq_event *out, qa_error *error) {
    do {
        if (!qa_roq_decoder_chunk(decoder, NULL, out, error))
            return false;
    } while (out->kind == QA_ROQ_INFO || out->kind == QA_ROQ_METADATA);
    return true;
}
bool qa_roq_decoder_rewind(qa_roq_decoder *decoder, qa_error *error) {
    if (!decoder || decoder->busy)
        return roq_fail(error, "Cannot rewind an active RoQ decoder");
    if (!roq_stream_init(&decoder->stream, true, error)) {
        decoder->faulted = true;
        return false;
    }
    decoder->rate = qa_load_u16le(decoder->stream.state.header + 6);
    if (!decoder->rate)
        decoder->rate = 30;
    decoder->index = -1;
    decoder->unknown = 0;
    decoder->faulted = false;
    return true;
}
bool qa_roq_decoder_scratch_rebind_ready(const qa_roq_decoder *decoder,
    const qa_roq_scratch *scratch, qa_error *error) {
    if (!decoder || decoder->busy || decoder->stream.pending || !scratch ||
        !scratch->references || (scratch != decoder->scratch && scratch->references == SIZE_MAX))
        return roq_fail(error, "RoQ scratch adoption requires returned physical owners");
    return true;
}
void qa_roq_decoder_scratch_rebind(qa_roq_decoder *decoder, qa_roq_scratch *scratch) {
    if (scratch == decoder->scratch) return;
    qa_roq_scratch_retain(scratch);
    qa_roq_scratch_release(decoder->scratch);
    decoder->scratch = scratch;
    decoder->stream.file = scratch->file;
}
uint16_t qa_roq_decoder_rate(const qa_roq_decoder *decoder) { return decoder->rate; }
void qa_roq_decoder_dimensions(const qa_roq_decoder *decoder, uint32_t *width, uint32_t *height) {
    *width = decoder->width;
    *height = decoder->height;
}
bool qa_roq_decoder_in_packet(const qa_roq_decoder *decoder) {
    return decoder->stream.state.buffered_next;
}
bool qa_roq_decoder_invalid(const qa_roq_decoder *decoder) {
    return decoder->unknown || decoder->stream.state.invalid;
}
bool qa_roq_decoder_reset_after_run(const qa_roq_decoder *decoder) { return decoder->unknown == 2; }
bool qa_roq_decoder_view(const qa_roq_decoder *decoder, size_t offset, size_t length, qa_bytes *out,
                         qa_error *error) {
    if (!decoder || !out || offset > ROQ_FRAME_BYTES || length > ROQ_FRAME_BYTES - offset)
        return roq_fail(error, "RoQ upload exceeds physical image buffers");
    *out = (qa_bytes){decoder->scratch->frames + offset, length};
    return true;
}
bool qa_roq_decoder_capture(qa_roq_decoder *decoder, qa_roq_checkpoint *out, qa_error *error) {
    if (!decoder || !out || decoder->busy || decoder->faulted || decoder->stream.pending)
        return roq_fail(error, "Cannot checkpoint an active or failed RoQ decoder");
    qa_roq_checkpoint saved = {.input_size = qa_media_input_size(decoder->stream.input),
                               .stream = decoder->stream.state,
                               .width = decoder->width,
                               .height = decoder->height,
                               .rate = decoder->rate,
                               .next_frame = decoder->index,
                               .end_policy = decoder->stream.policy,
                               .unknown_chunk = decoder->unknown,
                               .silent = decoder->silent};
    if (!qa_media_input_digest(decoder->stream.input, &saved.content, error))
        return false;
    saved.scratch.data = malloc(ROQ_SAVED_BYTES);
    saved.scratch.size = ROQ_SAVED_BYTES;
    if (!saved.scratch.data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ checkpoint");
        return false;
    }
    uint8_t *bytes = saved.scratch.data;
    memcpy(bytes, decoder->scratch->file, ROQ_FILE_BYTES);
    bytes += ROQ_FILE_BYTES;
    memcpy(bytes, &decoder->scratch->books, sizeof(qa_roq_codebooks));
    bytes += sizeof(qa_roq_codebooks);
    memcpy(bytes, decoder->scratch->frames, ROQ_FRAME_BYTES);
    *out = saved;
    return true;
}
bool qa_roq_decoder_restore(qa_roq_decoder *decoder, const qa_roq_checkpoint *saved,
                            qa_error *error) {
    if (!decoder || !saved || decoder->busy ||
        saved->input_size != qa_media_input_size(decoder->stream.input) ||
        saved->end_policy != decoder->stream.policy || saved->silent != decoder->silent ||
        !saved->scratch.data || saved->scratch.size != ROQ_SAVED_BYTES || !saved->rate ||
        saved->unknown_chunk > 2 || saved->next_frame < -1 ||
        (saved->width == 0) != (saved->height == 0) || saved->width % 8 || saved->height % 8 ||
        (uint64_t)saved->width * saved->height > 512u * 512u ||
        (saved->next_frame >= 0 && !saved->width))
        return roq_fail(error, "Invalid RoQ decoder checkpoint");
    const qa_roq_stream_checkpoint *state = &saved->stream;
    if (state->position > saved->input_size || state->buffer_offset > saved->input_size ||
        state->buffered_length > ROQ_FILE_BYTES || state->chunk_offset > 65536 ||
        state->next_size > 0xffffff ||
        (!state->invalid && state->has_next && state->next_size > 65536) ||
        (state->buffered_next && (!state->has_next || state->invalid)))
        return roq_fail(error, "Invalid RoQ stream checkpoint");
    qa_sha256_digest digest;
    if (!qa_media_input_digest(decoder->stream.input, &digest, error))
        return false;
    if (!qa_sha256_equal(&digest, &saved->content))
        return roq_fail(error, "RoQ checkpoint source changed");
    const uint8_t *bytes = saved->scratch.data;
    memcpy(decoder->scratch->file, bytes, ROQ_FILE_BYTES);
    bytes += ROQ_FILE_BYTES;
    memcpy(&decoder->scratch->books, bytes, sizeof(qa_roq_codebooks));
    bytes += sizeof(qa_roq_codebooks);
    memcpy(decoder->scratch->frames, bytes, ROQ_FRAME_BYTES);
    decoder->stream.state = *state;
    decoder->stream.pending = false;
    decoder->width = saved->width;
    decoder->height = saved->height;
    decoder->rate = saved->rate;
    decoder->index = saved->next_frame;
    decoder->unknown = saved->unknown_chunk;
    decoder->faulted = false;
    return true;
}
void qa_roq_checkpoint_free(qa_roq_checkpoint *saved) {
    if (!saved)
        return;
    qa_buffer_free(&saved->scratch);
    memset(saved, 0, sizeof(*saved));
}

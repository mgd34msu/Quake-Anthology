#include "qa/media_save.h"
#include "qa/source_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t size = value->size;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating saved media buffer"); return false;
        }
    } else if (size && !value->data) return false;
    return qa_source_save_bytes(io, value->data, size);
}
static bool source_text(qa_source_save_io *io, char **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_buffer bytes = reading ? (qa_buffer){0} : (qa_buffer){(uint8_t *)*value, *value ? strlen(*value) : 0};
    if (!blob(io, &bytes)) { if (reading) qa_buffer_free(&bytes); return false; }
    if (!bytes.size || bytes.size == SIZE_MAX || memchr(bytes.data, 0, bytes.size)) {
        if (reading) qa_buffer_free(&bytes);
        return false;
    }
    if (reading) {
        char *text = realloc(bytes.data, bytes.size + 1);
        if (!text) { qa_buffer_free(&bytes); qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Retaining media source name"); return false; }
        text[bytes.size] = 0; *value = text;
    }
    return true;
}
static bool status(qa_source_save_io *io, qa_media_status *value)
{
    uint32_t field = *value;
    if (!qa_source_save_u32(io, &field) || field > QA_MEDIA_STOPPED) return false;
    *value = (qa_media_status)field; return true;
}
static bool frame(qa_source_save_io *io, qa_media_frame *value)
{
    return qa_source_save_u32(io, &value->width) && qa_source_save_u32(io, &value->height) &&
        qa_source_save_u64(io, &value->index) && qa_source_save_u64(io, &value->loop) &&
        qa_source_save_f64(io, &value->source_ms) && isfinite(value->source_ms) &&
        qa_source_save_f64(io, &value->presentation_ms) && isfinite(value->presentation_ms);
}
static bool picture(qa_source_save_io *io, qa_cin_picture_checkpoint *value)
{
    return qa_source_save_bool(io, &value->present) && qa_source_save_u64(io, &value->index) &&
        blob(io, &value->pixels) && (value->present || !value->pixels.size);
}
static bool cin(qa_source_save_io *io, qa_cin_playback_checkpoint *value)
{
    qa_cin_checkpoint *decoder = &value->decoder;
    return qa_source_save_bytes(io, decoder->content.bytes, sizeof(decoder->content.bytes)) &&
        qa_source_save_u64(io, &decoder->input_size) && qa_source_save_u64(io, &decoder->offset) &&
        qa_source_save_u64(io, &decoder->next_frame) && qa_source_save_bytes(io, decoder->palette, sizeof(decoder->palette)) &&
        qa_source_save_bool(io, &decoder->ended) && picture(io, &value->picture) && picture(io, &value->pending) &&
        qa_source_save_f64(io, &value->epoch_ms) && isfinite(value->epoch_ms) && qa_source_save_u64(io, &value->loop) &&
        status(io, &value->status) && qa_source_save_bool(io, &value->repeat) &&
        qa_source_save_bool(io, &value->hold) && qa_source_save_bool(io, &value->silent);
}
static bool roq(qa_source_save_io *io, qa_roq_playback_checkpoint *value)
{
    qa_roq_checkpoint *decoder = &value->decoder; qa_roq_stream_checkpoint *stream = &decoder->stream;
    uint32_t end_policy = decoder->end_policy; size_t physical = value->physical_offset;
    bool ok = qa_source_save_bytes(io, decoder->content.bytes, sizeof(decoder->content.bytes)) &&
        qa_source_save_u64(io, &decoder->input_size) && qa_source_save_u64(io, &stream->position) &&
        qa_source_save_u64(io, &stream->played) && qa_source_save_u64(io, &stream->buffer_offset) &&
        qa_source_save_u32(io, &stream->chunk_offset) && qa_source_save_u32(io, &stream->buffered_length) &&
        qa_source_save_u32(io, &stream->next_size) && qa_source_save_u16(io, &stream->next_id) &&
        qa_source_save_u16(io, &stream->next_flags) && qa_source_save_u16(io, &stream->packet_remaining) &&
        qa_source_save_bytes(io, stream->header, sizeof(stream->header)) && qa_source_save_bool(io, &stream->has_next) &&
        qa_source_save_bool(io, &stream->invalid) && qa_source_save_bool(io, &stream->retained_eof) &&
        qa_source_save_bool(io, &stream->buffered_next) && blob(io, &decoder->scratch) &&
        qa_source_save_u32(io, &decoder->width) && qa_source_save_u32(io, &decoder->height) &&
        qa_source_save_u16(io, &decoder->rate) && qa_source_save_i64(io, &decoder->next_frame) &&
        qa_source_save_u32(io, &end_policy) && end_policy <= QA_ROQ_CINEMATIC &&
        qa_source_save_u8(io, &decoder->unknown_chunk) && qa_source_save_bool(io, &decoder->silent) &&
        qa_source_save_u32(io, &value->epoch_ms) && qa_source_save_u32(io, &value->last_ms) &&
        qa_source_save_i64(io, &value->decoded_frames) && qa_source_save_u64(io, &value->source_sample) &&
        qa_source_save_u64(io, &value->loop) && status(io, &value->status) && frame(io, &value->frame) &&
        blob(io, &value->pixels) && qa_source_save_count(io, &physical, SIZE_MAX) &&
        qa_source_save_bool(io, &value->has_frame) && qa_source_save_bool(io, &value->pending_loop) &&
        qa_source_save_bool(io, &value->repeat) && qa_source_save_bool(io, &value->hold) &&
        qa_source_save_bool(io, &value->silent) && qa_source_save_bool(io, &value->shader);
    if (ok) { decoder->end_policy = (qa_roq_end_policy)end_policy; value->physical_offset = physical; }
    return ok;
}
static bool ogv(qa_source_save_io *io, qa_ogv_checkpoint *value)
{
    bool ok = qa_source_save_bytes(io, value->content.bytes, sizeof(value->content.bytes)) &&
        qa_source_save_f64(io, &value->epoch_ms) && isfinite(value->epoch_ms) &&
        qa_source_save_u64(io, &value->loop) && qa_source_save_u64(io, &value->next_index) &&
        qa_source_save_u64(io, &value->audio_position) && status(io, &value->status) &&
        frame(io, &value->frame) && blob(io, &value->pixels) && qa_source_save_bool(io, &value->has_frame) &&
        qa_source_save_bool(io, &value->has_audio) && qa_source_save_bool(io, &value->reset_audio) &&
        qa_source_save_bool(io, &value->repeat) && qa_source_save_bool(io, &value->hold) && qa_source_save_bool(io, &value->silent);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) value->frame.rgba = (qa_bytes){value->pixels.data, value->pixels.size};
    return ok;
}
static bool target(qa_source_save_io *io, const qa_media_checkpoint_refs *refs, qa_cinematic_target *value)
{
    uint32_t kind = value->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_CINEMATIC_MATERIAL) return false;
    value->kind = (qa_cinematic_target_kind)kind;
    if (kind == QA_CINEMATIC_SEAT) return qa_source_save_u32(io, &value->id.seat) && value->id.seat < 4;
    qa_buffer descriptor = {0}; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool ok = refs && (reading ? refs->material_decode != NULL : refs->material_encode != NULL);
    if (ok && !reading) ok = refs->material_encode(refs->context, value->id.material, &descriptor, io->error);
    if (ok) ok = blob(io, &descriptor) && descriptor.size;
    if (ok && reading) ok = refs->material_decode(refs->context, (qa_bytes){descriptor.data, descriptor.size}, &value->id.material, io->error);
    qa_buffer_free(&descriptor); return ok;
}
static bool fields(qa_source_save_io *io, const qa_media_checkpoint_refs *refs, qa_cinematic_checkpoint *value)
{
    uint8_t signature[4] = {'Q','A','M','C'}; uint32_t version = 2, format = value->format, audience = value->audio_audience.kind;
    bool ok = qa_source_save_bytes(io, signature, sizeof(signature)) && !memcmp(signature, "QAMC", 4) &&
        qa_source_save_u32(io, &version) && version == 2 && qa_source_save_u32(io, &format) && format <= QA_CINEMATIC_IMAGE;
    if (!ok) return false;
    value->format = (qa_cinematic_format)format;
    ok = source_text(io, &value->source) && target(io, refs, &value->target) &&
        qa_source_save_u32(io, &audience) && audience <= QA_CINEMATIC_AUDIO_WORLD &&
        qa_source_save_u32(io, &value->audio_audience.seat) &&
        (audience != QA_CINEMATIC_AUDIO_SEAT || value->audio_audience.seat < 4) &&
        qa_source_save_f64(io, &value->elapsed_ms) && isfinite(value->elapsed_ms) && value->elapsed_ms >= 0 &&
        status(io, &value->status) && status(io, &value->decoder_status) && qa_source_save_u64(io, &value->revision) &&
        qa_source_save_u64(io, &value->audio_loop) && qa_source_save_bool(io, &value->loop) &&
        qa_source_save_bool(io, &value->hold) && qa_source_save_bool(io, &value->silent) &&
        qa_source_save_bool(io, &value->paused) && qa_source_save_bool(io, &value->dirty) &&
        qa_source_save_bool(io, &value->completed) && qa_source_save_bool(io, &value->focus_paused) &&
        qa_source_save_bool(io, &value->audio_attached) && blob(io, &value->audio);
    if (!ok) return false;
    value->audio_audience.kind = (qa_cinematic_audio_kind)audience;
    switch (value->format) {
    case QA_CINEMATIC_CIN: return cin(io, &value->decoder.cin);
    case QA_CINEMATIC_ROQ: return roq(io, &value->decoder.roq);
    case QA_CINEMATIC_OGV: return ogv(io, &value->decoder.ogv);
    case QA_CINEMATIC_IMAGE: return qa_source_save_bytes(io, value->decoder.image.bytes, sizeof(value->decoder.image.bytes));
    }
    return false;
}
bool qa_cinematic_checkpoint_encode(const qa_cinematic_checkpoint *saved, const qa_media_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!saved || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing media checkpoint"); return false; }
    qa_source_save_io io = {0}; qa_cinematic_checkpoint copy = *saved;
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, refs, &copy) && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Invalid media checkpoint fields");
    qa_source_save_dispose(&io); return ok;
}
bool qa_cinematic_checkpoint_decode(qa_bytes bytes, const qa_media_checkpoint_refs *refs,
    qa_cinematic_checkpoint *out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing media checkpoint destination"); return false; }
    qa_source_save_io io = {0}; qa_cinematic_checkpoint saved = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, refs, &saved) && qa_source_save_finish(&io, NULL);
    if (ok) *out = saved;
    else {
        qa_cinematic_checkpoint_free(&saved);
        if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Invalid portable media checkpoint");
    }
    qa_source_save_dispose(&io); return ok;
}

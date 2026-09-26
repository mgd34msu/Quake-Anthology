#include "roq_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_roq_playback {
    qa_roq_decoder *decoder;
    qa_roq_scratch *scratch;
    qa_roq_playback_options options;
    qa_media_clock clock;
    qa_media_status status;
    qa_media_frame frame;
    uint8_t *published, *resampled;
    size_t published_capacity, physical_offset;
    uint32_t epoch, last;
    int64_t decoded;
    uint64_t source_sample, loop;
    bool has_frame, pending_loop, busy, faulted;
};
static bool clock_sample(qa_media_clock clock, double *out, qa_error *error) {
    if (!clock.sample)
        return roq_fail(error, "Missing cinematic clock");
    double value = clock.sample(clock.context);
    if (!isfinite(value) || value < 0 || (double)(float)value >= 2147483648.0)
        return roq_fail(error, "Cinematic clock exceeds nonnegative signed milliseconds");
    *out = value;
    return true;
}
static bool reset(qa_roq_playback *playback, qa_media_clock clock, bool loop, qa_error *error) {
    double now;
    if (!clock_sample(clock, &now, error))
        return false;
    if (loop && playback->loop == UINT64_MAX)
        return roq_fail(error, "RoQ loop counter overflow");
    if (!qa_roq_decoder_rewind(playback->decoder, error))
        return false;
    playback->epoch = (uint32_t)now;
    playback->last = playback->epoch;
    playback->status = QA_MEDIA_PLAYING;
    playback->pending_loop = true;
    playback->decoded = -1;
    playback->source_sample = 0;
    if (loop)
        ++playback->loop;
    else
        playback->loop = 0;
    playback->faulted = false;
    return true;
}
bool qa_roq_playback_create(qa_media_input *input, const qa_roq_playback_options *options,
                            qa_media_clock clock, qa_roq_playback **out, qa_error *error) {
    if (!input || !options || !out || (!options->silent && !options->audio))
        return roq_fail(error, "Invalid RoQ playback configuration");
    double now;
    if (!clock_sample(clock, &now, error))
        return false;
    qa_roq_playback *playback = calloc(1, sizeof(*playback));
    if (!playback) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ playback");
        return false;
    }
    playback->options = *options;
    playback->scratch = options->scratch;
    if (playback->scratch)
        qa_roq_scratch_retain(playback->scratch);
    else if (!qa_roq_scratch_create(&playback->scratch, error)) {
        free(playback);
        return false;
    }
    qa_roq_scratch_clear(playback->scratch, false);
    qa_roq_decoder_options decoder_options = {
        .end_policy = QA_ROQ_CINEMATIC, .silent = options->silent, .scratch = playback->scratch};
    if (!qa_roq_decoder_create(input, &decoder_options, &playback->decoder, error)) {
        qa_roq_playback_destroy(playback);
        return false;
    }
    playback->epoch = (uint32_t)now;
    playback->last = playback->epoch;
    playback->decoded = -1;
    playback->status = QA_MEDIA_PLAYING;
    *out = playback;
    return true;
}
void qa_roq_playback_destroy(qa_roq_playback *playback) {
    if (!playback || playback->busy)
        return;
    qa_roq_decoder_destroy(playback->decoder);
    qa_roq_scratch_release(playback->scratch);
    free(playback->published);
    free(playback->resampled);
    free(playback);
}
static bool before_stereo(void *context, qa_error *error) {
    qa_roq_playback *playback = context;
    return playback->decoded != -1 || !playback->options.before_audio_reset ||
           playback->options.before_audio_reset(playback->options.context, error);
}
static bool info(void *context, uint32_t width, uint32_t height, qa_error *error) {
    qa_roq_playback *playback = context;
    if (playback->decoded == -1) {
        if (playback->options.info &&
            !playback->options.info(playback->options.context, width, height, error))
            return false;
        double now;
        if (!clock_sample(playback->clock, &now, error))
            return false;
        playback->epoch = (uint32_t)now;
        playback->last = playback->epoch;
    }
    if (playback->decoded != 1)
        playback->decoded = 0;
    return true;
}
static bool audio(void *context, const qa_roq_event *event, qa_error *error) {
    qa_roq_playback *playback = context;
    if (playback->source_sample > UINT64_MAX - event->data.audio.frames)
        return roq_fail(error, "RoQ sample counter overflow");
    double source_ms = (double)playback->source_sample * 1000 / 22050;
    qa_media_audio sound = {
        .pcm = {(const uint8_t *)event->data.audio.samples,
                event->data.audio.frames * event->data.audio.channels * sizeof(int16_t)},
        .rate = 22050,
        .native_pcm = true,
        .channels = event->data.audio.channels,
        .sample_bytes = 2,
        .source_sample = playback->source_sample,
        .loop = playback->loop,
        .source_ms = source_ms,
        .presentation_ms = playback->epoch + source_ms,
        .reset = event->data.audio.channels == 2 && playback->decoded == -1};
    if (!playback->options.audio(playback->options.context, &sound, error))
        return false;
    playback->source_sample += event->data.audio.frames;
    return true;
}
static bool target_frames(qa_roq_playback *playback, int32_t *out, qa_error *error) {
    double now;
    if (!clock_sample(playback->clock, &now, error))
        return false;
    *out = (int32_t)(float)((now - playback->epoch) * 3 / 100);
    return true;
}
static bool publish(qa_roq_playback *playback, const qa_roq_event *event, qa_error *error) {
    if (playback->decoded == INT64_MAX)
        return roq_fail(error, "RoQ frame counter overflow");
    size_t bytes = event->data.frame.rgba.size;
    if (playback->options.scratch && bytes > playback->published_capacity) {
        uint8_t *grown = realloc(playback->published, bytes);
        if (!grown) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ published frame");
            return false;
        }
        playback->published = grown;
        playback->published_capacity = bytes;
    }
    ++playback->decoded;
    qa_bytes pixels = event->data.frame.rgba;
    /* Only the legacy shared-scratch policy needs a publication copy. */
    if (playback->options.scratch) {
        memcpy(playback->published, pixels.data, bytes);
        pixels = (qa_bytes){playback->published, bytes};
    }
    uint32_t width, height;
    qa_roq_decoder_dimensions(playback->decoder, &width, &height);
    playback->frame = (qa_media_frame){.rgba = pixels,
                                       .width = width,
                                       .height = height,
                                       .index = event->data.frame.index,
                                       .loop = playback->loop,
                                       .source_ms = event->data.frame.time_ms,
                                       .presentation_ms =
                                           playback->epoch + (double)playback->decoded * 1000 / 30};
    playback->physical_offset = event->data.frame.physical_offset;
    playback->has_frame = true;
    return !playback->options.frame ||
           playback->options.frame(playback->options.context, &playback->frame,
                                   playback->physical_offset, error);
}
static bool run(qa_roq_playback *playback, qa_media_tick *out, qa_error *error) {
    if (playback->status == QA_MEDIA_HELD || playback->status == QA_MEDIA_ENDED) {
        *out =
            (qa_media_tick){.status = playback->status, .frame = qa_roq_playback_frame(playback)};
        return true;
    }
    double now;
    if (!clock_sample(playback->clock, &now, error))
        return false;
    uint32_t this_time = (uint32_t)(float)now;
    uint32_t gap_bits = this_time - playback->last;
    int64_t gap = gap_bits > INT32_MAX ? (int64_t)gap_bits - INT64_C(4294967296) : gap_bits;
    if (playback->options.shader && (gap < -100 || gap > 100))
        playback->epoch += gap_bits;
    int32_t target;
    if (!target_frames(playback, &target, error))
        return false;
    uint32_t epoch = playback->epoch;
    bool changed = false, looped = false;
    qa_roq_decode_hooks hooks = {
        .context = playback, .before_stereo = before_stereo, .info = info, .audio = audio};
    while (playback->status == QA_MEDIA_PLAYING && !playback->pending_loop &&
           (target != playback->decoded || qa_roq_decoder_in_packet(playback->decoder) ||
            qa_roq_decoder_invalid(playback->decoder))) {
        bool invalid = qa_roq_decoder_invalid(playback->decoder);
        qa_roq_event event;
        if (!qa_roq_decoder_chunk(playback->decoder, &hooks, &event, error))
            return false;
        if (event.kind == QA_ROQ_FRAME) {
            if (!publish(playback, &event, error))
                return false;
            changed = true;
        }
        if (!invalid && qa_roq_decoder_invalid(playback->decoder) &&
            !qa_roq_decoder_reset_after_run(playback->decoder) && playback->options.diagnostic)
            playback->options.diagnostic(playback->options.context,
                                         "roq_size>65536||roq_id==0x1084\n");
        if (event.kind == QA_ROQ_END) {
            if (playback->options.hold && !qa_roq_decoder_invalid(playback->decoder))
                playback->status = QA_MEDIA_HELD;
            else if (playback->options.loop && !qa_roq_decoder_reset_after_run(playback->decoder)) {
                if (!reset(playback, playback->clock, true, error))
                    return false;
                looped = true;
            } else
                playback->status = QA_MEDIA_ENDED;
        }
        if (!qa_roq_decoder_in_packet(playback->decoder) && epoch != playback->epoch) {
            if (!target_frames(playback, &target, error))
                return false;
            epoch = playback->epoch;
        }
    }
    playback->last = this_time;
    if (playback->pending_loop) {
        playback->pending_loop = false;
        looped = true;
    }
    if (playback->status == QA_MEDIA_ENDED && playback->options.loop) {
        if (!reset(playback, playback->clock, true, error))
            return false;
        looped = true;
    }
    *out = (qa_media_tick){.status = playback->status,
                           .frame = qa_roq_playback_frame(playback),
                           .changed = changed,
                           .looped = looped};
    return true;
}
bool qa_roq_playback_tick(qa_roq_playback *playback, qa_media_clock clock, qa_media_tick *out,
                          qa_error *error) {
    if (!playback || !out || playback->busy || playback->faulted)
        return roq_fail(error, "RoQ playback is not available");
    playback->busy = true;
    playback->clock = clock;
    bool ok = run(playback, out, error);
    playback->busy = false;
    playback->clock = (qa_media_clock){0};
    playback->faulted = !ok;
    return ok;
}
bool qa_roq_playback_restart(qa_roq_playback *playback, qa_media_clock clock, bool full,
                             qa_error *error) {
    if (!playback || playback->busy)
        return roq_fail(error, "Cannot restart active RoQ playback");
    playback->busy = true;
    bool ok = reset(playback, clock, false, error);
    if (ok && full) {
        /* Reset reads its header into file scratch, so retain that header when
         * clearing the movie buffers and codebooks. */
        memset(&playback->scratch->books, 0, sizeof(playback->scratch->books));
        memset(playback->scratch->frames, 0, sizeof(playback->scratch->frames));
        memset(playback->scratch->file + 16, 0, ROQ_FILE_BYTES - 16);
        playback->has_frame = false;
        playback->frame = (qa_media_frame){0};
        playback->pending_loop = false;
    }
    playback->busy = false;
    playback->faulted = !ok;
    return ok;
}
const qa_media_frame *qa_roq_playback_frame(const qa_roq_playback *playback) {
    return playback->has_frame ? &playback->frame : NULL;
}
bool qa_roq_playback_image(qa_roq_playback *playback, bool shader, uint32_t draw_width,
                           uint32_t draw_height, bool dirty, qa_media_frame *out, qa_error *error) {
    if (!playback || !out || !playback->has_frame || !draw_width || !draw_height)
        return roq_fail(error, "Missing RoQ image or destination dimensions");
    qa_media_frame image = playback->frame;
    if (shader) {
        image.width = 256;
        image.height = 256;
        if (!qa_roq_decoder_view(playback->decoder, playback->physical_offset, 256u * 256u * 4u,
                                 &image.rgba, error))
            return false;
    } else if (!dirty || (image.width == draw_width && image.height == draw_height)) {
        uint64_t pixels = (uint64_t)draw_width * draw_height;
        if (pixels > ROQ_FRAME_BYTES / 4)
            return roq_fail(error, "RoQ upload dimensions exceed physical storage");
        if (!qa_roq_decoder_view(playback->decoder, playback->physical_offset, (size_t)pixels * 4,
                                 &image.rgba, error))
            return false;
        image.width = draw_width;
        image.height = draw_height;
    } else {
        qa_bytes pixels;
        if (!qa_roq_decoder_view(playback->decoder, playback->physical_offset, 512u * 512u * 4u,
                                 &pixels, error))
            return false;
        if (!playback->resampled)
            playback->resampled = malloc(256u * 256u * 4u);
        if (!playback->resampled) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ upload scratch");
            return false;
        }
        unsigned xm = image.width / 256, ym = image.height / 256,
                 shift = image.width == 512 ? 9 : 8;
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned x = 0; x < 256; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    size_t offset;
                    unsigned value;
                    if (xm == 2 && ym == 2) {
                        offset = ((size_t)y << 12) + x * 8 + c;
                        value = (pixels.data[offset] + pixels.data[offset + 4] +
                                 pixels.data[offset + 2048] + pixels.data[offset + 2052]) >>
                                2;
                    } else if (xm == 2 && ym == 1) {
                        offset = ((size_t)y << 11) + x * 8 + c;
                        value = (pixels.data[offset] + pixels.data[offset + 4]) >> 1;
                    } else {
                        offset = ((((size_t)y * ym) << shift) + x * xm) * 4 + c;
                        if (offset >= pixels.size)
                            return roq_fail(error, "RoQ resampling exceeds image storage");
                        value = pixels.data[offset];
                    }
                    playback->resampled[((size_t)y * 256 + x) * 4 + c] = (uint8_t)value;
                }
        image.width = 256;
        image.height = 256;
        image.rgba = (qa_bytes){playback->resampled, 256u * 256u * 4u};
    }
    *out = image;
    return true;
}
bool qa_roq_playback_capture(qa_roq_playback *playback, qa_roq_playback_checkpoint *out,
                             qa_error *error) {
    if (!playback || !out || playback->busy || playback->faulted)
        return roq_fail(error, "Cannot checkpoint active RoQ playback");
    qa_roq_playback_checkpoint saved = {.epoch_ms = playback->epoch,
                                        .last_ms = playback->last,
                                        .decoded_frames = playback->decoded,
                                        .source_sample = playback->source_sample,
                                        .loop = playback->loop,
                                        .status = playback->status,
                                        .frame = playback->frame,
                                        .physical_offset = playback->physical_offset,
                                        .has_frame = playback->has_frame,
                                        .pending_loop = playback->pending_loop,
                                        .repeat = playback->options.loop,
                                        .hold = playback->options.hold,
                                        .silent = playback->options.silent,
                                        .shader = playback->options.shader};
    if (!qa_roq_decoder_capture(playback->decoder, &saved.decoder, error))
        return false;
    if (playback->has_frame) {
        saved.pixels.size = playback->frame.rgba.size;
        saved.pixels.data = malloc(saved.pixels.size);
        if (!saved.pixels.data) {
            qa_roq_checkpoint_free(&saved.decoder);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating RoQ saved frame");
            return false;
        }
        memcpy(saved.pixels.data, playback->frame.rgba.data, saved.pixels.size);
    }
    saved.frame.rgba = (qa_bytes){0};
    *out = saved;
    return true;
}
bool qa_roq_playback_restore(qa_roq_playback *playback, const qa_roq_playback_checkpoint *saved,
                             qa_error *error) {
    if (!playback || !saved || playback->busy || saved->decoded_frames < -1 ||
        (saved->status != QA_MEDIA_PLAYING && saved->status != QA_MEDIA_HELD &&
         saved->status != QA_MEDIA_ENDED) ||
        saved->repeat != playback->options.loop || saved->hold != playback->options.hold ||
        saved->silent != playback->options.silent || saved->shader != playback->options.shader)
        return roq_fail(error, "Invalid RoQ playback checkpoint");
    uint8_t *replacement = NULL;
    if (saved->has_frame) {
        uint64_t pixels = (uint64_t)saved->frame.width * saved->frame.height;
        if (!saved->frame.width || !saved->frame.height || pixels > ROQ_FRAME_BYTES / 8)
            return roq_fail(error, "Invalid RoQ saved dimensions");
        size_t bytes = (size_t)pixels * 4;
        if (bytes != saved->pixels.size || !saved->pixels.data ||
            saved->physical_offset > ROQ_FRAME_BYTES - bytes || !isfinite(saved->frame.source_ms) ||
            !isfinite(saved->frame.presentation_ms))
            return roq_fail(error, "Invalid RoQ saved image");
        replacement = malloc(saved->pixels.size);
        if (!replacement) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring RoQ frame");
            return false;
        }
        memcpy(replacement, saved->pixels.data, saved->pixels.size);
    } else if (saved->pixels.size)
        return roq_fail(error, "RoQ saved pixels lack a frame");
    if (!qa_roq_decoder_restore(playback->decoder, &saved->decoder, error)) {
        free(replacement);
        return false;
    }
    free(playback->published);
    playback->published = replacement;
    playback->published_capacity = saved->has_frame ? saved->pixels.size : 0;
    playback->epoch = saved->epoch_ms;
    playback->last = saved->last_ms;
    playback->decoded = saved->decoded_frames;
    playback->source_sample = saved->source_sample;
    playback->loop = saved->loop;
    playback->status = saved->status;
    playback->frame = saved->frame;
    playback->frame.rgba = (qa_bytes){replacement, playback->published_capacity};
    playback->physical_offset = saved->physical_offset;
    playback->has_frame = saved->has_frame;
    playback->pending_loop = saved->pending_loop;
    playback->faulted = false;
    return true;
}
void qa_roq_playback_checkpoint_free(qa_roq_playback_checkpoint *saved) {
    if (!saved)
        return;
    qa_roq_checkpoint_free(&saved->decoder);
    qa_buffer_free(&saved->pixels);
    memset(saved, 0, sizeof(*saved));
}

#include "roq_internal.h"
#include "cinematic_internal.h"
#include "qa/arena.h"

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
    qa_arena storage;
    uint8_t *published, *resampled;
    size_t physical_offset;
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
    double now = 0;
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
    size_t published_bytes = options->scratch ? ROQ_FRAME_BYTES / 2 : 0;
    size_t resampled_bytes = 256u * 256u * 4u;
    qa_arena_init(&playback->storage, published_bytes + resampled_bytes);
    if (published_bytes)
        playback->published = qa_arena_alloc(&playback->storage, published_bytes, 1, error);
    playback->resampled = qa_arena_alloc(&playback->storage, resampled_bytes, 1, error);
    if ((published_bytes && !playback->published) || !playback->resampled) {
        qa_roq_playback_destroy(playback);
        return false;
    }
    qa_arena_seal(&playback->storage);
    playback->scratch = options->scratch;
    if (playback->scratch)
        qa_roq_scratch_retain(playback->scratch);
    else if (!qa_roq_scratch_create(&playback->scratch, error)) {
        qa_roq_playback_destroy(playback);
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
    qa_arena_destroy(&playback->storage);
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
static double resume_clock(void *context) { return *(const double *)context; }
static bool resume_picture_audio(void *context, const qa_media_audio *sound, qa_error *error) {
    (void)context; (void)sound; (void)error;
    return true;
}
static bool same_prefix(const qa_roq_prefix_cursor *a, const qa_roq_prefix_cursor *b) {
    return a->position == b->position && a->played == b->played &&
        a->buffer_offset == b->buffer_offset && a->chunk_offset == b->chunk_offset &&
        a->buffered_length == b->buffered_length && a->packet_remaining == b->packet_remaining &&
        a->terminal == b->terminal && a->has_next == b->has_next && a->invalid == b->invalid &&
        a->retained_eof == b->retained_eof && a->buffered_next == b->buffered_next;
}
bool cinematic_roq_resume(qa_roq_playback *playback, const qa_cinematic_checkpoint *checkpoint,
    double *elapsed_ms, qa_media_status *status, qa_error *error) {
    double now = 0;
    qa_media_clock clock = {&now, resume_clock};
    if (!qa_roq_playback_restart(playback, clock, true, error)) return false;
    bool exact = checkpoint->progress_kind == QA_CINEMATIC_PROGRESS_ROQ;
    const qa_cinematic_roq_progress *progress = &checkpoint->progress.roq;
    bool retained_picture = exact && progress->picture.present && progress->decoded <= 0 &&
        progress->picture.loop >= checkpoint->loop;
    qa_roq_playback_options options = playback->options;
    if (retained_picture) {
        playback->options.audio = resume_picture_audio;
        playback->options.before_audio_reset = NULL;
    }
    if (!exact) playback->loop = checkpoint->loop;
    playback->busy = true;
    playback->clock = clock;
    double wanted = trunc((double)(float)(checkpoint->source_ms * 3 / 100)) - 1;
    qa_roq_decode_hooks hooks = {
        .context = playback, .before_stereo = before_stereo, .info = info, .audio = audio};
    bool ok = true;
    for (;;) {
        if (retained_picture) {
            if (playback->has_frame && playback->frame.loop == progress->picture.loop &&
                playback->frame.index == progress->picture.index) {
                /* Explicit restart also retains its preceding picture while
                 * resetting the pass number and decoded prefix. */
                if (!reset(playback, clock, false, error)) { ok = false; break; }
                playback->pending_loop = false;
                playback->options = options;
                retained_picture = false;
                continue;
            }
        } else if (exact) {
            qa_roq_prefix_cursor cursor;
            cinematic_roq_decoder_cursor(playback->decoder, &cursor);
            if (playback->loop == checkpoint->loop && same_prefix(&cursor, &progress->cursor) &&
                playback->decoded == progress->decoded && playback->source_sample == progress->source_sample)
                break;
        } else if (playback->status != QA_MEDIA_PLAYING ||
            ((double)playback->decoded > wanted && !qa_roq_decoder_in_packet(playback->decoder)))
            break;
        qa_roq_event event;
        if (!qa_roq_decoder_chunk(playback->decoder, &hooks, &event, error)) { ok = false; break; }
        if (event.kind == QA_ROQ_FRAME && !publish(playback, &event, error)) { ok = false; break; }
        if (event.kind != QA_ROQ_END) continue;
        if (exact) {
            qa_roq_prefix_cursor cursor;
            cinematic_roq_decoder_cursor(playback->decoder, &cursor);
            if (!retained_picture && playback->loop == checkpoint->loop && same_prefix(&cursor, &progress->cursor) &&
                playback->decoded == progress->decoded && playback->source_sample == progress->source_sample)
                break;
            uint64_t wanted_loop = retained_picture ? progress->picture.loop : checkpoint->loop;
            if (playback->loop < wanted_loop) {
                /* Rewind retains the preceding pass's books and reference
                 * pictures, just as the ordinary loop boundary does. */
                if (!reset(playback, clock, true, error)) { ok = false; break; }
                playback->pending_loop = false;
                continue;
            }
            ok = roq_fail(error, "Saved RoQ prefix is not present in its resource");
            break;
        }
        playback->status = playback->options.hold && !qa_roq_decoder_invalid(playback->decoder) ?
            QA_MEDIA_HELD : QA_MEDIA_ENDED;
    }
    if (ok) {
        *elapsed_ms = exact ? progress->clock_ms : checkpoint->source_ms;
        double checked = 0;
        ok = clock_sample((qa_media_clock){elapsed_ms, resume_clock}, &checked, error);
        if (ok && exact && playback->has_frame != progress->picture.present)
            ok = roq_fail(error, "Saved RoQ picture is not present at its prefix");
        if (ok && exact) {
            uint32_t anchor = (uint32_t)(float)checked;
            playback->epoch = anchor - progress->epoch_back_ms;
            playback->last = anchor - progress->last_back_ms;
            playback->pending_loop = progress->pending_loop;
            playback->status = checkpoint->status == QA_MEDIA_HELD || checkpoint->status == QA_MEDIA_ENDED ?
                checkpoint->status : QA_MEDIA_PLAYING;
            if (playback->has_frame) {
                playback->frame.index = progress->picture.index;
                playback->frame.loop = progress->picture.loop;
                playback->frame.source_ms = progress->picture.source_ms;
                playback->frame.presentation_ms = progress->picture.presentation_ms;
            }
            *status = checkpoint->status;
        } else if (ok) {
            playback->last = (uint32_t)(float)checked;
            playback->pending_loop = false;
            *status = playback->status;
        }
    }
    playback->options = options;
    playback->clock = (qa_media_clock){0};
    playback->busy = false;
    playback->faulted = !ok;
    return ok;
}
void cinematic_roq_position(const qa_roq_playback *playback, double elapsed_ms,
    qa_cinematic_checkpoint *out) {
    out->source_ms = fmax(0, elapsed_ms - playback->epoch);
    out->loop = playback->loop;
    out->progress_kind = QA_CINEMATIC_PROGRESS_ROQ;
    qa_cinematic_roq_progress *progress = &out->progress.roq;
    cinematic_roq_decoder_cursor(playback->decoder, &progress->cursor);
    uint32_t anchor = (uint32_t)(float)elapsed_ms;
    progress->clock_ms = elapsed_ms;
    progress->epoch_back_ms = anchor - playback->epoch;
    progress->last_back_ms = anchor - playback->last;
    progress->decoded = playback->decoded;
    progress->source_sample = playback->source_sample;
    progress->pending_loop = playback->pending_loop;
    progress->picture = (qa_cinematic_picture_progress){.present = playback->has_frame};
    if (playback->has_frame) {
        progress->picture.index = playback->frame.index;
        progress->picture.loop = playback->frame.loop;
        progress->picture.source_ms = playback->frame.source_ms;
        progress->picture.presentation_ms = playback->frame.presentation_ms;
    }
}
const qa_media_frame *qa_roq_playback_frame(const qa_roq_playback *playback) {
    return playback->has_frame ? &playback->frame : NULL;
}
bool qa_roq_playback_scratch_rebind_ready(const qa_roq_playback *playback,
    const qa_roq_scratch *scratch, qa_error *error) {
    if (!playback || playback->busy || !playback->options.scratch || !scratch ||
        (scratch != playback->scratch && scratch->references > SIZE_MAX - 2u))
        return roq_fail(error, "RoQ scratch adoption requires an explicit returned playback owner");
    return qa_roq_decoder_scratch_rebind_ready(playback->decoder, scratch, error);
}
void qa_roq_playback_scratch_rebind(qa_roq_playback *playback, qa_roq_scratch *scratch) {
    if (scratch == playback->scratch) return;
    qa_roq_scratch_retain(scratch);
    qa_roq_decoder_scratch_rebind(playback->decoder, scratch);
    qa_roq_scratch_release(playback->scratch);
    playback->scratch = scratch;
    playback->options.scratch = scratch;
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
        unsigned xm = image.width / 256, ym = image.height / 256,
                 shift = image.width == 512 ? 9 : 8;
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned x = 0; x < 256; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    size_t offset;
                    unsigned value;
                    if (xm == 2 && ym == 2) {
                        offset = ((size_t)y << 12) + x * 8 + c;
                        value = ((unsigned)pixels.data[offset] + pixels.data[offset + 4] +
                                 pixels.data[offset + 2048] + pixels.data[offset + 2052]) >>
                                2;
                    } else if (xm == 2 && ym == 1) {
                        offset = ((size_t)y << 11) + x * 8 + c;
                        value = ((unsigned)pixels.data[offset] + pixels.data[offset + 4]) >> 1;
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

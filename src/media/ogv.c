#include "ogv_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static once_flag color_once = ONCE_FLAG_INIT;
static struct color_table {
    double y[256], red[256], green_u[256], green_v[256], blue[256];
} color;
static void color_init(void) {
    for (unsigned i = 0; i < 256; ++i) {
        double chroma = (double)i - 128;
        color.y[i] = 1.1643835616438356 * ((double)i - 16);
        color.red[i] = 1.5960267857142858 * chroma;
        color.green_u[i] = 0.39176229009491365 * chroma;
        color.green_v[i] = 0.8129676472377708 * chroma;
        color.blue[i] = 2.017232142857143 * chroma;
    }
}
static uint8_t color_byte(double value) {
    if (value <= 0)
        return 0;
    if (value >= 255)
        return 255;
    return (uint8_t)floor(value + 0.5);
}

static bool picture(qa_ogv_playback *playback, qa_error *error) {
    th_ycbcr_buffer planes;
    if (th_decode_ycbcr_out(playback->decoder, planes) != 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Theora output planes are unavailable");
        return false;
    }
    const th_info *info = &playback->asset->video_info;
    unsigned shift_x = info->pixel_fmt == TH_PF_444 ? 0 : 1;
    unsigned shift_y = info->pixel_fmt == TH_PF_420 ? 1 : 0;
    for (unsigned i = 0; i < 3; ++i) {
        int64_t stride = planes[i].stride;
        uint64_t magnitude = (uint64_t)(stride < 0 ? -stride : stride);
        if (!planes[i].data || planes[i].width <= 0 || planes[i].height <= 0 ||
            magnitude < (uint64_t)planes[i].width ||
            magnitude * (uint64_t)planes[i].height > UINT64_C(128) * 1024 * 1024) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Theora output plane");
            return false;
        }
    }
    uint32_t right = info->pic_x + info->pic_width - 1;
    uint32_t bottom = info->pic_y + info->pic_height - 1;
    if (right >= (uint32_t)planes[0].width || bottom >= (uint32_t)planes[0].height ||
        (right >> shift_x) >= (uint32_t)planes[1].width ||
        (bottom >> shift_y) >= (uint32_t)planes[1].height || planes[1].width != planes[2].width ||
        planes[1].height != planes[2].height) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Theora crop exceeds decoded planes");
        return false;
    }
    call_once(&color_once, color_init);
    for (uint32_t y = 0; y < info->pic_height; ++y) {
        uint32_t source_y = info->pic_y + y;
        const uint8_t *luma = planes[0].data + (ptrdiff_t)source_y * planes[0].stride;
        const uint8_t *cb = planes[1].data + (ptrdiff_t)(source_y >> shift_y) * planes[1].stride;
        const uint8_t *cr = planes[2].data + (ptrdiff_t)(source_y >> shift_y) * planes[2].stride;
        uint8_t *output = playback->pixels + (size_t)y * info->pic_width * 4;
        for (uint32_t x = 0; x < info->pic_width; ++x) {
            uint32_t source_x = info->pic_x + x;
            double yy = color.y[luma[source_x]];
            uint8_t u = cb[source_x >> shift_x], v = cr[source_x >> shift_x];
            output[0] = color_byte(yy + color.red[v]);
            output[1] = color_byte(yy - color.green_u[u] - color.green_v[v]);
            output[2] = color_byte(yy + color.blue[u]);
            output[3] = 255;
            output += 4;
        }
    }
    return true;
}

static bool header_info(qa_ogv_asset *asset, qa_error *error) {
    th_comment comments;
    th_comment_init(&comments);
    bool success = false;
    for (size_t i = 0; i < 3; ++i) {
        ogg_packet packet = qa_ogv_native_packet(&asset->movie, i);
        int result = th_decode_headerin(&asset->video_info, &comments, &asset->setup, &packet);
        if (result <= 0) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Invalid Theora header: %d", result);
            goto done;
        }
    }
    const th_info *info = &asset->video_info;
    if (!info->pic_width || !info->pic_height ||
        (uint64_t)info->frame_width * info->frame_height > UINT32_C(0x1000000) ||
        info->pic_x > info->frame_width || info->pic_width > info->frame_width - info->pic_x ||
        info->pic_y > info->frame_height || info->pic_height > info->frame_height - info->pic_y ||
        !info->fps_numerator || !info->fps_denominator ||
        (info->pixel_fmt != TH_PF_420 && info->pixel_fmt != TH_PF_422 &&
         info->pixel_fmt != TH_PF_444)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "Unsupported Theora dimensions, rate, or pixel format");
        goto done;
    }
    double frame_ms = (double)info->fps_denominator * 1000 / info->fps_numerator;
    if (frame_ms < 1 || frame_ms > 10000 || !asset->setup) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Invalid Theora setup or frame rate");
        goto done;
    }
    asset->info = (qa_ogv_info){.width = info->pic_width,
                                .height = info->pic_height,
                                .frames = asset->movie.packet_count - 3,
                                .frame_ms = frame_ms,
                                .has_audio = asset->movie.audio.size != 0};
    asset->rgba_bytes = (size_t)info->pic_width * info->pic_height * 4;
    success = true;
done:
    th_comment_clear(&comments);
    return success;
}

bool qa_ogv_asset_load(qa_media_input *input, qa_ogv_asset **out, qa_error *error) {
    if (!input || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing OGV asset input or output");
        return false;
    }
    qa_ogv_asset *asset = calloc(1, sizeof(*asset));
    if (!asset) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating OGV asset");
        return false;
    }
    asset->references = 1;
    th_info_init(&asset->video_info);
    if (!qa_ogv_demux(input, &asset->movie, error) || !header_info(asset, error)) {
        qa_ogv_asset_release(asset);
        return false;
    }
    *out = asset;
    return true;
}
void qa_ogv_asset_retain(qa_ogv_asset *asset) {
    if (asset)
        ++asset->references;
}
void qa_ogv_asset_release(qa_ogv_asset *asset) {
    if (!asset || --asset->references)
        return;
    th_setup_free(asset->setup);
    th_info_clear(&asset->video_info);
    qa_ogv_movie_free(&asset->movie);
    free(asset);
}
qa_ogv_info qa_ogv_asset_info(const qa_ogv_asset *asset) { return asset->info; }

static bool available(qa_ogv_playback *playback, bool require_valid, qa_error *error) {
    if (!playback || playback->dispatching || (require_valid && playback->faulted)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     !playback               ? "Missing OGV playback"
                     : playback->dispatching ? "OGV callback may not mutate playback"
                                             : "OGV playback requires restart or restore");
        return false;
    }
    return true;
}
static qa_ogv_playback *prepare(qa_ogv_asset *asset, const qa_ogv_options *options, double epoch,
                                qa_error *error) {
    if (!asset || !options || !isfinite(epoch)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid OGV playback options or clock");
        return NULL;
    }
    qa_ogv_playback *playback = calloc(1, sizeof(*playback));
    if (!playback) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating OGV playback");
        return NULL;
    }
    qa_ogv_asset_retain(asset);
    playback->asset = asset;
    playback->options = *options;
    playback->epoch_ms = epoch;
    playback->reset_audio = true;
    playback->status = QA_MEDIA_PLAYING;
    playback->decoder = th_decode_alloc(&asset->video_info, asset->setup);
    playback->pixels = malloc(asset->rgba_bytes);
    if (!playback->decoder || !playback->pixels) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Theora decoder or RGBA output");
        goto fail;
    }
    if (asset->info.has_audio && !options->silent) {
        if (!qa_audio_stream_open((qa_bytes){asset->movie.audio.data, asset->movie.audio.size},
                                  QA_WAV_FORMAT, &playback->audio, error))
            goto fail;
        unsigned channels = qa_audio_stream_channels(playback->audio);
        if (channels < 1 || channels > 2) {
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Unsupported OGV soundtrack channels");
            goto fail;
        }
        playback->pcm = malloc(QA_OGV_PCM_FRAMES * channels * sizeof(*playback->pcm));
        if (!playback->pcm) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating OGV soundtrack output");
            goto fail;
        }
    }
    return playback;
fail:
    qa_ogv_playback_destroy(playback);
    return NULL;
}
static bool decode_frame(qa_ogv_playback *playback, qa_error *error) {
    if (playback->next_index >= playback->asset->info.frames) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "OGV video cursor exceeds movie");
        return false;
    }
    double source_ms = (double)playback->next_index * playback->asset->info.frame_ms;
    double presentation_ms = playback->epoch_ms + source_ms;
    if (!isfinite(presentation_ms)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "OGV presentation clock overflow");
        return false;
    }
    ogg_packet packet =
        qa_ogv_native_packet(&playback->asset->movie, (size_t)playback->next_index + 3);
    int result = th_decode_packetin(playback->decoder, &packet, NULL);
    if (result < 0) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)playback->next_index,
                     "Invalid Theora frame: %d", result);
        return false;
    }
    if ((result != TH_DUPFRAME || !playback->has_frame) && !picture(playback, error))
        return false;
    playback->frame = (qa_media_frame){.rgba = {playback->pixels, playback->asset->rgba_bytes},
                                       .width = playback->asset->info.width,
                                       .height = playback->asset->info.height,
                                       .index = playback->next_index++,
                                       .loop = playback->loop,
                                       .source_ms = source_ms,
                                       .presentation_ms = presentation_ms};
    playback->has_frame = true;
    return true;
}
static bool queue_audio(qa_ogv_playback *playback, double elapsed, qa_error *error) {
    if (!playback->audio)
        return true;
    uint32_t rate = qa_audio_stream_rate(playback->audio);
    uint64_t total = qa_audio_stream_frames(playback->audio);
    double requested = ceil((elapsed + 200) * rate / 1000);
    uint64_t wanted = requested >= (double)total ? total : requested <= 0 ? 0 : (uint64_t)requested;
    while (qa_audio_stream_position(playback->audio) < wanted) {
        uint64_t source_sample = qa_audio_stream_position(playback->audio);
        uint64_t remaining = wanted - source_sample;
        size_t count = remaining < QA_OGV_PCM_FRAMES ? (size_t)remaining : QA_OGV_PCM_FRAMES;
        size_t frames;
        if (!qa_audio_stream_read(playback->audio, playback->pcm, count, &frames, error))
            return false;
        if (!frames) {
            qa_error_set(error, QA_ERROR_FORMAT, 0,
                         "OGV soundtrack ended before its declared cursor");
            return false;
        }
        unsigned channels = qa_audio_stream_channels(playback->audio);
        double source_ms = (double)source_sample * 1000 / rate;
        qa_media_audio audio = {
            .pcm = {(const uint8_t *)playback->pcm, frames * channels * sizeof(*playback->pcm)},
            .rate = rate,
            .channels = (uint8_t)channels,
            .sample_bytes = 2,
            .source_sample = source_sample,
            .loop = playback->loop,
            .source_ms = source_ms,
            .presentation_ms = playback->epoch_ms + source_ms,
            .reset = playback->reset_audio,
            .native_pcm = true};
        if (!isfinite(audio.presentation_ms)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "OGV audio clock overflow");
            return false;
        }
        if (playback->options.audio) {
            playback->dispatching = true;
            bool success = playback->options.audio(playback->options.context, &audio, error);
            playback->dispatching = false;
            if (!success)
                return false;
        }
        playback->reset_audio = false;
    }
    return true;
}

bool qa_ogv_playback_create(qa_ogv_asset *asset, const qa_ogv_options *options, double now,
                            qa_ogv_playback **out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing OGV playback output");
        return false;
    }
    qa_ogv_playback *playback = prepare(asset, options, now, error);
    if (!playback)
        return false;
    if (!decode_frame(playback, error) || !queue_audio(playback, 0, error)) {
        qa_ogv_playback_destroy(playback);
        return false;
    }
    *out = playback;
    return true;
}
void qa_ogv_playback_destroy(qa_ogv_playback *playback) {
    if (!playback || playback->dispatching)
        return;
    if (playback->decoder)
        th_decode_free(playback->decoder);
    qa_audio_stream_close(playback->audio);
    qa_ogv_asset_release(playback->asset);
    free(playback->pixels);
    free(playback->pcm);
    free(playback);
}
static bool rewind_decoder(qa_ogv_playback *playback, qa_error *error) {
    th_dec_ctx *next = th_decode_alloc(&playback->asset->video_info, playback->asset->setup);
    if (!next) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Restarting Theora decoder");
        return false;
    }
    if (playback->audio && !qa_audio_stream_seek(playback->audio, 0, error)) {
        th_decode_free(next);
        return false;
    }
    th_decode_free(playback->decoder);
    playback->decoder = next;
    playback->next_index = 0;
    playback->has_frame = false;
    playback->reset_audio = true;
    return true;
}
bool qa_ogv_playback_restart(qa_ogv_playback *playback, double now, qa_error *error) {
    if (!available(playback, false, error))
        return false;
    if (!isfinite(now)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid OGV restart clock");
        return false;
    }
    if (!rewind_decoder(playback, error)) {
        playback->faulted = true;
        return false;
    }
    playback->epoch_ms = now;
    playback->loop = 0;
    playback->status = QA_MEDIA_PLAYING;
    playback->faulted = false;
    if (!decode_frame(playback, error) || !queue_audio(playback, 0, error)) {
        playback->faulted = true;
        return false;
    }
    return true;
}
bool qa_ogv_playback_tick(qa_ogv_playback *playback, double now, qa_media_tick *out,
                          qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing OGV tick output");
        return false;
    }
    if (!available(playback, true, error))
        return false;
    qa_media_tick result = {.status = playback->status, .frame = qa_ogv_playback_frame(playback)};
    if (playback->status != QA_MEDIA_PLAYING) {
        *out = result;
        return true;
    }
    double elapsed = now - playback->epoch_ms;
    if (!isfinite(now) || !isfinite(elapsed) || elapsed < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "OGV clock must be finite and monotonic");
        return false;
    }
    const qa_ogv_info *info = &playback->asset->info;
    double duration = (double)info->frames * info->frame_ms;
    if (elapsed >= duration) {
        if (playback->options.hold) {
            while (playback->next_index < info->frames)
                if (!decode_frame(playback, error))
                    goto fault;
            playback->status = QA_MEDIA_HELD;
            result.status = QA_MEDIA_HELD;
            result.frame = qa_ogv_playback_frame(playback);
            result.changed = true;
            *out = result;
            return true;
        }
        if (!playback->options.loop) {
            playback->status = QA_MEDIA_ENDED;
            result.status = QA_MEDIA_ENDED;
            *out = result;
            return true;
        }
        double count = floor(elapsed / duration);
        if (!isfinite(count) || count >= 0x1p64 || (uint64_t)count > UINT64_MAX - playback->loop) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "OGV loop counter overflow");
            return false;
        }
        if (!rewind_decoder(playback, error))
            goto fault;
        playback->loop += (uint64_t)count;
        elapsed = fmod(elapsed, duration);
        playback->epoch_ms = now - elapsed;
        result.looped = true;
    }
    double target_value = floor(elapsed / info->frame_ms);
    uint64_t target =
        target_value >= (double)info->frames ? info->frames - 1 : (uint64_t)target_value;
    while (playback->next_index <= target) {
        if (!decode_frame(playback, error))
            goto fault;
        result.changed = true;
    }
    if (!queue_audio(playback, elapsed, error))
        goto fault;
    result.frame = qa_ogv_playback_frame(playback);
    *out = result;
    return true;
fault:
    playback->faulted = true;
    return false;
}
const qa_media_frame *qa_ogv_playback_frame(const qa_ogv_playback *playback) {
    return playback && playback->has_frame ? &playback->frame : NULL;
}

bool qa_ogv_playback_capture(qa_ogv_playback *playback, qa_ogv_checkpoint *out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing OGV checkpoint output");
        return false;
    }
    if (!available(playback, true, error))
        return false;
    qa_ogv_checkpoint checkpoint = {.epoch_ms = playback->epoch_ms,
                                    .loop = playback->loop,
                                    .next_index = playback->next_index,
                                    .audio_position = qa_audio_stream_position(playback->audio),
                                    .status = playback->status,
                                    .has_frame = playback->has_frame,
                                    .has_audio = playback->audio != NULL,
                                    .reset_audio = playback->reset_audio,
                                    .repeat = playback->options.loop,
                                    .hold = playback->options.hold,
                                    .silent = playback->options.silent};
    if (playback->has_frame) {
        checkpoint.pixels.data = malloc(playback->asset->rgba_bytes);
        if (!checkpoint.pixels.data) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing OGV frame");
            return false;
        }
        checkpoint.pixels.size = playback->asset->rgba_bytes;
        memcpy(checkpoint.pixels.data, playback->pixels, checkpoint.pixels.size);
        checkpoint.frame = playback->frame;
        checkpoint.frame.rgba = (qa_bytes){checkpoint.pixels.data, checkpoint.pixels.size};
    }
    *out = checkpoint;
    return true;
}
static bool checkpoint_valid(const qa_ogv_playback *playback, const qa_ogv_checkpoint *checkpoint,
                             qa_error *error) {
    const qa_ogv_info *info = &playback->asset->info;
    bool audio = info->has_audio && !playback->options.silent;
    bool valid = checkpoint &&
                 isfinite(checkpoint->epoch_ms) && checkpoint->next_index <= info->frames &&
                 checkpoint->has_frame == (checkpoint->next_index != 0) &&
                 checkpoint->has_audio == audio && (audio || checkpoint->audio_position == 0) &&
                 (!audio || checkpoint->reset_audio == (checkpoint->audio_position == 0)) &&
                 checkpoint->repeat == playback->options.loop &&
                 checkpoint->hold == playback->options.hold &&
                 checkpoint->silent == playback->options.silent &&
                 (!checkpoint->loop || (checkpoint->repeat && !checkpoint->hold)) &&
                 (checkpoint->status == QA_MEDIA_PLAYING || checkpoint->status == QA_MEDIA_ENDED ||
                  (checkpoint->status == QA_MEDIA_HELD && checkpoint->hold &&
                   checkpoint->next_index == info->frames));
    if (valid && checkpoint->has_frame) {
        const qa_media_frame *frame = &checkpoint->frame;
        double source_ms = (double)(checkpoint->next_index - 1) * info->frame_ms;
        valid = frame->width == info->width && frame->height == info->height &&
                frame->index == checkpoint->next_index - 1 && frame->loop == checkpoint->loop &&
                frame->source_ms == source_ms && isfinite(frame->presentation_ms) &&
                frame->presentation_ms == checkpoint->epoch_ms + source_ms &&
                checkpoint->pixels.data && checkpoint->pixels.size == playback->asset->rgba_bytes &&
                frame->rgba.size == checkpoint->pixels.size;
    } else if (valid)
        valid = checkpoint->pixels.size == 0 && checkpoint->frame.rgba.size == 0;
    if (!valid)
        qa_error_set(error, QA_ERROR_FORMAT, 0,
                     "OGV checkpoint content, policy, timing, or frame ownership differs");
    return valid;
}
bool qa_ogv_playback_restore(qa_ogv_playback *playback, const qa_ogv_checkpoint *checkpoint,
                             qa_error *error) {
    if (!available(playback, false, error) || !checkpoint_valid(playback, checkpoint, error))
        return false;
    qa_ogv_playback *next =
        prepare(playback->asset, &playback->options, checkpoint->epoch_ms, error);
    if (!next)
        return false;
    next->loop = checkpoint->loop;
    while (next->next_index < checkpoint->next_index)
        if (!decode_frame(next, error))
            goto fail;
    if (next->has_frame &&
        memcmp(next->pixels, checkpoint->pixels.data, next->asset->rgba_bytes) != 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0,
                     "Reconstructed Theora frame differs from checkpoint");
        goto fail;
    }
    if (next->audio) {
        if (checkpoint->audio_position > qa_audio_stream_frames(next->audio)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0,
                         "OGV checkpoint audio cursor exceeds soundtrack");
            goto fail;
        }
        while (qa_audio_stream_position(next->audio) < checkpoint->audio_position) {
            uint64_t remaining = checkpoint->audio_position - qa_audio_stream_position(next->audio);
            size_t count = remaining < QA_OGV_PCM_FRAMES ? (size_t)remaining : QA_OGV_PCM_FRAMES;
            size_t frames;
            if (!qa_audio_stream_read(next->audio, next->pcm, count, &frames, error))
                goto fail;
            if (!frames) {
                qa_error_set(error, QA_ERROR_FORMAT, 0,
                             "OGV checkpoint soundtrack prefix ended early");
                goto fail;
            }
        }
    }
    next->reset_audio = checkpoint->reset_audio;
    next->status = checkpoint->status;
    qa_ogv_playback previous = *playback;
    *playback = *next;
    *next = previous;
    qa_ogv_playback_destroy(next);
    return true;
fail:
    qa_ogv_playback_destroy(next);
    return false;
}
void qa_ogv_checkpoint_free(qa_ogv_checkpoint *checkpoint) {
    if (!checkpoint)
        return;
    qa_buffer_free(&checkpoint->pixels);
    *checkpoint = (qa_ogv_checkpoint){0};
}

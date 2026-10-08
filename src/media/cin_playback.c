#include "qa/media.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct cin_picture {
    uint8_t *pixels;
    uint64_t index;
    uint8_t palette[768];
    bool present;
} cin_picture;
struct qa_cin_playback {
    qa_cin_decoder *decoder;
    qa_cin_info info;
    qa_cin_playback_options options;
    uint8_t *storage;
    size_t pixels;
    cin_picture picture, pending;
    qa_media_frame frame;
    double epoch_ms;
    uint64_t loop;
    qa_media_status status;
    bool faulted, busy, image_dirty;
};

static bool fail(qa_error *error, const char *text) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
static bool clock_valid(double now) { return isfinite(now) && now >= 0; }
static void picture_reset(qa_cin_playback *player) {
    player->picture = (cin_picture){.pixels = player->storage};
    player->pending = (cin_picture){.pixels = player->storage + player->pixels};
    player->image_dirty = true;
}
static bool read_picture(qa_cin_playback *player, cin_picture *picture, qa_error *error) {
    qa_cin_frame frame;
    if (!qa_cin_decoder_next(player->decoder, &frame, error)) { player->faulted = true; return false; }
    picture->present = !frame.ended;
    if (frame.ended) return true;
    picture->index = frame.index;
    memcpy(picture->pixels, frame.pixels, player->pixels);
    memcpy(picture->palette, frame.palette, sizeof(picture->palette));
    player->image_dirty = true;
    if (frame.info.channels && !player->options.silent) {
        double source_ms = (double)frame.source_sample * 1000 / frame.info.sample_rate;
        qa_media_audio audio = {.pcm = frame.audio, .rate = frame.info.sample_rate,
            .channels = frame.info.channels, .sample_bytes = frame.info.sample_bytes,
            .source_sample = frame.source_sample, .loop = player->loop,
            .source_ms = source_ms, .presentation_ms = player->epoch_ms + source_ms,
            .reset = frame.source_sample == 0};
        if (!player->options.audio(player->options.context, &audio, error)) { player->faulted = true; return false; }
    }
    return true;
}

bool qa_cin_playback_create(qa_cin_asset *asset, const qa_cin_playback_options *options,
                            double now, qa_cin_playback **out, qa_error *error) {
    if (!asset || !options || !out || !clock_valid(now)) return fail(error, "Invalid CIN playback configuration");
    qa_cin_info info = qa_cin_asset_info(asset);
    if (info.channels && !options->silent && !options->audio) return fail(error, "CIN soundtrack has no output consumer");
    qa_cin_playback *player = calloc(1, sizeof(*player));
    if (!player) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CIN playback"); return false; }
    player->info = info; player->pixels = (size_t)info.width * info.height;
    player->storage = malloc(player->pixels * 6);
    if (!player->storage || !qa_cin_decoder_create(asset, &player->decoder, error)) {
        if (!player->storage) qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CIN presentation buffers");
        qa_cin_playback_destroy(player); return false;
    }
    player->options = *options;
    if (!qa_cin_playback_restart(player, now, error)) { qa_cin_playback_destroy(player); return false; }
    *out = player; return true;
}
void qa_cin_playback_destroy(qa_cin_playback *player) {
    if (!player || player->busy) return;
    qa_cin_decoder_destroy(player->decoder); free(player->storage); free(player);
}
bool qa_cin_playback_restart(qa_cin_playback *player, double now, qa_error *error) {
    if (!player || player->busy || !clock_valid(now)) return fail(error, "Invalid CIN restart or active callback");
    player->busy = true;
    qa_cin_decoder_rewind(player->decoder); picture_reset(player);
    player->epoch_ms = trunc(now); player->status = QA_MEDIA_PLAYING; player->faulted = false;
    bool ok = read_picture(player, &player->picture, error);
    if (ok && !player->picture.present) player->status = QA_MEDIA_ENDED;
    player->busy = false; return ok;
}
const qa_media_frame *qa_cin_playback_frame(qa_cin_playback *player) {
    if (!player || !player->picture.present) return NULL;
    uint8_t *rgba = player->storage + player->pixels * 2;
    if (player->image_dirty) {
        qa_error unused;
        if (!qa_cin_rgba((qa_bytes){player->picture.pixels, player->pixels},
            (qa_bytes){player->picture.palette, sizeof(player->picture.palette)},
            rgba, player->pixels * 4, &unused)) return NULL;
        player->image_dirty = false;
    }
    double source = (double)player->picture.index * 1000 / 14;
    player->frame = (qa_media_frame){.rgba = {rgba, player->pixels * 4},
        .width = player->info.width, .height = player->info.height, .index = player->picture.index,
        .loop = player->loop, .source_ms = source, .presentation_ms = player->epoch_ms + source};
    return &player->frame;
}
bool qa_cin_playback_tick(qa_cin_playback *player, double now, bool game_focus, qa_media_tick *out, qa_error *error) {
    if (!player || !out || player->busy || player->faulted || !clock_valid(now)) return fail(error, "Invalid CIN tick or faulted playback");
    bool changed = false, looped = false;
    if (player->status == QA_MEDIA_PLAYING) {
        now = trunc(now);
        uint64_t decoded = qa_cin_decoder_index(player->decoder);
        if (!game_focus) player->epoch_ms = now - trunc((double)decoded * 1000 / 14);
        else {
            double desired = trunc((now - player->epoch_ms) * 14 / 1000);
            if (desired > (double)decoded) {
                player->busy = true;
                if (desired > (double)decoded + 1) {
                    if (player->options.dropped_frame) {
                        uint64_t requested = desired >= 18446744073709551616.0 ? UINT64_MAX : (uint64_t)desired;
                        player->options.dropped_frame(player->options.context, requested, decoded);
                    }
                    player->epoch_ms = now - trunc((double)decoded * 1000 / 14);
                }
                cin_picture previous = player->picture;
                player->picture = player->pending; player->pending = previous;
                player->image_dirty = true;
                bool ok = read_picture(player, &player->pending, error);
                player->busy = false;
                if (!ok) return false;
                if (!player->pending.present) {
                    if (player->options.hold) {
                        if (!player->picture.present) player->picture = previous;
                        player->status = QA_MEDIA_HELD;
                    } else if (player->options.loop) {
                        if (player->loop == UINT64_MAX) { player->faulted = true; return fail(error, "CIN loop counter overflow"); }
                        ++player->loop;
                        if (!qa_cin_playback_restart(player, now, error)) return false;
                        looped = true;
                    } else { player->picture.present = false; player->status = QA_MEDIA_ENDED; }
                }
                changed = true;
            }
        }
    }
    *out = (qa_media_tick){.status = player->status, .frame = qa_cin_playback_frame(player), .changed = changed, .looped = looped};
    return true;
}

#include "qa/audio.h"
#include "qa/binary.h"
#include <float.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MUSIC_CHUNK_FRAMES = 16384, MUSIC_REMAP_TRACKS = 100 };

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "Music checkpoints require binary32 floats");

struct qa_audio_music {
    qa_audio_stream *stream, *loop;
    qa_audio_raw_stream *pcm;
    uint32_t output_rate;
    qa_audio_family family;
    float target_volume, smoothed_volume;
    uint64_t completions, request;
    unsigned cd_track;
    uint8_t remap[MUSIC_REMAP_TRACKS];
    bool source_volume, paused, enabled, reset_pcm;
    int16_t scratch[MUSIC_CHUNK_FRAMES * 2];
};

bool qa_audio_music_checkpoint(const qa_audio_music *music, qa_buffer *out, qa_error *error) {
    if (!music || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music checkpoint requires its live owner"); return false;
    }
    qa_buffer parts[3] = {0};
    bool same = music->stream && music->stream == music->loop;
    bool ok = (!music->stream || qa_audio_stream_checkpoint(music->stream, &parts[0], error)) &&
        (!music->loop || same || qa_audio_stream_checkpoint(music->loop, &parts[1], error)) &&
        qa_audio_raw_checkpoint(music->pcm, &parts[2], error);
    size_t size = 80 + MUSIC_REMAP_TRACKS;
    for (size_t i = 0; ok && i < 3; ++i) {
        if (parts[i].size > SIZE_MAX - size) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Music checkpoint extent overflows storage"); ok = false;
        } else size += parts[i].size;
    }
    qa_buffer buffer = {0};
    if (ok) {
        buffer = (qa_buffer){.data = calloc(1, size), .size = size};
        if (!buffer.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining music checkpoint"); ok = false; }
    }
    if (ok) {
        uint8_t *data = buffer.data;
        memcpy(data, "QAMU", 4); qa_store_u32le(data + 4, 1);
        qa_store_u32le(data + 8, music->output_rate); qa_store_u32le(data + 12, music->family);
        uint32_t flags = music->source_volume | music->paused << 1 | music->enabled << 2 |
            music->reset_pcm << 3 | (music->stream != NULL) << 4 | (music->loop != NULL) << 5 | same << 6;
        qa_store_u32le(data + 16, flags);
        uint32_t bits; memcpy(&bits, &music->target_volume, sizeof(bits)); qa_store_u32le(data + 24, bits);
        memcpy(&bits, &music->smoothed_volume, sizeof(bits)); qa_store_u32le(data + 28, bits);
        qa_store_u64le(data + 32, music->completions); qa_store_u64le(data + 40, music->request);
        qa_store_u32le(data + 48, music->cd_track);
        memcpy(data + 80, music->remap, MUSIC_REMAP_TRACKS);
        size_t offset = 80 + MUSIC_REMAP_TRACKS;
        for (size_t i = 0; i < 3; ++i) {
            qa_store_u64le(data + 56 + i * 8, parts[i].size);
            if (parts[i].size) memcpy(data + offset, parts[i].data, parts[i].size);
            offset += parts[i].size;
        }
        *out = buffer;
    }
    for (size_t i = 0; i < 3; ++i) qa_buffer_free(&parts[i]);
    return ok;
}

bool qa_audio_music_restore(qa_bytes bytes, qa_audio_music **out, qa_error *error) {
    const size_t header = 80 + MUSIC_REMAP_TRACKS;
    if (!out || !bytes.data || bytes.size < header || memcmp(bytes.data, "QAMU", 4) ||
        qa_load_u32le(bytes.data + 4) != 1 || qa_load_u32le(bytes.data + 20) ||
        qa_load_u32le(bytes.data + 52)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid music checkpoint header"); return false;
    }
    const uint8_t *data = bytes.data;
    uint32_t rate = qa_load_u32le(data + 8), family = qa_load_u32le(data + 12);
    uint32_t flags = qa_load_u32le(data + 16), cd = qa_load_u32le(data + 48);
    bool stream_present = (flags & 16) != 0, loop_present = (flags & 32) != 0, same = (flags & 64) != 0;
    float target = qa_load_f32le(data + 24), smooth = qa_load_f32le(data + 28);
    if (!rate || family > QA_AUDIO_Q3 || (flags & ~127u) || cd > 255 ||
        !isfinite(target) || target < 0 || !isfinite(smooth) || smooth < 0 ||
        (same && (!stream_present || !loop_present))) {
        qa_error_set(error, QA_ERROR_FORMAT, 8, "Invalid music checkpoint state"); return false;
    }
    qa_bytes parts[3]; size_t offset = header;
    for (size_t i = 0; i < 3; ++i) {
        uint64_t size = qa_load_u64le(data + 56 + i * 8);
        if (size > bytes.size - offset) {
            qa_error_set(error, QA_ERROR_FORMAT, offset, "Truncated music checkpoint record"); return false;
        }
        parts[i] = (qa_bytes){data + offset, (size_t)size}; offset += (size_t)size;
    }
    if (offset != bytes.size || stream_present != (parts[0].size != 0) ||
        (loop_present && !same) != (parts[1].size != 0) || !parts[2].size) {
        qa_error_set(error, QA_ERROR_FORMAT, offset, "Music checkpoint record set differs"); return false;
    }
    qa_audio_music *music = NULL;
    if (!qa_audio_music_create(rate, (qa_audio_family)family, (flags & 1) != 0, &music, error)) return false;
    qa_audio_raw_stream *pcm = NULL;
    bool ok = (!stream_present || qa_audio_stream_restore(parts[0], &music->stream, error)) &&
        (!loop_present || same || qa_audio_stream_restore(parts[1], &music->loop, error)) &&
        qa_audio_raw_restore(parts[2], rate, &pcm, error);
    if (same) music->loop = music->stream;
    if (!ok) { qa_audio_raw_destroy(pcm); qa_audio_music_destroy(music); return false; }
    qa_audio_raw_destroy(music->pcm); music->pcm = pcm;
    music->target_volume = target; music->smoothed_volume = smooth;
    music->completions = qa_load_u64le(data + 32); music->request = qa_load_u64le(data + 40);
    music->cd_track = cd; memcpy(music->remap, data + 80, MUSIC_REMAP_TRACKS);
    music->paused = (flags & 2) != 0; music->enabled = (flags & 4) != 0; music->reset_pcm = (flags & 8) != 0;
    *out = music; return true;
}

static void music_identity_remap(qa_audio_music *music) {
    for (unsigned track = 0; track < MUSIC_REMAP_TRACKS; ++track)
        music->remap[track] = (uint8_t)track;
}

uint32_t qa_audio_music_rate(const qa_audio_music *music) {
    return music != NULL ? music->output_rate : 0;
}

static void music_clear_pcm(qa_audio_music *music) {
    /* Empty reset cannot allocate or fail for this already validated format. */
    (void)qa_audio_raw_queue(music->pcm, NULL, 0, 2, music->output_rate, 0, true, NULL);
    music->reset_pcm = true;
}

bool qa_audio_music_create(uint32_t rate, qa_audio_family family, bool source_volume,
                           qa_audio_music **out, qa_error *error) {
    if (rate == 0 || out == NULL ||
        (family != QA_AUDIO_Q1 && family != QA_AUDIO_Q2 && family != QA_AUDIO_Q3)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid music format or destination");
        return false;
    }
    qa_audio_music *music = calloc(1, sizeof(*music));
    if (music == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating music player");
        return false;
    }
    if (!qa_audio_raw_create(rate, &music->pcm, error)) {
        free(music);
        return false;
    }
    music->output_rate = rate;
    music->family = family;
    music->source_volume = source_volume;
    music->target_volume = 0.25f;
    music->smoothed_volume = 0.5f;
    music->enabled = true;
    music->reset_pcm = true;
    music_identity_remap(music);
    *out = music;
    return true;
}

void qa_audio_music_stop(qa_audio_music *music) {
    if (music == NULL)
        return;
    qa_audio_stream *stream = music->stream, *loop = music->loop;
    music->stream = music->loop = NULL;
    music->cd_track = 0;
    ++music->request;
    if (stream != NULL)
        qa_audio_stream_close(stream);
    if (loop != NULL && loop != stream)
        qa_audio_stream_close(loop);
    music_clear_pcm(music);
}

void qa_audio_music_destroy(qa_audio_music *music) {
    if (music == NULL)
        return;
    qa_audio_music_stop(music);
    qa_audio_raw_destroy(music->pcm);
    free(music);
}

void qa_audio_music_start(qa_audio_music *music, qa_audio_stream *intro, qa_audio_stream *loop) {
    if (music == NULL) {
        if (intro != NULL)
            qa_audio_stream_close(intro);
        if (loop != NULL && loop != intro)
            qa_audio_stream_close(loop);
        return;
    }
    qa_audio_stream *previous = music->stream, *previous_loop = music->loop;
    music->stream = intro;
    music->loop = loop;
    music->cd_track = 0;
    ++music->request;
    if (previous != NULL && previous != intro && previous != loop)
        qa_audio_stream_close(previous);
    if (previous_loop != NULL && previous_loop != previous && previous_loop != intro &&
        previous_loop != loop)
        qa_audio_stream_close(previous_loop);
    music_clear_pcm(music);
    music->paused = false;
    if (intro == NULL)
        qa_audio_music_stop(music);
}

void qa_audio_music_pause(qa_audio_music *music, bool paused) {
    if (music != NULL)
        music->paused = paused;
}

void qa_audio_music_enable(qa_audio_music *music, bool enabled) {
    if (music == NULL)
        return;
    if (music->enabled != enabled)
        ++music->request;
    music->enabled = enabled;
}

bool qa_audio_music_volume(qa_audio_music *music, float volume, qa_error *error) {
    if (music == NULL || !isfinite(volume) || volume < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid music volume");
        return false;
    }
    music->target_volume = volume;
    return true;
}

void qa_audio_music_update(qa_audio_music *music) {
    if (music == NULL || music->family != QA_AUDIO_Q3 || !music->source_volume ||
        music->stream == NULL || music->paused || !music->enabled)
        return;
    float twice = (float)(music->target_volume * 2.0f);
    float sum = (float)(music->smoothed_volume + twice);
    music->smoothed_volume = (float)(sum / 4.0f);
}

static bool music_read_chunk(qa_audio_music *music, size_t *frames, uint64_t *source_frame,
                             qa_error *error) {
    qa_audio_stream *stream = music->stream;
    uint32_t rate = qa_audio_stream_rate(stream);
    unsigned channels = qa_audio_stream_channels(stream);
    if (rate == 0 || (channels != 1 && channels != 2)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid music stream format");
        return false;
    }
    *source_frame = qa_audio_stream_position(stream);
    *frames = 0;
    return qa_audio_stream_read(stream, music->scratch, MUSIC_CHUNK_FRAMES, frames, error);
}

static bool music_refill(qa_audio_music *music, bool *available, qa_error *error) {
    *available = false;
    if (music->stream == NULL)
        return true;
    size_t frames;
    uint64_t source_frame;
    if (!music_read_chunk(music, &frames, &source_frame, error))
        goto fail;
    if (frames == 0) {
        if (music->loop == NULL) {
            qa_audio_stream *finished = music->stream;
            music->stream = NULL;
            music->cd_track = 0;
            if (music->completions != UINT64_MAX)
                ++music->completions;
            qa_audio_stream_close(finished);
            return true;
        }
        qa_audio_stream *finished = music->stream;
        music->stream = music->loop;
        if (finished != music->loop)
            qa_audio_stream_close(finished);
        if (!qa_audio_stream_seek(music->stream, 0, error) ||
            !music_read_chunk(music, &frames, &source_frame, error))
            goto fail;
        if (frames == 0) {
            qa_audio_music_stop(music);
            return true;
        }
        music->reset_pcm = true;
    }
    if (frames > MUSIC_CHUNK_FRAMES) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "music decoder returned too many frames");
        goto fail;
    }
    if (!qa_audio_raw_queue(
            music->pcm, music->scratch, frames, qa_audio_stream_channels(music->stream),
            qa_audio_stream_rate(music->stream), source_frame, music->reset_pcm, error))
        goto fail;
    music->reset_pcm = false;
    *available = true;
    return true;

fail:
    qa_audio_music_stop(music);
    return false;
}

bool qa_audio_music_mix(qa_audio_music *music, float *stereo, size_t frames, qa_error *error) {
    if (music == NULL || (stereo == NULL && frames != 0) ||
        frames > SIZE_MAX / sizeof(*stereo) / 2) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid music mix destination or extent");
        return false;
    }
    if (!music->enabled || music->paused || music->stream == NULL || frames == 0)
        return true;
    float volume = music->family == QA_AUDIO_Q3 && music->source_volume ? music->smoothed_volume
                                                                        : music->target_volume;
    if (volume <= 0)
        return true;
    if (!isfinite(volume)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "music volume exceeds finite mixing range");
        return false;
    }
    size_t mixed = 0;
    while (mixed < frames) {
        mixed += qa_audio_raw_mix(music->pcm, stereo + mixed * 2, frames - mixed, volume);
        if (mixed == frames)
            break;
        bool available;
        if (!music_refill(music, &available, error))
            return false;
        if (!available)
            break;
    }
    return true;
}

bool qa_audio_music_playing(const qa_audio_music *music) {
    return music != NULL && music->stream != NULL;
}

uint64_t qa_audio_music_completions(const qa_audio_music *music) {
    return music != NULL ? music->completions : 0;
}

bool qa_audio_music_remap(qa_audio_music *music, const uint8_t *tracks, size_t count,
                          qa_error *error) {
    if (music == NULL || count >= MUSIC_REMAP_TRACKS || (tracks == NULL && count != 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CD remap requires at most 99 tracks");
        return false;
    }
    if (count != 0)
        memcpy(music->remap + 1, tracks, count);
    return true;
}

void qa_audio_music_reset(qa_audio_music *music) {
    if (music == NULL)
        return;
    qa_audio_music_stop(music);
    music->enabled = true;
    music_identity_remap(music);
}

bool qa_audio_music_cd_play(qa_audio_music *music, unsigned track, bool loop,
                            qa_audio_open_track_fn open, void *user, qa_error *error) {
    if (music == NULL || open == NULL || track > 255) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid CD track request");
        return false;
    }
    qa_error_set(error, QA_OK, 0, NULL);
    if (!music->enabled)
        return false;
    unsigned mapped = track < MUSIC_REMAP_TRACKS ? music->remap[track] : track;
    if (mapped == 0)
        return false;
    if (music->cd_track == mapped && music->stream != NULL)
        return true;
    qa_audio_music_stop(music);
    uint64_t request = music->request;
    static const char *const patterns[] = {"music/%02u.ogg", "music/track%02u.ogg",
                                           "music/%02u.wav", "music/track%02u.wav"};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        char path[32];
        (void)snprintf(path, sizeof(path), patterns[i], mapped);
        qa_audio_stream *stream = NULL;
        qa_error open_error = {0};
        bool opened = open(user, path, &stream, &open_error);
        if (request != music->request || !music->enabled) {
            if (stream != NULL)
                qa_audio_stream_close(stream);
            return false;
        }
        if (!opened) {
            if (stream != NULL)
                qa_audio_stream_close(stream);
            if (open_error.code == QA_OK || open_error.code == QA_ERROR_NOT_FOUND)
                continue;
            if (error != NULL)
                *error = open_error;
            return false;
        }
        if (stream == NULL)
            continue;
        qa_audio_music_start(music, stream, loop ? stream : NULL);
        music->cd_track = mapped;
        return true;
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "CD track %u has no supported music file", mapped);
    return false;
}

static bool music_campaign_equal(const char *campaign, const char *expected) {
    if (campaign == NULL)
        return false;
    while (*campaign != '\0' && *expected != '\0') {
        unsigned char value = (unsigned char)*campaign++;
        if (value >= 'A' && value <= 'Z')
            value = (unsigned char)(value + ('a' - 'A'));
        if (value != (unsigned char)*expected++)
            return false;
    }
    return *campaign == '\0' && *expected == '\0';
}

unsigned qa_audio_music_q2_track(unsigned track, const char *campaign, bool remastered) {
    if (!remastered || track < 2 || track > 11)
        return track;
    if (music_campaign_equal(campaign, "rogue"))
        return track + 10;
    if (music_campaign_equal(campaign, "xatrix")) {
        static const uint8_t tracks[] = {9, 13, 14, 7, 16, 2, 15, 3, 4, 18};
        return tracks[track - 2];
    }
    return track;
}

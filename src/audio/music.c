#include "music_internal.h"
#include "qa/binary.h"
#include <float.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MUSIC_CHUNK_FRAMES = 16384, MUSIC_REMAP_TRACKS = 100 };
struct qa_audio_music_controls {
    size_t references;
    qa_audio_music *players;
    uint8_t remap[MUSIC_REMAP_TRACKS];
    bool enabled;
};

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "Music checkpoints require binary32 floats");

struct qa_audio_music {
    size_t references;
    qa_audio_stream *stream, *loop;
    qa_audio_raw_stream *pcm;
    uint32_t output_rate;
    qa_audio_family family;
    float target_volume, smoothed_volume;
    uint64_t completions, request;
    unsigned cd_track;
    qa_audio_music_controls *controls;
    qa_audio_music *controls_next;
    bool source_volume, paused, reset_pcm, external_controls;
    qa_audio_music_selection *selection;
    qa_audio_music_hold *holds;
    bool destroy_requested;
    int16_t scratch[MUSIC_CHUNK_FRAMES * 2];
};

struct qa_audio_music_selection {
    qa_audio_music *music;
    qa_audio_stream *intro, *loop;
    qa_audio_raw_stream *pcm;
    qa_audio_music_selection_kind kind;
    uint64_t request;
    unsigned cd_track;
    qa_audio_music_source_profile source;
    float target_volume;
    bool new_source;
    bool new_target;
    bool ready, invalidated, consuming;
};
struct qa_audio_music_hold {
    qa_audio_music *music;
    qa_audio_music_hold *next;
    qa_audio_music_selection *selection;
    bool consumed, invalidated;
};
static void music_invalidate(qa_audio_music *music) {
    if (music->selection) music->selection->invalidated = true;
    for (qa_audio_music_hold *h = music->holds; h; h = h->next) h->invalidated = true;
}
static bool music_mutation(qa_audio_music *music, qa_error *error) {
    if (!music || !music->controls || music->selection || music->holds || music->destroy_requested) {
        if (music) music_invalidate(music);
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music retains its prepared playback selection");
        return false;
    }
    return true;
}
static bool controls_mutation(qa_audio_music_controls *controls, qa_error *error) {
    bool current = controls != NULL;
    for (qa_audio_music *p = controls ? controls->players : NULL; p; p = p->controls_next) {
        if (p->selection || p->holds) music_invalidate(p);
        if (!qa_audio_music_idle(p)) current = false;
    }
    if (!current) qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Shared CD controls retain a prepared player operation");
    return current;
}

bool qa_audio_music_checkpoint(const qa_audio_music *music, qa_buffer *out, qa_error *error) {
    if (!music || !out || !qa_audio_music_idle(music)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music checkpoint requires its live owner"); return false;
    }
    qa_buffer parts[3] = {0};
    bool same = music->stream && music->stream == music->loop;
    bool ok = (!music->stream || qa_audio_stream_checkpoint(music->stream, &parts[0], error)) &&
        (!music->loop || same || qa_audio_stream_checkpoint(music->loop, &parts[1], error)) &&
        qa_audio_raw_checkpoint(music->pcm, &parts[2], error);
    size_t size = 76 + MUSIC_REMAP_TRACKS;
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
        memcpy(data, "QAMU", 4);
        qa_store_u32le(data + 4, music->output_rate); qa_store_u32le(data + 8, music->family);
        uint32_t flags = (uint32_t)(music->source_volume | music->paused << 1 |
            (!music->external_controls && music->controls->enabled) << 2 |
            music->reset_pcm << 3 | (music->stream != NULL) << 4 | (music->loop != NULL) << 5 | same << 6 |
            music->external_controls << 7);
        qa_store_u32le(data + 12, flags);
        uint32_t bits; memcpy(&bits, &music->target_volume, sizeof(bits)); qa_store_u32le(data + 20, bits);
        memcpy(&bits, &music->smoothed_volume, sizeof(bits)); qa_store_u32le(data + 24, bits);
        qa_store_u64le(data + 28, music->completions); qa_store_u64le(data + 36, music->request);
        qa_store_u32le(data + 44, music->cd_track);
        if (!music->external_controls) memcpy(data + 76, music->controls->remap, MUSIC_REMAP_TRACKS);
        size_t offset = 76 + MUSIC_REMAP_TRACKS;
        for (size_t i = 0; i < 3; ++i) {
            qa_store_u64le(data + 52 + i * 8, parts[i].size);
            if (parts[i].size) memcpy(data + offset, parts[i].data, parts[i].size);
            offset += parts[i].size;
        }
        *out = buffer;
    }
    for (size_t i = 0; i < 3; ++i) qa_buffer_free(&parts[i]);
    return ok;
}

bool qa_audio_music_restore(qa_bytes bytes, qa_audio_music **out, qa_error *error) {
    const size_t header = 76 + MUSIC_REMAP_TRACKS;
    if (!out || !bytes.data || bytes.size < header || memcmp(bytes.data, "QAMU", 4) ||
        qa_load_u32le(bytes.data + 16) ||
        qa_load_u32le(bytes.data + 48)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid music checkpoint header"); return false;
    }
    const uint8_t *data = bytes.data;
    uint32_t rate = qa_load_u32le(data + 4), family = qa_load_u32le(data + 8);
    uint32_t flags = qa_load_u32le(data + 12), cd = qa_load_u32le(data + 44);
    bool stream_present = (flags & 16) != 0, loop_present = (flags & 32) != 0, same = (flags & 64) != 0;
    float target = qa_load_f32le(data + 20), smooth = qa_load_f32le(data + 24);
    bool external = (flags & 128) != 0;
    bool bad_controls = external && (flags & 4);
    for (size_t i = 0; external && i < MUSIC_REMAP_TRACKS; ++i) bad_controls |= data[76 + i] != 0;
    if (!rate || family > QA_AUDIO_Q3 || (flags & ~255u) || bad_controls || cd > 255 ||
        !isfinite(target) || target < 0 || !isfinite(smooth) || smooth < 0 ||
        (same && (!stream_present || !loop_present))) {
        qa_error_set(error, QA_ERROR_FORMAT, 4, "Invalid music checkpoint state"); return false;
    }
    qa_bytes parts[3]; size_t offset = header;
    for (size_t i = 0; i < 3; ++i) {
        uint64_t size = qa_load_u64le(data + 52 + i * 8);
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
    music->completions = qa_load_u64le(data + 28); music->request = qa_load_u64le(data + 36);
    music->cd_track = cd;
    if (external) {
        qa_audio_music_controls *controls = music->controls;
        controls->players = NULL; music->controls = NULL; qa_audio_music_controls_release(controls);
        music->external_controls = true;
    } else {
        memcpy(music->controls->remap, data + 76, MUSIC_REMAP_TRACKS);
        music->controls->enabled = (flags & 4) != 0;
    }
    music->paused = (flags & 2) != 0; music->reset_pcm = (flags & 8) != 0;
    *out = music; return true;
}

static void music_identity_remap(qa_audio_music_controls *controls) {
    for (unsigned track = 0; track < MUSIC_REMAP_TRACKS; ++track)
        controls->remap[track] = (uint8_t)track;
}

uint32_t qa_audio_music_rate(const qa_audio_music *music) {
    return music != NULL ? music->output_rate : 0;
}

bool qa_audio_music_profile_is(const qa_audio_music *music, uint32_t rate,
                              qa_audio_family family, bool source_volume) {
    return music != NULL && music->output_rate == rate && music->family == family &&
        music->source_volume == source_volume;
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
    if (!qa_audio_raw_create(rate, &music->pcm, error) || !qa_audio_music_controls_create(&music->controls, error)) {
        qa_audio_raw_destroy(music->pcm);
        free(music);
        return false;
    }
    music->output_rate = rate;
    music->references = 1;
    music->family = family;
    music->source_volume = source_volume;
    music->target_volume = 0.25f;
    music->smoothed_volume = 0.5f;
    music->controls->players = music;
    music->reset_pcm = true;
    *out = music;
    return true;
}

void qa_audio_music_stop(qa_audio_music *music) {
    if (music == NULL || !music_mutation(music, NULL))
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
    if (music->selection || music->holds) {
        music_invalidate(music);
        music->destroy_requested = true;
        return;
    }
    music->destroy_requested = false;
    if (music->controls) qa_audio_music_stop(music);
    else {
        qa_audio_stream_close(music->stream); if (music->loop != music->stream) qa_audio_stream_close(music->loop);
        music->stream = music->loop = NULL;
    }
    qa_audio_music_release(music);
}
bool qa_audio_music_retain(qa_audio_music *music, qa_error *error) {
    if (!music || !music->references || music->references == SIZE_MAX || music->destroy_requested) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music ownership requires its retained actual player"); return false;
    }
    ++music->references; return true;
}
void qa_audio_music_release(qa_audio_music *music) {
    if (!music) return;
    if (music->references == 1 && (music->selection || music->holds)) {
        music_invalidate(music); music->destroy_requested = true; return;
    }
    if (--music->references) return;
    qa_audio_stream_close(music->stream);
    if (music->loop != music->stream) qa_audio_stream_close(music->loop);
    qa_audio_raw_destroy(music->pcm);
    if (music->controls) {
        qa_audio_music **link = &music->controls->players;
        while (*link && *link != music) link = &(*link)->controls_next;
        if (*link) *link = music->controls_next;
        qa_audio_music_controls_release(music->controls);
    }
    free(music);
}

void qa_audio_music_start(qa_audio_music *music, qa_audio_stream *intro, qa_audio_stream *loop) {
    if (music == NULL || !music_mutation(music, NULL)) {
        if (intro != NULL && (!music || (intro != music->stream && intro != music->loop)))
            qa_audio_stream_close(intro);
        if (loop != NULL && loop != intro && (!music || (loop != music->stream && loop != music->loop)))
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
    if (music != NULL && music_mutation(music, NULL))
        music->paused = paused;
}

void qa_audio_music_enable(qa_audio_music *music, bool enabled) {
    if (music && music_mutation(music, NULL)) (void)qa_audio_music_controls_enable(music->controls, enabled, NULL);
}

bool qa_audio_music_volume(qa_audio_music *music, float volume, qa_error *error) {
    if (music == NULL || !isfinite(volume) || volume < 0 || !music_mutation(music, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid music volume");
        return false;
    }
    qa_audio_music_target_publish(music, volume);
    return true;
}

void qa_audio_music_target_publish(qa_audio_music *music, float volume) {
    music->target_volume = volume;
}
bool qa_audio_music_target_mutation_ready(qa_audio_music *music, qa_error *error) {
    return music_mutation(music, error);
}

void qa_audio_music_update(qa_audio_music *music) {
    if (music == NULL || !music->controls || music->family != QA_AUDIO_Q3 || !music->source_volume ||
        music->stream == NULL || music->paused || !music->controls->enabled)
        return;
    if (!music_mutation(music, NULL)) return;
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
    if (!music_mutation(music, error)) return false;
    if (!music->controls->enabled || music->paused || music->stream == NULL || frames == 0)
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
    return music_mutation(music, error) && qa_audio_music_controls_remap(music->controls, tracks, count, error);
}

void qa_audio_music_reset(qa_audio_music *music) {
    if (music == NULL || !music_mutation(music, NULL) || !controls_mutation(music->controls, NULL))
        return;
    qa_audio_music_stop(music);
    (void)qa_audio_music_controls_reset(music->controls, NULL);
}

bool qa_audio_music_cd_play(qa_audio_music *music, unsigned track, bool loop,
                            qa_audio_open_track_fn open, void *user, qa_error *error) {
    if (music == NULL || open == NULL || track > 255 || !music_mutation(music, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid CD track request");
        return false;
    }
    qa_error_set(error, QA_OK, 0, NULL);
    if (!music->controls->enabled)
        return false;
    unsigned mapped = track < MUSIC_REMAP_TRACKS ? music->controls->remap[track] : track;
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
        if (request != music->request || !music->controls->enabled) {
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

bool qa_audio_music_state_read(const qa_audio_music *music, qa_audio_music_state *out) {
    if (!music || !out || !music->controls) return false;
    *out = (qa_audio_music_state){.output_rate = music->output_rate, .family = music->family,
        .request = music->request, .completions = music->completions, .cd_track = music->cd_track,
        .target_volume = music->target_volume,
        .source_volume = music->source_volume, .enabled = music->controls->enabled,
        .paused = music->paused, .playing = music->stream != NULL};
    return true;
}
bool qa_audio_music_idle(const qa_audio_music *music) {
    return music && music->controls && !music->selection && !music->holds && !music->destroy_requested;
}
unsigned qa_audio_music_mapped_track(const qa_audio_music *music, unsigned track) {
    return qa_audio_music_controls_mapped_track(music ? music->controls : NULL, track);
}
static bool selection_current(const qa_audio_music_selection *selection, const qa_audio_music *music) {
    return selection && music && selection->music == music && music->selection == selection &&
        !music->destroy_requested && !selection->invalidated && !selection->consuming &&
        music->request == selection->request &&
        (!selection->new_target || (isfinite(selection->target_volume) && selection->target_volume >= 0)) &&
        (selection->kind == QA_AUDIO_MUSIC_KEEP ? !selection->intro && !selection->loop && !selection->pcm :
         selection->pcm && qa_audio_raw_rate(selection->pcm) == music->output_rate &&
         (selection->kind == QA_AUDIO_MUSIC_STOP ? !selection->intro && !selection->loop :
          selection->intro && qa_audio_stream_rate(selection->intro) &&
          (qa_audio_stream_channels(selection->intro) == 1 || qa_audio_stream_channels(selection->intro) == 2) &&
          (!selection->loop || (qa_audio_stream_rate(selection->loop) &&
            (qa_audio_stream_channels(selection->loop) == 1 || qa_audio_stream_channels(selection->loop) == 2)))));
}
bool qa_audio_music_selection_prepare(qa_audio_music *music, qa_audio_music_selection_kind kind,
    qa_audio_stream *intro, qa_audio_stream *loop, unsigned cd_track,
    const qa_audio_music_source_profile *new_source,
    qa_audio_music_selection **out, qa_error *error) {
    if (!out || *out || !qa_audio_music_idle(music) || cd_track > 255 ||
        (kind != QA_AUDIO_MUSIC_KEEP && kind != QA_AUDIO_MUSIC_START && kind != QA_AUDIO_MUSIC_STOP) ||
        (new_source && (kind == QA_AUDIO_MUSIC_KEEP ||
            (new_source->family != QA_AUDIO_Q1 && new_source->family != QA_AUDIO_Q2 && new_source->family != QA_AUDIO_Q3))) ||
        (kind != QA_AUDIO_MUSIC_START && (intro || loop || cd_track)) ||
        (kind == QA_AUDIO_MUSIC_START && (!intro || !qa_audio_stream_rate(intro) ||
            (qa_audio_stream_channels(intro) != 1 && qa_audio_stream_channels(intro) != 2))) ||
        (loop && (!qa_audio_stream_rate(loop) || (qa_audio_stream_channels(loop) != 1 && qa_audio_stream_channels(loop) != 2))) ||
        (intro && (intro == music->stream || intro == music->loop)) ||
        (loop && (loop == music->stream || loop == music->loop))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music selection requires its idle player and separately owned prepared streams");
        return false;
    }
    qa_audio_music_selection *selection = calloc(1, sizeof(*selection));
    if (!selection) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining prepared music selection"); return false; }
    if (kind != QA_AUDIO_MUSIC_KEEP && !qa_audio_raw_create(music->output_rate, &selection->pcm, error)) {
        free(selection); return false;
    }
    selection->music = music; selection->kind = kind; selection->intro = intro; selection->loop = loop;
    selection->cd_track = cd_track; selection->request = music->request;
    if (new_source) { selection->source = *new_source; selection->new_source = true; }
    music->selection = selection; *out = selection; return true;
}
bool qa_audio_music_selection_ready(qa_audio_music_selection *selection, qa_error *error) {
    if (!selection_current(selection, selection ? selection->music : NULL)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared music selection lost its actual playback claim"); return false;
    }
    selection->ready = true; return true;
}
bool qa_audio_music_selection_volume(qa_audio_music_selection *selection, float volume, qa_error *error) {
    if (!isfinite(volume) || volume < 0 || !selection_current(selection, selection ? selection->music : NULL)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared music target requires its actual current selection"); return false;
    }
    selection->target_volume = volume; selection->new_target = true; selection->ready = false; return true;
}
const qa_audio_music_selection *qa_audio_music_selection_read(const qa_audio_music *music) {
    return music ? music->selection : NULL;
}
bool qa_audio_music_selection_current(const qa_audio_music_selection *selection, const qa_audio_music *music) {
    return selection_current(selection, music);
}
bool qa_audio_music_selection_ready_is(const qa_audio_music_selection *selection, const qa_audio_music *music) {
    return selection && selection->ready && selection_current(selection, music);
}
void qa_audio_music_selection_publish(qa_audio_music_selection **in) {
    qa_audio_music_selection *selection = *in; qa_audio_music *music = selection->music;
    selection->consuming = true;
    if (selection->new_target) music->target_volume = selection->target_volume;
    if (selection->kind != QA_AUDIO_MUSIC_KEEP) {
        qa_audio_stream *previous = music->stream, *previous_loop = music->loop;
        qa_audio_raw_stream *previous_pcm = music->pcm;
        music->stream = selection->intro; music->loop = selection->loop; music->pcm = selection->pcm;
        music->cd_track = selection->cd_track; music->reset_pcm = true; ++music->request;
        if (selection->new_source) {
            music->family = selection->source.family; music->source_volume = selection->source.source_volume;
            music->smoothed_volume = 0.5f; music->completions = 0; music->paused = false;
        }
        if (selection->kind == QA_AUDIO_MUSIC_START) music->paused = false;
        selection->intro = selection->loop = NULL; selection->pcm = NULL;
        qa_audio_stream_close(previous);
        if (previous_loop != previous) qa_audio_stream_close(previous_loop);
        qa_audio_raw_destroy(previous_pcm);
    }
    for (qa_audio_music_hold *h = music->holds; h; h = h->next) if (h->selection == selection) {
        h->selection = NULL; h->consumed = true;
    }
    music->selection = NULL; free(selection); *in = NULL;
}
bool qa_audio_music_selection_abort(qa_audio_music_selection **in, qa_error *error) {
    if (!in || !*in) return true;
    qa_audio_music_selection *selection = *in;
    if (selection->consuming || !selection->music || selection->music->selection != selection) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music abort retains a consuming or displaced playback claim"); return false;
    }
    selection->consuming = true;
    qa_audio_stream_close(selection->intro);
    if (selection->loop != selection->intro) qa_audio_stream_close(selection->loop);
    qa_audio_raw_destroy(selection->pcm);
    for (qa_audio_music_hold *h = selection->music->holds; h; h = h->next) if (h->selection == selection) {
        h->selection = NULL; h->consumed = true;
    }
    selection->music->selection = NULL; free(selection); *in = NULL; return true;
}
bool qa_audio_music_hold_prepare(qa_audio_music *music, qa_audio_music_hold **out, qa_error *error) {
    if (!out || *out || !music || !music->controls || music->holds || music->destroy_requested ||
        (music->selection && !selection_current(music->selection, music))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Music hold requires its actual returned player or current selection"); return false;
    }
    qa_audio_music_hold *hold = calloc(1, sizeof(*hold));
    if (!hold) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining enclosing music resource hold"); return false; }
    if (!qa_audio_music_retain(music, error)) { free(hold); return false; }
    hold->music = music; hold->selection = music->selection; hold->consumed = !music->selection;
    hold->next = music->holds; music->holds = hold; *out = hold; return true;
}
bool qa_audio_music_hold_current(const qa_audio_music_hold *hold, const qa_audio_music *music) {
    if (!hold || !music || hold->music != music || hold->invalidated || music->destroy_requested || !music->controls) return false;
    bool found = false;
    for (const qa_audio_music_hold *h = music->holds; h; h = h->next) if (h == hold) found = true;
    return found && (hold->selection ? music->selection == hold->selection && selection_current(hold->selection, music) :
        hold->consumed && !music->selection);
}
bool qa_audio_music_hold_consumed(const qa_audio_music_hold *hold, const qa_audio_music *music) {
    return qa_audio_music_hold_current(hold, music) && hold->consumed && !hold->selection;
}
void qa_audio_music_hold_release(qa_audio_music_hold **in) {
    if (!in || !*in) return;
    qa_audio_music_hold *hold = *in; qa_audio_music *music = hold->music;
    qa_audio_music_hold **link = &music->holds;
    while (*link && *link != hold) link = &(*link)->next;
    if (*link != hold) return;
    *link = hold->next; *in = NULL; free(hold); qa_audio_music_release(music);
}
bool qa_audio_music_controls_create(qa_audio_music_controls **out, qa_error *error) {
    if (!out || *out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CD controls require an empty actual owner slot"); return false; }
    qa_audio_music_controls *controls = calloc(1, sizeof(*controls));
    if (!controls) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining application CD controls"); return false; }
    controls->references = 1; controls->enabled = true; music_identity_remap(controls);
    *out = controls; return true;
}
void qa_audio_music_controls_release(qa_audio_music_controls *controls) {
    if (controls && --controls->references == 0) free(controls);
}
bool qa_audio_music_controls_bind(qa_audio_music *music, qa_audio_music_controls *controls, qa_error *error) {
    if (!music || !controls || !controls->references || controls->references == SIZE_MAX ||
        music->destroy_requested || music->selection || (!music->controls && !music->external_controls) ||
        !controls_mutation(controls, error) || (music->controls && !controls_mutation(music->controls, error))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CD control binding requires the actual returned player and control owners"); return false;
    }
    if (music->controls == controls) return true;
    if (music->controls) {
        qa_audio_music **link = &music->controls->players;
        while (*link && *link != music) link = &(*link)->controls_next;
        if (!*link) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Music player lost its actual CD control binding"); return false; }
        *link = music->controls_next; qa_audio_music_controls_release(music->controls);
    }
    ++controls->references; music->controls = controls; music->controls_next = controls->players;
    controls->players = music; music->external_controls = true; return true;
}
bool qa_audio_music_controls_is(const qa_audio_music *music, const qa_audio_music_controls *controls) {
    if (!music || !controls || music->controls != controls || !music->external_controls) return false;
    for (const qa_audio_music *p = controls->players; p; p = p->controls_next) if (p == music) return true;
    return false;
}
bool qa_audio_music_controls_restore_pending(const qa_audio_music *music) {
    return music && music->references && music->external_controls && !music->controls &&
        !music->selection && !music->holds && !music->destroy_requested;
}
bool qa_audio_music_controls_restore_ready(const qa_audio_music_controls *controls, size_t count) {
    if (!controls || !controls->references || count > SIZE_MAX - controls->references) return false;
    for (const qa_audio_music *p = controls->players; p; p = p->controls_next)
        if (!qa_audio_music_idle(p)) return false;
    return true;
}
bool qa_audio_music_controls_enabled(const qa_audio_music_controls *controls, bool *out) {
    if (!controls || !controls->references || !out) return false;
    *out = controls->enabled; return true;
}
unsigned qa_audio_music_controls_mapped_track(const qa_audio_music_controls *controls, unsigned track) {
    return controls && track < MUSIC_REMAP_TRACKS ? controls->remap[track] : track;
}
bool qa_audio_music_controls_enable(qa_audio_music_controls *controls, bool enabled, qa_error *error) {
    if (!controls_mutation(controls, error)) return false;
    if (controls->enabled != enabled)
        for (qa_audio_music *p = controls->players; p; p = p->controls_next) ++p->request;
    controls->enabled = enabled; return true;
}
bool qa_audio_music_controls_remap(qa_audio_music_controls *controls, const uint8_t *tracks, size_t count, qa_error *error) {
    if (count >= MUSIC_REMAP_TRACKS || (!tracks && count) || !controls_mutation(controls, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CD remap requires at most 99 tracks"); return false;
    }
    if (count) memcpy(controls->remap + 1, tracks, count);
    return true;
}
bool qa_audio_music_controls_reset(qa_audio_music_controls *controls, qa_error *error) {
    if (!controls_mutation(controls, error)) return false;
    controls->enabled = true; music_identity_remap(controls); return true;
}
bool qa_audio_music_controls_checkpoint(const qa_audio_music_controls *controls, qa_buffer *out, qa_error *error) {
    if (!controls || !controls->references || !out || out->data || out->size) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CD controls capture requires its genuine idle owner"); return false;
    }
    for (const qa_audio_music *p = controls->players; p; p = p->controls_next) if (!qa_audio_music_idle(p)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "CD controls capture retains a prepared player"); return false;
    }
    uint8_t *data = malloc(105);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining shared CD control continuation"); return false; }
    memcpy(data, "QAMC", 4); data[4] = controls->enabled;
    memcpy(data + 5, controls->remap, MUSIC_REMAP_TRACKS); *out = (qa_buffer){data, 105}; return true;
}
bool qa_audio_music_controls_restore(qa_bytes bytes, qa_audio_music_controls **out, qa_error *error) {
    if (!out || *out || !bytes.data || bytes.size != 105 || memcmp(bytes.data, "QAMC", 4) ||
        bytes.data[4] > 1 || bytes.data[5] != 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid shared CD control continuation"); return false;
    }
    if (!qa_audio_music_controls_create(out, error)) return false;
    (*out)->enabled = bytes.data[4] != 0; memcpy((*out)->remap, bytes.data + 5, MUSIC_REMAP_TRACKS); return true;
}

#include "qa/binary.h"
#include "haptics_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_haptic_pattern_parse(qa_bytes bytes, qa_haptic_pattern **out, qa_error *error) {
    if (!out || (!bytes.data && bytes.size)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid tactile input");
        return false;
    }
    if (bytes.size < 12) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Truncated BNVIB header");
        return false;
    }
    uint32_t metadata = qa_load_u32le(bytes.data);
    if ((metadata != 4 && metadata != 12 && metadata != 16) || bytes.size < (size_t)metadata + 8 ||
        qa_load_u16le(bytes.data + 4) != 3 || qa_load_u16le(bytes.data + 6) == 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid BNVIB metadata");
        return false;
    }
    size_t data_size = qa_load_u32le(bytes.data + 4 + metadata);
    if (data_size % 4 || data_size > bytes.size - metadata - 8 ||
        data_size > SIZE_MAX - sizeof(qa_haptic_pattern)) {
        qa_error_set(error, QA_ERROR_FORMAT, metadata + 4, "Invalid BNVIB sample size");
        return false;
    }
    uint32_t start = metadata >= 12 ? qa_load_u32le(bytes.data + 8) : 0,
             end = metadata >= 12 ? qa_load_u32le(bytes.data + 12) : 0;
    if (metadata >= 12 && (start >= end || end > data_size / 4)) {
        qa_error_set(error, QA_ERROR_FORMAT, 8, "BNVIB loop exceeds samples");
        return false;
    }
    qa_haptic_pattern *pattern = malloc(sizeof(*pattern) + data_size);
    if (!pattern) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating tactile pattern");
        return false;
    }
    *pattern = (qa_haptic_pattern){.references = 1,
                                   .count = data_size / 4,
                                   .rate = qa_load_u16le(bytes.data + 6),
                                   .loop = metadata >= 12,
                                   .start = start,
                                   .end = end,
                                   .interval = metadata == 16 ? qa_load_u32le(bytes.data + 16) : 0};
    memcpy(pattern->samples, bytes.data + metadata + 8, data_size);
    *out = pattern;
    return true;
}
void qa_haptic_pattern_retain(qa_haptic_pattern *p) {
    if (p)
        ++p->references;
}
void qa_haptic_pattern_release(qa_haptic_pattern *p) {
    if (p && --p->references == 0)
        free(p);
}
qa_haptic_cache *qa_haptic_cache_create(qa_error *error) {
    qa_haptic_cache *c = calloc(1, sizeof(*c));
    if (!c)
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating tactile cache");
    return c;
}
void qa_haptic_cache_destroy(qa_haptic_cache *c) {
    if (!c)
        return;
    struct haptic_entry *entry = c->entries;
    while (entry) {
        struct haptic_entry *next = entry->next;
        qa_haptic_pattern_release(entry->pattern);
        qa_resource_release(entry->source);
        free(entry);
        entry = next;
    }
    free(c);
}
bool qa_haptic_cache_sound(qa_haptic_cache *cache, qa_vfs *vfs, const char *sound,
                           qa_haptic_pattern **out, qa_error *error) {
    if (!cache || !vfs || !sound || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid tactile resource request");
        return false;
    }
    *out = NULL;
    if (strncmp(sound, "sound/", 6) == 0)
        sound += 6;
    if (*sound == '#')
        ++sound;
    size_t n = strlen(sound);
    if (n <= 4 || strcmp(sound + n - 4, ".wav") != 0)
        return true;
    if (n > SIZE_MAX - 12) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Tactile path length overflow");
        return false;
    }
    char *path = malloc(n + 12);
    if (!path) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating tactile path");
        return false;
    }
    memcpy(path, "tactile/", 8);
    memcpy(path + 8, sound, n - 4);
    memcpy(path + 8 + n - 4, ".bnvib", 7);
    qa_resource *resource = NULL;
    qa_error failure = {0};
    bool success = qa_vfs_acquire(vfs, path, &resource, NULL, &failure);
    free(path);
    if (!success) {
        if (failure.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = failure;
        return false;
    }
    for (struct haptic_entry *entry = cache->entries; entry; entry = entry->next)
        if (entry->source == resource) {
            qa_haptic_pattern_retain(entry->pattern);
            *out = entry->pattern;
            qa_resource_release(resource);
            return true;
        }
    qa_haptic_pattern *pattern = NULL;
    if (!qa_haptic_pattern_parse(qa_resource_bytes(resource), &pattern, error)) {
        qa_resource_release(resource);
        return false;
    }
    struct haptic_entry *entry = malloc(sizeof(*entry));
    if (!entry) {
        qa_resource_release(resource);
        qa_haptic_pattern_release(pattern);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Caching tactile pattern");
        return false;
    }
    qa_resource_retain(resource);
    *entry = (struct haptic_entry){.pattern = pattern,
                                   .source = resource, .next = cache->entries};
    cache->entries = entry;
    qa_resource_release(resource);
    qa_haptic_pattern_retain(pattern);
    *out = pattern;
    return true;
}
void qa_haptic_player_init(qa_haptic_player *p, qa_rumble_sink output, void *user) {
    *p = (qa_haptic_player){.output = output,
                            .user = user,
                            .strength = 1,
                            .enabled = true,
                            .active = true,
                            .last_index = -1,
                            .last_ms = -INFINITY};
}
bool qa_haptic_stop(qa_haptic_player *p, qa_error *error) {
    qa_haptic_pattern_release(p->pattern);
    p->pattern = NULL;
    p->last_index = -1;
    return !p->output || p->output(p->user, 0, 0, 0, error);
}
bool qa_haptic_update(qa_haptic_player *p, double now, qa_error *error) {
    if (!p || !isfinite(now) || now < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid tactile clock");
        return false;
    }
    qa_haptic_pattern *pattern = p->pattern;
    if (!pattern)
        return true;
    if (!p->enabled || !p->active)
        return qa_haptic_stop(p, error);
    double period = 1000.0 / pattern->rate,
           index_value = floor(fmax(0, now - p->start_ms) / period);
    int64_t index;
    if (index_value >= (double)pattern->count) {
        if (!pattern->loop)
            return qa_haptic_stop(p, error);
        uint64_t length = (uint64_t)pattern->end - pattern->start,
                 cycle = length + pattern->interval;
        uint64_t position = (uint64_t)fmod(index_value - (double)pattern->count, (double)cycle);
        index = position >= length ? -2 : (int64_t)((uint64_t)pattern->start + position);
    } else
        index = (int64_t)index_value;
    uint32_t hold = (uint32_t)fmax(50, ceil(period) + 20);
    if (index == p->last_index && now - p->last_ms < (double)hold / 2)
        return true;
    float low = index < 0 ? 0 : (float)pattern->samples[(size_t)index * 4] / 255 * p->strength;
    float high = index < 0 ? 0 : (float)pattern->samples[(size_t)index * 4 + 2] / 255 * p->strength;
    if (p->output && !p->output(p->user, low, high, hold, error))
        return false;
    p->last_index = index;
    p->last_ms = now;
    return true;
}
bool qa_haptic_play(qa_haptic_player *p, qa_haptic_pattern *pattern, double now, qa_error *error) {
    if (!p || !pattern || !isfinite(now) || now < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid tactile playback");
        return false;
    }
    if (!p->enabled || !p->active)
        return true;
    qa_haptic_pattern_retain(pattern);
    qa_haptic_pattern_release(p->pattern);
    p->pattern = pattern;
    p->start_ms = now;
    p->last_index = -1;
    return qa_haptic_update(p, now, error);
}
bool qa_haptic_strength(qa_haptic_player *p, float strength, double now, qa_error *error) {
    if (!p || !isfinite(strength) || strength < 0 || strength > 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Vibration strength must be in 0..1");
        return false;
    }
    if (p->strength == strength)
        return true;
    p->strength = strength;
    p->last_index = -1;
    return qa_haptic_update(p, now, error);
}
bool qa_haptic_enable(qa_haptic_player *p, bool enabled, bool active, qa_error *error) {
    bool changed = p->enabled != enabled || p->active != active;
    p->enabled = enabled;
    p->active = active;
    return (enabled && active) || (!changed && !p->pattern) || qa_haptic_stop(p, error);
}

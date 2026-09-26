#include "qa/audio.h"
#include <stdlib.h>
#include <string.h>

struct qa_audio_ring {
    int16_t *samples;
    size_t capacity;
    atomic_size_t read, write;
};
bool qa_audio_ring_create(size_t capacity, qa_audio_ring **out, qa_error *error) {
    if (!out || !capacity || capacity > SIZE_MAX / 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid PCM ring capacity");
        return false;
    }
    size_t rounded = 1;
    while (rounded < capacity) {
        if (rounded > SIZE_MAX / 8) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "PCM ring rounding overflows");
            return false;
        }
        rounded *= 2;
    }
    capacity = rounded;
    qa_audio_ring *ring = calloc(1, sizeof(*ring));
    if (!ring) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "PCM ring allocation failed");
        return false;
    }
    ring->samples = malloc(capacity * 2 * sizeof(int16_t));
    if (!ring->samples) {
        free(ring);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "PCM ring storage allocation failed");
        return false;
    }
    ring->capacity = capacity;
    atomic_init(&ring->read, 0);
    atomic_init(&ring->write, 0);
    *out = ring;
    return true;
}
void qa_audio_ring_destroy(qa_audio_ring *ring) {
    if (ring) {
        free(ring->samples);
        free(ring);
    }
}
void qa_audio_ring_reset(qa_audio_ring *ring) {
    if (ring) {
        atomic_store_explicit(&ring->read, 0, memory_order_relaxed);
        atomic_store_explicit(&ring->write, 0, memory_order_relaxed);
    }
}
size_t qa_audio_ring_queued(const qa_audio_ring *ring) {
    if (!ring)
        return 0;
    size_t read = atomic_load_explicit(&ring->read, memory_order_acquire),
           write = atomic_load_explicit(&ring->write, memory_order_acquire);
    size_t count = write - read;
    return count > ring->capacity ? ring->capacity : count;
}
size_t qa_audio_ring_write(qa_audio_ring *ring, const int16_t *stereo, size_t frames) {
    if (!ring || (!stereo && frames))
        return 0;
    size_t write = atomic_load_explicit(&ring->write, memory_order_relaxed),
           read = atomic_load_explicit(&ring->read, memory_order_acquire);
    size_t available = ring->capacity - (write - read);
    if (frames > available)
        frames = available;
    size_t at = write & (ring->capacity - 1),
           first = frames < ring->capacity - at ? frames : ring->capacity - at;
    if (first)
        memcpy(ring->samples + at * 2, stereo, first * 2 * sizeof(int16_t));
    if (frames > first)
        memcpy(ring->samples, stereo + first * 2, (frames - first) * 2 * sizeof(int16_t));
    atomic_store_explicit(&ring->write, write + frames, memory_order_release);
    return frames;
}
size_t qa_audio_ring_read(qa_audio_ring *ring, int16_t *stereo, size_t frames) {
    if (!ring || (!stereo && frames))
        return 0;
    size_t read = atomic_load_explicit(&ring->read, memory_order_relaxed),
           write = atomic_load_explicit(&ring->write, memory_order_acquire);
    size_t available = write - read;
    if (frames > available)
        frames = available;
    size_t at = read & (ring->capacity - 1),
           first = frames < ring->capacity - at ? frames : ring->capacity - at;
    if (first)
        memcpy(stereo, ring->samples + at * 2, first * 2 * sizeof(int16_t));
    if (frames > first)
        memcpy(stereo + first * 2, ring->samples, (frames - first) * 2 * sizeof(int16_t));
    atomic_store_explicit(&ring->read, read + frames, memory_order_release);
    return frames;
}
bool qa_audio_output_encode(const int16_t *stereo, size_t frames, qa_audio_output_format format,
                            void *out, size_t capacity, size_t *written, qa_error *error) {
    if (!written || (!stereo && frames) || (!out && frames) || format.sample_rate < 8000 ||
        format.sample_rate > 192000 || (format.channels != 1 && format.channels != 2) ||
        (format.sample_bits != 8 && format.sample_bits != 16) ||
        frames > SIZE_MAX / (2 * sizeof(*stereo)) ||
        frames > SIZE_MAX / format.channels / (format.sample_bits / 8)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid PCM output format or buffer");
        return false;
    }
    size_t size = frames * format.channels * (format.sample_bits / 8);
    if (capacity < size) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "PCM output buffer too small");
        return false;
    }
    if (format.channels == 2 && format.sample_bits == 16) {
        if (size)
            memmove(out, stereo, size);
        *written = size;
        return true;
    }
    for (size_t frame = 0; frame < frames; frame++)
        for (unsigned channel = 0; channel < format.channels; channel++) {
            int value = format.channels == 1 ? ((int)stereo[frame * 2] + stereo[frame * 2 + 1]) / 2
                                             : stereo[frame * 2 + channel];
            size_t index = frame * format.channels + channel;
            if (format.sample_bits == 8)
                ((uint8_t *)out)[index] = (uint8_t)((value + 32768) / 256);
            else {
                int16_t sample = (int16_t)value;
                memcpy((uint8_t *)out + index * 2, &sample, sizeof(sample));
            }
        }
    *written = size;
    return true;
}

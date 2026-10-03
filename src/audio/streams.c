#include "qa/audio.h"
#include "qa/binary.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "PCM checkpoints require binary64 doubles");

struct qa_audio_raw_stream {
    int16_t *samples;
    size_t capacity_samples, head, count;
    uint64_t begin, end, position;
    uint32_t input_rate, output_rate, remainder, step_frames, step_remainder;
    /* Only device-rate changes alter this anchor. Constant-rate stepping uses
     * integer remainders, so neither chunk size nor playback duration adds drift.
     */
    double fraction;
    unsigned channels;
    bool paused;
};

static void raw_steps(qa_audio_raw_stream *stream) {
    stream->step_frames = stream->input_rate / stream->output_rate;
    stream->step_remainder = stream->input_rate % stream->output_rate;
}

uint32_t qa_audio_raw_rate(const qa_audio_raw_stream *stream) {
    return stream != NULL ? stream->output_rate : 0;
}

uint64_t qa_audio_raw_position(const qa_audio_raw_stream *stream) {
    if (stream == NULL)
        return 0;
    if (stream->fraction == 0)
        return stream->position;
    long double phase =
        (long double)stream->fraction + (long double)stream->remainder / stream->output_rate;
    return stream->position + (uint64_t)(phase >= 1 && stream->position < UINT64_MAX);
}

uint64_t qa_audio_raw_queued(const qa_audio_raw_stream *stream) {
    if (stream == NULL)
        return 0;
    uint64_t position = qa_audio_raw_position(stream);
    return position < stream->end ? stream->end - position : 0;
}

static void raw_discard_consumed(qa_audio_raw_stream *stream) {
    uint64_t position = qa_audio_raw_position(stream);
    if (stream->count == 0 || position <= stream->begin)
        return;
    uint64_t distance = position - stream->begin;
    size_t discard = distance < stream->count ? (size_t)distance : stream->count;
    size_t capacity = stream->capacity_samples / stream->channels;
    size_t tail = capacity - stream->head;
    stream->head = discard < tail ? stream->head + discard : discard - tail;
    stream->begin += discard;
    stream->count -= discard;
    if (stream->count == 0)
        stream->head = 0;
}

static bool raw_reserve(qa_audio_raw_stream *stream, size_t frames, unsigned channels,
                        bool preserve, qa_error *error) {
    const size_t maximum = SIZE_MAX / sizeof(int16_t);
    if (frames > maximum / channels) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "PCM queue size overflows storage");
        return false;
    }
    size_t needed = frames * channels;
    if (needed <= stream->capacity_samples)
        return true;
    size_t capacity = stream->capacity_samples ? stream->capacity_samples : 2048;
    while (capacity < needed) {
        if (capacity > maximum / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    int16_t *samples = malloc(capacity * sizeof(*samples));
    if (samples == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating PCM queue");
        return false;
    }
    if (preserve && stream->count != 0) {
        size_t first = stream->capacity_samples / stream->channels - stream->head;
        if (first > stream->count)
            first = stream->count;
        memcpy(samples, stream->samples + stream->head * stream->channels,
               first * stream->channels * sizeof(*samples));
        memcpy(samples + first * stream->channels, stream->samples,
               (stream->count - first) * stream->channels * sizeof(*samples));
    }
    free(stream->samples);
    stream->samples = samples;
    stream->capacity_samples = capacity;
    stream->head = 0;
    return true;
}

bool qa_audio_raw_create(uint32_t output_rate, qa_audio_raw_stream **out, qa_error *error) {
    if (output_rate == 0 || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid PCM output rate or destination");
        return false;
    }
    qa_audio_raw_stream *stream = calloc(1, sizeof(*stream));
    if (stream == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating PCM stream");
        return false;
    }
    stream->output_rate = output_rate;
    stream->channels = 2;
    *out = stream;
    return true;
}

void qa_audio_raw_destroy(qa_audio_raw_stream *stream) {
    if (stream == NULL)
        return;
    free(stream->samples);
    free(stream);
}

bool qa_audio_raw_queue(qa_audio_raw_stream *stream, const int16_t *samples, size_t frames,
                        unsigned channels, uint32_t rate, uint64_t source_frame, bool reset,
                        qa_error *error) {
    if (stream == NULL || rate == 0 || (channels != 1 && channels != 2) ||
        (samples == NULL && frames != 0) || frames > UINT64_MAX - source_frame ||
        frames > SIZE_MAX / sizeof(*samples) / channels) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid streamed PCM extent or format");
        return false;
    }
    reset = reset || stream->input_rate == 0;
    if (!reset && (rate != stream->input_rate || channels != stream->channels)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "PCM stream format changed without reset");
        return false;
    }
    if (!reset && source_frame != stream->end) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "PCM source frames are not contiguous");
        return false;
    }
    if (!reset)
        raw_discard_consumed(stream);
    size_t retained = reset ? 0 : stream->count;
    if (frames > SIZE_MAX - retained ||
        !raw_reserve(stream, retained + frames, channels, !reset, error)) {
        if (frames > SIZE_MAX - retained)
            qa_error_set(error, QA_ERROR_MEMORY, 0, "PCM frame count overflows storage");
        return false;
    }
    if (reset) {
        stream->head = stream->count = 0;
        stream->begin = stream->end = stream->position = source_frame;
        stream->fraction = 0;
        stream->remainder = 0;
        stream->input_rate = rate;
        stream->channels = channels;
        raw_steps(stream);
    }
    if (frames != 0) {
        size_t capacity = stream->capacity_samples / channels;
        size_t to_end = capacity - stream->head;
        size_t tail =
            stream->count < to_end ? stream->head + stream->count : stream->count - to_end;
        size_t first = capacity - tail;
        if (first > frames)
            first = frames;
        memcpy(stream->samples + tail * channels, samples, first * channels * sizeof(*samples));
        memcpy(stream->samples, samples + first * channels,
               (frames - first) * channels * sizeof(*samples));
        stream->count += frames;
    }
    stream->end = source_frame + frames;
    return true;
}

bool qa_audio_raw_set_rate(qa_audio_raw_stream *stream, uint32_t rate, qa_error *error) {
    if (stream == NULL || rate == 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid PCM output rate");
        return false;
    }
    if (rate == stream->output_rate)
        return true;
    long double fraction =
        (long double)stream->fraction + (long double)stream->remainder / stream->output_rate;
    if (fraction >= 1) {
        if (stream->position < UINT64_MAX)
            ++stream->position;
        fraction -= 1;
    }
    stream->fraction = (double)fraction;
    if (stream->fraction >= 1)
        stream->fraction = nextafter(1.0, 0.0);
    stream->remainder = 0;
    stream->output_rate = rate;
    raw_steps(stream);
    return true;
}

bool qa_audio_raw_clone_rate(const qa_audio_raw_stream *source, uint32_t rate,
                             qa_audio_raw_stream **out, qa_error *error) {
    if (!source || !rate || !out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid PCM conversion copy destination");
        return false;
    }
    qa_audio_raw_stream *copy = malloc(sizeof(*copy));
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating PCM conversion copy");
        return false;
    }
    *copy = *source;
    copy->samples = NULL;
    if (copy->capacity_samples) {
        copy->samples = malloc(copy->capacity_samples * sizeof(*copy->samples));
        if (!copy->samples) {
            free(copy);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Copying queued PCM conversion input");
            return false;
        }
        size_t capacity = copy->capacity_samples / copy->channels;
        size_t first = capacity - copy->head;
        if (first > copy->count) first = copy->count;
        if (first)
            memcpy(copy->samples + copy->head * copy->channels,
                   source->samples + source->head * source->channels,
                   first * copy->channels * sizeof(*copy->samples));
        if (first < copy->count)
            memcpy(copy->samples, source->samples,
                   (copy->count - first) * copy->channels * sizeof(*copy->samples));
    }
    if (!qa_audio_raw_set_rate(copy, rate, error)) {
        qa_audio_raw_destroy(copy);
        return false;
    }
    *out = copy;
    return true;
}

void qa_audio_raw_pause(qa_audio_raw_stream *stream, bool paused) {
    if (stream != NULL)
        stream->paused = paused;
}

static void raw_advance(qa_audio_raw_stream *stream) {
    uint64_t remainder = (uint64_t)stream->remainder + stream->step_remainder;
    uint64_t advance = stream->step_frames;
    if (remainder >= stream->output_rate) {
        remainder -= stream->output_rate;
        ++advance;
    }
    if (advance > UINT64_MAX - stream->position) {
        stream->position = UINT64_MAX;
        stream->fraction = 0;
        stream->remainder = 0;
    } else {
        stream->position += advance;
        stream->remainder = (uint32_t)remainder;
    }
}

size_t qa_audio_raw_mix(qa_audio_raw_stream *stream, float *stereo, size_t frames, float gain) {
    if (stream == NULL || stream->paused || stereo == NULL || !isfinite(gain) ||
        frames > SIZE_MAX / sizeof(*stereo) / 2)
        return 0;
    size_t mixed = 0;
    while (mixed < frames) {
        uint64_t position = qa_audio_raw_position(stream);
        if (position >= stream->end)
            break;
        size_t offset = (size_t)(position - stream->begin);
        size_t capacity = stream->capacity_samples / stream->channels;
        size_t to_end = capacity - stream->head;
        size_t frame = offset < to_end ? stream->head + offset : offset - to_end;
        const int16_t *sample = stream->samples + frame * stream->channels;
        stereo[mixed * 2] += (float)sample[0] * gain;
        stereo[mixed * 2 + 1] += (float)sample[stream->channels - 1] * gain;
        ++mixed;
        raw_advance(stream);
    }
    raw_discard_consumed(stream);
    return mixed;
}

enum { RAW_CHECKPOINT_HEADER = 68 };

bool qa_audio_raw_checkpoint(const qa_audio_raw_stream *stream, qa_buffer *out, qa_error *error) {
    if (stream == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid PCM checkpoint destination");
        return false;
    }
    if (stream->count > (SIZE_MAX - RAW_CHECKPOINT_HEADER) / sizeof(int16_t) / stream->channels) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "PCM checkpoint size overflows storage");
        return false;
    }
    size_t size = RAW_CHECKPOINT_HEADER + stream->count * stream->channels * sizeof(int16_t);
    uint8_t *data = calloc(1, size);
    if (data == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating PCM checkpoint");
        return false;
    }
    memcpy(data, "QARS", 4);
    qa_store_u32le(data + 4, stream->output_rate);
    qa_store_u32le(data + 8, stream->input_rate);
    qa_store_u32le(data + 12, stream->channels);
    qa_store_u32le(data + 16, stream->paused ? 1 : 0);
    qa_store_u64le(data + 20, stream->position);
    uint64_t fraction_bits;
    memcpy(&fraction_bits, &stream->fraction, sizeof(fraction_bits));
    qa_store_u64le(data + 28, fraction_bits);
    qa_store_u32le(data + 36, stream->remainder);
    qa_store_u64le(data + 44, stream->begin);
    qa_store_u64le(data + 52, stream->end);
    qa_store_u64le(data + 60, stream->count);
    size_t capacity = stream->capacity_samples / stream->channels;
    size_t frame = stream->head;
    for (size_t i = 0; i < stream->count; ++i) {
        for (unsigned channel = 0; channel < stream->channels; ++channel)
            qa_store_u16le(data + RAW_CHECKPOINT_HEADER + (i * stream->channels + channel) * 2,
                           (uint16_t)stream->samples[frame * stream->channels + channel]);
        if (++frame == capacity)
            frame = 0;
    }
    *out = (qa_buffer){data, size};
    return true;
}

bool qa_audio_raw_restore(qa_bytes checkpoint, uint32_t output_rate, qa_audio_raw_stream **out,
                          qa_error *error) {
    if (out == NULL || output_rate == 0 || (checkpoint.data == NULL && checkpoint.size != 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid PCM checkpoint arguments");
        return false;
    }
    if (checkpoint.size < RAW_CHECKPOINT_HEADER || memcmp(checkpoint.data, "QARS", 4) != 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid PCM checkpoint header");
        return false;
    }
    const uint8_t *data = checkpoint.data;
    qa_audio_raw_stream saved = {0};
    saved.output_rate = qa_load_u32le(data + 4);
    saved.input_rate = qa_load_u32le(data + 8);
    saved.channels = qa_load_u32le(data + 12);
    uint32_t flags = qa_load_u32le(data + 16);
    saved.paused = (flags & 1) != 0;
    saved.position = qa_load_u64le(data + 20);
    uint64_t fraction_bits = qa_load_u64le(data + 28);
    memcpy(&saved.fraction, &fraction_bits, sizeof(saved.fraction));
    saved.remainder = qa_load_u32le(data + 36);
    saved.begin = qa_load_u64le(data + 44);
    saved.end = qa_load_u64le(data + 52);
    uint64_t count = qa_load_u64le(data + 60);
    if (saved.output_rate == 0 || (saved.channels != 1 && saved.channels != 2) || flags > 1 ||
        qa_load_u32le(data + 40) != 0 || !isfinite(saved.fraction) || saved.fraction < 0 ||
        saved.fraction >= 1 || saved.remainder >= saved.output_rate || saved.end < saved.begin ||
        count != saved.end - saved.begin ||
        count > (SIZE_MAX - RAW_CHECKPOINT_HEADER) / 2 / saved.channels) {
        qa_error_set(error, QA_ERROR_FORMAT, 4, "invalid PCM checkpoint state");
        return false;
    }
    saved.count = (size_t)count;
    if (checkpoint.size != RAW_CHECKPOINT_HEADER + saved.count * saved.channels * 2 ||
        qa_audio_raw_position(&saved) < saved.begin ||
        (saved.input_rate == 0 &&
         (saved.count != 0 || saved.begin != 0 || saved.end != 0 || saved.position != 0 ||
          saved.remainder != 0 || saved.fraction != 0))) {
        qa_error_set(error, QA_ERROR_FORMAT, RAW_CHECKPOINT_HEADER,
                     "invalid PCM checkpoint extent or cursor");
        return false;
    }
    qa_audio_raw_stream *stream = malloc(sizeof(*stream));
    if (stream == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating restored PCM stream");
        return false;
    }
    *stream = saved;
    if (!raw_reserve(stream, saved.count, saved.channels, false, error)) {
        qa_audio_raw_destroy(stream);
        return false;
    }
    for (size_t i = 0; i < saved.count * saved.channels; ++i)
        stream->samples[i] = qa_load_i16le(data + RAW_CHECKPOINT_HEADER + i * 2);
    raw_steps(stream);
    if (!qa_audio_raw_set_rate(stream, output_rate, error)) {
        qa_audio_raw_destroy(stream);
        return false;
    }
    *out = stream;
    return true;
}

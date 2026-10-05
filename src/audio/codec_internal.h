#ifndef QA_AUDIO_CODEC_INTERNAL_H
#define QA_AUDIO_CODEC_INTERNAL_H

#include "qa/audio.h"
#include "qa/binary.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static inline bool qa_audio_codec_fail(qa_error *error, qa_status code, size_t offset,
                                       const char *message) {
    qa_error_set(error, code, offset, "%s", message);
    return false;
}

static inline bool qa_audio_pcm_size(uint64_t frames, unsigned channels, size_t *size,
                                     qa_error *error) {
    if (channels != 1 && channels != 2)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0,
                                   "PCM must have one or two channels");
    if (frames > SIZE_MAX / (channels * sizeof(int16_t)))
        return qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "PCM allocation size overflows");
    *size = (size_t)frames * channels * sizeof(int16_t);
    return true;
}

typedef struct qa_audio_source_layout {
    uint64_t frames, loop_start;
    uint64_t step256;
} qa_audio_source_layout;
bool qa_audio_source_layout_compute(const qa_audio_sample *sample, uint32_t output_rate,
                                    qa_audio_family family, qa_audio_source_layout *out,
                                    qa_error *error);
static inline bool qa_audio_source_index(const qa_audio_source_layout *layout, uint64_t frame,
                                         uint64_t *out) {
    uint64_t step = layout->step256, whole = step >> 8, fraction = step & 255;
    if (whole && frame > UINT64_MAX / whole)
        return false;
    uint64_t index = frame * whole;
    uint64_t fractional = (frame >> 8) * fraction + (((frame & 255) * fraction) >> 8);
    if (fractional > UINT64_MAX - index)
        return false;
    *out = index + fractional;
    return true;
}

#endif

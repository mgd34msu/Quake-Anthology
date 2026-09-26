#include "qa/media.h"

static int32_t signed_word(uint32_t value) {
    value &= UINT32_C(65535);
    return value >= UINT32_C(32768) ? (int32_t)value - INT32_C(65536) : (int32_t)value;
}
static int32_t signed_dword(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296));
}
static int32_t delta(uint8_t value) {
    int32_t magnitude = value & 127;
    int32_t square = magnitude * magnitude;
    return value & 128 ? -square : square;
}
bool qa_roq_audio_decode(qa_bytes input, size_t size, uint16_t flags, qa_roq_audio_mode mode,
                          bool signed_output, int16_t *output, size_t capacity, size_t *frames, qa_error *error) {
    if (!frames || mode < QA_ROQ_MONO_TO_MONO || mode > QA_ROQ_STEREO_TO_MONO ||
        (size && (!input.data || !output)) || size > UINT32_MAX || size > SIZE_MAX / 2) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid RoQ audio decode"); return false;
    }
    size_t required_input = mode == QA_ROQ_STEREO_TO_MONO ? size * 2 : size;
    size_t required_output = mode == QA_ROQ_MONO_TO_STEREO ? size * 2 : size;
    if (required_input > input.size || required_output > capacity || (mode == QA_ROQ_STEREO_TO_STEREO && (size & 1))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Truncated RoQ audio or output"); return false;
    }
    int32_t bias = signed_output ? 32768 : 0;
    if (mode == QA_ROQ_MONO_TO_MONO || mode == QA_ROQ_MONO_TO_STEREO) {
        int32_t previous = (int32_t)flags - bias;
        for (size_t i = 0; i < size; ++i) {
            previous = signed_word((uint32_t)(previous + delta(input.data[i])));
            if (mode == QA_ROQ_MONO_TO_MONO) output[i] = (int16_t)previous;
            else output[i * 2 + 1] = output[i * 2] = (int16_t)previous;
        }
        *frames = size;
    } else {
        int32_t left = (int32_t)(flags & 0xff00u) - bias, right = (int32_t)((flags & 255u) << 8) - bias;
        size_t count = mode == QA_ROQ_STEREO_TO_STEREO ? size / 2 : size;
        for (size_t i = 0; i < count; ++i) {
            if (mode == QA_ROQ_STEREO_TO_STEREO) {
                left = signed_word((uint32_t)(left + delta(input.data[i * 2])));
                right = signed_word((uint32_t)(right + delta(input.data[i * 2 + 1])));
                output[i * 2] = (int16_t)left; output[i * 2 + 1] = (int16_t)right;
            } else {
                left = signed_dword((uint32_t)left + (uint32_t)delta(input.data[i * 2]));
                right = signed_dword((uint32_t)right + (uint32_t)delta(input.data[i * 2 + 1]));
                int32_t sum = signed_dword((uint32_t)left + (uint32_t)right);
                output[i] = (int16_t)signed_word((uint32_t)(sum / 2));
            }
        }
        *frames = count;
    }
    return true;
}

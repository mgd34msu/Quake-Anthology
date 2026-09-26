/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Daubechies and mu-law compatibility from id Software's snd_wavelet.c.
 * Copyright (C) 1999-2005 Id Software, Inc. */
#include "codec_internal.h"

#define WAVELET_MAX_SAMPLES 2048u

int16_t qa_audio_mulaw_decode(uint8_t byte) {
    unsigned law = (unsigned)(uint8_t)~byte;
    unsigned exponent = (law >> 4) & 7;
    int adjusted = (int)(((law & 15) + 16) << (exponent + 3)) - 132;
    return (int16_t)((law & 128) ? adjusted : -adjusted);
}

uint8_t qa_audio_mulaw_encode(int16_t sample) {
    int value = sample;
    unsigned sign = value < 0 ? 0 : 128;
    unsigned adjusted = (unsigned)(value < 0 ? -value : value) + 132;
    if (adjusted > 32767)
        adjusted = 32767;
    unsigned exponent = 0;
    for (unsigned bits = adjusted >> 7; bits > 1; bits >>= 1)
        ++exponent;
    unsigned mantissa = (adjusted >> (exponent + 3)) & 15;
    return (uint8_t)~(sign | (exponent << 4) | mantissa);
}

static bool wavelet_length(size_t count, bool encode, qa_error *error) {
    if (count < 4 || count > WAVELET_MAX_SAMPLES)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0,
                                   "A wavelet block must contain 4..2048 samples");
    size_t start = count / 4;
    if (encode) {
        for (size_t length = count; length >= start; length >>= 1)
            if (length >= 4 && (length & 1))
                return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, length,
                                           "Wavelet forward stage has an undefined odd length");
    } else {
        for (size_t length = start; length <= count; length <<= 1)
            if (length >= 4 && (length & 1))
                return qa_audio_codec_fail(error, QA_ERROR_FORMAT, length,
                                           "Wavelet inverse stage has an undefined odd length");
    }
    return true;
}

static void daub4(float *samples, size_t size, bool encode, float *scratch) {
    static const double c0 = 0.4829629131445341;
    static const double c1 = 0.8365163037378079;
    static const double c2 = 0.2241438680420134;
    static const double c3 = -0.1294095225512604;
    if (size < 4)
        return;
    size_t half = size / 2;
    if (encode) {
        size_t i = 0;
        for (size_t j = 0; j <= size - 4; j += 2, ++i) {
            scratch[i] = (float)(c0 * samples[j] + c1 * samples[j + 1] + c2 * samples[j + 2] +
                                 c3 * samples[j + 3]);
            scratch[i + half] = (float)(c3 * samples[j] - c2 * samples[j + 1] +
                                        c1 * samples[j + 2] - c0 * samples[j + 3]);
        }
        scratch[i] = (float)(c0 * samples[size - 2] + c1 * samples[size - 1] + c2 * samples[0] +
                             c3 * samples[1]);
        scratch[i + half] = (float)(c3 * samples[size - 2] - c2 * samples[size - 1] +
                                    c1 * samples[0] - c0 * samples[1]);
    } else {
        scratch[0] = (float)(c2 * samples[half - 1] + c1 * samples[size - 1] + c0 * samples[0] +
                             c3 * samples[half]);
        scratch[1] = (float)(c3 * samples[half - 1] - c0 * samples[size - 1] + c1 * samples[0] -
                             c2 * samples[half]);
        for (size_t i = 0, j = 2; i < half - 1; ++i) {
            scratch[j++] = (float)(c2 * samples[i] + c1 * samples[i + half] + c0 * samples[i + 1] +
                                   c3 * samples[i + half + 1]);
            scratch[j++] = (float)(c3 * samples[i] - c0 * samples[i + half] + c1 * samples[i + 1] -
                                   c2 * samples[i + half + 1]);
        }
    }
    memcpy(samples, scratch, size * sizeof(*samples));
}

static void wavelet_transform(float *samples, size_t count, bool encode) {
    float scratch[WAVELET_MAX_SAMPLES];
    size_t start = count / 4;
    if (encode) {
        for (size_t size = count; size >= start; size >>= 1)
            daub4(samples, size, true, scratch);
    } else {
        for (size_t size = start; size <= count; size <<= 1)
            daub4(samples, size, false, scratch);
    }
}

bool qa_audio_wavelet_decode(qa_bytes bytes, size_t samples, int16_t *out, size_t capacity,
                             qa_error *error) {
    if ((!bytes.data && bytes.size) || !out || capacity < samples)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid wavelet decode arguments");
    if (!wavelet_length(samples, false, error))
        return false;
    if (bytes.size < samples)
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Truncated wavelet block");
    float scratch[WAVELET_MAX_SAMPLES];
    int16_t pcm[WAVELET_MAX_SAMPLES];
    for (size_t i = 0; i < samples; ++i)
        scratch[i] = qa_audio_mulaw_decode(bytes.data[i]);
    wavelet_transform(scratch, samples, false);
    for (size_t i = 0; i < samples; ++i) {
        /* The donor rejects an out-of-range short instead of relying on C's
         * undefined conversion. Truncation allows values in (-32769,32768). */
        if (!(scratch[i] > -32769.0f && scratch[i] < 32768.0f))
            return qa_audio_codec_fail(error, QA_ERROR_FORMAT, i,
                                       "Decoded wavelet coefficient exceeds signed PCM range");
        pcm[i] = (int16_t)scratch[i];
    }
    memcpy(out, pcm, samples * sizeof(*out));
    return true;
}

bool qa_audio_wavelet_encode(const int16_t *samples, size_t count, qa_buffer *out,
                             qa_error *error) {
    if (!samples || !out)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid wavelet encode arguments");
    if (!wavelet_length(count, true, error))
        return false;
    uint8_t *data = malloc(count);
    if (!data)
        return qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate wavelet block");
    float scratch[WAVELET_MAX_SAMPLES];
    for (size_t i = 0; i < count; ++i)
        scratch[i] = samples[i];
    wavelet_transform(scratch, count, true);
    for (size_t i = 0; i < count; ++i) {
        float value = scratch[i];
        if (value > 32767.0f)
            value = 32767.0f;
        else if (value < -32768.0f)
            value = -32768.0f;
        data[i] = qa_audio_mulaw_encode((int16_t)value);
    }
    *out = (qa_buffer){data, count};
    return true;
}

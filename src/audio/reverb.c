#include "qa/audio.h"
#include "checkpoint_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Instance-owned Freeverb adaptation from src/audio/reverb.ts. Its tunings
 * are public domain; this is the donor DSP, not OpenAL EAX output parity. */
enum { COMB_COUNT = 8, ALLPASS_COUNT = 4 };
static const unsigned comb_tuning[COMB_COUNT] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const unsigned allpass_tuning[ALLPASS_COUNT] = {556, 441, 341, 225};
static const double tau = 6.283185307179586476925286766559;

typedef struct delay_line {
    float *samples;
    size_t length, position;
    float store, feedback;
} delay_line;

typedef struct reverb_network {
    delay_line comb[COMB_COUNT], allpass[ALLPASS_COUNT];
} reverb_network;

struct qa_audio_reverb {
    uint32_t rate;
    reverb_network network[2];
    float *delay[2], shelf[2];
    size_t delay_length, position, storage_length;
    float storage[];
};

static size_t scaled_length(unsigned tuning, unsigned spread, uint32_t rate) {
    size_t length = ((size_t)(tuning + spread) * rate + 22050) / 44100;
    return length ? length : 1;
}

bool qa_audio_reverb_create(uint32_t rate, qa_audio_reverb **out, qa_error *error) {
    if (!out || rate < 8000 || rate > 192000) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid reverb sample rate or output");
        return false;
    }
    size_t delay_length = ((size_t)rate * 3 + 9) / 10 + 1;
    size_t length = delay_length * 2;
    for (unsigned channel = 0; channel < 2; ++channel) {
        unsigned spread = channel * 23;
        for (size_t i = 0; i < COMB_COUNT; ++i)
            length += scaled_length(comb_tuning[i], spread, rate);
        for (size_t i = 0; i < ALLPASS_COUNT; ++i)
            length += scaled_length(allpass_tuning[i], spread, rate);
    }
    qa_audio_reverb *reverb = calloc(1, sizeof(*reverb) + length * sizeof(float));
    if (!reverb) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate reverb delay lines");
        return false;
    }
    reverb->rate = rate;
    reverb->delay_length = delay_length;
    reverb->storage_length = length;
    float *next = reverb->storage;
    for (unsigned channel = 0; channel < 2; ++channel) {
        unsigned spread = channel * 23;
        reverb->delay[channel] = next;
        next += delay_length;
        for (size_t i = 0; i < COMB_COUNT; ++i) {
            delay_line *line = &reverb->network[channel].comb[i];
            line->length = scaled_length(comb_tuning[i], spread, rate);
            line->samples = next;
            next += line->length;
        }
        for (size_t i = 0; i < ALLPASS_COUNT; ++i) {
            delay_line *line = &reverb->network[channel].allpass[i];
            line->length = scaled_length(allpass_tuning[i], spread, rate);
            line->samples = next;
            next += line->length;
        }
    }
    *out = reverb;
    return true;
}

void qa_audio_reverb_destroy(qa_audio_reverb *reverb) { free(reverb); }
uint32_t qa_audio_reverb_rate(const qa_audio_reverb *reverb) { return reverb ? reverb->rate : 0; }

bool qa_audio_reverb_checkpoint(const qa_audio_reverb *reverb, qa_buffer *out, qa_error *error) {
    if (!reverb || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Reverb checkpoint requires its live owner"); return false;
    }
    qa_ac_writer w = {.error = error};
    qa_ac_write(&w, "QARV", 4); qa_ac_u32(&w, 1); qa_ac_u32(&w, reverb->rate);
    qa_ac_u64(&w, reverb->storage_length); qa_ac_u64(&w, reverb->position);
    for (size_t channel = 0; channel < 2; ++channel) {
        qa_ac_float(&w, reverb->shelf[channel]);
        for (size_t i = 0; i < COMB_COUNT + ALLPASS_COUNT; ++i) {
            const delay_line *line = i < COMB_COUNT ? &reverb->network[channel].comb[i] :
                &reverb->network[channel].allpass[i - COMB_COUNT];
            qa_ac_u64(&w, line->position); qa_ac_float(&w, line->store); qa_ac_float(&w, line->feedback);
        }
    }
    for (size_t i = 0; i < reverb->storage_length; ++i) qa_ac_float(&w, reverb->storage[i]);
    return qa_ac_finish(&w, out);
}

bool qa_audio_reverb_restore(qa_bytes bytes, qa_audio_reverb **out, qa_error *error) {
    if (!out || !bytes.data || bytes.size < 28 || memcmp(bytes.data, "QARV", 4)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid reverb checkpoint header"); return false;
    }
    qa_ac_reader r = {.bytes = bytes, .offset = 4, .error = error};
    if (qa_ac_get32(&r) != 1) return qa_ac_bad(&r, "Unknown reverb checkpoint schema");
    uint32_t rate = qa_ac_get32(&r);
    uint64_t length = qa_ac_get64(&r), position = qa_ac_get64(&r);
    if (r.failed || rate < 8000 || rate > 192000 || length > (bytes.size - r.offset) / 4)
        return qa_ac_bad(&r, "Invalid reverb checkpoint extent or rate");
    qa_audio_reverb *reverb = NULL;
    if (!qa_audio_reverb_create(rate, &reverb, error)) return false;
    if (length != reverb->storage_length || position >= reverb->delay_length) {
        qa_audio_reverb_destroy(reverb); return qa_ac_bad(&r, "Reverb checkpoint delay dimensions differ");
    }
    reverb->position = (size_t)position;
    for (size_t channel = 0; !r.failed && channel < 2; ++channel) {
        reverb->shelf[channel] = qa_ac_getfloat(&r);
        for (size_t i = 0; !r.failed && i < COMB_COUNT + ALLPASS_COUNT; ++i) {
            delay_line *line = i < COMB_COUNT ? &reverb->network[channel].comb[i] :
                &reverb->network[channel].allpass[i - COMB_COUNT];
            uint64_t cursor = qa_ac_get64(&r);
            if (cursor >= line->length) { qa_ac_bad(&r, "Reverb checkpoint cursor leaves its delay line"); break; }
            line->position = (size_t)cursor; line->store = qa_ac_getfloat(&r); line->feedback = qa_ac_getfloat(&r);
        }
    }
    for (size_t i = 0; !r.failed && i < reverb->storage_length; ++i) reverb->storage[i] = qa_ac_getfloat(&r);
    if (!r.failed && r.offset != bytes.size) qa_ac_bad(&r, "Reverb checkpoint has trailing fields");
    if (r.failed) { qa_audio_reverb_destroy(reverb); return false; }
    *out = reverb; return true;
}

void qa_audio_reverb_reset(qa_audio_reverb *reverb) {
    if (!reverb)
        return;
    memset(reverb->storage, 0, reverb->storage_length * sizeof(float));
    reverb->position = 0;
    for (size_t channel = 0; channel < 2; ++channel) {
        reverb->shelf[channel] = 0;
        for (size_t i = 0; i < COMB_COUNT; ++i) {
            reverb->network[channel].comb[i].position = 0;
            reverb->network[channel].comb[i].store = 0;
        }
        for (size_t i = 0; i < ALLPASS_COUNT; ++i)
            reverb->network[channel].allpass[i].position = 0;
    }
}

static float network_process(reverb_network *network, float input, float damping) {
    float output = 0;
    for (size_t i = 0; i < COMB_COUNT; ++i) {
        delay_line *line = &network->comb[i];
        float delayed = line->samples[line->position];
        line->store = delayed * (1 - damping) + line->store * damping;
        line->samples[line->position] = input + line->store * line->feedback;
        if (++line->position == line->length)
            line->position = 0;
        output += delayed;
    }
    for (size_t i = 0; i < ALLPASS_COUNT; ++i) {
        delay_line *line = &network->allpass[i];
        float delayed = line->samples[line->position];
        line->samples[line->position] = output + delayed * line->feedback;
        if (++line->position == line->length)
            line->position = 0;
        output = delayed - output;
    }
    return output;
}

void qa_audio_reverb_process(qa_audio_reverb *reverb, float *stereo, size_t frames,
                             const qa_audio_reverb_params *params) {
    if (!reverb || !params || !stereo || frames > SIZE_MAX / 2 / sizeof(*stereo))
        return;
    if (!isfinite(params->density) || !isfinite(params->diffusion) ||
        !isfinite(params->decay_time) || !isfinite(params->decay_hf_ratio) ||
        !isfinite(params->late_delay) || !isfinite(params->hf_reference) ||
        !isfinite(params->gain) || !isfinite(params->gain_hf) || !isfinite(params->late_gain) ||
        !isfinite(params->reflections_gain))
        return;

    float damping =
        fminf(0.99f, fmaxf(0, 1 - params->decay_hf_ratio) + (params->decay_hf_limit ? 0.05f : 0));
    float diffusion = fminf(1, fmaxf(0, params->diffusion * 0.7f + params->density * 0.3f));
    for (size_t channel = 0; channel < 2; ++channel) {
        reverb_network *network = &reverb->network[channel];
        for (size_t i = 0; i < COMB_COUNT; ++i) {
            delay_line *line = &network->comb[i];
            line->feedback =
                (float)fmin(0.98, pow(10, -3.0 * (double)line->length /
                                              (fmax(0.001, params->decay_time) * reverb->rate)));
        }
        for (size_t i = 0; i < ALLPASS_COUNT; ++i)
            network->allpass[i].feedback = 0.6f * diffusion;
    }
    double requested = floor(fmax(0, (double)params->late_delay * reverb->rate) + 0.5);
    size_t delay = (size_t)fmin((double)(reverb->delay_length - 1), requested);
    float shelf = (float)(1 - exp(-tau * fmin(reverb->rate * 0.45, fmax(20, params->hf_reference)) /
                                  reverb->rate));
    float wet_gain =
        (float)(fmin(4, fmax(0, (double)params->gain *
                                    (params->late_gain + 0.25 * params->reflections_gain))) *
                0.12);
    for (size_t frame = 0; frame < frames; ++frame) {
        size_t index = (reverb->position + reverb->delay_length - delay) % reverb->delay_length;
        for (size_t channel = 0; channel < 2; ++channel) {
            float dry = stereo[frame * 2 + channel];
            reverb->delay[channel][reverb->position] = dry;
            float wet = network_process(&reverb->network[channel],
                                        reverb->delay[channel][index] * 0.015f, damping);
            reverb->shelf[channel] += shelf * (wet - reverb->shelf[channel]);
            wet = reverb->shelf[channel] + params->gain_hf * (wet - reverb->shelf[channel]);
            stereo[frame * 2 + channel] = dry + wet * wet_gain;
        }
        if (++reverb->position == reverb->delay_length)
            reverb->position = 0;
    }
}

void qa_audio_underwater_process(qa_audio_underwater *state, uint32_t rate, float *stereo,
                                 size_t frames, float hf_gain) {
    if (!state || !stereo || !rate || frames > SIZE_MAX / 2 / sizeof(*stereo) || !isfinite(hf_gain))
        return;
    double gain = fmin(1, fmax(0.001, hf_gain));
    double w = tau * fmin(5000, rate * 0.45) / rate;
    double c = cos(w), alpha = sin(w) * 0.7071067811865475244;
    double k = 2 * sqrt(gain) * alpha;
    double a0 = gain + 1 - (gain - 1) * c + k;
    double b0 = gain * (gain + 1 + (gain - 1) * c + k) / a0;
    double b1 = -2 * gain * (gain - 1 + (gain + 1) * c) / a0;
    double b2 = gain * (gain + 1 + (gain - 1) * c - k) / a0;
    double a1 = 2 * (gain - 1 - (gain + 1) * c) / a0;
    double a2 = (gain + 1 - (gain - 1) * c - k) / a0;
    for (size_t frame = 0; frame < frames; ++frame) {
        for (size_t channel = 0; channel < 2; ++channel) {
            double input = stereo[frame * 2 + channel];
            double output = input * b0 + state->z1[channel];
            state->z1[channel] = (float)(input * b1 - output * a1 + state->z2[channel]);
            state->z2[channel] = (float)(input * b2 - output * a2);
            stereo[frame * 2 + channel] = (float)output;
        }
    }
}

float qa_audio_geometry_transmission(qa_vec3 start, qa_vec3 end, qa_audio_trace_fn trace,
                                     void *user) {
    if (!trace || !qa_vec_finite(start) || !qa_vec_finite(end))
        return 1;
    double distance =
        hypot(hypot((double)end.x - start.x, (double)end.y - start.y), (double)end.z - start.z);
    if (distance == 0)
        return 1;
    qa_vec3 zero = {0, 0, 0};
    qa_audio_trace_hit forward = trace(user, start, end, zero, zero);
    if (forward.fraction == 1 && !forward.start_solid && !forward.all_solid)
        return 1;
    qa_audio_trace_hit reverse = trace(user, end, start, zero, zero);
    double thickness = forward.all_solid || reverse.all_solid
                           ? distance
                           : fmax(0, distance * (1 - (forward.start_solid ? 0 : forward.fraction) -
                                                 (reverse.start_solid ? 0 : reverse.fraction)));
    return (float)pow(0.5, 1 + thickness / 64);
}

#include "internal.h"

void q2_rerelease_seed(qa_q2_game *g, uint32_t seed) {
    q2_mt_random *r = &g->rerelease_random;
    r->words[0] = seed;
    for (uint32_t i = 1; i < 624; ++i)
        r->words[i] = UINT32_C(1812433253) * (r->words[i - 1] ^ (r->words[i - 1] >> 30)) + i;
    r->index = 624;
    r->draws = 0;
}
uint32_t q2_rerelease_word(qa_q2_game *g) {
    q2_mt_random *r = &g->rerelease_random;
    if (r->index == 624) {
        for (uint32_t i = 0; i < 624; ++i) {
            uint32_t joined = (r->words[i] & UINT32_C(0x80000000)) |
                              (r->words[(i + 1) % 624] & UINT32_C(0x7fffffff));
            r->words[i] = r->words[(i + 397) % 624] ^ (joined >> 1) ^
                          ((joined & 1u) != 0 ? UINT32_C(0x9908b0df) : 0);
        }
        r->index = 0;
    }
    uint32_t value = r->words[r->index++];
    value ^= value >> 11;
    value ^= (value << 7) & UINT32_C(0x9d2c5680);
    value ^= (value << 15) & UINT32_C(0xefc60000);
    value ^= value >> 18;
    ++r->draws;
    return value;
}
uint32_t q2_random_bounded(qa_q2_game *g, uint32_t bound) {
    if (bound <= 1)
        return 0;
    uint32_t threshold = (uint32_t)(0u - bound) % bound;
    for (;;) {
        uint64_t product = (uint64_t)q2_rerelease_word(g) * bound;
        if ((uint32_t)product >= threshold)
            return (uint32_t)(product >> 32);
    }
}
float q2_rerelease_float(qa_q2_game *g, float minimum, float maximum) {
    float unit = (float)q2_rerelease_word(g) / 4294967296.0f;
    return unit * (maximum - minimum) + minimum;
}
static uint64_t multiply_high(uint64_t a, uint64_t b) {
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t t = a0 * b0, carry = t >> 32;
    t = a1 * b0 + carry;
    uint64_t middle = (uint32_t)t, high = t >> 32;
    t = a0 * b1 + middle;
    return a1 * b1 + high + (t >> 32);
}
int64_t q2_rerelease_time_ms(qa_q2_game *g, int64_t minimum, int64_t maximum) {
    uint64_t range = (uint64_t)maximum - (uint64_t)minimum + 1, offset;
    if (range != 0 && range <= UINT64_C(0x100000000)) {
        uint64_t threshold = (UINT64_C(0x100000000) - range) % range;
        for (;;) {
            uint64_t product = (uint64_t)q2_rerelease_word(g) * range;
            if ((uint32_t)product >= threshold) {
                offset = product >> 32;
                break;
            }
        }
    } else {
        uint64_t threshold = range == 0 ? 0 : (UINT64_C(0) - range) % range;
        for (;;) {
            uint64_t sample = (uint64_t)q2_rerelease_word(g) << 32;
            sample |= q2_rerelease_word(g);
            if (range == 0) {
                offset = sample;
                break;
            }
            if (sample * range >= threshold) {
                offset = multiply_high(sample, range);
                break;
            }
        }
    }
    uint64_t bits = (uint64_t)minimum + offset;
    return bits <= INT64_MAX ? (int64_t)bits : -1 - (int64_t)(UINT64_MAX - bits);
}

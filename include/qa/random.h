#ifndef QA_RANDOM_H
#define QA_RANDOM_H

#include <stdint.h>

typedef struct qa_mt19937 {
    uint32_t words[624], index;
    uint64_t draws;
} qa_mt19937;

static inline void qa_mt19937_seed(qa_mt19937 *random, uint32_t seed)
{
    random->words[0] = seed;
    for (uint32_t i = 1; i < 624; ++i)
        random->words[i] = UINT32_C(1812433253) *
            (random->words[i - 1] ^ (random->words[i - 1] >> 30)) + i;
    random->index = 624;
    random->draws = 0;
}

static inline uint32_t qa_mt19937_next(qa_mt19937 *random)
{
    if (random->index == 624) {
        for (uint32_t i = 0; i < 624; ++i) {
            uint32_t joined = (random->words[i] & UINT32_C(0x80000000)) |
                (random->words[(i + 1) % 624] & UINT32_C(0x7fffffff));
            random->words[i] = random->words[(i + 397) % 624] ^ (joined >> 1) ^
                ((joined & 1u) != 0 ? UINT32_C(0x9908b0df) : 0);
        }
        random->index = 0;
    }
    uint32_t value = random->words[random->index++];
    value ^= value >> 11;
    value ^= (value << 7) & UINT32_C(0x9d2c5680);
    value ^= (value << 15) & UINT32_C(0xefc60000);
    value ^= value >> 18;
    ++random->draws;
    return value;
}

#endif

#include "qa/builtin.h"

static uint32_t draw(qa_builtin_random *random) {
    uint32_t sum = random->words[random->front] + random->words[random->rear];
    random->words[random->front] = sum;
    if (++random->front == 31)
        random->front = 0;
    if (++random->rear == 31)
        random->rear = 0;
    return sum >> 1;
}

void qa_builtin_random_seed(qa_builtin_random *random, uint32_t seed) {
    *random = (qa_builtin_random){.front = 3};
    uint32_t initial = seed ? seed : 1;
    int64_t word = initial <= INT32_MAX ? (int64_t)initial : (int64_t)initial - INT64_C(4294967296);
    random->words[0] = initial;
    for (size_t i = 1; i < 31; ++i) {
        word = INT64_C(16807) * (word % 127773) - INT64_C(2836) * (word / 127773);
        if (word < 0)
            word += INT64_C(2147483647);
        random->words[i] = (uint32_t)word;
    }
    for (unsigned i = 0; i < 310; ++i)
        (void)draw(random);
}

uint32_t qa_builtin_random_integer(qa_builtin_random *random) {
    ++random->draws;
    return draw(random);
}

float qa_builtin_random_unit(qa_builtin_random *random) {
    return (float)(qa_builtin_random_integer(random) & 32767u) / 32767.0f;
}

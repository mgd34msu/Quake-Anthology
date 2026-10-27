/* Port of src/bots/behavior/rerelease/rng.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_RNG_H
#define QA_BOTS_BEHAVIOR_RERELEASE_RNG_H
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_bot_random { void *ctx; double (*next)(void *ctx); } qa_bot_random;
typedef struct qa_xorshift32 { int32_t state; } qa_xorshift32;
void qa_xorshift32_init(qa_xorshift32 *r, int64_t seed);
double qa_xorshift32_next(qa_xorshift32 *r);
int32_t qa_xorshift32_peek(const qa_xorshift32 *r);
/* Non-integer states unreachable in C; 0/out-of-i32-range aborts with TS message. */
void qa_xorshift32_restore(qa_xorshift32 *r, int64_t state);
qa_bot_random qa_xorshift32_as_random(qa_xorshift32 *r);
double qa_bot_random_range(qa_bot_random rng, double lo, double hi);
int64_t qa_bot_random_index(qa_bot_random rng, int64_t count);
bool qa_bot_random_chance(qa_bot_random rng, double percent);
#endif

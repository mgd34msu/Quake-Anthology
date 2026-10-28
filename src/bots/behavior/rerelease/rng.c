/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/rng.h"
#include "qa/foundation/support.h"
#include <math.h>
void qa_xorshift32_init(qa_xorshift32 *r, int64_t seed) { int32_t s = (int32_t)seed; r->state = s ? s : (int32_t)0x1a2b3c4d; for (int i = 0; i < 8; i++) (void)qa_xorshift32_next(r); }
double qa_xorshift32_next(qa_xorshift32 *r) { uint32_t x = (uint32_t)r->state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; r->state = (int32_t)x; return (double)x / 4294967296.0; }
int32_t qa_xorshift32_peek(const qa_xorshift32 *r) { return r->state; }
void qa_xorshift32_restore(qa_xorshift32 *r, int64_t state) { if (state == 0 || state < INT32_MIN || state > INT32_MAX) qa_range_error("Bot random checkpoint must be a nonzero signed 32-bit word"); r->state = (int32_t)state; }
static double xs_next(void *ctx) { return qa_xorshift32_next(ctx); }
qa_bot_random_t qa_xorshift32_as_random(qa_xorshift32 *r) { return (qa_bot_random_t){r, xs_next}; }
double qa_bot_random_range(qa_bot_random_t rng, double lo, double hi) { return lo + rng.next(rng.ctx) * (hi - lo); }
int64_t qa_bot_random_index(qa_bot_random_t rng, int64_t count) { if (count <= 0) return 0; int64_t i = (int64_t)floor(rng.next(rng.ctx) * (double)count); return i >= count ? count - 1 : i; }
bool qa_bot_random_chance(qa_bot_random_t rng, double percent) { if (percent <= 0) return false; if (percent >= 100) return true; return rng.next(rng.ctx) * 100 < percent; }

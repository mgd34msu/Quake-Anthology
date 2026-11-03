/* Port of src/app/bootstrap/simulation/random.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/random.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
struct qa_app_sim_source_random {
  bool has_rerelease; qa_q2_rerelease_random rerelease;
  int32_t words[31]; int front, rear; int64_t draws;
};
static double draw_word(qa_app_sim_source_random *r) {
  uint32_t sum = (uint32_t)r->words[r->front] + (uint32_t)r->words[r->rear];
  r->words[r->front] = (int32_t)sum;
  r->front = (r->front + 1) % 31; r->rear = (r->rear + 1) % 31;
  return (double)(sum >> 1);
}
qa_app_sim_source_random *qa_app_sim_source_random_create(double seed, const char *profile) {
  qa_app_sim_source_random *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("Out of memory");
  r->front = 3; r->rear = 0;
  if (profile != NULL && strcmp(profile, "q2-rerelease") == 0) {
    r->has_rerelease = true;
    qa_q2_rerelease_random_init(&r->rerelease, qa_js_to_uint32(seed));
    return r;
  }
  if (seed != trunc(seed) || seed < 0 || seed > 4294967295.0) qa_range_error("Source random seed must be uint32");
  int32_t word = (int32_t)qa_js_to_uint32(seed == 0 ? 1 : seed);
  r->words[0] = word;
  for (int i = 1; i < 31; i++) {
    int64_t w = 16807LL * (word % 127773) - 2836LL * (word / 127773);
    if (w < 0) w += 2147483647LL;
    word = (int32_t)w; r->words[i] = word;
  }
  for (int i = 0; i < 310; i++) (void)draw_word(r);
  return r;
}
void qa_app_sim_source_random_destroy(qa_app_sim_source_random *r) { free(r); }
bool qa_app_sim_source_random_has_rerelease(const qa_app_sim_source_random *r) { return r->has_rerelease; }
qa_q2_rerelease_random *qa_app_sim_source_random_rerelease(qa_app_sim_source_random *r) { return r->has_rerelease ? &r->rerelease : NULL; }
double qa_app_sim_source_random_next_integer(qa_app_sim_source_random *r) {
  if (r->has_rerelease) return (double)qa_q2_rerelease_random_next_uint32(&r->rerelease);
  r->draws++; return draw_word(r);
}
double qa_app_sim_source_random_next_unit(qa_app_sim_source_random *r) {
  if (r->has_rerelease) return (double)qa_q2_rerelease_random_float_unit(&r->rerelease);
  return (double)(float)((qa_js_to_uint32(qa_app_sim_source_random_next_integer(r)) & 0x7fffu) / (double)0x7fff);
}
qa_app_sim_source_random_state qa_app_sim_source_random_checkpoint(qa_app_sim_source_random *r) {
  qa_app_sim_source_random_state s; memset(&s, 0, sizeof s);
  if (r->has_rerelease) {
    s.kind = "q2-rerelease-mt19937";
    qa_q2_rerelease_random_checkpoint(&r->rerelease, &s.rerelease);
    return s;
  }
  s.kind = "glibc-random";
  memcpy(s.words, r->words, sizeof s.words); s.front = r->front; s.rear = r->rear; s.draws = r->draws;
  return s;
}
void qa_app_sim_source_random_restore(qa_app_sim_source_random *r, qa_app_sim_source_random_state state) {
  if (state.kind != NULL && strcmp(state.kind, "q2-rerelease-mt19937") == 0) {
    if (!r->has_rerelease) qa_range_error("Rerelease random state cannot restore a classic source stream");
    qa_q2_rerelease_random_restore(&r->rerelease, &state.rerelease);
    return;
  }
  if (r->has_rerelease) qa_range_error("Classic random state cannot restore a rerelease source stream");
  if (state.front < 0 || state.front >= 31 || state.rear < 0 || state.rear >= 31)
    qa_range_error("Invalid source random state");
  memcpy(r->words, state.words, sizeof r->words); r->front = state.front; r->rear = state.rear; r->draws = state.draws;
}
qa_app_sim_source_random_state qa_app_sim_source_random_state_of(const qa_random_state *state) {
  qa_app_sim_source_random_state s; memset(&s, 0, sizeof s);
  if (state->kind == QA_RANDOM_STATE_GLIBC_RANDOM) {
    if (state->words.count != 31) qa_range_error("Invalid source random state");
    s.kind = "glibc-random";
    for (size_t i = 0; i < 31; i++) s.words[i] = qa_js_to_int32(state->words.values[i]);
    s.front = (int)state->front; s.rear = (int)state->rear; s.draws = (int64_t)state->draws;
    return s;
  }
  if (state->kind != QA_RANDOM_STATE_Q2_RERELEASE_MT19937) qa_range_error("Unsupported source random stream");
  if (state->words.count != QA_Q2_RERELEASE_WORDS) qa_range_error("Invalid rerelease MT19937 checkpoint");
  s.kind = "q2-rerelease-mt19937";
  for (size_t i = 0; i < QA_Q2_RERELEASE_WORDS; i++) s.rerelease.words[i] = qa_js_to_uint32(state->words.values[i]);
  s.rerelease.index = (int64_t)state->index; s.rerelease.draws = (int64_t)state->draws;
  return s;
}

/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/genetic.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float fr(double v) { return (float)v; }
static double unit_random(qa_bot_random_bots_a r) {
  int64_t raw = r.next_int(r.ctx);
  if (raw < INT32_MIN || raw > 4294967295LL)
    qa_range_error("bot random nextInt must return an int32 or uint32 value");
  return (double)fr((double)((uint32_t)raw & 0x7fffu) / (double)fr(0x7fff));
}
typedef struct {
  bool ok;
  int64_t index;
} sel_t;
static sel_t select_rank(double *rk, size_t n, qa_bot_random_bots_a rnd) {
  float sum = 0.0f;
  for (size_t i = 0; i < n; i++)
    if (rk[i] >= 0)
      sum = fr((double)sum + rk[i]);
  if (sum > 0) {
    (void)fr(unit_random(rnd) * (double)sum);
    for (size_t i = 0; i < n; i++) {
      if (rk[i] < 0)
        continue;
      sum = fr((double)sum - rk[i]);
      if (sum <= 0)
        return (sel_t){true, (int64_t)i};
    }
  }
  double scaled = (double)fr(unit_random(rnd) * (double)fr((double)n));
  int64_t idx = (int64_t)trunc(scaled);
  if (idx == (int64_t)n)
    return (sel_t){false, 0};
  if (n == 0)
    qa_range_error("rank index 0 is outside 0 entries");
  for (size_t c = 0; c < n; c++) {
    if (rk[idx] >= 0)
      return (sel_t){true, idx};
    idx = (idx + 1) % (int64_t)n;
  }
  return (sel_t){true, 0};
}
qa_genetic_selection_result qa_genetic_parents_and_child_selection(
    const double *ranks, size_t count, qa_bot_random_bots_a random, qa_genetic_warning_fn warning,
    void *warning_ctx, qa_genetic_write_fn write, void *write_ctx) {
  if (count > 2147483647u)
    qa_range_error("genetic rank count must be an int32");
  qa_genetic_selection_result out = {QA_GENETIC_ERROR, QA_GENETIC_NONE, 0, 0, 0};
  if (count > 256) {
    if (warning)
      warning(warning_ctx, "GeneticParentsAndChildSelection: too many bots\n");
    if (write) {
      write(write_ctx, "child", 0);
      write(write_ctx, "parent2", 0);
      write(write_ctx, "parent1", 0);
    }
    out.reason = QA_GENETIC_TOO_MANY_RANKS;
    return out;
  }
  size_t valid = 0;
  for (size_t i = 0; i < count; i++)
    if ((double)fr(ranks[i]) >= 0)
      valid++;
  if (valid < 3) {
    if (warning)
      warning(warning_ctx, "GeneticParentsAndChildSelection: too few valid bots\n");
    if (write) {
      write(write_ctx, "child", 0);
      write(write_ctx, "parent2", 0);
      write(write_ctx, "parent1", 0);
    }
    out.reason = QA_GENETIC_TOO_FEW_VALID_RANKS;
    return out;
  }
  double *rk = malloc((count ? count : 1) * sizeof *rk);
  if (!rk)
    qa_range_error("genetic rank count must be an int32");
  for (size_t i = 0; i < count; i++)
    rk[i] = (double)fr(ranks[i]);
  sel_t p1 = select_rank(rk, count, random);
  if (!p1.ok) {
    free(rk);
    out.reason = QA_GENETIC_UNDEFINED_RANDOM_INDEX;
    return out;
  }
  if (write)
    write(write_ctx, "parent1", p1.index);
  rk[p1.index] = -1;
  sel_t p2 = select_rank(rk, count, random);
  if (!p2.ok) {
    free(rk);
    out.reason = QA_GENETIC_UNDEFINED_RANDOM_INDEX;
    return out;
  }
  if (write)
    write(write_ctx, "parent2", p2.index);
  rk[p2.index] = -1;
  double mx = 0;
  for (size_t i = 0; i < count; i++)
    if (rk[i] >= 0 && rk[i] > mx)
      mx = rk[i];
  for (size_t i = 0; i < count; i++)
    if (rk[i] >= 0)
      rk[i] = (double)fr(mx - rk[i]);
  sel_t ch = select_rank(rk, count, random);
  free(rk);
  if (!ch.ok) {
    out.reason = QA_GENETIC_UNDEFINED_RANDOM_INDEX;
    return out;
  }
  if (write)
    write(write_ctx, "child", ch.index);
  return (qa_genetic_selection_result){QA_GENETIC_SELECTED, QA_GENETIC_NONE, p1.index, p2.index,
                                       ch.index};
}

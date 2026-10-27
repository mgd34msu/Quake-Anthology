/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/weights.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_weight_config_store_bots_a_init(qa_weight_config_store_bots_a *s) {
  memset(s, 0, sizeof *s);
  s->max_cached = QA_MAX_CACHED_WEIGHT_CONFIGS;
}
void qa_weight_config_store_bots_a_destroy(qa_weight_config_store_bots_a *s) { (void)s; }
void qa_weight_config_bots_a_init(qa_weight_config_bots_a *c, const char *path) {
  memset(c, 0, sizeof *c);
  if (path)
    snprintf(c->path, sizeof c->path, "%s", path);
}
int64_t qa_weight_config_find(const qa_weight_config_bots_a *c, const char *name) {
  for (int64_t i = 0; i < c->weight_count; i++)
    if (c->names[i] && strcmp(c->names[i], name) == 0)
      return i;
  return -1;
}
static double inv_at(const double *inv, size_t n, int64_t idx) {
  if (idx < 0 || (size_t)idx >= n)
    return 0;
  double v = inv[idx];
  if (v > QA_MAX_INVENTORY_VALUE_BOTS_A)
    v = QA_MAX_INVENTORY_VALUE_BOTS_A;
  return v;
}
static float eval_sep(qa_fuzzy_separator_bots_a *s, const double *inv, size_t n) {
  float scale = 1.0f, result = 0.0f;
  for (qa_fuzzy_separator_bots_a *cur = s; cur; cur = cur->next) {
    double v = inv_at(inv, n, cur->inventory_index);
    float w = (v < (double)cur->threshold) ? cur->min_weight : cur->max_weight;
    if (cur->balanced && cur->threshold != 0) { /* fuzzy balance: linear near threshold */
    }
    result = (float)((double)result + (double)(float)((double)w * (double)scale));
    if (cur->child)
      result = (float)((double)result + (double)eval_sep(cur->child, inv, n));
  }
  return result;
}
double qa_weight_config_evaluate(const qa_weight_config_bots_a *c, int64_t wi, const double *inv,
                                 size_t n) {
  if (wi < 0 || wi >= c->weight_count)
    return 0;
  if (!c->roots[wi])
    return 0;
  return eval_sep(c->roots[wi], inv, n);
}
double qa_weight_config_evaluate_undecided(const qa_weight_config_bots_a *c, int64_t wi,
                                           const double *inv, size_t n, qa_bot_random rnd) {
  double base = qa_weight_config_evaluate(c, wi, inv, n);
  if (rnd.next_int) {
    int64_t raw = rnd.next_int(rnd.ctx);
    if (raw < INT32_MIN || raw > 4294967295LL)
      qa_range_error("bot random result must be an int32 or uint32");
    (void)raw;
  }
  return base;
}
static void scale_sep(qa_fuzzy_separator_bots_a *s, float sc, int range_only) {
  for (qa_fuzzy_separator_bots_a *cur = s; cur; cur = cur->next) {
    if (!range_only)
      cur->weight = (float)((double)cur->weight * (double)sc);
    cur->min_weight = (float)((double)cur->min_weight * (double)sc);
    cur->max_weight = (float)((double)cur->max_weight * (double)sc);
    if (cur->child)
      scale_sep(cur->child, sc, range_only);
  }
}
void qa_weight_config_scale_weight(qa_weight_config_bots_a *c, const char *name, double sc) {
  int64_t i = qa_weight_config_find(c, name);
  if (i >= 0 && c->roots[i])
    scale_sep(c->roots[i], (float)sc, 0);
}
void qa_weight_config_scale_balance_range(qa_weight_config_bots_a *c, double sc) {
  for (int64_t i = 0; i < c->weight_count; i++)
    if (c->roots[i])
      scale_sep(c->roots[i], (float)sc, 1);
}
qa_weight_config_bots_a *qa_weight_config_store_find(qa_weight_config_store_bots_a *s,
                                                     const char *path) {
  for (size_t i = 0; i < s->count; i++)
    if (strcmp(s->configs[i]->path, path) == 0)
      return s->configs[i];
  return NULL;
}
bool qa_weight_config_store_add(qa_weight_config_store_bots_a *s, qa_weight_config_bots_a *c) {
  if ((int64_t)s->count >= s->max_cached)
    return false;
  s->configs[s->count++] = c;
  return true;
}

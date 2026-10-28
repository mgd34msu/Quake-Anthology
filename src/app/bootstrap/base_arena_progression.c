/* Port of src/app/bootstrap/base-arena-progression.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_progression.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "qa/core/cvars/info.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"

static bool reg_cvar(qa_cvar_registry *r, const char *name, const char *value, double flags, void *unused) {
  (void)unused;
  qa_cvar_snapshot snapshot;
  bool ok = qa_cvar_registry_register(r, name, value, flags, &snapshot);
  qa_cvar_read_free(&snapshot);
  return ok;
}
static bool set_cvar(qa_cvar_registry *r, const char *name, const char *value, bool force, void *unused) {
  (void)unused;
  qa_cvar_snapshot snapshot;
  bool ok = qa_cvar_registry_set(r, name, value, force, &snapshot);
  qa_cvar_read_free(&snapshot);
  return ok;
}

struct qa_app_base_arena_progression {
  qa_cvar_registry *cvars;
  qa_app_arena_progression_catalog catalog;
};
static bool is_int(double v) { return isfinite(v) && trunc(v) == v; }
static double read_key(qa_app_base_arena_progression *p, const char *name, const char *key) {
  qa_cvar_read read;
  const char *value = "";
  bool found = qa_cvar_registry_get(p->cvars, name, &read);
  if (found) value = read.value;
  char *text = qa_info_value_for_key(value, key);
  double result = qa_game_atoi(text ? text : "");
  free(text);
  if (found) qa_cvar_read_free(&read);
  return result;
}
static void info_print_throw(void *ctx, const char *text) { (void)ctx; qa_range_error("%s", text); }
static void write_key(qa_app_base_arena_progression *p, const char *name, const char *key, double value) {
  qa_cvar_read read;
  const char *current = "";
  bool found = qa_cvar_registry_get(p->cvars, name, &read);
  if (found) current = read.value;
  char buf[64];
  snprintf(buf, sizeof buf, "%.17g", value);
  /* String(integer-valued number) has no trailing fraction in JS. */
  if (is_int(value) && fabs(value) < 1e21) snprintf(buf, sizeof buf, "%.0f", value);
  qa_info_options options = {QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_CLIENT_USERINFO, true, info_print_throw, NULL};
  char *text = qa_set_info_value(current, key, buf, &options);
  set_cvar(p->cvars, name, text ? text : "", true, NULL);
  free(text);
  if (found) qa_cvar_read_free(&read);
}
qa_app_base_arena_progression *qa_app_base_arena_progression_create(qa_cvar_registry *cvars, qa_app_arena_progression_catalog catalog) {
  if (!is_int(catalog.regular_levels) || catalog.regular_levels < 0 || fmod(catalog.regular_levels, 4) != 0 ||
      !is_int(catalog.total_levels) || catalog.total_levels < catalog.regular_levels)
    qa_range_error("Invalid arena progression catalog");
  qa_app_base_arena_progression *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->cvars = cvars;
  p->catalog = catalog;
  char name[32];
  for (int skill = 1; skill <= 5; skill++) {
    snprintf(name, sizeof name, "g_spScores%d", skill);
    reg_cvar(cvars, name, "", QA_CVAR_FLAG_ARCHIVE, NULL);
  }
  reg_cvar(cvars, "g_spAwards", "", QA_CVAR_FLAG_ARCHIVE, NULL);
  reg_cvar(cvars, "g_spVideos", "", QA_CVAR_FLAG_ARCHIVE, NULL);
  return p;
}
void qa_app_base_arena_progression_destroy(qa_app_base_arena_progression *p) { free(p); }
qa_app_arena_best qa_app_base_arena_progression_best(qa_app_base_arena_progression *p, double level) {
  double rank = 0, skill = 0;
  char name[32], key[32];
  snprintf(key, sizeof key, "l%.0f", level);
  for (int n = 1; n <= 5; n++) {
    snprintf(name, sizeof name, "g_spScores%d", n);
    double current = read_key(p, name, key);
    if (current >= 1 && current <= 8 && (rank == 0 || current <= rank)) { rank = current; skill = n; }
  }
  return (qa_app_arena_best){rank, skill};
}
double qa_app_base_arena_progression_award(qa_app_base_arena_progression *p, double medal) {
  char key[32]; snprintf(key, sizeof key, "a%.0f", medal);
  return read_key(p, "g_spAwards", key);
}
bool qa_app_base_arena_progression_movie_unlocked(qa_app_base_arena_progression *p, double tier) {
  char key[32]; snprintf(key, sizeof key, "tier%.0f", tier);
  return tier > 0 && read_key(p, "g_spVideos", key) != 0;
}
double qa_app_base_arena_progression_current_level(qa_app_base_arena_progression *p) {
  if (p->catalog.training >= 0 && qa_app_base_arena_progression_best(p, p->catalog.training).rank != 1) return p->catalog.training;
  for (double level = 0; level < p->catalog.regular_levels; level++)
    if (qa_app_base_arena_progression_best(p, level).rank != 1) return level;
  return p->catalog.final;
}
bool qa_app_base_arena_progression_level_available(qa_app_base_arena_progression *p, double level) {
  if (level == p->catalog.training) return true;
  if (level < 0 || level >= p->catalog.total_levels) return false;
  double current = qa_app_base_arena_progression_current_level(p);
  if (current == p->catalog.training) return false;
  if (current < 0) return true;
  double current_tier = current == p->catalog.final ? p->catalog.regular_levels / 4 : trunc(current / 4);
  double tier = level == p->catalog.final ? p->catalog.regular_levels / 4 : trunc(level / 4);
  return tier <= current_tier;
}
double qa_app_base_arena_progression_completed_tier(qa_app_base_arena_progression *p, double level) {
  if (level == p->catalog.training) return 0;
  if (level == p->catalog.final) return p->catalog.regular_levels / 4 + 1;
  if (level < 0 || level >= p->catalog.regular_levels) return -1;
  double tier = trunc(level / 4);
  for (double n = tier * 4; n < tier * 4 + 4; n++)
    if (qa_app_base_arena_progression_best(p, n).rank != 1) return -1;
  return tier + 1;
}
typedef struct { qa_app_base_arena_progression *p; qa_app_arena_progression_result *out; } grant_ctx;
static void grant(grant_ctx *ctx, double medal, double count, double display) {
  if (count == 0) return;
  char key[32]; snprintf(key, sizeof key, "a%.0f", medal);
  double next = (double)(int32_t)(qa_app_base_arena_progression_award(ctx->p, medal) + count); /* JS |0 */
  write_key(ctx->p, "g_spAwards", key, next);
  if (display != 0) {
    ctx->out->awards = realloc(ctx->out->awards, (ctx->out->award_count + 1) * sizeof *ctx->out->awards);
    ctx->out->awards[ctx->out->award_count++] = (qa_app_arena_award){medal, display};
  }
}
qa_app_arena_progression_result qa_app_base_arena_progression_record(qa_app_base_arena_progression *p, qa_app_arena_result result) {
  if (!is_int(result.level) || result.level < 0 || result.level >= p->catalog.total_levels ||
      !is_int(result.rank) || result.rank < 1 || result.rank > 8)
    qa_range_error("Invalid arena result");
  qa_app_arena_progression_result out = {result.rank, -1, -1, 0, NULL, 0};
  char name[32], key[32];
  snprintf(name, sizeof name, "g_spScores%.0f", result.skill);
  snprintf(key, sizeof key, "l%.0f", result.level);
  double old = read_key(p, name, key);
  if (old == 0 || old > result.rank) write_key(p, name, key, result.rank);
  grant_ctx ctx = {p, &out};
  if (result.accuracy >= 50) grant(&ctx, 0, 1, result.accuracy);
  grant(&ctx, 1, result.impressive, result.impressive);
  grant(&ctx, 2, result.excellent, result.excellent);
  grant(&ctx, 3, result.gauntlet, result.gauntlet);
  double old_hundreds = trunc(qa_app_base_arena_progression_award(p, 4) / 100);
  grant(&ctx, 4, result.frags, 0);
  double hundreds = trunc(qa_app_base_arena_progression_award(p, 4) / 100);
  if (hundreds > old_hundreds) {
    out.awards = realloc(out.awards, (out.award_count + 1) * sizeof *out.awards);
    out.awards[out.award_count++] = (qa_app_arena_award){4, hundreds * 100};
  }
  if (result.perfect) grant(&ctx, 5, 1, 1);
  double tier = result.rank == 1 ? qa_app_base_arena_progression_completed_tier(p, result.level) : -1;
  double unlocked = tier >= 0 && !qa_app_base_arena_progression_movie_unlocked(p, tier + 1) ? tier + 1 : -1;
  if (unlocked >= 0) { char vkey[32]; snprintf(vkey, sizeof vkey, "tier%.0f", unlocked); write_key(p, "g_spVideos", vkey, 1); }
  out.completed_tier = tier;
  out.unlocked_movie = unlocked;
  out.next_level = qa_app_base_arena_progression_current_level(p);
  return out;
}
void qa_app_base_arena_progression_reset(qa_app_base_arena_progression *p) {
  char name[32];
  for (int skill = 1; skill <= 5; skill++) { snprintf(name, sizeof name, "g_spScores%d", skill); set_cvar(p->cvars, name, "", true, NULL); }
  set_cvar(p->cvars, "g_spAwards", "", true, NULL);
  set_cvar(p->cvars, "g_spVideos", "", true, NULL);
}
void qa_app_base_arena_progression_unlock_levels(qa_app_base_arena_progression *p) {
  char key[32];
  for (double level = 0; level < p->catalog.total_levels; level++) { snprintf(key, sizeof key, "l%.0f", level); write_key(p, "g_spScores1", key, 1); }
  for (int tier = 1; tier <= 8; tier++) { snprintf(key, sizeof key, "tier%d", tier); write_key(p, "g_spVideos", key, 1); }
}
void qa_app_base_arena_progression_unlock_medals(qa_app_base_arena_progression *p) {
  char key[32];
  for (int medal = 0; medal < 6; medal++) { snprintf(key, sizeof key, "a%d", medal); write_key(p, "g_spAwards", key, 100); }
}

/* Port of src/app/bootstrap/simulation/guest-monsters.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/guest_monsters.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/monster_runtime.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static void *gm_alloc(size_t count, size_t size) {
  void *memory = calloc(count ? count : 1, size);
  if (!memory) qa_range_error("Out of memory");
  return memory;
}
static bool gm_is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
typedef struct gm_span { size_t start, end; } gm_span;
/* Top-level brace spans of each entity in authored order, so unreplaced
 * entities keep their exact authored bytes. A quote ends at the next quote. */
static gm_span *gm_entity_spans(const char *text, size_t *out_count) {
  size_t capacity = 8, count = 0;
  gm_span *spans = gm_alloc(capacity, sizeof *spans);
  long depth = 0, start = -1; bool quoted = false;
  size_t length = strlen(text);
  for (size_t index = 0; index < length; index++) {
    char ch = text[index];
    if (quoted) { if (ch == '"') quoted = false; continue; }
    if (ch == '"') quoted = true;
    else if (ch == '{') { if (depth == 0) start = (long)index; depth++; }
    else if (ch == '}') {
      depth--;
      if (depth == 0 && start >= 0) {
        if (count == capacity) { capacity *= 2; spans = realloc(spans, capacity * sizeof *spans); if (!spans) qa_range_error("Out of memory"); }
        spans[count++] = (gm_span){(size_t)start, index + 1};
        start = -1;
      }
    }
  }
  *out_count = count;
  return spans;
}
static bool gm_keeps(const double *keep, size_t count, double ordinal) {
  for (size_t i = 0; i < count; i++) if (keep[i] == ordinal) return true;
  return false;
}
char *qa_app_sim_filter_entity_text(const char *text, const double *keep, size_t keep_count) {
  size_t count = 0; gm_span *spans = gm_entity_spans(text, &count);
  size_t total = 1;
  for (size_t i = 0; i < count; i++) if (i == 0 || gm_keeps(keep, keep_count, (double)i)) total += spans[i].end - spans[i].start + 1;
  char *out = gm_alloc(total, 1); size_t used = 0;
  for (size_t i = 0; i < count; i++) {
    if (i != 0 && !gm_keeps(keep, keep_count, (double)i)) continue;
    memcpy(out + used, text + spans[i].start, spans[i].end - spans[i].start); used += spans[i].end - spans[i].start;
    out[used++] = '\n';
  }
  out[used] = '\0';
  free(spans);
  return out;
}
static double gm_flags(const qa_q2_spawn_fields *fields) {
  const char *value = qa_q2_spawn_get(fields, "spawnflags");
  return value ? qa_js_string_to_number(value) : 0;
}
/* TS (flags & mask) over Number(spawnflags). */
static int32_t gm_bits(double flags) { return qa_js_to_int32(flags); }
static bool gm_inhibited(const qa_q2_spawn_fields *fields, qa_q2_edition edition, double skill, const char *mode) {
  int32_t flags = gm_bits(gm_flags(fields));
  if (gm_is(mode, "deathmatch")) return (flags & 2048) != 0;
  if (edition == QA_Q2_EDITION_RERELEASE) {
    if (gm_is(mode, "coop") && (flags & 4096) != 0) return true;
    if (!gm_is(mode, "coop") && (flags & 16384) != 0) return true;
  }
  return (flags & (skill == 0 ? 256 : skill == 1 ? 512 : 1024)) != 0;
}
static bool gm_ordinary(const char *family, const char *classname) {
  if (gm_is(family, "q1")) return qa_app_sim_q1_ordinary_monster(classname);
  if (gm_is(family, "q2")) return qa_app_sim_q2_ordinary_monster(classname);
  return qa_app_sim_q1_ordinary_monster(classname) || qa_app_sim_q2_ordinary_monster(classname);
}
static bool gm_present(const qa_q2_spawn_fields *fields, const char *key) {
  const char *value = qa_q2_spawn_get(fields, key);
  return value != NULL && value[0] != '\0';
}
static void gm_keep(qa_app_sim_guest_monster_plan *plan, const char *classname, const char *reason) {
  qa_app_sim_guest_monster_kept *kept = &plan->kept[plan->kept_count++];
  kept->classname = classname;
  kept->reason = gm_alloc(strlen(reason) + 1, 1);
  memcpy(kept->reason, reason, strlen(reason) + 1);
}
qa_app_sim_guest_monster_plan *qa_app_sim_plan_guest_monsters(const char *entities, qa_q2_edition edition, const char *authored_family,
    const qa_enemy_selection *selection, double skill, const char *mode, bool projection) {
  qa_app_sim_guest_monster_plan *plan = gm_alloc(1, sizeof *plan);
  if (!qa_q2_parse_entities(entities, edition, &plan->authored, &plan->authored_count)) qa_range_error("Out of memory");
  /* Q3-authored maps carry no native monster classes; both ordinary sets apply there. */
  int32_t inhibition = gm_is(authored_family, "q1") ? 0xf00 : edition == QA_Q2_EDITION_RERELEASE ? 0xff00 : 0x1f00;
  plan->replacements = gm_alloc(plan->authored_count, sizeof *plan->replacements);
  plan->kept = gm_alloc(plan->authored_count, sizeof *plan->kept);
  bool *removed = gm_alloc(plan->authored_count, sizeof *removed);
  for (size_t i = 0; i < plan->authored_count; i++) {
    const qa_q2_spawn_fields *entity = &plan->authored[i];
    const char *classname = entity->classname;
    if (strncmp(classname, "monster_", 8) != 0) continue;
    const qa_monster_selection_target *target = &selection->default_target;
    for (size_t j = 0; j < selection->by_classname_count; j++)
      if (gm_is(selection->by_classname[j].classname, classname)) { target = &selection->by_classname[j].target; break; }
    if (target->is_map_defined) continue;
    double flags = gm_flags(entity);
    if (!projection) { gm_keep(plan, classname, "guest has no declared combat profile to project selected monsters"); continue; }
    if (!gm_ordinary(authored_family, classname)) { gm_keep(plan, classname, "authored obligations are not preserved by selected admission"); continue; }
    if ((gm_bits(flags) & ~inhibition & ~1) != 0) {
      char reason[128];
      snprintf(reason, sizeof reason, "spawn flags %s need guest-native handling", QA_JS_NUMBER(flags));
      gm_keep(plan, classname, reason); continue;
    }
    if (gm_present(entity, "targetname")) { gm_keep(plan, classname, "triggered monsters stay guest-native"); continue; }
    if (gm_present(entity, "deathtarget") || gm_present(entity, "killtarget")) { gm_keep(plan, classname, "death target chains stay guest-native"); continue; }
    if (gm_present(entity, "combattarget")) { gm_keep(plan, classname, "combat targets stay guest-native"); continue; }
    if (gm_present(entity, "item")) { gm_keep(plan, classname, "item drops stay guest-native"); continue; }
    if (gm_inhibited(entity, edition, skill, mode)) continue;
    plan->replacements[plan->replacement_count++] = (qa_app_sim_guest_monster_replacement){entity->ordinal, classname, entity, target->reference};
    removed[i] = true;
  }
  if (plan->replacement_count == 0) {
    plan->entities = gm_alloc(strlen(entities) + 1, 1);
    memcpy(plan->entities, entities, strlen(entities) + 1);
    free(removed);
    return plan;
  }
  size_t span_count = 0; gm_span *spans = gm_entity_spans(entities, &span_count);
  if (span_count != plan->authored_count) {
    free(spans); free(removed); qa_app_sim_guest_monster_plan_destroy(plan);
    qa_raise("Error", 0, 0, "Native monster admission could not segment the authored entities");
  }
  size_t total = 1;
  for (size_t i = 0; i < plan->authored_count; i++) if (!removed[i]) total += spans[i].end - spans[i].start + 1;
  plan->entities = gm_alloc(total, 1); size_t used = 0;
  for (size_t i = 0; i < plan->authored_count; i++) {
    if (removed[i]) continue;
    size_t ordinal = (size_t)plan->authored[i].ordinal;
    size_t start = ordinal < span_count ? spans[ordinal].start : 0, end = ordinal < span_count ? spans[ordinal].end : 0;
    memcpy(plan->entities + used, entities + start, end - start); used += end - start;
    plan->entities[used++] = '\n';
  }
  plan->entities[used] = '\0';
  free(spans); free(removed);
  return plan;
}
void qa_app_sim_guest_monster_plan_destroy(qa_app_sim_guest_monster_plan *plan) {
  if (!plan) return;
  for (size_t i = 0; i < plan->kept_count; i++) free(plan->kept[i].reason);
  free(plan->entities); free(plan->replacements); free(plan->kept);
  qa_q2_entities_free(plan->authored, plan->authored_count);
  free(plan);
}

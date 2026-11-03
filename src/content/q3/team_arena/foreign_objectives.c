/* Port of src/content/q3/team-arena/foreign-objectives.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/foreign_objectives.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("out of memory"); return p; }
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = xmalloc(n); memcpy(p, s, n); return p; }
static void num_str(double v, char *buf, size_t cap) {
  qa_net_js_number(v, buf, cap);
}
typedef struct ent_list { qa_q1_entity *values; size_t count, cap; } ent_list;
static qa_q1_entity clone_entity(const qa_q1_entity *e) {
  qa_q1_entity out;
  out.property_count = e->property_count;
  out.properties = xmalloc((out.property_count ? out.property_count : 1) * sizeof *out.properties);
  for (size_t i = 0; i < out.property_count; i++) {
    out.properties[i].key = xstrdup(e->properties[i].key);
    out.properties[i].value = xstrdup(e->properties[i].value);
  }
  return out;
}
static qa_q1_entity with_field(const qa_q1_entity *e, const char *name, const char *value) {
  qa_q1_entity out;
  size_t kept = 0;
  for (size_t i = 0; i < e->property_count; i++) if (strcmp(e->properties[i].key, name) != 0) kept++;
  out.property_count = kept + 1;
  out.properties = xmalloc(out.property_count * sizeof *out.properties);
  size_t o = 0;
  for (size_t i = 0; i < e->property_count; i++) {
    if (strcmp(e->properties[i].key, name) == 0) continue;
    out.properties[o].key = xstrdup(e->properties[i].key);
    out.properties[o].value = xstrdup(e->properties[i].value);
    o++;
  }
  out.properties[o].key = xstrdup(name);
  out.properties[o].value = xstrdup(value);
  return out;
}
static void push(ent_list *list, qa_q1_entity e) {
  if (list->count == list->cap) {
    list->cap = list->cap ? list->cap * 2 : 16;
    qa_q1_entity *grown = realloc(list->values, list->cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    list->values = grown;
  }
  list->values[list->count++] = e;
}
static const char *classname_of(const qa_q1_entity *e) {
  const char *v = qa_q1_entity_value(e, "classname");
  return v ? v : "";
}
static const char *alias_for(const char *classname, bool obelisks) {
  if (strcmp(classname, "item_flag_team1") == 0) return obelisks ? "team_redobelisk" : "team_CTF_redflag";
  if (strcmp(classname, "item_flag_team2") == 0) return obelisks ? "team_blueobelisk" : "team_CTF_blueflag";
  if (strcmp(classname, "info_player_team1") == 0) return "team_CTF_redplayer";
  if (strcmp(classname, "info_player_team2") == 0) return "team_CTF_blueplayer";
  if (strcmp(classname, "info_player_coop") == 0) return "info_player_deathmatch";
  if (strcmp(classname, "info_player_start2") == 0) return "info_player_deathmatch";
  if (strcmp(classname, "info_player_start") == 0) return "info_player_deathmatch";
  return NULL;
}
static qa_q3_ta_objective_placement_result check(qa_q3_base_product product, double game_type,
    const qa_q1_entity *entities, size_t count, const char **missing) {
  const char **names = xmalloc((count ? count : 1) * sizeof *names);
  for (size_t i = 0; i < count; i++) names[i] = qa_q1_entity_value(&entities[i], "classname");
  qa_q3_ta_objective_placement_result result =
      qa_q3_ta_check_objective_placements(product, game_type, names, count, missing);
  free(names);
  return result;
}
static qa_q3_ta_foreign_objective_adaptation from_check(qa_q3_ta_objective_placement_result r) {
  qa_q3_ta_foreign_objective_adaptation out;
  memset(&out, 0, sizeof out);
  out.kind = r.kind;
  out.product = r.product;
  out.game_type = r.game_type;
  if (r.kind == QA_Q3_TA_OBJECTIVE_MISSING) {
    out.missing_count = r.classname_count;
    for (size_t i = 0; i < r.classname_count; i++) out.missing[i] = r.classnames[i];
  }
  return out;
}
void qa_q3_ta_foreign_objective_adaptation_destroy(qa_q3_ta_foreign_objective_adaptation *result) {
  if (!result) return;
  free(result->entities);
  result->entities = NULL;
}
static char *quote(const char *value) {
  if (strchr(value, '"') || strchr(value, '\0') != value + strlen(value)) qa_range_error("Invalid quoted entity field");
  size_t n = strlen(value);
  char *out = xmalloc(n + 3);
  out[0] = '"'; memcpy(out + 1, value, n); out[n + 1] = '"'; out[n + 2] = '\0';
  return out;
}
qa_q3_ta_foreign_objective_adaptation qa_q3_ta_adapt_foreign_q3_objectives(
    qa_q3_ta_world_kind world_kind, const char *entities_text,
    qa_q3_base_product product, double game_type,
    const qa_q3_ta_explicit_objective_placement *explicit_placements, size_t explicit_count) {
  if (world_kind == QA_Q3_TA_WORLD_Q3_BSP && explicit_count == 0) {
    size_t count = 0;
    qa_q1_entity *parsed = qa_q1_parse_entities(entities_text, &count);
    if (!parsed && count > 0) qa_range_error("out of memory");
    const char *missing[3];
    qa_q3_ta_objective_placement_result result = check(product, game_type, parsed, count, missing);
    qa_q1_entities_destroy(parsed, count);
    if (result.kind == QA_Q3_TA_OBJECTIVE_READY) {
      qa_q3_ta_foreign_objective_adaptation out;
      memset(&out, 0, sizeof out);
      out.kind = QA_Q3_TA_OBJECTIVE_READY;
      out.entities = xstrdup(entities_text);
      out.translated = 0;
      return out;
    }
    return from_check(result);
  }
  double translated = 0;
  ent_list entities = {NULL, 0, 0};
  bool obelisks = game_type == QA_Q3_BASE_GT_OBELISK || game_type == QA_Q3_BASE_GT_HARVESTER;
  size_t parsed_count = 0;
  qa_q1_entity *parsed = qa_q1_parse_entities(entities_text, &parsed_count);
  if (!parsed && parsed_count > 0) qa_range_error("out of memory");
  for (size_t i = 0; i < parsed_count; i++) {
    const char *classname = classname_of(&parsed[i]);
    const char *replacement = world_kind == QA_Q3_TA_WORLD_Q3_BSP ? NULL : alias_for(classname, obelisks);
    qa_q1_entity current = replacement == NULL ? clone_entity(&parsed[i])
                                               : with_field(&parsed[i], "classname", replacement);
    push(&entities, current);
    if (replacement != NULL) translated++;
    if (replacement != NULL && (strcmp(replacement, "team_CTF_redplayer") == 0 || strcmp(replacement, "team_CTF_blueplayer") == 0)) {
      qa_q1_entity spawn = with_field(&current, "classname",
          strcmp(replacement, "team_CTF_redplayer") == 0 ? "team_CTF_redspawn" : "team_CTF_bluespawn");
      push(&entities, spawn);
      translated++;
    }
  }
  qa_q1_entities_destroy(parsed, parsed_count);
  for (size_t i = 0; i < explicit_count; i++) {
    qa_vec3 o = explicit_placements[i].origin;
    if (!isfinite(o.x) || !isfinite(o.y) || !isfinite(o.z)) qa_range_error("Objective position must be finite");
    bool exists = false;
    for (size_t j = 0; j < entities.count; j++) {
      const char *cn = qa_q1_entity_value(&entities.values[j], "classname");
      if (cn && strcmp(cn, explicit_placements[i].classname) == 0) exists = true;
    }
    if (exists) continue;
    char xb[40], yb[40], zb[40], origin[128];
    num_str(o.x, xb, sizeof xb); num_str(o.y, yb, sizeof yb); num_str(o.z, zb, sizeof zb);
    snprintf(origin, sizeof origin, "%s %s %s", xb, yb, zb);
    qa_q1_entity e;
    e.property_count = 2;
    e.properties = xmalloc(2 * sizeof *e.properties);
    e.properties[0].key = xstrdup("classname");
    e.properties[0].value = xstrdup(explicit_placements[i].classname);
    e.properties[1].key = xstrdup("origin");
    e.properties[1].value = xstrdup(origin);
    push(&entities, e);
  }
  const char *missing[3];
  qa_q3_ta_objective_placement_result result = check(product, game_type, entities.values, entities.count, missing);
  if (result.kind != QA_Q3_TA_OBJECTIVE_READY) {
    qa_q1_entities_destroy(entities.values, entities.count);
    return from_check(result);
  }
  bool has_spawn = false;
  for (size_t i = 0; i < entities.count; i++) {
    const char *cn = classname_of(&entities.values[i]);
    if (strcmp(cn, "info_player_deathmatch") == 0 || strcmp(cn, "team_CTF_redplayer") == 0
        || strcmp(cn, "team_CTF_blueplayer") == 0) has_spawn = true;
  }
  if (!has_spawn) qa_range_error("Q3 rules require an authored player spawn");
  size_t total = 1;
  for (size_t i = 0; i < entities.count; i++) {
    total += 3;
    for (size_t j = 0; j < entities.values[i].property_count; j++)
      total += strlen(entities.values[i].properties[j].key) + strlen(entities.values[i].properties[j].value) + 8;
  }
  char *serialized = xmalloc(total);
  size_t at = 0;
  for (size_t i = 0; i < entities.count; i++) {
    at += (size_t)snprintf(serialized + at, total - at, "{\n");
    for (size_t j = 0; j < entities.values[i].property_count; j++) {
      if (j > 0) at += (size_t)snprintf(serialized + at, total - at, "\n");
      char *k = quote(entities.values[i].properties[j].key);
      char *v = quote(entities.values[i].properties[j].value);
      at += (size_t)snprintf(serialized + at, total - at, "%s %s", k, v);
      free(k); free(v);
    }
    at += (size_t)snprintf(serialized + at, total - at, "\n}\n");
  }
  serialized[at] = '\0';
  qa_q1_entities_destroy(entities.values, entities.count);
  qa_q3_ta_foreign_objective_adaptation out;
  memset(&out, 0, sizeof out);
  out.kind = QA_Q3_TA_OBJECTIVE_READY;
  out.entities = serialized;
  out.translated = translated;
  return out;
}

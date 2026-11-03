/* Port of src/app/bootstrap/simulation/native-q2-map.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_q2_map.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, s, n); return p; }
char *qa_app_sim_prepare_native_q2_map(qa_app_sim_native_q2_map_geometry world, const char *edition, const char *mode) {
  double maximum = is(edition, "classic") ? 255 : 8190;
  if (world.model_count > maximum) qa_range_error("Native Quake II map exceeds source inline-model capacity");
  if (is(world.kind, "q2-bsp")) return dup(world.entities);
  static const char *alias_from[] = {"team_CTF_redflag","team_CTF_blueflag","team_CTF_redplayer","team_CTF_redspawn","team_CTF_blueplayer","team_CTF_bluespawn"};
  static const char *alias_to[] = {"item_flag_team1","item_flag_team2","info_player_team1","info_player_team1","info_player_team2","info_player_team2"};
  size_t count = 0; qa_q1_entity *parsed = qa_q1_parse_entities(world.entities, &count);
  /* Replace classnames in place (values owned by parsed). */
  for (size_t i = 0; i < count; i++) {
    const char *cn = qa_q1_entity_value(&parsed[i], "classname"); if (cn == NULL) cn = "";
    if (!is(world.kind, "q3-bsp")) continue;
    for (size_t a = 0; a < 6; a++) if (strcmp(cn, alias_from[a]) == 0) {
      for (size_t p = 0; p < parsed[i].property_count; p++)
        if (strcmp(parsed[i].properties[p].key, "classname") == 0) { free(parsed[i].properties[p].value); parsed[i].properties[p].value = dup(alias_to[a]); }
      break; /* cn pointed at the value just freed; aliases are mutually exclusive. */
    }
  }
  const char *wanted = is(mode, "deathmatch") ? "info_player_deathmatch" : "info_player_start";
  bool has = false; for (size_t i = 0; i < count; i++) { const char *cn = qa_q1_entity_value(&parsed[i], "classname"); if (cn != NULL && strcmp(cn, wanted) == 0) has = true; }
  /* Extra appended entities kept in a side list of (source index) to emit with replaced classname. */
  size_t cap = count + 16, extra_n = 0; size_t *extra = malloc(cap * sizeof *extra); if (extra == NULL) qa_range_error("Out of memory");
  if (!has) {
    static const char *dm_eq[] = {"info_player_start","info_player_coop","info_player_team1","info_player_team2"};
    static const char *sp_eq[] = {"info_player_deathmatch"};
    size_t orig = count;
    for (size_t i = 0; i < orig; i++) {
      const char *cn = qa_q1_entity_value(&parsed[i], "classname"); if (cn == NULL) cn = "";
      bool eq = false;
      if (is(mode, "deathmatch")) { for (size_t e = 0; e < 4; e++) if (strcmp(cn, dm_eq[e]) == 0) eq = true; }
      else if (strcmp(cn, sp_eq[0]) == 0) eq = true;
      if (eq) extra[extra_n++] = i;
    }
  }
  size_t total = 0;
  for (size_t i = 0; i < count + extra_n; i++) {
    const qa_q1_entity *e = &parsed[i < count ? i : extra[i - count]];
    total += 4;
    for (size_t p = 0; p < e->property_count; p++) {
      const char *v = (i >= count && strcmp(e->properties[p].key, "classname") == 0) ? wanted : e->properties[p].value;
      if (strchr(e->properties[p].key, '"') != NULL || strchr(v, '"') != NULL) qa_range_error("Native Quake II entity field cannot be quoted");
      total += strlen(e->properties[p].key) + strlen(v) + 6;
    }
  }
  char *out = malloc(total + 1); if (out == NULL) qa_range_error("Out of memory"); size_t o = 0;
  for (size_t i = 0; i < count + extra_n; i++) {
    const qa_q1_entity *e = &parsed[i < count ? i : extra[i - count]];
    o += (size_t)snprintf(out + o, total + 1 - o, "{\n");
    for (size_t p = 0; p < e->property_count; p++) {
      const char *v = (i >= count && strcmp(e->properties[p].key, "classname") == 0) ? wanted : e->properties[p].value;
      o += (size_t)snprintf(out + o, total + 1 - o, "\"%s\" \"%s\"\n", e->properties[p].key, v);
    }
    o += (size_t)snprintf(out + o, total + 1 - o, "}\n");
  }
  out[o] = '\0'; free(extra); qa_q1_entities_destroy(parsed, count); return out;
}

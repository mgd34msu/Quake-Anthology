/* Port of src/app/bootstrap/simulation/quakec-map.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/support.h"
static char *xstrdup(const char *s) { size_t n = strlen(s); char *o = malloc(n + 1); if (!o) qa_range_error("Out of memory"); memcpy(o, s, n + 1); return o; }
static const char *world_entities(qa_decoded_world w) {
  switch (w.kind) {
    case QA_DECODED_WORLD_Q1: return w.as.q1->entities;
    case QA_DECODED_WORLD_Q2: return w.as.q2->entities;
    case QA_DECODED_WORLD_Q3: return w.as.q3->entities;
  }
  return "";
}
char *qa_app_sim_quakec_map_entities(qa_decoded_world world, const char *mode) {
  const char *text = world_entities(world);
  if (world.kind == QA_DECODED_WORLD_Q1) return xstrdup(text);
  size_t count = 0;
  qa_q1_entity *entities = qa_q1_parse_entities(text, &count);
  bool deathmatch = mode != NULL && strcmp(mode, "deathmatch") == 0;
  const char *wanted = deathmatch ? "info_player_deathmatch" : "info_player_start";
  bool has = false;
  for (size_t i = 0; i < count; i++) {
    const char *c = qa_q1_entity_value(&entities[i], "classname");
    if (c && strcmp(c, wanted) == 0) has = true;
  }
  /* Extra entities appended by the TS are collected separately. */
  typedef struct extra { size_t source; } extra;
  extra extras[64]; size_t extra_count = 0;
  if (!has) {
    static const char *const dm[] = {"info_player_start", "info_player_coop", "team_CTF_redplayer", "team_CTF_blueplayer", "team_CTF_redspawn", "team_CTF_bluespawn"};
    static const char *const sp[] = {"info_player_deathmatch", "team_CTF_redplayer", "team_CTF_blueplayer"};
    const char *const *candidates = deathmatch ? dm : sp; size_t cand_count = deathmatch ? 6 : 3;
    for (size_t i = 0; i < count; i++) {
      const char *c = qa_q1_entity_value(&entities[i], "classname");
      bool match = false;
      for (size_t k = 0; k < cand_count; k++) if (c && strcmp(c, candidates[k]) == 0) match = true;
      if (match && extra_count < 64) extras[extra_count++].source = i;
    }
  }
  size_t total = count + extra_count;
  size_t cap = 1024, len = 0; char *out = malloc(cap);
  if (!out) qa_range_error("Out of memory");
  for (size_t e = 0; e < total; e++) {
    size_t src = e < count ? e : extras[e - count].source;
    bool replaced = e >= count;
    qa_q1_entity *entity = &entities[src];
    const char *parts[512]; size_t part_count = entity->property_count < 256 ? entity->property_count : 256;
    /* Build "{\n" + quoted fields + "\n}\n" with quote validation. */
    size_t need = 4;
    for (size_t p = 0; p < part_count; p++) {
      const char *key = entity->properties[p].key;
      const char *value = (replaced && strcmp(key, "classname") == 0) ? wanted : entity->properties[p].value;
      if (strchr(key, '"') || strchr(value, '"')) qa_range_error("QuakeC entity field cannot be quoted");
      parts[p * 2] = key; parts[p * 2 + 1] = value;
      need += strlen(key) + strlen(value) + 6;
    }
    if (len + need + 1 > cap) { while (len + need + 1 > cap) cap *= 2; out = realloc(out, cap); if (!out) qa_range_error("Out of memory"); }
    len += (size_t)sprintf(out + len, "{\n");
    for (size_t p = 0; p < part_count; p++)
      len += (size_t)sprintf(out + len, "%s\"%s\" \"%s\"", p ? "\n" : "", parts[p * 2], parts[p * 2 + 1]);
    len += (size_t)sprintf(out + len, "\n}\n");
  }
  out[len] = '\0';
  qa_q1_entities_destroy(entities, count);
  return out;
}

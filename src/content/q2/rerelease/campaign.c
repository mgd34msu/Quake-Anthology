/* Port of src/content/q2/rerelease/campaign.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/rerelease/players.h"
#include "rr_internal.h"
#include <stdlib.h>
#include <string.h>

bool qa_q2_create_rerelease_campaign_state(qa_q2_rerelease_campaign_state *out) {
  memset(out, 0, sizeof *out);
  out->mission.primary = qa_q2rr_dup("");
  out->mission.secondary = qa_q2rr_dup("");
  return out->mission.primary && out->mission.secondary;
}
void qa_q2_rerelease_campaign_destroy(qa_q2_rerelease_campaign_state *c) {
  for (size_t i = 0; i < c->visited_count; i++) free(c->visited_maps[i]);
  free(c->visited_maps);
  for (size_t i = 0; i < c->level_count; i++) { free(c->levels[i].map); free(c->levels[i].name); }
  free(c->levels); free(c->mission.primary); free(c->mission.secondary);
  memset(c, 0, sizeof *c);
}
bool qa_q2_rerelease_campaign_clone(qa_q2_rerelease_campaign_state *dst, const qa_q2_rerelease_campaign_state *src) {
  qa_q2_rerelease_campaign_state t;
  if (!qa_q2_create_rerelease_campaign_state(&t)) return false;
  t.cross_unit_flags = src->cross_unit_flags;
  free(t.mission.primary); free(t.mission.secondary);
  t.mission = src->mission;
  t.mission.primary = qa_q2rr_dup(src->mission.primary);
  t.mission.secondary = qa_q2rr_dup(src->mission.secondary);
  if (!t.mission.primary || !t.mission.secondary) goto fail;
  if (src->visited_count) {
    t.visited_maps = calloc(src->visited_count, sizeof(char *));
    if (!t.visited_maps) goto fail;
    t.visited_cap = src->visited_count;
    for (size_t i = 0; i < src->visited_count; i++) {
      t.visited_maps[i] = qa_q2rr_dup(src->visited_maps[i]);
      if (!t.visited_maps[i]) { t.visited_count = i; goto fail; }
      t.visited_count = i + 1;
    }
  }
  if (src->level_count) {
    t.levels = calloc(src->level_count, sizeof *t.levels);
    if (!t.levels) goto fail;
    t.level_cap = src->level_count;
    for (size_t i = 0; i < src->level_count; i++) {
      t.levels[i] = src->levels[i];
      t.levels[i].map = qa_q2rr_dup(src->levels[i].map);
      t.levels[i].name = qa_q2rr_dup(src->levels[i].name);
      t.level_count = i + 1;
      if (!t.levels[i].map || !t.levels[i].name) goto fail;
    }
  }
  *dst = t; return true;
fail: qa_q2_rerelease_campaign_destroy(&t); return false;
}
qa_q2_rerelease_level_entry *qa_q2_rerelease_campaign_get_level(qa_q2_rerelease_campaign_state *c, const char *map) {
  for (size_t i = 0; i < c->level_count; i++) if (map && strcmp(c->levels[i].map, map) == 0) return &c->levels[i];
  return NULL;
}
static bool grow_levels(qa_q2_rerelease_campaign_state *c) {
  size_t nc = c->level_cap ? c->level_cap * 2 : 8;
  void *p = realloc(c->levels, nc * sizeof *c->levels);
  if (!p) return false;
  c->levels = p; c->level_cap = nc; return true;
}
static qa_q2_rerelease_level_entry *add_level(qa_q2_rerelease_campaign_state *c, const char *map) {
  qa_q2_rerelease_level_entry *prev = qa_q2_rerelease_campaign_get_level(c, map);
  if (prev) return prev;
  if (c->level_count >= 8) return NULL;
  if (c->level_count == c->level_cap && !grow_levels(c)) return NULL;
  qa_q2_rerelease_level_entry *e = &c->levels[c->level_count];
  memset(e, 0, sizeof *e);
  e->map = qa_q2rr_dup(map); e->name = qa_q2rr_dup("");
  if (!e->map || !e->name) { free(e->map); free(e->name); return NULL; }
  c->level_count++;
  return e;
}
static bool visited_add(qa_q2_rerelease_campaign_state *c, const char *map) {
  for (size_t i = 0; i < c->visited_count; i++) if (strcmp(c->visited_maps[i], map) == 0) return true;
  if (c->visited_count == c->visited_cap) {
    size_t nc = c->visited_cap ? c->visited_cap * 2 : 8;
    void *p = realloc(c->visited_maps, nc * sizeof(char *));
    if (!p) return false;
    c->visited_maps = p; c->visited_cap = nc;
  }
  char *d = qa_q2rr_dup(map);
  if (!d) return false;
  c->visited_maps[c->visited_count++] = d;
  return true;
}

void qa_q2_enter_rerelease_level(qa_q2_game_services *g, qa_q2_rerelease_players *players, qa_q2_rerelease_campaign_state *c) {
  qa_actor_id wa = g->host->world_actor(g->host->ctx);
  qa_q2_entity *world = rr_ent(g, &wa);
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH || (world && qa_q2_number_field(&world->spawn, "hub_map", 0) != 0)) return;
  qa_q2_rerelease_level_entry *entry = add_level(c, g->options.map_name);
  if (!entry) { rr_diag(g, "More than 8 maps in unit; cannot track remaining levels"); return; }
  if (strcmp(entry->name, "") == 0) {
    const char *nm = (world && world->message && world->message[0]) ? world->message : g->options.map_name;
    free(entry->name); entry->name = qa_q2rr_dup(nm ? nm : "");
    double mx = 0;
    for (size_t i = 0; i < c->level_count; i++) if (c->levels[i].visit_order > mx) mx = c->levels[i].visit_order;
    entry->visit_order = (mx > 0 ? mx : 0) + 1;
    visited_add(c, g->options.map_name);
    if (players->rerelease_options.coop_lives)
      for (size_t i = 0; i < players->extra_count; i++) {
        qa_q2rr_player_extra_state *x = players->rerelease_states[i].state;
        double cap = players->rerelease_options.coop_num_lives + 1;
        x->lives = x->lives + 1 < cap ? x->lives + 1 : cap;
      }
  }
  size_t n = 0;
  qa_q2_entity *const *ents = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++) {
    qa_q2_entity *e = ents[i];
    if (!e->classname || strcmp(e->classname, "target_changelevel") != 0) continue;
    if (!e->map || !e->map[0] || strchr(e->map, '*')) continue;
    const char *plus = strchr(e->map, '+');
    const char *dest = plus ? plus + 1 : e->map + strlen(e->map); /* TS slice(indexOf+1): -1+1=0 -> whole */
    if (!plus) dest = e->map;
    if (strstr(dest, ".cin") || strstr(dest, ".pcx")) continue;
    const char *dollar = strchr(dest, '$');
    size_t len = dollar ? (size_t)(dollar - dest) : strlen(dest);
    if (len == 0) continue;
    char buf[512]; if (len >= sizeof buf) len = sizeof buf - 1;
    memcpy(buf, dest, len); buf[len] = '\0';
    if (!add_level(c, buf)) { rr_diag(g, "More than 8 maps in unit; cannot track remaining levels"); return; }
  }
}
void qa_q2_update_rerelease_level(qa_q2_game_services *g, qa_q2_rerelease_campaign_state *c) {
  qa_q2_rerelease_level_entry *e = qa_q2_rerelease_campaign_get_level(c, g->options.map_name);
  if (e) {
    e->found_secrets = g->counters->found_secrets; e->total_secrets = g->counters->total_secrets;
    e->killed_monsters = g->counters->killed_monsters; e->total_monsters = g->counters->total_monsters;
  }
}
static double order_of(const qa_q2_rerelease_level_entry *e) {
  return e->visit_order != 0 ? e->visit_order : (strcmp(e->name, "") == 0 ? 10 : 9);
}
static int cmp_entry(const void *a, const void *b) {
  double d = order_of(a) - order_of(b);
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}
qa_q2_rerelease_level_entry *qa_q2_rerelease_unit_report(const qa_q2_rerelease_campaign_state *c, size_t *n) {
  *n = c->level_count;
  if (!c->level_count) return NULL;
  qa_q2_rerelease_level_entry *out = calloc(c->level_count, sizeof *out);
  if (!out) { *n = 0; return NULL; }
  for (size_t i = 0; i < c->level_count; i++) {
    out[i] = c->levels[i];
    out[i].map = qa_q2rr_dup(c->levels[i].map);
    out[i].name = qa_q2rr_dup(c->levels[i].name);
  }
  qsort(out, c->level_count, sizeof *out, cmp_entry);
  return out;
}
void qa_q2_rerelease_unit_report_free(qa_q2_rerelease_level_entry *e, size_t n) {
  for (size_t i = 0; i < n; i++) { free(e[i].map); free(e[i].name); }
  free(e);
}

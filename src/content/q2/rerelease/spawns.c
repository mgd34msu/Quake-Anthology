/* Port of src/content/q2/rerelease/spawns.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/spawns.h"
#include "qa/content/q2/base/player/landmarks.h"
#include "qa/content/q2/base/player/spawns.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"
#include "rr_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

const qa_bounds QA_Q2_RERELEASE_PLAYER_BOUNDS = {{-16, -16, -24}, {16, 16, 32}};
/* TS q2EntitiesNamed (base/player/spawns.ts): source-slot order. Fills buf
 * with up to cap entries and returns the total count. */
size_t qa_q2rr_entities_named(qa_q2_game_services *g, const char *name, qa_q2_entity **buf, size_t cap) {
  size_t total = 0;
  qa_q2_entity **all = qa_q2_entities_named(g, name, &total);
  for (size_t i = 0; i < total && i < cap; i++) buf[i] = all[i];
  free(all);
  return total;
}
static void start_drop(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_bounds = true; ch.bounds = QA_Q2_RERELEASE_PLAYER_BOUNDS;
  g->move(g->ctx, e, ch, false); g->solid(g->ctx, e, "trigger"); g->motion(g->ctx, e, "toss"); g->link(g->ctx, e);
}
static bool spawns_spawn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  const char *name = e->classname;
  if (!name) return false;
  bool is_start = strcmp(name, "info_player_start") == 0, is_coop = strcmp(name, "info_player_coop") == 0;
  bool is_lava = strcmp(name, "info_player_coop_lava") == 0, is_inter = strcmp(name, "info_player_intermission") == 0;
  bool is_dm = strcmp(name, "info_player_deathmatch") == 0;
  if (!is_start && !is_coop && !is_lava && !is_inter && !is_dm) return false;
  if (((is_coop || is_lava) && g->options.mode != QA_Q2_MODE_COOP) || (is_dm && g->options.mode != QA_Q2_MODE_DEATHMATCH)) {
    g->remove(g->ctx, e); return true;
  }
  if (is_inter) return true;
  if (is_dm) {
    e->model = "models/objects/dmspot/tris.md2"; e->skin = 1;
    qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_bounds = true;
    ch.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, -16}};
    g->move(g->ctx, e, ch, false); g->solid(g->ctx, e, "box"); g->show(g->ctx, e);
    return true;
  }
  qa_vec3 origin = rr_body_of(g, e).origin;
  qa_q2_trace_request req; memset(&req, 0, sizeof req);
  req.start = origin; req.end = origin; req.bounds = &QA_Q2_RERELEASE_PLAYER_BOUNDS; req.ignore = &e->actor.id; req.mask = 1;
  if (g->host->trace(g->host->ctx, req).start_solid) {
    qa_vec3 fixed;
    if (qa_q2_fix_stuck_player(e, g, origin, QA_Q2_RERELEASE_PLAYER_BOUNDS, &fixed)) {
      qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_origin = true; ch.origin = fixed;
      g->move(g->ctx, e, ch, true);
    }
  }
  if (!is_lava && qa_q2_is_n64(&g->options))
    g->schedule(g->ctx, e, rr_frame(g), (qa_q2_think){NULL, start_drop});
  return true;
}
qa_q2_spawn_module qa_q2_get_rerelease_spawns(void) {
  qa_q2_spawn_module m = {0}; m.spawn = spawns_spawn; return m;
}
static qa_q2_named_think sp_thinks[1];
qa_q2_callback_definitions qa_q2_rerelease_spawns_callbacks(void) {
  sp_thinks[0] = (qa_q2_named_think){"rr.info_player_start_drop", {NULL, start_drop}};
  qa_q2_callback_definitions d = {0}; d.think = sp_thinks; d.think_count = 1; return d;
}

qa_q2_entity *qa_q2_rerelease_single_spawn(qa_q2_game_services *g, const char *spawn_point) {
  size_t n = 0;
  qa_q2_entity **starts = qa_q2_entities_named(g, "info_player_start", &n);
  qa_q2_entity *found = NULL;
  for (size_t i = 0; i < n && !found; i++) if (qa_q2rr_ieq(starts[i]->targetname, spawn_point)) found = starts[i];
  for (size_t i = 0; i < n && !found; i++) if (starts[i]->targetname[0] == '\0') found = starts[i];
  if (!found && n) found = starts[0];
  free(starts);
  return found;
}
static qa_q2_entity *lava_spawn(qa_q2_game_services *g) {
  double lava_top = -99999;
  size_t wn = 0;
  qa_q2_entity **waters = qa_q2_entities_named(g, "func_water", &wn);
  for (size_t i = 0; i < wn; i++) {
    qa_body_state b = rr_body_of(g, waters[i]);
    qa_vec3 center = qa_q2_add(b.origin, qa_q2_scale(qa_q2_add(b.bounds.min, b.bounds.max), 0.5));
    if (rr_band(waters[i]->spawnflags, 2) != 0 && rr_band(g->host->point_contents(g->host->ctx, center), 56) != 0)
      lava_top = qa_js_max(lava_top, b.origin.z + b.bounds.max.z);
  }
  free(waters);
  if (lava_top == -99999) return NULL;
  qa_q2_entity *best = NULL; double height = 999999;
  size_t sn = 0;
  qa_q2_entity **spots = qa_q2_entities_named(g, "info_player_coop_lava", &sn);
  for (size_t i = 0; i < sn && i < 64; i++) { /* TS slice(0, 64) */
    double z = rr_body_of(g, spots[i]).origin.z;
    if (z >= lava_top + 64 && z < height && qa_q2_players_range(g, spots[i]) > 32) { best = spots[i]; height = z; }
  }
  free(spots);
  return best;
}
static bool dm_clear(qa_q2_game_services *g, qa_q2_entity *spot, const qa_bounds *bounds) {
  qa_vec3 o = qa_q2_add(rr_body_of(g, spot).origin, rr_v3(0, 0, 9));
  qa_q2_trace_request r; memset(&r, 0, sizeof r);
  r.start = o; r.end = o; r.bounds = bounds; r.ignore = &spot->actor.id; r.mask = (double)0x42000000;
  return !g->host->trace(g->host->ctx, r).start_solid;
}
static qa_q2_entity **append_named(qa_q2_game_services *g, qa_q2_entity **list, size_t *n, const char *name) {
  size_t more = 0;
  qa_q2_entity **extra = qa_q2_entities_named(g, name, &more);
  if (more) {
    qa_q2_entity **grown = realloc(list, (*n + more) * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    memcpy(grown + *n, extra, more * sizeof *grown);
    list = grown; *n += more;
  }
  free(extra);
  return list;
}
static qa_q2_entity *select_deathmatch(qa_q2_game_services *g, const qa_bounds *bounds, qa_q2_rerelease_options options, bool force) {
  size_t n = 0;
  qa_q2_entity **spots = qa_q2_entities_named(g, "info_player_deathmatch", &n);
  if (n == 0) {
    free(spots); spots = NULL;
    spots = append_named(g, spots, &n, "info_player_team1");
    spots = append_named(g, spots, &n, "info_player_team2");
  }
  if (n == 0) {
    size_t sn = 0;
    qa_q2_entity **starts = qa_q2_entities_named(g, "info_player_start", &sn);
    if (sn) { free(spots); spots = malloc(sizeof *spots); if (!spots) qa_range_error("Out of memory"); spots[0] = starts[0]; n = 1; }
    free(starts);
  }
  if (n == 0) { free(spots); qa_q2rr_error("Q2 rerelease: no valid spawn points found"); }
  qa_q2_entity *chosen = NULL;
  if (n == 1) {
    chosen = force || dm_clear(g, spots[0], bounds) ? spots[0] : NULL;
    free(spots);
    return chosen;
  }
  /* TS [...spots].sort by q2PlayersRange: a stable sort; the range has no side effects. */
  qa_q2_entity **sorted = malloc(n * sizeof *sorted);
  double *ranges = malloc(n * sizeof *ranges);
  if (!sorted || !ranges) qa_range_error("Out of memory");
  for (size_t i = 0; i < n; i++) { sorted[i] = spots[i]; ranges[i] = qa_q2_players_range(g, spots[i]); }
  for (size_t i = 1; i < n; i++) {
    qa_q2_entity *key = sorted[i]; double range = ranges[i];
    size_t j = i;
    while (j > 0 && ranges[j - 1] - range > 0) { sorted[j] = sorted[j - 1]; ranges[j] = ranges[j - 1]; j--; }
    sorted[j] = key; ranges[j] = range;
  }
  free(ranges);
  if (options.deathmatch_spawn_farthest) {
    for (size_t i = n; i-- > 0 && !chosen;) if (dm_clear(g, sorted[i], bounds)) chosen = sorted[i];
  } else {
    for (size_t index = n - 1; index > 2; index--) {
      size_t dest = 2 + (size_t)floor(g->host->random(g->host->ctx) * (double)(index - 1));
      qa_q2_entity *tmp = sorted[index]; sorted[index] = sorted[dest]; sorted[dest] = tmp;
    }
    /* [...sorted.slice(2), ...sorted.slice(0, 2).reverse()] */
    for (size_t k = 0; k < n && !chosen; k++) {
      size_t i = k < n - 2 ? k + 2 : n - 1 - k;
      if (dm_clear(g, sorted[i], bounds)) chosen = sorted[i];
    }
  }
  free(sorted);
  if (!chosen && force) chosen = spots[(size_t)floor(g->host->random(g->host->ctx) * (double)n)];
  free(spots);
  return chosen;
}
qa_q2_entity *qa_q2_select_rerelease_spawn(qa_q2_game_services *g, qa_q2_entity *player, qa_bounds bounds,
                                           qa_q2_rerelease_options options, const char *spawn_point, bool force) {
  if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER) return qa_q2_rerelease_single_spawn(g, spawn_point);
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) return select_deathmatch(g, &bounds, options, force);
  if (qa_q2rr_ieq(g->options.map_name, "rmine2")) return lava_spawn(g);
  qa_q2_entity *first = qa_q2_rerelease_single_spawn(g, spawn_point);
  size_t cn = 0;
  qa_q2_entity **coop = qa_q2_entities_named(g, "info_player_coop", &cn);
  /* ordered: first (when present), then the matching coop spots, else the unnamed ones. */
  qa_q2_entity **ordered = malloc((cn + 1) * sizeof *ordered);
  if (!ordered) qa_range_error("Out of memory");
  size_t on = 0;
  if (first) ordered[on++] = first;
  size_t matched = 0;
  for (size_t i = 0; i < cn; i++) if (qa_q2rr_ieq(coop[i]->targetname, spawn_point)) { ordered[on++] = coop[i]; matched++; }
  if (matched == 0) for (size_t i = 0; i < cn; i++) if (coop[i]->targetname[0] == '\0') ordered[on++] = coop[i];
  free(coop);
  qa_q2_entity *chosen = NULL;
  for (int pass = 0; pass < 2 && !chosen; pass++) {
    bool check_players = pass == 0;
    for (size_t i = 0; i < on && !chosen; i++) {
      qa_q2_entity *spot = ordered[i];
      qa_vec3 origin = rr_body_of(g, spot).origin;
      size_t pn = 0;
      const qa_actor_id *exclude = check_players ? &player->actor.id : g->host->players(g->host->ctx, &pn);
      size_t exclude_count = check_players ? 1 : pn;
      for (int attempt = 0; attempt < 3; attempt++) {
        qa_q2_trace_request r; memset(&r, 0, sizeof r);
        r.start = origin; r.end = origin; r.bounds = &bounds; r.ignore = &player->actor.id;
        r.mask = (double)(check_players ? 0x42010003 : 0x2010003);
        r.exclude = exclude; r.exclude_count = exclude_count;
        qa_trace_result result = g->host->trace(g->host->ctx, r);
        bool hit_player = result.hit.kind == QA_TRACE_HIT_ACTOR && g->host->is_player(g->host->ctx, result.hit.actor);
        if (result.start_solid && !hit_player && attempt == 0) { origin = qa_q2_add(origin, rr_v3(0, 0, 1)); continue; }
        if (result.start_solid && !hit_player && attempt == 1) {
          qa_vec3 fixed;
          if (!qa_q2_fix_stuck_player(player, g, origin, bounds, &fixed)) break;
          origin = fixed; continue;
        }
        if (result.fraction == 1 || (!check_players && hit_player)) chosen = spot;
        break;
      }
    }
  }
  free(ordered);
  if (chosen) return chosen;
  return (force || !options.coop_player_collision) ? first : NULL;
}

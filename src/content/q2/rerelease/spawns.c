/* Port of src/content/q2/rerelease/spawns.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/spawns.h"
#include "qa/content/q2/foundation/fields.h"
#include "rr_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const qa_bounds QA_Q2_RERELEASE_PLAYER_BOUNDS = {{-16, -16, -24}, {16, 16, 32}};
qa_q2rr_spawn_helpers qa_q2rr_spawns;

size_t qa_q2rr_entities_named(qa_q2_game_services *g, const char *name, qa_q2_entity **buf, size_t cap) {
  if (qa_q2rr_spawns.entities_named) {
    size_t n = 0;
    qa_q2_entity **r = qa_q2rr_spawns.entities_named(qa_q2rr_spawns.ctx, g, name, &n, buf, cap);
    (void)r; return n;
  }
  size_t total = 0, en = 0;
  qa_q2_entity *const *ents = g->entities(g->ctx, &en);
  for (size_t i = 0; i < en; i++)
    if (ents[i]->classname && strcmp(ents[i]->classname, name) == 0) {
      if (total < cap) buf[total] = ents[i];
      total++;
    }
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
  if (g->host->trace(g->host->ctx, req).start_solid && qa_q2rr_spawns.fix_stuck_player) {
    qa_vec3 fixed;
    if (qa_q2rr_spawns.fix_stuck_player(qa_q2rr_spawns.ctx, e, g, origin, QA_Q2_RERELEASE_PLAYER_BOUNDS, &fixed)) {
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
  qa_q2_entity *starts[256]; size_t n = qa_q2rr_entities_named(g, "info_player_start", starts, 256);
  if (n > 256) n = 256;
  for (size_t i = 0; i < n; i++) if (qa_q2rr_ieq(starts[i]->targetname, spawn_point)) return starts[i];
  for (size_t i = 0; i < n; i++) if (starts[i]->targetname && starts[i]->targetname[0] == '\0') return starts[i];
  /* targetname may be NULL for "" in C entities: treat NULL as "" */
  for (size_t i = 0; i < n; i++) if (!starts[i]->targetname) return starts[i];
  return n ? starts[0] : NULL;
}
static qa_q2_entity *lava_spawn(qa_q2_game_services *g) {
  double lava_top = -99999;
  qa_q2_entity *waters[256]; size_t wn = qa_q2rr_entities_named(g, "func_water", waters, 256); if (wn > 256) wn = 256;
  for (size_t i = 0; i < wn; i++) {
    qa_body_state b = rr_body_of(g, waters[i]);
    qa_vec3 center = qa_q2_add(b.origin, qa_q2_scale(qa_q2_add(b.bounds.min, b.bounds.max), 0.5));
    if (rr_band(waters[i]->spawnflags, 2) != 0 && rr_band(g->host->point_contents(g->host->ctx, center), 56) != 0) {
      double top = b.origin.z + b.bounds.max.z;
      if (top > lava_top) lava_top = top;
    }
  }
  if (lava_top == -99999) return NULL;
  qa_q2_entity *best = NULL; double height = 999999;
  qa_q2_entity *spots[64]; size_t sn = qa_q2rr_entities_named(g, "info_player_coop_lava", spots, 64); if (sn > 64) sn = 64;
  for (size_t i = 0; i < sn; i++) {
    double z = rr_body_of(g, spots[i]).origin.z;
    double range = qa_q2rr_spawns.players_range ? qa_q2rr_spawns.players_range(qa_q2rr_spawns.ctx, g, spots[i]) : 0;
    if (z >= lava_top + 64 && z < height && range > 32) { best = spots[i]; height = z; }
  }
  return best;
}
static qa_q2_game_services *sort_g;
static int cmp_range(const void *a, const void *b) {
  double ra = qa_q2rr_spawns.players_range ? qa_q2rr_spawns.players_range(qa_q2rr_spawns.ctx, sort_g, *(qa_q2_entity *const *)a) : 0;
  double rb = qa_q2rr_spawns.players_range ? qa_q2rr_spawns.players_range(qa_q2rr_spawns.ctx, sort_g, *(qa_q2_entity *const *)b) : 0;
  return ra < rb ? -1 : ra > rb ? 1 : 0;
}
qa_q2_entity *qa_q2_select_rerelease_spawn(qa_q2_game_services *g, qa_q2_entity *player, qa_bounds bounds,
                                           qa_q2_rerelease_options options, const char *spawn_point, bool force) {
  if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER) return qa_q2_rerelease_single_spawn(g, spawn_point);
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) {
    qa_q2_entity *spots[512]; size_t n = qa_q2rr_entities_named(g, "info_player_deathmatch", spots, 512);
    if (n == 0) {
      n = qa_q2rr_entities_named(g, "info_player_team1", spots, 256);
      size_t n2 = qa_q2rr_entities_named(g, "info_player_team2", spots + (n < 256 ? n : 256), 256);
      n = (n < 256 ? n : 256) + (n2 < 256 ? n2 : 256);
    }
    if (n == 0) { qa_q2_entity *s[1]; size_t sn = qa_q2rr_entities_named(g, "info_player_start", s, 1); if (sn) { spots[0] = s[0]; n = 1; } }
    if (n == 0) qa_q2rr_error("Q2 rerelease: no valid spawn points found");
    if (n > 512) n = 512;
    /* clear(spot) inline lambda */
    if (n == 1) {
      qa_vec3 o = qa_q2_add(rr_body_of(g, spots[0]).origin, rr_v3(0, 0, 9));
      qa_q2_trace_request r; memset(&r, 0, sizeof r); r.start = o; r.end = o; r.bounds = &bounds; r.ignore = &spots[0]->actor.id; r.mask = (double)(int32_t)0x42000000;
      bool clear = !g->host->trace(g->host->ctx, r).start_solid;
      return (force || clear) ? spots[0] : NULL;
    }
    qa_q2_entity *sorted[512]; memcpy(sorted, spots, n * sizeof *sorted);
    sort_g = g; qsort(sorted, n, sizeof *sorted, cmp_range);
    if (options.deathmatch_spawn_farthest) {
      for (size_t i = n; i-- > 0;) {
        qa_vec3 o = qa_q2_add(rr_body_of(g, sorted[i]).origin, rr_v3(0, 0, 9));
        qa_q2_trace_request r; memset(&r, 0, sizeof r); r.start = o; r.end = o; r.bounds = &bounds; r.ignore = &sorted[i]->actor.id; r.mask = (double)(int32_t)0x42000000;
        if (!g->host->trace(g->host->ctx, r).start_solid) return sorted[i];
      }
    } else {
      for (long index = (long)n - 1; index > 2; index--) {
        long dest = 2 + (long)floor(g->host->random(g->host->ctx) * (double)(index - 1));
        qa_q2_entity *tmp = sorted[index]; sorted[index] = sorted[dest]; sorted[dest] = tmp;
      }
      /* [...sorted.slice(2), ...sorted.slice(0,2).reverse()] */
      for (size_t k = 0; k < n; k++) {
        size_t i = k + 2 < n ? k + 2 : (n - 1 - (k - (n - 2))); /* indices 2..n-1 then 1,0 */
        if (k >= n - 2) i = (k == n - 2) ? 1 : 0;
        qa_vec3 o = qa_q2_add(rr_body_of(g, sorted[i]).origin, rr_v3(0, 0, 9));
        qa_q2_trace_request r; memset(&r, 0, sizeof r); r.start = o; r.end = o; r.bounds = &bounds; r.ignore = &sorted[i]->actor.id; r.mask = (double)(int32_t)0x42000000;
        if (!g->host->trace(g->host->ctx, r).start_solid) return sorted[i];
      }
    }
    if (!force) return NULL;
    return spots[(size_t)floor(g->host->random(g->host->ctx) * (double)n) % n];
  }
  if (qa_q2rr_ieq(g->options.map_name, "rmine2")) return lava_spawn(g);
  qa_q2_entity *first = qa_q2_rerelease_single_spawn(g, spawn_point);
  qa_q2_entity *coop[256]; size_t cn = qa_q2rr_entities_named(g, "info_player_coop", coop, 256); if (cn > 256) cn = 256;
  qa_q2_entity *cands[256]; size_t kn = 0;
  for (size_t i = 0; i < cn; i++) if (qa_q2rr_ieq(coop[i]->targetname, spawn_point)) cands[kn++] = coop[i];
  if (kn == 0) for (size_t i = 0; i < cn; i++) if (!coop[i]->targetname || coop[i]->targetname[0] == '\0') cands[kn++] = coop[i];
  /* ordered list: first?, then candidates */
  qa_q2_entity *ordered[257]; size_t on = 0;
  if (first) ordered[on++] = first;
  for (size_t i = 0; i < kn; i++) ordered[on++] = cands[i];
  for (int pass = 0; pass < 2; pass++) {
    bool check_players = pass == 0;
    for (size_t i = 0; i < on; i++) {
      qa_q2_entity *spot = ordered[i];
      qa_vec3 origin = rr_body_of(g, spot).origin;
      qa_actor_id excl_one[1]; size_t pn = 0; const qa_actor_id *all_players = NULL;
      const qa_actor_id *exclude; size_t exclude_count;
      if (check_players) { excl_one[0] = player->actor.id; exclude = excl_one; exclude_count = 1; }
      else { all_players = g->host->players(g->host->ctx, &pn); exclude = all_players; exclude_count = pn; }
      for (int attempt = 0; attempt < 3; attempt++) {
        qa_q2_trace_request r; memset(&r, 0, sizeof r);
        r.start = origin; r.end = origin; r.bounds = &bounds; r.ignore = &player->actor.id;
        r.mask = (double)(int32_t)(check_players ? 0x42010003 : 0x2010003);
        r.exclude = exclude; r.exclude_count = exclude_count;
        qa_trace_result result = g->host->trace(g->host->ctx, r);
        bool hit_player = result.hit.kind == QA_TRACE_HIT_ACTOR && g->host->is_player(g->host->ctx, result.hit.actor);
        if (result.start_solid && !hit_player && attempt == 0) { origin = qa_q2_add(origin, rr_v3(0, 0, 1)); continue; }
        if (result.start_solid && !hit_player && attempt == 1) {
          qa_vec3 fixed;
          if (!qa_q2rr_spawns.fix_stuck_player || !qa_q2rr_spawns.fix_stuck_player(qa_q2rr_spawns.ctx, player, g, origin, bounds, &fixed)) break;
          origin = fixed; continue;
        }
        if (result.fraction == 1 || (!check_players && hit_player)) return spot;
        break;
      }
    }
  }
  return (force || !options.coop_player_collision) ? first : NULL;
}

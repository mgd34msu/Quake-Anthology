/* Mirrors of src/content/q2/base/player/spawns.ts helpers used by the
 * multiplayer slice (TS owner: base/player/spawns.ts, not yet ported)
 * plus q2Userinfo from base/player/index.ts. GPL-2.0-or-later. */
#include "multiplayer_internal.h"
#include <ctype.h>

static int cmp_source_slot(qa_q2_game_services *g, qa_q2_entity *a, qa_q2_entity *b) {
  qa_actor_source_result sa = qa_session_actor_registry_source_of(g->host->actors, a->actor.id);
  qa_actor_source_result sb = qa_session_actor_registry_source_of(g->host->actors, b->actor.id);
  int64_t va = sa.has_value ? sa.slot : a->actor.id.slot;
  int64_t vb = sb.has_value ? sb.slot : b->actor.id.slot;
  return va < vb ? -1 : va > vb ? 1 : 0;
}

qa_q2_entity **qa_q2mp_entities_named(qa_q2_game_services *g, const char *classname, size_t *out_count) {
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  qa_q2_entity **out = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *out));
  size_t count = 0;
  for (size_t i = 0; i < n; i++)
    if (strcmp(all[i]->classname, classname) == 0) out[count++] = all[i];
  /* Stable insertion sort by source slot, as Array.prototype.sort. */
  for (size_t i = 1; i < count; i++) {
    qa_q2_entity *v = out[i];
    size_t j = i;
    while (j > 0 && cmp_source_slot(g, out[j - 1], v) > 0) { out[j] = out[j - 1]; j--; }
    out[j] = v;
  }
  *out_count = count;
  return out;
}

double qa_q2mp_players_range(qa_q2_game_services *g, qa_q2_entity *spot) {
  double closest = 9999999;
  size_t n = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &n);
  for (size_t i = 0; i < n; i++) {
    if (qa_q2mp_health_or(g, players[i], 0) <= 0) continue;
    qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, players[i]);
    if (body.has_value) {
      double d = qa_q2_length(qa_q2_subtract(body.value.origin, qa_q2_game_body(g, spot).origin));
      if (d < closest) closest = d;
    }
  }
  return closest;
}

static bool eq_ci(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++; b++;
  }
  return *a == *b;
}

qa_q2_entity *qa_q2mp_select_spawn(qa_q2_game_services *g, double slot, const char *spawn_point) {
  qa_q2_entity *spot = NULL;
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) {
    size_t n = 0;
    qa_q2_entity **spots = qa_q2mp_entities_named(g, "info_player_deathmatch", &n);
    if (((int64_t)g->options.deathmatch_flags & 512) != 0) {
      double best = 0;
      for (size_t i = 0; i < n; i++) {
        double d = qa_q2mp_players_range(g, spots[i]);
        if (d > best) { best = d; spot = spots[i]; }
      }
      if (spot == NULL && n > 0) spot = spots[0];
    } else {
      qa_q2_entity *first = NULL, *second = NULL;
      double first_range = 99999, second_range = 99999;
      for (size_t i = 0; i < n; i++) {
        double range = qa_q2mp_players_range(g, spots[i]);
        if (range < first_range) { first_range = range; first = spots[i]; }
        else if (range < second_range) { second_range = range; second = spots[i]; }
      }
      double count = (double)n <= 2 ? (double)n : (double)n - 2;
      if (count > 0) {
        double selection = floor(g->host->random(g->host->ctx) * count);
        for (size_t i = 0; i < n; i++) {
          if (n > 2 && (spots[i] == first || spots[i] == second)) selection++;
          if (selection-- == 0) { spot = spots[i]; break; }
        }
      }
    }
    free(spots);
  } else if (g->options.mode == QA_Q2_MODE_COOP && slot != 0) {
    size_t n = 0;
    qa_q2_entity **coops = qa_q2mp_entities_named(g, "info_player_coop", &n);
    size_t idx = 0;
    for (size_t i = 0; i < n; i++)
      if (eq_ci(coops[i]->targetname, spawn_point)) {
        if (idx == (size_t)(slot - 1)) { spot = coops[i]; break; }
        idx++;
      }
    free(coops);
  }
  if (spot == NULL) {
    size_t n = 0;
    qa_q2_entity **starts = qa_q2mp_entities_named(g, "info_player_start", &n);
    for (size_t i = 0; i < n; i++) {
      bool match = spawn_point[0] == '\0' ? starts[i]->targetname[0] == '\0'
                                          : eq_ci(starts[i]->targetname, spawn_point);
      if (match) { spot = starts[i]; break; }
    }
    if (spot == NULL && spawn_point[0] == '\0' && n > 0) spot = starts[0];
    free(starts);
  }
  if (spot == NULL)
    qa_q2f_error("No Q2 player spawn for '%s' on %s", spawn_point, g->options.map_name);
  return spot;
}

bool qa_q2mp_mirror_kill_box(qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_body_state body = qa_q2_game_body(g, entity);
  for (;;) {
    qa_trace_result trace = qa_q2mp_trace(g, body.origin, body.origin, &body.bounds, &entity->actor.id, 0x2010003);
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR) return !trace.start_solid;
    qa_actor_id target = trace.hit.actor;
    g->damage(g->ctx, target, entity, target, &entity->actor.id, 100000, 0, QA_Q2_ZERO, body.origin, QA_Q2_ZERO, 21, 32, NULL);
    if (qa_session_actor_registry_is_live(g->host->actors, target)) {
      qa_q2_entity *other = qa_q2_game_entity_of(g, &target);
      if (other == NULL || strcmp(other->solid, "none") != 0) return false;
    }
  }
}

qa_vec3 qa_q2mp_spawn_origin(qa_q2_entity *entity, qa_q2_game_services *g) {
  return qa_q2_add(qa_q2_game_body(g, entity).origin, (qa_vec3){0, 0, 10});
}

const char *qa_q2mp_userinfo_get(const char *source, const char *key, char *buf, size_t size) {
  const char *p = source[0] == '\\' ? source + 1 : source;
  while (*p) {
    const char *kend = strchr(p, '\\');
    if (kend == NULL) return NULL;
    const char *vstart = kend + 1;
    const char *vend = strchr(vstart, '\\');
    size_t klen = (size_t)(kend - p);
    if (klen == strlen(key) && strncmp(p, key, klen) == 0) {
      size_t vlen = vend == NULL ? strlen(vstart) : (size_t)(vend - vstart);
      if (vlen >= size) vlen = size - 1;
      memcpy(buf, vstart, vlen);
      buf[vlen] = '\0';
      return buf;
    }
    if (vend == NULL) return NULL;
    p = vend + 1;
  }
  return NULL;
}

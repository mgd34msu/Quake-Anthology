/* Port of src/content/q2/rerelease/entities.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/entities.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/targets.h"
#include "qa/content/q2/rerelease/fog.h"
#include "qa/content/q2/rerelease/world_text.h"
#include "qa/core/math.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_q2rr_health_target_set(qa_q2_rerelease_entities *e, qa_actor_id k, qa_actor_id v) {
  for (size_t i = 0; i < e->ht_count; i++) if (qa_same_actor(e->health_targets[i].key, k)) { e->health_targets[i].value = v; return true; }
  if (e->ht_count == e->ht_cap) { size_t nc = e->ht_cap ? e->ht_cap * 2 : 8; void *q = realloc(e->health_targets, nc * sizeof *e->health_targets); if (!q) return false; e->health_targets = q; e->ht_cap = nc; }
  e->health_targets[e->ht_count].key = k; e->health_targets[e->ht_count].value = v; e->ht_count++; return true;
}
bool qa_q2rr_health_target_get(qa_q2_rerelease_entities *e, qa_actor_id k, qa_actor_id *out) {
  for (size_t i = 0; i < e->ht_count; i++) if (qa_same_actor(e->health_targets[i].key, k)) { *out = e->health_targets[i].value; return true; }
  return false;
}
void qa_q2rr_health_target_delete(qa_q2_rerelease_entities *e, qa_actor_id k) {
  for (size_t i = 0; i < e->ht_count; i++) if (qa_same_actor(e->health_targets[i].key, k)) {
    memmove(&e->health_targets[i], &e->health_targets[i + 1], (e->ht_count - i - 1) * sizeof *e->health_targets); e->ht_count--; return;
  }
}
void qa_q2rr_health_target_clear(qa_q2_rerelease_entities *e) { e->ht_count = 0; }

static qa_bounds abs_bounds(qa_q2_entity *e, qa_q2_game_services *g) {
  qa_body_state b = rr_body_of(g, e);
  qa_bounds r = {qa_q2_add(b.origin, b.bounds.min), qa_q2_add(b.origin, b.bounds.max)}; return r;
}
static bool bounds_intersect(qa_bounds a, qa_bounds b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

static void ent_trigger(qa_q2_entity *e, qa_q2_game_services *g) {
  qa_vec3 angles = rr_body_of(g, e).angles;
  qa_vec3 a2 = angles; a2.y = angles.y == 0 ? 360 : angles.y;
  e->movedir = qa_q2_movedir(a2);
  e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
  qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_angles = true; ch.angles = QA_Q2_ZERO;
  g->move(g->ctx, e, ch, false); g->solid(g->ctx, e, "trigger"); g->link(g->ctx, e);
}

static void flare_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a; e->server_flags = rr_bxor(e->server_flags, 1); g->link(g->ctx, e);
}
static void music_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  const char *s = qa_q2_spawn_get(&e->spawn, "sounds");
  qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev); ev.kind = "music"; ev.track = s ? s : "0"; rr_emit_host(g, ev);
}
static void sky_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)g; (void)o; (void)a; qa_q2_rerelease_entities *m = c;
  const char *sky = qa_q2_spawn_get(&e->spawn, "sky");
  if (sky) { free(m->sky.name); m->sky.name = qa_q2rr_dup(sky); }
  if (qa_q2_spawn_has(&e->spawn, "skyrotate")) m->sky.rotation = qa_q2_number_field(&e->spawn, "skyrotate", 0);
  if (qa_q2_spawn_has(&e->spawn, "skyautorotate")) m->sky.auto_rotate = qa_q2_number_field(&e->spawn, "skyautorotate", 0) != 0;
  if (qa_q2_spawn_has(&e->spawn, "skyaxis")) m->sky.axis = qa_q2_vector_field(&e->spawn, "skyaxis");
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "sky"; ev.sky_name = m->sky.name; ev.rotation = m->sky.rotation; ev.auto_rotate = m->sky.auto_rotate; ev.axis = m->sky.axis;
  rr_emit_rr(&m->hooks, ev);
}
static void crossunit_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a; qa_q2_rerelease_entities *m = c;
  m->campaign.cross_unit_flags = rr_bor(m->campaign.cross_unit_flags, e->spawnflags);
  g->remove(g->ctx, e);
}
static void crossunit_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_entities *m = c;
  double want = rr_band(rr_band(m->campaign.cross_unit_flags, 0xff00ff), e->spawnflags);
  if (e->spawnflags == want) { qa_q2_game_use_targets_entity(g, e, &e->actor.id, false); g->remove(g->ctx, e); }
}
static void autosave_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)e; (void)o; (void)a; qa_q2_rerelease_entities *m = c;
  if (rr_now(g) - m->last_auto_save > m->players->rerelease_options.auto_save_minimum_time) {
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev); ev.kind = "autosave"; rr_emit_rr(&m->hooks, ev);
    m->last_auto_save = rr_now(g);
  }
}
static void achievement_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)g; (void)o; (void)a; qa_q2_rerelease_entities *m = c;
  const char *id = qa_q2_spawn_get(&e->spawn, "achievement");
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev); ev.kind = "achievement"; ev.id = id ? id : "";
  rr_emit_rr(&m->hooks, ev);
}
static void story_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)g; (void)o; (void)a; qa_q2_rerelease_entities *m = c;
  free(m->story); m->story = qa_q2rr_dup(e->message ? e->message : "");
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev); ev.kind = "story"; ev.text = m->story;
  rr_emit_rr(&m->hooks, ev);
}
static void healthbar_check(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_entities *m = c;
  qa_q2_entity *target = g->pick_target(g->ctx, e->target ? e->target : "");
  if (!target || !g->host->is_monster(g->host->ctx, target->actor.id)) { g->remove(g->ctx, e); return; }
  qa_q2rr_health_target_set(m, e->actor.id, target->actor.id);
}
static void healthbar_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a; qa_q2_rerelease_entities *m = c;
  qa_q2_entity *target = g->pick_target(g->ctx, e->target ? e->target : "");
  qa_actor_id saved; bool has = qa_q2rr_health_target_get(m, e->actor.id, &saved);
  if (!target || !has || !qa_same_actor(saved, target->actor.id)) { g->remove(g->ctx, e); return; }
  int slot = -1;
  for (int i = 0; i < 2; i++) if (!m->health_bars[i].has) { slot = i; break; }
  if (slot < 0) { rr_diag(g, "target_healthbar: too many health bars"); g->remove(g->ctx, e); return; }
  e->has_enemy = true; e->enemy = target->actor.id;
  m->health_bars[slot] = (qa_q2rr_health_bar){true, e->actor.id, target->actor.id, false, 0};
}
static void changelevel_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  qa_q2_rerelease_entities *m = c;
  if (!m->players->intermission.playing) return;
  if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER) {
    bool ok = false;
    for (size_t i = 0; i < m->players->state_count; i++)
      if (m->players->states[i].state->slot == 0) { ok = rr_health(g, m->players->states[i].actor) > 0; break; }
    if (!ok) return;
  }
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) {
    qa_actor_id wa = g->host->world_actor(g->host->ctx);
    if (!m->players->rerelease_options.deathmatch_allow_exit && (!other || !qa_same_actor(*other, wa))) {
      qa_q2_entity *target = rr_ent(g, other);
      if (target) g->damage(g->ctx, target->actor.id, e, e->actor.id, &e->actor.id, 10 * target->max_health, 1000, QA_Q2_ZERO, rr_body_of(g, target).origin, QA_Q2_ZERO, 28, 0, NULL);
      return;
    }
    if (rr_now(g) < 10) return;
    qa_q2rr_player_state *pl = act ? qa_q2rr_players_get_state(m->players, *act) : NULL;
    if (pl) {
      const char *args[1] = {pl->name};
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "localized-print"; ev.level = "high"; ev.text = "$g_exited_level"; ev.args = args; ev.arg_count = 1;
      rr_emit_rr(&m->hooks, ev);
    }
  }
  if (e->map && strchr(e->map, '*')) g->counters->server_flags = rr_band(g->counters->server_flags, 0x0000ff00);
  qa_q2_landmark_carry landmark; const qa_q2_landmark_carry *lm = NULL;
  if (act && g->options.mode != QA_Q2_MODE_DEATHMATCH && qa_q2rr_players_get_state(m->players, *act)) {
    qa_q2_entity *target = g->pick_target(g->ctx, e->target ? e->target : "");
    e->has_goal = target != NULL; if (target) e->goal = target->actor.id;
    qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, *act);
    qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, *act);
    if (target && br.has_value && view.has_value) {
      qa_body_state ref = rr_body_of(g, target);
      landmark.player = *act; landmark.name = target->targetname;
      landmark.relative_origin = qa_q2_unrotate_landmark(qa_q2_subtract(br.value.origin, ref.origin), ref.angles);
      landmark.relative_velocity = qa_q2_unrotate_landmark(view.value.old_velocity, ref.angles);
      landmark.relative_view_angles = qa_q2_subtract(view.value.view_angles, ref.angles);
      lm = &landmark;
    }
  }
  qa_q2_rerelease_players_begin_rerelease_intermission(m->players, g, e->map ? e->map : "", lm, e->spawnflags);
}

static size_t team_list(qa_q2_entity *e, qa_q2_game_services *g, qa_q2_entity **buf, size_t cap) {
  const char *name = qa_q2_spawn_get(&e->spawn, "team");
  if (!name || !name[0]) { if (cap) buf[0] = e; return 1; }
  size_t n = 0, en = 0; qa_q2_entity *const *ents = g->entities(g->ctx, &en);
  for (size_t i = 0; i < en; i++) {
    const char *tn = qa_q2_spawn_get(&ents[i]->spawn, "team");
    if (tn && strcmp(tn, name) == 0) { if (n < cap) buf[n] = ents[i]; n++; }
  }
  return n;
}
static void poi_setup(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  if (rr_band(e->spawnflags, 5) != 0) {
    qa_q2_entity *team[256]; size_t n = team_list(e, g, team, 256); if (n > 256) n = 256;
    for (size_t i = 0; i < n; i++) team[i]->spawnflags = rr_bor(team[i]->spawnflags, rr_band(e->spawnflags, 5));
  }
}
static void poi_use(void *c, qa_q2_entity *source, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  (void)other; qa_q2_rerelease_entities *m = c;
  source->spawnflags = rr_bnot_and(source->spawnflags, 8);
  if (source->count != 0 && m->poi_stage > source->count) return;
  qa_q2_entity *selected = source;
  qa_q2_entity *team[256]; size_t tn = team_list(source, g, team, 256); if (tn > 256) tn = 256;
  qa_q2_entity *master = tn ? team[0] : source;
  if (qa_q2_spawn_has(&source->spawn, "team")) {
    selected = NULL;
    double best_dist = INFINITY, best_style = 2147483647;
    qa_q2_entity *fallback = NULL;
    qa_vec3 origin = QA_Q2_ZERO;
    if (act) { qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, *act); if (br.has_value) origin = br.value.origin; }
    for (size_t i = 0; i < tn; i++) {
      qa_q2_entity *member = team[i];
      if (rr_band(member->spawnflags, 8) != 0) continue;
      if (rr_band(member->spawnflags, 2) != 0) { fallback = member; continue; }
      if ((member->count != 0 && m->poi_stage > member->count) || member->style > best_style) continue;
      qa_vec3 dest = rr_body_of(g, member).origin;
      qa_q2rr_navigation path = m->hooks.navigation(m->hooks.ctx, origin, dest, act);
      double distance;
      if (path.kind && strcmp(path.kind, "path") == 0) distance = path.distance_squared;
      else if (path.kind && strcmp(path.kind, "no-navigation") == 0) { qa_vec3 d = qa_q2_subtract(dest, origin); distance = qa_q2_dot(d, d); }
      else distance = INFINITY;
      bool nearest = rr_band(master->spawnflags, 1) != 0;
      if (nearest && selected && distance > best_dist) continue;
      if (member->style < best_style) {
        if (nearest && distance == INFINITY) continue;
        best_style = member->style; if (nearest) best_dist = distance; selected = member;
      } else if (!nearest || distance < best_dist) { best_dist = distance; selected = member; }
    }
    if (!selected && fallback && rr_band(fallback->spawnflags, 4) != 0) selected = fallback;
  }
  if (!selected) return;
  if (selected->classname && strcmp(selected->classname, "target_poi") == 0 &&
      rr_band(selected->spawnflags, 2) != 0 && rr_band(selected->spawnflags, 4) == 0) return;
  if (selected->count != 0) m->poi_stage = selected->count;
  bool has_dyn = false; qa_actor_id dyn = {0};
  if (rr_band(selected->spawnflags, 4) != 0)
    for (size_t i = 0; i < tn; i++) if (rr_band(team[i]->spawnflags, 2) != 0) { has_dyn = true; dyn = team[i]->actor.id; break; }
  const char *image = qa_q2_spawn_get(&selected->spawn, "image");
  free(m->poi.image);
  m->poi.has = true; m->poi.actor = selected->actor.id; m->poi.origin = rr_body_of(g, selected).origin;
  m->poi.image = qa_q2rr_dup(image ? image : "friend");
  m->poi.has_dynamic = has_dyn; m->poi.dynamic = dyn;
}

static qa_q2_entity **eligible(qa_q2_rerelease_entities *m, qa_q2_game_services *g, qa_q2_entity **buf, size_t *n) {
  size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
  size_t k = 0;
  for (size_t i = 0; i < pn; i++) {
    qa_q2_entity *pl = rr_ent(g, &pls[i]);
    qa_q2rr_player_state *st = qa_q2rr_players_get_state(m->players, pls[i]);
    if (pl && st && !st->dead && !st->spectator && !st->noclip && rr_health(g, pls[i]) > 0) buf[k++] = pl;
  }
  *n = k; return buf;
}
static void fire_relay(qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *actor) {
  const char *msg = e->message; e->message = "";
  qa_q2_game_use_targets_entity(g, e, actor, false);
  e->message = msg;
}
static void coop_relay_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  (void)other; qa_q2_rerelease_entities *m = c;
  if (g->options.mode != QA_Q2_MODE_COOP) { fire_relay(e, g, act); return; }
  qa_bounds bounds = abs_bounds(e, g);
  qa_q2_entity *buf[64]; size_t n = 0; eligible(m, g, buf, &n); if (n > 64) n = 64;
  qa_q2_entity *outside[64]; size_t on = 0;
  for (size_t i = 0; i < n; i++) if (!bounds_intersect(bounds, abs_bounds(buf[i], g))) outside[on++] = buf[i];
  if (on == 0) { fire_relay(e, g, act); return; }
  if (e->timestamp < rr_now(g)) {
    for (size_t i = 0; i < on; i++) {
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "centerprint"; ev.has_actor = true; ev.actor = outside[i]->actor.id; ev.text = e->map; rr_emit_host(g, ev);
    }
    if (act) {
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "centerprint"; ev.has_actor = true; ev.actor = *act; ev.text = e->message; rr_emit_host(g, ev);
    }
  }
  e->timestamp = rr_now(g) + 5;
}
static void coop_relay_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_entities *m = c;
  qa_bounds bounds = abs_bounds(e, g);
  qa_q2_entity *buf[64]; size_t n = 0; eligible(m, g, buf, &n); if (n > 64) n = 64;
  qa_q2_entity *inside[64]; size_t in_n = 0;
  for (size_t i = 0; i < n; i++) if (bounds_intersect(bounds, abs_bounds(buf[i], g))) inside[in_n++] = buf[i];
  if (in_n == n) {
    size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
    fire_relay(e, g, pn ? &pls[0] : NULL); g->remove(g->ctx, e); return;
  }
  if (in_n != 0 && e->timestamp < rr_now(g)) {
    size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
    for (size_t i = 0; i < pn; i++) {
      bool is_in = false;
      for (size_t j = 0; j < in_n; j++) if (qa_same_actor(inside[j]->actor.id, pls[i])) is_in = true;
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "centerprint"; ev.has_actor = true; ev.actor = pls[i]; ev.text = is_in ? e->message : e->map;
      rr_emit_host(g, ev);
    }
    e->timestamp = rr_now(g) + 5;
  }
  g->schedule(g->ctx, e, e->wait, m->coop_relay_think);
}

static void flashlight_touch(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_rerelease_entities *m = c;
  if (!g->host->is_player(g->host->ctx, contact.other)) return;
  if (rr_band(e->spawnflags, 1) != 0 && !m->hooks.clip_trigger(m->hooks.ctx, e, contact.other, g)) return;
  if (e->style == 1 || e->style == 2) { qa_q2_rerelease_entities_toggle_flashlight(m, contact.other, g, e->style == 1); return; }
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, contact.other);
  if (br.has_value && qa_q2_dot(br.value.velocity, br.value.velocity) > 32)
    qa_q2_rerelease_entities_toggle_flashlight(m, contact.other, g, qa_q2_dot(qa_q2_normalize(br.value.velocity), e->movedir) > 0);
}
static void fog_touch(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_rerelease_entities *m = c;
  if (!g->host->is_player(g->host->ctx, contact.other) || e->timestamp > rr_now(g)) return;
  e->timestamp = rr_now(g) + e->wait;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->players, contact.other);
  qa_q2_entity *values = e->has_goal ? rr_ent(g, &e->goal) : NULL;
  if (!values) values = e;
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, contact.other);
  if (!br.has_value) return;
  qa_body_state body = br.value;
  extra->fog_transition = rr_band(e->spawnflags, 4) != 0 ? 0 : (values->delay != 0 ? values->delay : 0.5);
  qa_q2_fog_state value;
  if (rr_band(e->spawnflags, 16) != 0) {
    qa_bounds bounds = abs_bounds(e, g);
    qa_vec3 center = qa_q2_scale(qa_q2_add(bounds.min, bounds.max), 0.5);
    qa_vec3 size = qa_q2_scale(qa_q2_add(qa_q2_subtract(bounds.max, bounds.min), qa_q2_subtract(body.bounds.max, body.bounds.min)), 0.5);
    qa_vec3 dir = e->movedir;
    qa_vec3 start = rr_v3(-dir.x * size.x, -dir.y * size.y, -dir.z * size.z);
    qa_vec3 end = qa_q2_scale(start, -1);
    qa_vec3 rel = qa_q2_subtract(body.origin, center);
    qa_vec3 distance = rr_v3(rel.x * fabs(dir.x), rel.y * fabs(dir.y), rel.z * fabs(dir.z));
    double fraction = qa_q2_length(qa_q2_subtract(distance, start)) / qa_q2_length(qa_q2_subtract(start, end));
    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    value = qa_q2_interpolate_fog(qa_q2_rerelease_fog_fields(&values->spawn, true), qa_q2_rerelease_fog_fields(&values->spawn, false), fraction);
  } else {
    if (rr_band(e->spawnflags, 8) == 0 && qa_q2_length(body.velocity) <= 0.0001) return;
    bool on = rr_band(e->spawnflags, 8) != 0 || qa_q2_dot(qa_q2_normalize(body.velocity), e->movedir) > 0;
    value = qa_q2_rerelease_fog_fields(&values->spawn, !on);
  }
  if (rr_band(e->spawnflags, 1) != 0) extra->wanted_fog.fog = value.fog;
  if (rr_band(e->spawnflags, 2) != 0) extra->wanted_fog.height_fog = value.height_fog;
}

static void world_text_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_entities *m = c;
  static const qa_vec4 colors[8] = {{1,1,1,1},{1,0,0,1},{0,0,1,1},{0,1,0,1},{1,1,0,1},{0,0,0,1},{0,1,1,1},{116.0/255,61.0/255,50.0/255,1}};
  double sel = qa_q2_number_field(&e->spawn, "sounds", 0);
  qa_vec4 color = {1, 1, 1, 1};
  if (sel == trunc(sel) && sel >= 0 && sel < 8) color = colors[(size_t)sel];
  else rr_diag(g, "info_world_text: invalid color");
  qa_body_state body = rr_body_of(g, e);
  double yaw = qa_angle_mod(body.angles.y) + 180;
  double radius = qa_q2_number_field(&e->spawn, "radius", 0);
  if (radius < 0) { g->schedule(g->ctx, e, rr_frame(g), m->world_text_think); return; }
  char textbuf[160];
  qa_vec3 fixed_angles = rr_v3(0, yaw > 360 ? yaw - 360 : yaw, 0);
  qa_world_text wt = qa_q2_world_text(body.origin, body.angles.y == -3 ? NULL : &fixed_angles,
                                      e->message ? e->message : "", color, radius == 0 ? 0.2 : radius, true,
                                      textbuf, sizeof textbuf);
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "world-text"; ev.lifetime = rr_frame(g); ev.text_value = wt;
  /* text buffer is stack-local: copy into event via static ring */
  static char ring[8][160]; static size_t ring_i;
  char *slot = ring[ring_i++ % 8];
  snprintf(slot, 160, "%s", textbuf); ev.text_value.text = slot;
  rr_emit_rr(&m->hooks, ev);
  g->schedule(g->ctx, e, rr_frame(g), m->world_text_think);
}
static void world_text_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  (void)other; qa_q2_rerelease_entities *m = c;
  if (!e->has_activator) { e->has_activator = act != NULL; if (act) e->activator = *act; world_text_think(c, e, g); }
  else { g->cancel(g->ctx, e); e->think = m->world_text_think; e->has_activator = false; }
  if (rr_band(e->spawnflags, 2) != 0) e->use = (qa_q2_use){0};
  qa_q2_entity *target = g->pick_target(g->ctx, e->target ? e->target : "");
  if (target && target->use.fn) target->use.fn(target->use.ctx, target, g, &e->actor.id, &e->actor.id);
  if (rr_band(e->spawnflags, 4) != 0) g->remove(g->ctx, e);
}

bool qa_q2_rerelease_entities_init(qa_q2_rerelease_entities *m, qa_q2_rerelease_players *p,
                                   qa_q2_rerelease_hooks h, qa_q2_rerelease_campaign_state *campaign) {
  memset(m, 0, sizeof *m);
  m->players = p; m->hooks = h;
  if (campaign) { /* borrowed: alias by pointer swap trick - store copy of header fields via campaign pointer in goals; keep owned empty */
  }
  if (!qa_q2_create_rerelease_campaign_state(&m->campaign)) return false;
  m->owns_campaign = true;
  if (campaign) {
    /* TS shares the passed campaign object; C moves it in. */
    qa_q2_rerelease_campaign_destroy(&m->campaign);
    m->campaign = *campaign;
    memset(campaign, 0, sizeof *campaign);
    /* recreate emptied caller state so destroy stays safe */
    qa_q2_create_rerelease_campaign_state(campaign);
  }
  m->world_fog = qa_q2_create_fog();
  m->story = qa_q2rr_dup("");
  m->sky.name = qa_q2rr_dup("unit1_"); m->sky.auto_rotate = true; m->sky.axis = rr_v3(0, 0, 1);
  m->poi_use = (qa_q2_use){m, poi_use}; m->flare_use = (qa_q2_use){m, flare_use};
  m->music_use = (qa_q2_use){m, music_use}; m->sky_use = (qa_q2_use){m, sky_use};
  m->cross_unit_use = (qa_q2_use){m, crossunit_use}; m->autosave_use = (qa_q2_use){m, autosave_use};
  m->achievement_use = (qa_q2_use){m, achievement_use}; m->story_use = (qa_q2_use){m, story_use};
  m->change_level_use = (qa_q2_use){m, changelevel_use}; m->healthbar_use = (qa_q2_use){m, healthbar_use};
  m->world_text_use = (qa_q2_use){m, world_text_use}; m->coop_relay_use = (qa_q2_use){m, coop_relay_use};
  m->poi_setup = (qa_q2_think){m, poi_setup}; m->coop_relay_think = (qa_q2_think){m, coop_relay_think};
  m->cross_unit_think = (qa_q2_think){m, crossunit_think}; m->healthbar_check = (qa_q2_think){m, healthbar_check};
  m->world_text_think = (qa_q2_think){m, world_text_think};
  m->flashlight_touch = (qa_q2_touch){m, flashlight_touch}; m->fog_touch = (qa_q2_touch){m, fog_touch};
  return m->story && m->sky.name;
}
void qa_q2_rerelease_entities_destroy(qa_q2_rerelease_entities *m) {
  qa_q2_rerelease_campaign_destroy(&m->campaign);
  free(m->story); free(m->sky.name); free(m->poi.image); free(m->health_targets);
  memset(m, 0, sizeof *m);
}
void qa_q2_rerelease_entities_end_of_unit(qa_q2_rerelease_entities *m, qa_q2_game_services *g) {
  qa_q2_update_rerelease_level(g, &m->campaign);
  size_t n = 0; qa_q2_rerelease_level_entry *levels = qa_q2_rerelease_unit_report(&m->campaign, &n);
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "end-of-unit"; ev.levels = levels; ev.level_count = n; ev.button_time = rr_now(g) + 5;
  rr_emit_rr(&m->hooks, ev);
  qa_q2_rerelease_unit_report_free(levels, n);
}
void qa_q2_rerelease_entities_transfer_healthbar_target(qa_q2_rerelease_entities *m, qa_actor_id old_a, qa_actor_id new_a, qa_q2_game_services *g) {
  for (int i = 0; i < 2; i++) {
    if (!m->health_bars[i].has || !qa_same_actor(m->health_bars[i].target, old_a)) continue;
    m->health_bars[i].target = new_a;
    qa_q2_entity *ctrl = rr_ent(g, &m->health_bars[i].controller);
    if (ctrl) { ctrl->has_enemy = true; ctrl->enemy = new_a; }
  }
}
bool qa_q2_rerelease_entities_spawn(qa_q2_rerelease_entities *m, qa_q2_entity *e, qa_q2_game_services *g) {
  const char *name = e->classname; if (!name) return false;
  if (strcmp(name, "worldspawn") == 0) {
    m->world_fog = qa_q2_rerelease_fog_fields(&e->spawn, false);
    const char *sky = qa_q2_spawn_get(&e->spawn, "sky");
    free(m->sky.name); m->sky.name = qa_q2rr_dup(sky ? sky : "unit1_");
    m->sky.rotation = qa_q2_number_field(&e->spawn, "skyrotate", 0);
    m->sky.auto_rotate = qa_q2_number_field(&e->spawn, "skyautorotate", 1) != 0;
    m->sky.axis = qa_q2_spawn_has(&e->spawn, "skyaxis") ? qa_q2_vector_field(&e->spawn, "skyaxis") : rr_v3(0, 0, 1);
    return false;
  }
  if (strcmp(name, "misc_flare") == 0) {
    long sf = (long)(int32_t)e->spawnflags;
    long rf = 0x200000L | ((sf & 7) << 10) | ((sf & 8) != 0 ? 1 : 0) | (qa_q2_spawn_get(&e->spawn, "image") ? 256 : 0);
    e->render_flags = (double)(int32_t)rf;
    e->scale = qa_q2_number_field(&e->spawn, "radius", 0);
    g->solid(g->ctx, e, "none");
    qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_bounds = true;
    ch.bounds = (qa_bounds){{-32, -32, -32}, {32, 32, 32}};
    g->move(g->ctx, e, ch, false);
    if (e->targetname && e->targetname[0]) e->use = m->flare_use;
    g->link(g->ctx, e); return true;
  }
  if (strcmp(name, "info_world_text") == 0) {
    if (!qa_q2_spawn_has(&e->spawn, "message")) { rr_diag(g, "info_world_text: no message"); g->remove(g->ctx, e); return true; }
    e->think = m->world_text_think; e->use = m->world_text_use;
    if (rr_band(e->spawnflags, 1) == 0) { e->has_activator = true; e->activator = e->actor.id; g->schedule(g->ctx, e, rr_frame(g), m->world_text_think); }
    return true;
  }
  if (strcmp(name, "trigger_flashlight") == 0) {
    ent_trigger(e, g); e->movedir.z = qa_q2_number_field(&e->spawn, "height", 0); e->touch = m->flashlight_touch; return true;
  }
  if (strcmp(name, "trigger_fog") == 0) {
    ent_trigger(e, g); if (e->delay == 0) e->delay = 0.5;
    qa_q2_entity *t = g->pick_target(g->ctx, e->target ? e->target : "");
    e->has_goal = t != NULL; if (t) e->goal = t->actor.id;
    e->touch = m->fog_touch;
    if (rr_band(e->spawnflags, 3) == 0) rr_diag(g, "trigger_fog does not affect fog or height fog");
    return true;
  }
  if (strcmp(name, "trigger_coop_relay") == 0) {
    ent_trigger(e, g);
    if (!e->message || !e->message[0]) e->message = "$g_coop_wait_for_players";
    if (!e->map || !e->map[0]) e->map = "$g_coop_players_waiting_for_you";
    if (e->wait == 0) e->wait = 1;
    if (rr_band(e->spawnflags, 1) != 0) g->schedule(g->ctx, e, e->wait, m->coop_relay_think);
    else e->use = m->coop_relay_use;
    return true;
  }
  if (strcmp(name, "target_poi") == 0) {
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return true; }
    e->visible = false; e->server_flags = rr_bor(e->server_flags, 1); e->use = m->poi_use;
    g->schedule(g->ctx, e, 0.001, m->poi_setup); return true;
  }
  if (strcmp(name, "target_music") == 0) { e->use = m->music_use; return true; }
  if (strcmp(name, "target_changelevel") == 0) {
    if (!e->map || !e->map[0]) { rr_diag(g, "target_changelevel has no map"); g->remove(g->ctx, e); return true; }
    e->visible = false; e->server_flags = rr_bor(e->server_flags, 1); e->use = m->change_level_use; return true;
  }
  if (strcmp(name, "target_sky") == 0) { e->use = m->sky_use; return true; }
  bool multi = strcmp(name, "target_crossunit_trigger") == 0 || strcmp(name, "target_crossunit_target") == 0 ||
               strcmp(name, "target_autosave") == 0 || strcmp(name, "target_achievement") == 0 ||
               strcmp(name, "target_story") == 0 || strcmp(name, "target_healthbar") == 0;
  if (multi) {
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return true; }
    e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
    if (strcmp(name, "target_crossunit_trigger") == 0) e->use = m->cross_unit_use;
    else if (strcmp(name, "target_crossunit_target") == 0) { if (e->delay == 0) e->delay = 1; g->schedule(g->ctx, e, e->delay, m->cross_unit_think); }
    else if (strcmp(name, "target_autosave") == 0) e->use = m->autosave_use;
    else if (strcmp(name, "target_achievement") == 0) e->use = m->achievement_use;
    else if (strcmp(name, "target_story") == 0) e->use = m->story_use;
    else if (!e->target || !e->target[0] || !e->message || !e->message[0]) { rr_diag(g, "target_healthbar requires target and message"); g->remove(g->ctx, e); }
    else { e->use = m->healthbar_use; g->schedule(g->ctx, e, 0.025, m->healthbar_check); }
    return true;
  }
  return false;
}
void qa_q2_rerelease_entities_toggle_flashlight(qa_q2_rerelease_entities *m, qa_actor_id a, qa_q2_game_services *g, bool enabled) {
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->players, a);
  qa_q2_entity *e = rr_ent(g, &a);
  if (extra->flashlight == enabled) return;
  extra->flashlight = enabled;
  if (e) e->flags = enabled ? rr_bor(e->flags, 0x400000) : rr_bnot_and(e->flags, 0x400000);
  if (e) rr_sound_ent(g, e, enabled ? "items/flashlight_on.wav" : "items/flashlight_off.wav", 0, 1, 3);
  qa_q2_rerelease_players_emit_flashlight(m->players, a, g);
}
void qa_q2_rerelease_entities_force_fog(qa_q2_rerelease_entities *m, qa_actor_id a, bool instant) {
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(m->players, a);
  if (qa_q2_equal_fog(extra->fog, extra->wanted_fog)) return;
  double ms = trunc(extra->fog_transition * 1000); ms = ms < 0 ? 0 : ms > 65535 ? 65535 : ms;
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "fog"; ev.has_actor = true; ev.actor = a; ev.fog_value = extra->wanted_fog;
  ev.transition_milliseconds = instant ? 0 : ms;
  rr_emit_rr(&m->hooks, ev);
  extra->fog = extra->wanted_fog;
}
void qa_q2_rerelease_entities_end_player_frame(qa_q2_rerelease_entities *m, qa_q2_entity *ent, qa_q2_game_services *g) {
  qa_q2_rerelease_entities_force_fog(m, ent->actor.id, false);
  for (int slot = 0; slot < 2; slot++) {
    qa_q2rr_health_bar *bar = &m->health_bars[slot];
    if (!bar->has) continue;
    qa_q2_entity *controller = rr_ent(g, &bar->controller);
    qa_q2_entity *target = rr_ent(g, &bar->target);
    double now = rr_now(g);
    if (!controller || (bar->has_dead_until && bar->dead_until < now)) {
      qa_actor_id bt = bar->target; const char *nm = controller && controller->message ? controller->message : "";
      bar->has = false;
      size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
      for (size_t i = 0; i < pn; i++) {
        qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
        ev.kind = "healthbar"; ev.has_actor = true; ev.actor = pls[i]; ev.slot = slot;
        ev.has_target = true; ev.target = bt; ev.name = nm; ev.fraction = 0; ev.visible = false;
        rr_emit_rr(&m->hooks, ev);
      }
      continue;
    }
    double health = target ? rr_health(g, bar->target) : 0;
    if (health <= 0 && !bar->has_dead_until &&
        !(m->hooks.monster_holds_health_bar && m->hooks.monster_holds_health_bar(m->hooks.ctx, bar->target))) {
      if (controller->delay == 0) {
        qa_actor_id bt = bar->target; const char *nm = controller->message ? controller->message : "";
        bar->has = false;
        size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
        for (size_t i = 0; i < pn; i++) {
          qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
          ev.kind = "healthbar"; ev.has_actor = true; ev.actor = pls[i]; ev.slot = slot;
          ev.has_target = true; ev.target = bt; ev.name = nm; ev.fraction = 0; ev.visible = false;
          rr_emit_rr(&m->hooks, ev);
        }
        continue;
      }
      bar->has_dead_until = true; bar->dead_until = now + controller->delay;
    }
    bool empty = bar->has_dead_until || health <= 0;
    bool visible = empty || rr_band(controller->spawnflags, 1) == 0 ||
                   (target && g->host->in_pvs(g->host->ctx, rr_body_of(g, ent).origin, rr_body_of(g, target).origin));
    double frac = 0;
    if (!empty && target) { frac = health / target->max_health; frac = frac < 0 ? 0 : frac > 1 ? 1 : frac; }
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "healthbar"; ev.has_actor = true; ev.actor = ent->actor.id; ev.slot = slot;
    ev.has_target = true; ev.target = bar->target; ev.name = controller->message; ev.fraction = frac; ev.visible = visible;
    rr_emit_rr(&m->hooks, ev);
  }
}
static qa_q2_named_think en_thinks[5]; static qa_q2_named_touch en_touches[2]; static qa_q2_named_use en_uses[12];
qa_q2_callback_definitions qa_q2_rerelease_entities_callbacks(qa_q2_rerelease_entities *m) {
  en_thinks[0] = (qa_q2_named_think){"rr.info_world_text_think", m->world_text_think};
  en_thinks[1] = (qa_q2_named_think){"rr.target_poi_setup", m->poi_setup};
  en_thinks[2] = (qa_q2_named_think){"rr.trigger_coop_relay_think", m->coop_relay_think};
  en_thinks[3] = (qa_q2_named_think){"rr.target_crossunit_target_think", m->cross_unit_think};
  en_thinks[4] = (qa_q2_named_think){"rr.check_target_healthbar", m->healthbar_check};
  en_touches[0] = (qa_q2_named_touch){"rr.trigger_flashlight_touch", m->flashlight_touch};
  en_touches[1] = (qa_q2_named_touch){"rr.trigger_fog_touch", m->fog_touch};
  static const char *names[12] = {"rr.misc_flare_use","rr.info_world_text_use","rr.trigger_coop_relay_use","rr.target_poi_use",
    "rr.use_target_music","rr.use_target_sky","rr.trigger_crossunit_trigger_use","rr.use_target_autosave",
    "rr.use_target_achievement","rr.use_target_story","rr.use_target_healthbar","rr.use_target_changelevel"};
  qa_q2_use uses[12] = {m->flare_use, m->world_text_use, m->coop_relay_use, m->poi_use, m->music_use, m->sky_use,
                        m->cross_unit_use, m->autosave_use, m->achievement_use, m->story_use, m->healthbar_use, m->change_level_use};
  for (int i = 0; i < 12; i++) en_uses[i] = (qa_q2_named_use){names[i], uses[i]};
  qa_q2_callback_definitions d = {0};
  d.think = en_thinks; d.think_count = 5; d.touch = en_touches; d.touch_count = 2; d.use = en_uses; d.use_count = 12;
  return d;
}
static bool ent_spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_rerelease_entities_spawn(c, e, g); }
qa_q2_spawn_module qa_q2_rerelease_entities_as_spawn(qa_q2_rerelease_entities *m) {
  qa_q2_spawn_module mod = {0}; mod.ctx = m; mod.spawn = ent_spawn_cb; return mod;
}

/* Port of src/content/q2/foundation/movers.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/movers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"

typedef struct door_state {
  qa_q2_entity *e;
  qa_vec3 start, end, safe_direction;
  double distance, water_divisor, debounce;
  bool button, angular, water, reversed, activated;
  const char *phase;
  qa_q2_entity *master;
  qa_q2_entity **team; size_t team_count;
} door_state;
typedef struct train_state { qa_q2_entity *e, *destination; double debounce; bool ship; } train_state;

struct qa_q2_mover_module {
  qa_q2_mover_hooks hooks;
  qa_q2_linear_motion *linear;
  qa_q2_angular_motion *angular;
  door_state *doors; size_t door_count, door_cap;
  train_state *trains; size_t train_count, train_cap;
  qa_q2_named_think thinks[32]; size_t think_count;
  qa_q2_named_use uses[4];
  qa_q2_named_touch touches[5];
  qa_q2_named_die dies[1];
  qa_q2_named_blocked blockeds[4];
  qa_q2_callback_definitions defs;
  qa_q2_door_checkpoint *cp_doors; qa_saved_actor_id **cp_teams;
  qa_q2_train_checkpoint *cp_trains;
  qa_q2_train_stop *route_scratch; size_t route_cap;
  char *buf;
};

#define TCB(m, fn) ((qa_q2_think){(m), (fn)})
static void bottom_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void down_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void top_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void prepare_door_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void smart_water_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void train_wait_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void train_next_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void train_piece_wait_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void train_find_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void bob_top_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void bob_bottom_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void bob_up_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void bob_down_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void door_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void door_activate_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void train_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void rotating_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void button_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void door_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void door_trigger_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void rotating_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void path_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void door_killed_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_death_reaction);
static void door_blocked_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);
static void smart_water_blocked_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);
static void train_blocked_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);
static void rotating_damage_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);

static door_state *find_door(qa_q2_mover_module *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->door_count; i++) if (m->doors[i].e == e) return &m->doors[i];
  return NULL;
}
static door_state *door_of(qa_q2_mover_module *m, qa_q2_entity *e) {
  door_state *d = find_door(m, e);
  if (!d) qa_range_error("Missing Q2 door state");
  return d;
}
static train_state *find_train(qa_q2_mover_module *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->train_count; i++) if (m->trains[i].e == e) return &m->trains[i];
  return NULL;
}
static train_state *train_of(qa_q2_mover_module *m, qa_q2_entity *e) {
  train_state *t = find_train(m, e);
  if (!t) qa_range_error("Missing Q2 train state");
  return t;
}
static bool live(qa_q2_game_services *g, qa_actor_id id) { return qa_session_actor_registry_is_live(g->host->actors, id); }
static void move_to(qa_q2_mover_module *m, door_state *d, qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 dest, qa_q2_think done) {
  if (d->angular) qa_q2_angular_motion_move_to(m->angular, e, g, dest, done);
  else qa_q2_linear_motion_move_to(m->linear, e, g, dest, done);
}

static void loop_sound(qa_q2_entity *e, qa_q2_game_services *g, const char *path, bool start) {
  if (!path || path[0] == '\0') return;
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = e->actor.id; ev.origin = qa_q2_game_body(g, e).origin;
  ev.path = path; ev.channel = 2; ev.volume = 1; ev.attenuation = 3; ev.loop = start ? "start" : "stop";
  g->host->emit(g->host->ctx, ev);
}

static void door_sound(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, bool start) {
  door_state *st = door_of(m, e);
  double sounds = qa_q2_integer_field(&e->spawn, "sounds", 0);
  bool enabled = st->water ? (sounds == 1 || sounds == 2) : sounds != 1;
  static const char *water_d[3] = {"world/mov_watr.wav", "", "world/stp_watr.wav"};
  static const char *button_d[3] = {"switches/butn2.wav", "", ""};
  static const char *door_d[3] = {"doors/dr1_strt.wav", "doors/dr1_mid.wav", "doors/dr1_end.wav"};
  const char *const *defaults = st->water ? water_d : st->button ? button_d : door_d;
  const char *sel[3];
  static const char *keys[3] = {"noise_start", "noise_middle", "noise_end"};
  int idx[3] = {0, 1, 2};
  for (int i = 0; i < 3; i++) {
    const char *value = g->options.edition == QA_Q2_EDITION_RERELEASE ? qa_q2_spawn_get(&e->spawn, keys[i]) : NULL;
    sel[i] = value == NULL ? (enabled ? defaults[idx[i]] : "") : (strcmp(value, "0") == 0 || strcmp(value, " ") == 0 ? "" : value);
  }
  const char *sound = start ? sel[0] : sel[2], *middle = sel[1];
  double attenuation = (g->options.edition == QA_Q2_EDITION_RERELEASE && !st->water) ? qa_q2_number_field(&e->spawn, "attenuation", 3) : 3;
  double att = attenuation == -1 ? 0 : attenuation;
  if (st->master == e && sound[0]) {
    qa_vec3 origin = qa_q2_game_body(g, e).origin;
    if (g->options.edition == QA_Q2_EDITION_RERELEASE && st->team_count > 1) {
      qa_vec3 sum = QA_Q2_ZERO;
      for (size_t i = 0; i < st->team_count; i++) {
        qa_body_state b = qa_q2_game_body(g, st->team[i]);
        sum = qa_q2_add(sum, qa_q2_add(b.origin, qa_q2_scale(qa_q2_add(b.bounds.min, b.bounds.max), 0.5)));
      }
      qa_vec3 center = qa_q2_scale(sum, 1 / (double)st->team_count);
      if (((int64_t)g->host->point_contents(g->host->ctx, center) & 1) == 0) origin = center;
    }
    qa_q2_presentation_event ev = {0};
    ev.kind = "sound"; ev.has_actor = true; ev.actor = e->actor.id; ev.origin = origin; ev.path = sound;
    ev.channel = 2; ev.volume = 1; ev.attenuation = att; ev.loop = "once";
    g->host->emit(g->host->ctx, ev);
  }
  if (middle[0]) {
    qa_q2_presentation_event ev = {0};
    ev.kind = "sound"; ev.has_actor = true; ev.actor = e->actor.id; ev.origin = qa_q2_game_body(g, e).origin; ev.path = middle;
    ev.channel = 2; ev.volume = 1; ev.attenuation = att; ev.loop = start ? "start" : "stop";
    g->host->emit(g->host->ctx, ev);
  }
}

static void portals(qa_q2_entity *e, qa_q2_game_services *g, bool open) {
  size_t n = 0;
  qa_q2_entity *const *ts = g->targets(g->ctx, e->target, &n);
  qa_q2_entity **copy = NULL;
  if (n) { copy = malloc(n * sizeof *copy); if (!copy) qa_range_error("out of memory"); memcpy(copy, ts, n * sizeof *copy); }
  for (size_t i = 0; i < n; i++)
    if (strcmp(copy[i]->classname, "func_areaportal") == 0)
      g->host->set_area_portal(g->host->ctx, qa_q2_integer_field(&copy[i]->spawn, "style", 0), open);
  free(copy);
}

static void bottom_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  door_state *st = door_of(m, e);
  st->phase = "bottom";
  if (st->button) { e->effects = (double)(((int64_t)e->effects & ~0x800) | 0x400); g->show(g->ctx, e); }
  else {
    door_sound(m, e, g, false);
    if (g->options.edition == QA_Q2_EDITION_CLASSIC || ((int64_t)e->spawnflags & 1) == 0) portals(e, g, false);
  }
}
static void down_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  door_state *st = door_of(m, e);
  st->phase = "down";
  if (e->max_health > 0 && !st->water) {
    qa_gameplay_authority_set_health(g->host->combat, e->actor, e->max_health);
    qa_combat_trait_changes tc = {0}; tc.has_can_take_damage = true; tc.can_take_damage = true;
    qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
  }
  if (st->button) { e->frame = 0; g->show(g->ctx, e); }
  else door_sound(m, e, g, true);
  move_to(m, st, e, g, st->start, TCB(m, bottom_fn));
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && !st->button && ((int64_t)e->spawnflags & 1) != 0) portals(e, g, true);
}
static void top_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  door_state *st = door_of(m, e);
  st->phase = "top";
  if (st->button) {
    e->effects = (double)(((int64_t)e->effects & ~0x400) | 0x800);
    e->frame = 1;
    g->show(g->ctx, e);
    qa_actor_id act = e->activator;
    qa_q2_game_use_targets_entity(g, e, e->has_activator ? &act : NULL, false);
    if (!live(g, e->actor.id)) return;
  } else {
    door_sound(m, e, g, false);
    if (((int64_t)e->spawnflags & 32) != 0) return;
  }
  if (e->wait >= 0) g->schedule(g->ctx, e, e->wait, TCB(m, down_fn));
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && !st->button && ((int64_t)e->spawnflags & 1) != 0) portals(e, g, false);
}
static void up(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *activator) {
  door_state *st = door_of(m, e);
  if (strcmp(st->phase, "up") == 0) return;
  if (strcmp(st->phase, "top") == 0) {
    if (!st->button && e->wait >= 0 && ((int64_t)e->spawnflags & 32) == 0) g->schedule(g->ctx, e, e->wait, TCB(m, down_fn));
    return;
  }
  e->has_activator = activator != NULL;
  if (activator) e->activator = *activator;
  st->phase = "up";
  door_sound(m, e, g, true);
  move_to(m, st, e, g, st->reversed ? qa_q2_scale(st->end, -1) : st->end, TCB(m, top_fn));
  if (!st->button) {
    qa_q2_game_use_targets_entity(g, e, activator, false);
    if (g->options.edition == QA_Q2_EDITION_CLASSIC || ((int64_t)e->spawnflags & 1) == 0) portals(e, g, true);
  }
}
static void use_door(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *activator) {
  door_state *st = door_of(m, e);
  if (st->master != e) return;
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && st->angular && ((int64_t)e->spawnflags & 0x20000) != 0 &&
      (strcmp(st->phase, "bottom") == 0 || strcmp(st->phase, "down") == 0) && activator) {
    qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, *activator);
    if (body.has_value) st->reversed = qa_q2_dot(qa_q2_normalize(qa_q2_subtract(body.value.origin, qa_q2_game_body(g, e).origin)), st->safe_direction) > 0;
  }
  bool close = !st->button && ((int64_t)e->spawnflags & 32) != 0 && (strcmp(st->phase, "up") == 0 || strcmp(st->phase, "top") == 0);
  if (!close && g->options.edition == QA_Q2_EDITION_RERELEASE && st->water && ((int64_t)e->spawnflags & 2) != 0) {
    qa_body_state b = qa_q2_game_body(g, e);
    qa_vec3 center = qa_q2_scale(qa_q2_add(b.bounds.min, b.bounds.max), 0.5);
    if (((int64_t)g->host->point_contents(g->host->ctx, center) & 56) != 0) {
      e->message = ""; e->touch = (qa_q2_touch){0};
      e->has_enemy = activator != NULL;
      if (activator) e->enemy = *activator;
      smart_water_fn(m, e, g);
      return;
    }
  }
  for (size_t i = 0; i < st->team_count; i++) {
    qa_q2_entity *member = st->team[i];
    member->message = ""; member->touch = (qa_q2_touch){0};
    if (close) down_fn(m, member, g);
    else up(m, member, g, activator);
  }
}

static qa_vec3 train_destination(qa_q2_entity *e, qa_q2_entity *target, qa_q2_game_services *g) {
  qa_vec3 origin = qa_q2_game_body(g, target).origin;
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && ((int64_t)e->spawnflags & 32) != 0) return origin;
  qa_vec3 dest = qa_q2_subtract(origin, qa_q2_game_body(g, e).bounds.min);
  return (g->options.edition == QA_Q2_EDITION_RERELEASE && ((int64_t)e->spawnflags & 16) != 0)
      ? qa_q2_subtract(dest, (qa_vec3){1, 1, 1}) : dest;
}

bool qa_q2_mover_module_train_route(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_q2_train_route *out) {
  train_state *st = find_train(m, e);
  if (!st || st->ship) return false;
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  /* target(name): unique path_corner with that targetname, else NULL */
  const char *start_name = qa_q2_spawn_get(&e->spawn, "target");
  if (!start_name) start_name = e->target;
  size_t stop_count = 0;
  qa_actor_id visited[256];
  size_t visited_count = 0;
  const char *cur_name = start_name;
  for (;;) {
    qa_q2_entity *corner = NULL;
    bool ambiguous = false;
    for (size_t i = 0; i < n; i++) {
      if (entities[i]->targetname[0] == '\0' || strcmp(entities[i]->targetname, cur_name) != 0) continue;
      if (corner != NULL || strcmp(entities[i]->classname, "path_corner") != 0) ambiguous = true;
      corner = entities[i];
    }
    if (ambiguous) corner = NULL;
    if (!corner) break;
    bool seen = false;
    for (size_t i = 0; i < visited_count; i++) if (qa_same_actor(visited[i], corner->actor.id)) seen = true;
    if (seen) break;
    if (visited_count < 256) visited[visited_count++] = corner->actor.id;
    /* next */
    qa_q2_entity *next = NULL;
    bool next_ambiguous = false;
    for (size_t i = 0; i < n; i++) {
      if (entities[i]->targetname[0] == '\0' || strcmp(entities[i]->targetname, corner->target) != 0) continue;
      if (next != NULL || strcmp(entities[i]->classname, "path_corner") != 0) next_ambiguous = true;
      next = entities[i];
    }
    if (next_ambiguous) next = NULL;
    if (corner->target[0] && !next) return false;
    if (stop_count == m->route_cap) { m->route_cap = m->route_cap ? m->route_cap * 2 : 8; m->route_scratch = realloc(m->route_scratch, m->route_cap * sizeof *m->route_scratch); if (!m->route_scratch) qa_range_error("out of memory"); }
    qa_q2_train_stop *stop = &m->route_scratch[stop_count++];
    *stop = (qa_q2_train_stop){0};
    stop->actor = corner->actor.id;
    stop->origin = train_destination(e, corner, g);
    stop->has_next = next != NULL;
    if (next) stop->next = next->actor.id;
    stop->wait = corner->wait;
    stop->teleport = ((int64_t)corner->spawnflags & 1) != 0;
    if (!next) break;
    cur_name = corner->target;
    (void)n;
  }
  if (st->destination) {
    bool seen = false;
    for (size_t i = 0; i < visited_count; i++) if (qa_same_actor(visited[i], st->destination->actor.id)) seen = true;
    if (!seen) return false;
  }
  out->running = ((int64_t)e->spawnflags & 1) != 0;
  out->has_destination = st->destination != NULL;
  if (st->destination) out->destination = st->destination->actor.id;
  out->stops = m->route_scratch;
  out->stop_count = stop_count;
  return true;
}

qa_q2_traversal qa_q2_mover_module_traversal(qa_q2_mover_module *m, qa_q2_entity *e) {
  door_state *st = find_door(m, e);
  qa_q2_entity *master = st ? st->master : e;
  door_state *door = find_door(m, master);
  qa_q2_traversal out = {0};
  out.locked = door && (strcmp(door->phase, "bottom") == 0 || strcmp(door->phase, "down") == 0) &&
      ((master->use.fn == door_activate_fn && master->use.ctx == m) ||
       (!door->button && (master->max_health > 0 || (master->targetname[0] && !door->activated))));
  out.has_destination = qa_q2_linear_motion_destination(m->linear, e, &out.destination);
  return out;
}

static qa_saved_actor_id ref_of(qa_q2_entity *e) { return (qa_saved_actor_id){(double)e->actor.id.slot, (double)e->actor.id.generation}; }

qa_q2_movers_checkpoint qa_q2_mover_module_capture(qa_q2_mover_module *m, qa_q2_game_services *g) {
  if (m->cp_teams) { for (size_t i = 0; i < m->door_count; i++) free(m->cp_teams[i]); }
  free(m->cp_doors); free(m->cp_trains); free(m->cp_teams);
  m->cp_doors = NULL; m->cp_trains = NULL; m->cp_teams = NULL;
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  m->cp_doors = malloc((n ? n : 1) * sizeof *m->cp_doors);
  m->cp_teams = calloc(n ? n : 1, sizeof *m->cp_teams);
  m->cp_trains = malloc((n ? n : 1) * sizeof *m->cp_trains);
  if (!m->cp_doors || !m->cp_teams || !m->cp_trains) qa_range_error("out of memory");
  size_t dn = 0, tn = 0;
  for (size_t i = 0; i < n; i++) {
    door_state *d = find_door(m, entities[i]);
    train_state *t = find_train(m, entities[i]);
    if (d) {
      qa_saved_actor_id *team = malloc((d->team_count ? d->team_count : 1) * sizeof *team);
      if (!team) qa_range_error("out of memory");
      for (size_t j = 0; j < d->team_count; j++) team[j] = ref_of(d->team[j]);
      m->cp_teams[dn] = team;
      m->cp_doors[dn] = (qa_q2_door_checkpoint){ref_of(entities[i]),
        {d->start, d->end, d->safe_direction, d->distance, d->water_divisor, d->debounce, d->button, d->angular, d->water, d->reversed, d->activated, d->phase},
        ref_of(d->master), team, d->team_count};
      dn++;
    }
    if (t) {
      m->cp_trains[tn] = (qa_q2_train_checkpoint){ref_of(entities[i]), t->destination != NULL,
          t->destination ? ref_of(t->destination) : (qa_saved_actor_id){0, 0}, t->debounce, t->ship};
      tn++;
    }
  }
  return (qa_q2_movers_checkpoint){m->cp_doors, dn, m->cp_trains, tn,
      qa_q2_linear_motion_capture(m->linear, g), qa_q2_angular_motion_capture(m->angular, g)};
}

static qa_q2_entity *ref_entity(qa_q2_game_services *g, qa_saved_actor_id saved) {
  qa_owned_actor owner = qa_q2_restore_actor(g, saved);
  qa_q2_entity *e = g->entity(g->ctx, &owner.id);
  if (!e) qa_range_error("Q2 mover checkpoint has no source actor");
  return e;
}

void qa_q2_mover_module_restore(qa_q2_mover_module *m, qa_q2_game_services *g, qa_q2_movers_checkpoint cp) {
  for (size_t i = 0; i < m->door_count; i++) free(m->doors[i].team);
  m->door_count = 0; m->train_count = 0;
  for (size_t i = 0; i < cp.door_count; i++) {
    qa_q2_entity *e = ref_entity(g, cp.doors[i].actor);
    if (m->door_count == m->door_cap) { m->door_cap = m->door_cap ? m->door_cap * 2 : 8; m->doors = realloc(m->doors, m->door_cap * sizeof *m->doors); if (!m->doors) qa_range_error("out of memory"); }
    door_state *d = &m->doors[m->door_count++];
    *d = (door_state){0};
    d->e = e;
    d->start = cp.doors[i].state.start; d->end = cp.doors[i].state.end; d->safe_direction = cp.doors[i].state.safe_direction;
    d->distance = cp.doors[i].state.distance; d->water_divisor = cp.doors[i].state.water_divisor; d->debounce = cp.doors[i].state.debounce;
    d->button = cp.doors[i].state.button; d->angular = cp.doors[i].state.angular; d->water = cp.doors[i].state.water;
    d->reversed = cp.doors[i].state.reversed; d->activated = cp.doors[i].state.activated; d->phase = cp.doors[i].state.phase;
    d->master = ref_entity(g, cp.doors[i].master);
    d->team_count = cp.doors[i].team_count;
    d->team = malloc((d->team_count ? d->team_count : 1) * sizeof *d->team);
    if (!d->team) qa_range_error("out of memory");
    for (size_t j = 0; j < d->team_count; j++) d->team[j] = ref_entity(g, cp.doors[i].team[j]);
  }
  for (size_t i = 0; i < cp.train_count; i++) {
    qa_q2_entity *e = ref_entity(g, cp.trains[i].actor);
    if (m->train_count == m->train_cap) { m->train_cap = m->train_cap ? m->train_cap * 2 : 8; m->trains = realloc(m->trains, m->train_cap * sizeof *m->trains); if (!m->trains) qa_range_error("out of memory"); }
    m->trains[m->train_count++] = (train_state){e, cp.trains[i].has_destination ? ref_entity(g, cp.trains[i].destination) : NULL, cp.trains[i].debounce, cp.trains[i].ship};
  }
  qa_q2_linear_motion_restore(m->linear, g, cp.linear);
  qa_q2_angular_motion_restore(m->angular, g, cp.angular);
}

static void train_piece_wait_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) { (void)c; (void)e; (void)g; }

static void spawn_bobbing(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_body_state body = qa_q2_game_body(g, e);
  double height = qa_q2_number_field(&e->spawn, "height", 0);
  if (height == 0) height = 32;
  e->movedir = (body.angles.x == 0 && body.angles.y == 0 && body.angles.z == 0) ? (qa_vec3){0, 0, 1} : qa_q2_movedir(body.angles);
  e->pos1 = body.origin;
  e->pos2 = qa_q2_add(body.origin, qa_q2_scale(e->movedir, height));
  double speed_field = qa_q2_number_field(&e->spawn, "speed", 0);
  e->speed = 2 * height / fmax(speed_field != 0 ? speed_field : 4, 0.1);
  e->accel = e->speed; e->decel = e->speed;
  g->motion(g->ctx, e, "push");
  g->solid(g->ctx, e, "brush");
  qa_q2_linear_motion_move_to(m->linear, e, g, e->pos2, TCB(m, bob_top_fn));
}
static void bob_top_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  if (e->wait > 0) g->schedule(g->ctx, e, e->wait, TCB(m, bob_down_fn));
  else qa_q2_linear_motion_move_to(m->linear, e, g, e->pos1, TCB(m, bob_bottom_fn));
}
static void bob_down_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  qa_q2_linear_motion_move_to(m->linear, e, g, e->pos1, TCB(m, bob_bottom_fn));
}
static void bob_bottom_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  if (e->wait > 0) g->schedule(g->ctx, e, e->wait, TCB(m, bob_up_fn));
  else qa_q2_linear_motion_move_to(m->linear, e, g, e->pos2, TCB(m, bob_top_fn));
}
static void bob_up_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  qa_q2_linear_motion_move_to(m->linear, e, g, e->pos2, TCB(m, bob_top_fn));
}
static void rotating_touch_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  rotating_damage_fn(c, e, g, contact.other);
}

static void spawn_door(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  bool button = strcmp(e->classname, "func_button") == 0;
  bool angular = strcmp(e->classname, "func_door_rotating") == 0;
  bool water = strcmp(e->classname, "func_water") == 0;
  int64_t sf = (int64_t)e->spawnflags;
  qa_vec3 safe = (angular && (sf & 0x20000) != 0) ? qa_q2_movedir(qa_q2_game_body(g, e).angles) : QA_Q2_ZERO;
  double water_divisor = e->accel != 0 ? e->accel : 20;
  e->movedir = angular ? ((sf & 64) ? (qa_vec3){0, 0, 1} : (sf & 128) ? (qa_vec3){1, 0, 0} : (qa_vec3){0, 1, 0})
                       : qa_q2_movedir(qa_q2_game_body(g, e).angles);
  if (angular && (sf & 2)) e->movedir = qa_q2_scale(e->movedir, -1);
  qa_q2_body_changes zero_angles = {0};
  zero_angles.has_angles = true; zero_angles.angles = QA_Q2_ZERO;
  g->move(g->ctx, e, zero_angles, false);
  g->solid(g->ctx, e, "brush");
  g->motion(g->ctx, e, button ? "stop" : "push");
  if (e->speed == 0) e->speed = button ? 40 : water ? 25 : 100;
  if (!button && !angular && !water && g->options.mode == QA_Q2_MODE_DEATHMATCH) e->speed *= 2;
  if (e->accel == 0) e->accel = e->speed;
  if (e->decel == 0) e->decel = e->speed;
  if (e->wait == 0) e->wait = water ? -1 : 3;
  if (e->damage == 0) e->damage = 2;
  qa_body_state body = qa_q2_game_body(g, e);
  qa_vec3 size = qa_q2_subtract(body.bounds.max, body.bounds.min);
  qa_vec3 dir = {fabs(e->movedir.x), fabs(e->movedir.y), fabs(e->movedir.z)};
  double lip = qa_q2_number_field(&e->spawn, "lip", 0);
  double distance_field = qa_q2_number_field(&e->spawn, "distance", 0);
  double distance = angular ? (distance_field != 0 ? distance_field : 90)
      : qa_q2_dot(dir, size) - (lip != 0 ? lip : (button ? 4 : water ? 0 : 8));
  qa_vec3 start = angular ? QA_Q2_ZERO : body.origin;
  qa_vec3 end = qa_q2_add(start, qa_q2_scale(e->movedir, distance));
  if (!button && (sf & 1)) {
    if (g->options.edition == QA_Q2_EDITION_RERELEASE && angular && (sf & 0x20000)) {
      e->spawnflags = (double)((int64_t)e->spawnflags & ~0x20000);
      g->host->diagnostic(g->host->ctx, "Q2 rotating door SAFE_OPEN is incompatible with START_OPEN");
    }
    qa_vec3 prev = start; start = end; end = prev;
    qa_q2_body_changes ch = {0};
    if (angular) { ch.has_angles = true; ch.angles = start; }
    else { ch.has_origin = true; ch.origin = start; }
    g->move(g->ctx, e, ch, true);
    if (angular) e->movedir = qa_q2_scale(e->movedir, -1);
  }
  if (m->door_count == m->door_cap) { m->door_cap = m->door_cap ? m->door_cap * 2 : 8; m->doors = realloc(m->doors, m->door_cap * sizeof *m->doors); if (!m->doors) qa_range_error("out of memory"); }
  door_state *d = &m->doors[m->door_count++];
  *d = (door_state){0};
  d->e = e; d->start = start; d->end = end; d->distance = distance;
  d->button = button; d->angular = angular; d->water = water; d->safe_direction = safe; d->water_divisor = water_divisor;
  d->phase = "bottom"; d->master = e;
  d->team = malloc(sizeof *d->team);
  if (!d->team) qa_range_error("out of memory");
  d->team[0] = e; d->team_count = 1;
  if (!qa_q2_spawn_get(&e->spawn, "team")) { e->has_team_master = true; e->team_master = e->actor.id; }
  if (water) {
    e->accel = e->decel = e->speed;
    if (e->wait == -1) e->spawnflags = (double)((int64_t)e->spawnflags | 32);
    if (g->options.edition == QA_Q2_EDITION_CLASSIC) e->classname = "func_door";
  }
  if (button) e->effects = (double)((int64_t)e->effects | 0x400);
  else if (!water) {
    if (((int64_t)e->spawnflags & 16) != 0) e->effects = (double)((int64_t)e->effects | 0x1000);
    if (!angular && ((int64_t)e->spawnflags & 64) != 0) e->effects = (double)((int64_t)e->effects | 0x2000);
  }
  e->use = (qa_q2_use){m, door_use_fn};
  if (e->max_health > 0 && !water) {
    qa_combat_state st = {0};
    st.health = e->max_health; st.can_take_damage = true;
    st.armor.regular.kind = QA_REGULAR_ARMOR_NONE; st.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(g->host->combat, e->actor, st, NULL, NULL);
    e->die = (qa_q2_die){m, door_killed_fn};
  } else if (button && e->targetname[0] == '\0') e->touch = (qa_q2_touch){m, button_touch_fn};
  else if (!button && e->targetname[0] && e->message[0]) e->touch = (qa_q2_touch){m, door_touch_fn};
  e->blocked = (qa_q2_blocked){m, door_blocked_fn};
  g->show(g->ctx, e);
  if (!button && !water) g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), TCB(m, prepare_door_fn));
  if (water) {
    e->blocked = (qa_q2_blocked){0};
    if (g->options.edition == QA_Q2_EDITION_RERELEASE && ((int64_t)e->spawnflags & 2) != 0) e->blocked = (qa_q2_blocked){m, smart_water_blocked_fn};
  }
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && angular && ((int64_t)e->spawnflags & 0x10000) != 0) {
    if (e->max_health > 0) {
      qa_combat_trait_changes tc = {0}; tc.has_can_take_damage = true; tc.can_take_damage = false;
      qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
    }
    e->die = (qa_q2_die){0};
    g->cancel(g->ctx, e);
    e->use = (qa_q2_use){m, door_activate_fn};
  }
}

static void prepare_door_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  size_t tn = 0;
  const qa_owned_actor *team_actors = g->push_team(g->ctx, e->actor.id, &tn);
  qa_q2_entity **team = malloc((tn ? tn : 1) * sizeof *team);
  if (!team) qa_range_error("out of memory");
  size_t team_count = 0;
  for (size_t i = 0; i < tn; i++) {
    qa_q2_entity *member = g->entity(g->ctx, &team_actors[i].id);
    if (member && find_door(m, member)) team[team_count++] = member;
  }
  qa_q2_entity *master = team_count ? team[0] : e;
  for (size_t i = 0; i < team_count; i++) {
    door_state *d = door_of(m, team[i]);
    d->master = master;
    free(d->team);
    d->team = malloc((team_count ? team_count : 1) * sizeof *d->team);
    if (!d->team) qa_range_error("out of memory");
    memcpy(d->team, team, team_count * sizeof *team);
    d->team_count = team_count;
  }
  if (master != e) { free(team); return; }
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && !door_of(m, e)->angular && ((int64_t)e->spawnflags & 1) != 0) portals(e, g, true);
  double shortest = 0;
  for (size_t i = 0; i < team_count; i++) {
    double a = fabs(door_of(m, team[i])->distance);
    if (i == 0 || a < shortest) shortest = a;
  }
  double time = shortest / e->speed;
  if (time > 0) {
    for (size_t i = 0; i < team_count; i++) {
      qa_q2_entity *member = team[i];
      double speed = fabs(door_of(m, member)->distance) / time;
      double ratio = speed / member->speed;
      member->accel *= ratio; member->decel *= ratio; member->speed = speed;
    }
  }
  if (e->max_health > 0 || (e->targetname[0] && !door_of(m, e)->activated)) { free(team); return; }
  qa_vec3 mn = {INFINITY, INFINITY, INFINITY}, mx = {-INFINITY, -INFINITY, -INFINITY};
  for (size_t i = 0; i < team_count; i++) {
    qa_body_state b = qa_q2_game_body(g, team[i]);
    qa_vec3 low = qa_q2_add(b.origin, b.bounds.min), high = qa_q2_add(b.origin, b.bounds.max);
    mn.x = fmin(mn.x, low.x); mn.y = fmin(mn.y, low.y); mn.z = fmin(mn.z, low.z);
    mx.x = fmax(mx.x, high.x); mx.y = fmax(mx.y, high.y); mx.z = fmax(mx.z, high.z);
  }
  free(team);
  qa_q2_entity *trigger = g->create(g->ctx, "door_trigger", NULL, 0);
  trigger->has_owner = true; trigger->owner = e->actor.id; trigger->visible = false;
  qa_q2_body_changes ch = {0};
  ch.has_bounds = true;
  ch.bounds = (qa_bounds){{mn.x - 60, mn.y - 60, mn.z}, {mx.x + 60, mx.y + 60, mx.z}};
  g->move(g->ctx, trigger, ch, false);
  trigger->touch = (qa_q2_touch){m, door_trigger_touch_fn};
  g->solid(g->ctx, trigger, "trigger");
  if (((int64_t)e->spawnflags & 1) != 0) portals(e, g, true);
}

static void smart_water_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  door_state *st = door_of(m, e);
  qa_body_state body = qa_q2_game_body(g, e);
  double top = body.origin.z + body.bounds.max.z + 1;
  if (strcmp(st->phase, "top") == 0) {
    if (e->wait >= 0) g->schedule(g->ctx, e, e->wait, TCB(m, smart_water_fn));
    return;
  }
  if (e->max_health != 0 && top >= e->max_health) {
    qa_q2_body_changes ch = {0}; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
    g->move(g->ctx, e, ch, false);
    g->motion(g->ctx, e, e->motion);
    g->cancel(g->ctx, e);
    st->phase = "top";
    return;
  }
  door_sound(m, e, g, true);
  const qa_actor_id *lowest = NULL;
  double height = 999999;
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  qa_actor_id lowest_id = {0};
  for (size_t i = 0; i < pn; i++) {
    qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, players[i]);
    if ((cs.has_value ? cs.value.health : 0) <= 0) continue;
    qa_body_state_result pb = qa_shared_body_table_read(g->host->bodies, players[i]);
    if (pb.has_value && pb.value.origin.z + pb.value.bounds.min.z - 1 < height) {
      lowest_id = players[i];
      lowest = &lowest_id;
      height = pb.value.origin.z + pb.value.bounds.min.z - 1;
    }
  }
  if (!lowest) return;
  double distance = height - top;
  double speed = fmin(e->speed, fmax(5, distance < st->water_divisor ? 5 : distance / st->water_divisor));
  qa_q2_body_changes ch = {0}; ch.has_velocity = true; ch.velocity = (qa_vec3){0, 0, speed};
  g->move(g->ctx, e, ch, false);
  g->motion(g->ctx, e, e->motion);
  if (strcmp(st->phase, "up") != 0) {
    qa_q2_game_use_targets_entity(g, e, lowest, false);
    portals(e, g, true);
    st->phase = "up";
  }
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), TCB(m, smart_water_fn));
}

void qa_q2_mover_module_resume_train_at(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_q2_entity *corner) {
  train_state *st = train_of(m, e);
  st->destination = corner;
  e->spawnflags = (double)((int64_t)e->spawnflags | 1);
  qa_q2_linear_motion_move_to(m->linear, e, g, train_destination(e, corner, g), TCB(m, train_wait_fn));
}
qa_vec3 qa_q2_mover_module_train_direction(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_entity *corner = train_of(m, e)->destination;
  if (!corner) return QA_Q2_ZERO;
  qa_vec3 delta = qa_q2_subtract(train_destination(e, corner, g), qa_q2_game_body(g, e).origin);
  double d = qa_q2_length(delta);
  return d == 0 ? QA_Q2_ZERO : qa_q2_scale(delta, 1 / d);
}

static void train_wait_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  qa_q2_entity *target = train_of(m, e)->destination;
  if (!target) return;
  const char *path_target = qa_q2_spawn_get(&target->spawn, "pathtarget");
  if (path_target && path_target[0]) {
    const char *prev = target->target;
    target->target = path_target;
    qa_actor_id act = e->activator;
    qa_q2_game_use_targets_entity(g, target, e->has_activator ? &act : NULL, false);
    target->target = prev;
    if (!live(g, e->actor.id)) return;
  }
  if (target->wait == 0) { train_next_fn(m, e, g); return; }
  if (target->wait > 0) g->schedule(g->ctx, e, target->wait, TCB(m, train_next_fn));
  else if (((int64_t)e->spawnflags & 2) != 0) {
    if (g->options.edition == QA_Q2_EDITION_RERELEASE) train_of(m, e)->destination = NULL;
    else train_next_fn(m, e, g);
    e->spawnflags = (double)((int64_t)e->spawnflags & ~1);
    qa_q2_body_changes ch = {0}; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
    g->move(g->ctx, e, ch, false);
    g->motion(g->ctx, e, e->motion);
    g->cancel(g->ctx, e);
  }
  const char *noise = qa_q2_spawn_get(&e->spawn, "noise");
  loop_sound(e, g, noise ? noise : "", false);
}

static void train_next_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  bool teleported = false;
  for (;;) {
    if (e->target[0] == '\0') {
      const char *noise = qa_q2_spawn_get(&e->spawn, "noise");
      loop_sound(e, g, noise ? noise : "", false);
      return;
    }
    /* copy target name: pickTarget result may share storage */
    char name[512];
    snprintf(name, sizeof name, "%s", e->target);
    qa_q2_entity *target = g->pick_target(g->ctx, name);
    if (!target) {
      if (!m->buf) m->buf = malloc(1024);
      if (m->buf) snprintf(m->buf, 1024, "Q2 train target missing: %s", name);
      g->host->diagnostic(g->host->ctx, m->buf ? m->buf : "Q2 train target missing");
      return;
    }
    e->target = target->target;
    if (((int64_t)target->spawnflags & 1) != 0) {
      if (teleported) { g->host->diagnostic(g->host->ctx, "Q2 train has consecutive teleport corners"); return; }
      teleported = true;
      qa_q2_body_changes ch = {0}; ch.has_origin = true; ch.origin = train_destination(e, target, g);
      g->move(g->ctx, e, ch, true);
      qa_q2_presentation_event ev = {0};
      ev.kind = "effect"; ev.effect = "q2:other-teleport"; ev.origin = qa_q2_game_body(g, e).origin; ev.direction = QA_Q2_ZERO; ev.count = 1;
      g->host->emit(g->host->ctx, ev);
      continue;
    }
    train_of(m, e)->destination = target;
    if (g->options.edition == QA_Q2_EDITION_RERELEASE && target->speed != 0) {
      e->speed = target->speed;
      e->accel = target->accel != 0 ? target->accel : target->speed;
      e->decel = target->decel != 0 ? target->decel : target->speed;
    }
    const char *noise = qa_q2_spawn_get(&e->spawn, "noise");
    loop_sound(e, g, noise ? noise : "", true);
    e->spawnflags = (double)((int64_t)e->spawnflags | 1);
    qa_vec3 dest = train_destination(e, target, g);
    qa_vec3 delta = qa_q2_subtract(dest, qa_q2_game_body(g, e).origin);
    qa_q2_linear_motion_move_to(m->linear, e, g, dest, TCB(m, train_wait_fn));
    if (g->options.edition == QA_Q2_EDITION_RERELEASE && ((int64_t)e->spawnflags & 8) != 0) {
      size_t tn = 0;
      const qa_owned_actor *team = g->push_team(g->ctx, e->actor.id, &tn);
      qa_owned_actor *copy = NULL;
      if (tn) { copy = malloc(tn * sizeof *copy); if (!copy) qa_range_error("out of memory"); memcpy(copy, team, tn * sizeof *copy); }
      for (size_t i = 0; i < tn; i++) {
        qa_q2_entity *member = g->entity(g->ctx, &copy[i].id);
        if (!member || member == e) continue;
        member->speed = e->speed; member->accel = e->accel; member->decel = e->decel;
        g->motion(g->ctx, member, "push");
        qa_q2_linear_motion_move_to(m->linear, member, g, qa_q2_add(qa_q2_game_body(g, member).origin, delta), TCB(m, train_piece_wait_fn));
      }
      free(copy);
    }
    return;
  }
}

void qa_q2_mover_module_spawn_train(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  bool ship = strcmp(e->classname, "func_train") != 0;
  if (m->train_count == m->train_cap) { m->train_cap = m->train_cap ? m->train_cap * 2 : 8; m->trains = realloc(m->trains, m->train_cap * sizeof *m->trains); if (!m->trains) qa_range_error("out of memory"); }
  m->trains[m->train_count++] = (train_state){e, NULL, 0, ship};
  if (ship) {
    e->model = strcmp(e->classname, "misc_strogg_ship") == 0 ? "models/ships/strogg1/tris.md2" : "models/ships/viper/tris.md2";
    e->visible = false;
    qa_q2_body_changes ch = {0}; ch.has_bounds = true; ch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 32}};
    g->move(g->ctx, e, ch, false);
  }
  g->solid(g->ctx, e, ship ? "none" : "brush");
  g->motion(g->ctx, e, "push");
  qa_q2_body_changes za = {0}; za.has_angles = true; za.angles = QA_Q2_ZERO;
  g->move(g->ctx, e, za, true);
  if (e->speed == 0) e->speed = ship ? 300 : 100;
  e->accel = e->decel = e->speed;
  e->damage = ((int64_t)e->spawnflags & 4) != 0 ? 0 : (e->damage != 0 ? e->damage : 100);
  e->use = (qa_q2_use){m, train_use_fn};
  e->blocked = (qa_q2_blocked){m, train_blocked_fn};
  g->show(g->ctx, e);
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), TCB(m, train_find_fn));
}

static void spawn_rotating(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  int64_t sf = (int64_t)e->spawnflags;
  e->movedir = (sf & 4) ? (qa_vec3){0, 0, 1} : (sf & 8) ? (qa_vec3){1, 0, 0} : (qa_vec3){0, 1, 0};
  if (sf & 2) e->movedir = qa_q2_scale(e->movedir, -1);
  if (e->speed == 0) e->speed = 100;
  if (e->damage == 0) e->damage = 2;
  g->solid(g->ctx, e, "brush");
  g->motion(g->ctx, e, (sf & 32) ? "stop" : "push");
  e->blocked = (qa_q2_blocked){m, rotating_damage_fn};
  e->use = (qa_q2_use){m, rotating_use_fn};
  if (sf & 1) rotating_use_fn(m, e, g, NULL, NULL);
  if (sf & 64) e->effects = (double)((int64_t)e->effects | 0x1000);
  if (sf & 128) e->effects = (double)((int64_t)e->effects | 0x2000);
  g->show(g->ctx, e);
}
static void spawn_path(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  if (e->targetname[0] == '\0' && strcmp(e->classname, "path_corner") == 0) { g->remove(g->ctx, e); return; }
  qa_q2_body_changes ch = {0}; ch.has_bounds = true; ch.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
  g->move(g->ctx, e, ch, false);
  e->visible = false;
  e->touch = (qa_q2_touch){m, path_touch_fn};
  g->solid(g->ctx, e, "trigger");
}

bool qa_q2_mover_module_spawn(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g) {
  const char *cn = e->classname;
  if (strcmp(cn, "func_door") == 0 || strcmp(cn, "func_door_rotating") == 0 || strcmp(cn, "func_water") == 0 || strcmp(cn, "func_button") == 0) { spawn_door(m, e, g); return true; }
  if (strcmp(cn, "func_train") == 0 || strcmp(cn, "misc_strogg_ship") == 0 || strcmp(cn, "misc_viper") == 0) { qa_q2_mover_module_spawn_train(m, e, g); return true; }
  if (strcmp(cn, "func_rotating") == 0) { spawn_rotating(m, e, g); return true; }
  if (strcmp(cn, "func_bobbing") == 0) { spawn_bobbing(m, e, g); return true; }
  if (strcmp(cn, "path_corner") == 0 || strcmp(cn, "point_combat") == 0) { spawn_path(m, e, g); return true; }
  return false;
}
static bool spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_mover_module_spawn(c, e, g); }

static void door_use_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o;
  use_door(c, e, g, a);
}
static void door_killed_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  qa_q2_mover_module *m = c;
  door_state *st = door_of(m, e);
  qa_q2_entity *master = st->master;
  size_t tn = st->team_count;
  for (size_t i = 0; i < tn; i++) {
    qa_q2_entity *member = st->team[i];
    if (member->max_health <= 0) continue;
    qa_gameplay_authority_set_health(g->host->combat, member->actor, member->max_health);
    qa_combat_trait_changes tc = {0}; tc.has_can_take_damage = true; tc.can_take_damage = false;
    qa_gameplay_authority_set_traits(g->host->combat, member->actor, tc);
  }
  use_door(m, master, g, r.pain.attacker);
}
static void button_touch_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, contact.other);
  if (g->host->is_player(g->host->ctx, contact.other) && (cs.has_value ? cs.value.health : 0) > 0) use_door(c, e, g, &contact.other);
}
static void door_touch_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_mover_module *m = c;
  door_state *st = door_of(m, e);
  if (!g->host->is_player(g->host->ctx, contact.other) || g->host->now(g->host->ctx) < st->debounce) return;
  st->debounce = g->host->now(g->host->ctx) + 5;
  qa_q2_presentation_event ev = {0};
  ev.kind = "centerprint"; ev.has_actor = true; ev.actor = contact.other; ev.text = e->message;
  g->host->emit(g->host->ctx, ev);
  g->sound(g->ctx, e->actor, "misc/talk1.wav", 0, 1, 1);
}
static void door_blocked_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id other) {
  qa_q2_mover_module *m = c;
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, other);
  if (!body.has_value) return;
  qa_actor_id self = e->actor.id;
  if (!g->host->is_player(g->host->ctx, other) && !g->host->is_monster(g->host->ctx, other)) {
    if (qa_gameplay_authority_read(g->host->combat, other).has_value)
      g->damage(g->ctx, other, e, e->actor.id, &self, 100000, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
    qa_q2_entity *victim = g->entity(g->ctx, &other);
    if (victim) g->remove(g->ctx, victim);
    return;
  }
  g->damage(g->ctx, other, e, e->actor.id, &self, e->damage, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
  if (((int64_t)e->spawnflags & 4) != 0 || e->wait < 0) return;
  door_state *st = door_of(m, e);
  bool reverse = strcmp(st->phase, "down") == 0;
  for (size_t i = 0; i < st->team_count; i++) {
    qa_q2_entity *member = st->team[i];
    if (reverse) {
      qa_actor_id act = member->activator;
      up(m, member, g, member->has_activator ? &act : NULL);
    } else down_fn(m, member, g);
  }
}
static void smart_water_blocked_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id other) {
  (void)c;
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, other);
  if (!body.has_value) return;
  bool living = g->host->is_player(g->host->ctx, other) || g->host->is_monster(g->host->ctx, other);
  qa_actor_id self = e->actor.id;
  if (qa_gameplay_authority_read(g->host->combat, other).has_value)
    g->damage(g->ctx, other, e, e->actor.id, &self, living ? 100 : 100000, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 19, 0, NULL);
  qa_q2_entity *victim = g->entity(g->ctx, &other);
  if (!living && victim && live(g, other) && strcmp(victim->solid, "none") != 0) g->remove(g->ctx, victim);
}
static void door_activate_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  qa_q2_mover_module *m = c;
  e->use = (qa_q2_use){0};
  e->die = e->max_health > 0 ? (qa_q2_die){m, door_killed_fn} : (qa_q2_die){0};
  door_of(m, e)->activated = true;
  if (e->max_health > 0) {
    qa_combat_trait_changes tc = {0}; tc.has_can_take_damage = true; tc.can_take_damage = true;
    qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
  }
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), TCB(m, prepare_door_fn));
}
static void door_trigger_touch_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_mover_module *m = c;
  qa_q2_entity *entity = e->has_owner ? g->entity(g->ctx, &e->owner) : NULL;
  if (!entity) return;
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, contact.other);
  if ((cs.has_value ? cs.value.health : 0) <= 0 || g->host->now(g->host->ctx) < e->timestamp) return;
  bool monster = g->host->is_monster(g->host->ctx, contact.other);
  if ((!monster && !g->host->is_player(g->host->ctx, contact.other)) || (monster && ((int64_t)entity->spawnflags & 8) != 0)) return;
  e->timestamp = g->host->now(g->host->ctx) + 1;
  use_door(m, entity, g, &contact.other);
}
static void train_use_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o;
  qa_q2_mover_module *m = c;
  e->has_activator = a != NULL;
  if (a) e->activator = *a;
  if (train_of(m, e)->ship && !e->visible) { e->visible = true; g->show(g->ctx, e); }
  if (((int64_t)e->spawnflags & 1) != 0) {
    if (((int64_t)e->spawnflags & 2) == 0) return;
    e->spawnflags = (double)((int64_t)e->spawnflags & ~1);
    qa_q2_body_changes ch = {0}; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
    g->move(g->ctx, e, ch, false);
    g->motion(g->ctx, e, e->motion);
    g->cancel(g->ctx, e);
    return;
  }
  qa_q2_entity *dest = train_of(m, e)->destination;
  if (!dest) { train_next_fn(m, e, g); return; }
  e->spawnflags = (double)((int64_t)e->spawnflags | 1);
  qa_q2_linear_motion_move_to(m->linear, e, g, train_destination(e, dest, g), TCB(m, train_wait_fn));
}
static void train_blocked_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id other) {
  qa_q2_mover_module *m = c;
  train_state *st = train_of(m, e);
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, other);
  if (!body.has_value || e->damage == 0 || g->host->now(g->host->ctx) < st->debounce) return;
  st->debounce = g->host->now(g->host->ctx) + 0.5;
  qa_actor_id self = e->actor.id;
  if (qa_gameplay_authority_read(g->host->combat, other).has_value)
    g->damage(g->ctx, other, e, e->actor.id, &self, e->damage, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
}
static void train_find_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_mover_module *m = c;
  char name[512];
  snprintf(name, sizeof name, "%s", e->target);
  qa_q2_entity *target = g->pick_target(g->ctx, name);
  if (!target) {
    if (!m->buf) m->buf = malloc(1024);
    if (m->buf) snprintf(m->buf, 1024, "Q2 train first target missing: %s", name);
    g->host->diagnostic(g->host->ctx, m->buf ? m->buf : "Q2 train first target missing");
    return;
  }
  e->target = target->target;
  qa_q2_body_changes ch = {0}; ch.has_origin = true; ch.origin = train_destination(e, target, g);
  g->move(g->ctx, e, ch, true);
  if (e->targetname[0] == '\0') e->spawnflags = (double)((int64_t)e->spawnflags | 1);
  if (((int64_t)e->spawnflags & 1) != 0) {
    e->has_activator = true; e->activator = e->actor.id;
    g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), TCB(m, train_next_fn));
  }
}
static void rotating_damage_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id other) {
  (void)c;
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, other);
  qa_actor_id self = e->actor.id;
  if (body.has_value && qa_gameplay_authority_read(g->host->combat, other).has_value)
    g->damage(g->ctx, other, e, e->actor.id, &self, e->damage, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
}
static void rotating_use_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  qa_q2_mover_module *m = c;
  bool stop = qa_q2_length(e->angular_velocity) != 0;
  e->angular_velocity = stop ? QA_Q2_ZERO : qa_q2_scale(e->movedir, e->speed);
  e->touch = (!stop && ((int64_t)e->spawnflags & 16) != 0) ? (qa_q2_touch){m, rotating_touch_fn} : (qa_q2_touch){0};
  g->motion(g->ctx, e, e->motion);
}
static void path_touch_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_mover_module *m = c;
  if (strcmp(e->classname, "path_corner") == 0) m->hooks.path_corner(m->hooks.ctx, e, g, contact.other);
  else m->hooks.combat_point(m->hooks.ctx, e, g, contact.other);
}

qa_q2_mover_module *qa_q2_mover_module_create(qa_q2_mover_hooks hooks) {
  qa_q2_mover_module *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->hooks = hooks;
  m->linear = qa_q2_linear_motion_create("q2:foundation/linear");
  m->angular = qa_q2_angular_motion_create("q2:foundation/angular");
  if (!m->linear || !m->angular) { qa_q2_mover_module_destroy(m); return NULL; }
  qa_q2_callback_definitions lin = qa_q2_linear_motion_callbacks(m->linear);
  qa_q2_callback_definitions ang = qa_q2_angular_motion_callbacks(m->angular);
  size_t k = 0;
  for (size_t i = 0; i < lin.think_count; i++) m->thinks[k++] = lin.think[i];
  for (size_t i = 0; i < ang.think_count; i++) m->thinks[k++] = ang.think[i];
  m->thinks[k++] = (qa_q2_named_think){"door_hit_bottom", {m, bottom_fn}};
  m->thinks[k++] = (qa_q2_named_think){"door_go_down", {m, down_fn}};
  m->thinks[k++] = (qa_q2_named_think){"door_hit_top", {m, top_fn}};
  m->thinks[k++] = (qa_q2_named_think){"Think_SpawnDoorTrigger", {m, prepare_door_fn}};
  m->thinks[k++] = (qa_q2_named_think){"smart_water_go_up", {m, smart_water_fn}};
  m->thinks[k++] = (qa_q2_named_think){"train_wait", {m, train_wait_fn}};
  m->thinks[k++] = (qa_q2_named_think){"train_next", {m, train_next_fn}};
  m->thinks[k++] = (qa_q2_named_think){"train_piece_wait", {m, train_piece_wait_fn}};
  m->thinks[k++] = (qa_q2_named_think){"func_train_find", {m, train_find_fn}};
  m->thinks[k++] = (qa_q2_named_think){"bob_hit_top", {m, bob_top_fn}};
  m->thinks[k++] = (qa_q2_named_think){"bob_hit_bottom", {m, bob_bottom_fn}};
  m->thinks[k++] = (qa_q2_named_think){"bob_go_up", {m, bob_up_fn}};
  m->thinks[k++] = (qa_q2_named_think){"bob_go_down", {m, bob_down_fn}};
  m->think_count = k;
  m->uses[0] = (qa_q2_named_use){"door_use", {m, door_use_fn}};
  m->uses[1] = (qa_q2_named_use){"Door_Activate", {m, door_activate_fn}};
  m->uses[2] = (qa_q2_named_use){"train_use", {m, train_use_fn}};
  m->uses[3] = (qa_q2_named_use){"rotating_use", {m, rotating_use_fn}};
  m->touches[0] = (qa_q2_named_touch){"button_touch", {m, button_touch_fn}};
  m->touches[1] = (qa_q2_named_touch){"door_touch", {m, door_touch_fn}};
  m->touches[2] = (qa_q2_named_touch){"Touch_DoorTrigger", {m, door_trigger_touch_fn}};
  m->touches[3] = (qa_q2_named_touch){"rotating_touch", {m, rotating_touch_fn}};
  m->touches[4] = (qa_q2_named_touch){"q2_path_touch", {m, path_touch_fn}};
  m->dies[0] = (qa_q2_named_die){"door_killed", {m, door_killed_fn}};
  m->blockeds[0] = (qa_q2_named_blocked){"door_blocked", {m, door_blocked_fn}};
  m->blockeds[1] = (qa_q2_named_blocked){"smart_water_blocked", {m, smart_water_blocked_fn}};
  m->blockeds[2] = (qa_q2_named_blocked){"train_blocked", {m, train_blocked_fn}};
  m->blockeds[3] = (qa_q2_named_blocked){"rotating_blocked", {m, rotating_damage_fn}};
  m->defs = (qa_q2_callback_definitions){.think = m->thinks, .think_count = k, .use = m->uses, .use_count = 4,
      .touch = m->touches, .touch_count = 5, .die = m->dies, .die_count = 1, .blocked = m->blockeds, .blocked_count = 4};
  return m;
}
qa_q2_mover_module *qa_q2_create_mover_module(qa_q2_mover_hooks hooks) { return qa_q2_mover_module_create(hooks); }
void qa_q2_mover_module_destroy(qa_q2_mover_module *m) {
  if (!m) return;
  for (size_t i = 0; i < m->door_count; i++) free(m->doors[i].team);
  if (m->cp_teams) { for (size_t i = 0; i < m->door_count; i++) free(m->cp_teams[i]); }
  free(m->doors); free(m->trains); free(m->cp_doors); free(m->cp_trains); free(m->cp_teams);
  free(m->route_scratch); free(m->buf);
  qa_q2_linear_motion_destroy(m->linear);
  qa_q2_angular_motion_destroy(m->angular);
  free(m);
}
qa_q2_spawn_module qa_q2_mover_module_as_spawn(qa_q2_mover_module *m) {
  return (qa_q2_spawn_module){.ctx = m, .callbacks = &m->defs, .spawn = spawn_cb};
}
qa_q2_linear_motion *qa_q2_mover_module_linear(qa_q2_mover_module *m) { return m->linear; }
qa_q2_angular_motion *qa_q2_mover_module_angular(qa_q2_mover_module *m) { return m->angular; }
qa_q2_callback_definitions qa_q2_mover_module_callbacks(qa_q2_mover_module *m) { return m->defs; }

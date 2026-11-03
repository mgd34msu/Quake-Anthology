/* Port of src/content/q2/foundation/monsters/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/index.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "monsters_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/infantry.h"
#include "qa/content/q2/foundation/monsters/moves.h"
#include "qa/content/q2/foundation/monsters/soldier.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

static bool grow(void **arr, size_t *cap, size_t count, size_t size) {
  if (count < *cap) return true;
  size_t next = *cap ? *cap * 2 : 8;
  void *p = realloc(*arr, next * size);
  if (!p) return false;
  *arr = p; *cap = next; return true;
}

/* ---- shared callbacks ---- */
static void sh_done_dodge(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void sh_duck_down(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_duck_time = q2m_now(c->game) + 5;
  qa_q2m_set_duck(c, true);
}
static void sh_duck_hold(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2m_now(c->game) < c->state->duck_wait;
}
static void sh_duck_up(void *ctx, qa_q2m_context *c) {
  double left;
  double right;
  (void)ctx;
  if (!c->state->ducked) return;
  qa_q2m_set_duck(c, false);
  if (c->state->next_duck_time > q2m_now(c->game))
    c->state->next_duck_time = (left = q2m_now(c->game), right = (c->state->next_duck_time - q2m_now(c->game)) * 0.5, left + right);
}
static void sh_footstep(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_body(c->game, c->entity).ground != NULL) {
    qa_q2_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = "entity-event"; e.has_actor = true; e.actor = c->entity->actor.id; e.event = 8;
    c->game->host->emit(c->game->host->ctx, e);
  }
}
static const qa_q2m_named_handler shared_callbacks[] = {
  {"monster_done_dodge", {NULL, sh_done_dodge}},
  {"monster_duck_down", {NULL, sh_duck_down}},
  {"monster_duck_hold", {NULL, sh_duck_hold}},
  {"monster_duck_up", {NULL, sh_duck_up}},
  {"monster_footstep", {NULL, sh_footstep}},
};
static const size_t shared_callback_count = sizeof shared_callbacks / sizeof shared_callbacks[0];
static const qa_q2m_handler *shared_callback(const char *name) {
  for (size_t i = 0; i < shared_callback_count; i++)
    if (strcmp(name, shared_callbacks[i].name) == 0) return &shared_callbacks[i].handler;
  return NULL;
}
static qa_q2m_named_handler *builtin_callbacks;
static size_t builtin_callback_count;
static const qa_q2m_named_handler *builtins(void) {
  if (builtin_callbacks) return builtin_callbacks;
  builtin_callback_count = qa_q2m_soldier_callback_count + qa_q2m_infantry_callback_count + shared_callback_count;
  builtin_callbacks = malloc(builtin_callback_count * sizeof *builtin_callbacks);
  if (!builtin_callbacks) return NULL;
  size_t at = 0;
  memcpy(builtin_callbacks + at, qa_q2m_soldier_callbacks, qa_q2m_soldier_callback_count * sizeof *builtin_callbacks);
  at += qa_q2m_soldier_callback_count;
  memcpy(builtin_callbacks + at, qa_q2m_infantry_callbacks, qa_q2m_infantry_callback_count * sizeof *builtin_callbacks);
  at += qa_q2m_infantry_callback_count;
  memcpy(builtin_callbacks + at, shared_callbacks, shared_callback_count * sizeof *builtin_callbacks);
  return builtin_callbacks;
}
static const qa_bounds normal_bounds = {{-16, -16, -24}, {16, 16, 32}};

static void infantry_idle(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rr(c->game) && c->entity->has_enemy) return;
  c->set_move(c->impl_ctx, c, "infantry_move_fidget", false, false);
  q2m_sound(c->game, c->entity, "infantry/infidle1.wav", true, 2, true, 1, true, 2);
}
static void soldier_initialize(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!q2m_rr(c->game) || (qa_js_to_uint32(c->entity->spawnflags) & 8u) != 0) {
    qa_q2m_soldier_stand_handler(NULL, c);
    return;
  }
  (void)q2m_rnd(c->game);
  c->set_move(c->impl_ctx, c, "soldier_move_stand1", false, false);
}
static qa_q2m_definition *alloc_builtin(qa_q2m_monsters *m) {
  if (m->builtin_count >= m->builtin_cap) {
    if (m->builtin_cap == 0) {
      m->builtin_storage = malloc(32 * sizeof *m->builtin_storage);
      if (!m->builtin_storage) return NULL;
      m->builtin_cap = 32;
    } else return NULL;
  }
  qa_q2m_definition *d = &m->builtin_storage[m->builtin_count++];
  memset(d, 0, sizeof *d);
  return d;
}
/* TS q2SoldierVariants: the soldier family of the base game and, in the rerelease, the three Xatrix soldiers:
 * one model, one move table, skins 6 to 11. */
static const qa_q2m_soldier_variant SOLDIER_VARIANTS[] = {
  {"monster_soldier_light", 20, QA_Q2M_WEAPON_BLASTER, 0, 0, false},
  {"monster_soldier", 30, QA_Q2M_WEAPON_SHOTGUN, 2, 2, false},
  {"monster_soldier_ss", 40, QA_Q2M_WEAPON_MACHINEGUN, 4, 4, false},
  {"monster_soldier_ripper", 50, QA_Q2M_WEAPON_BLASTER, 6, 0, true},
  {"monster_soldier_hypergun", 60, QA_Q2M_WEAPON_SHOTGUN, 8, 2, true},
  {"monster_soldier_lasergun", 70, QA_Q2M_WEAPON_MACHINEGUN, 10, 4, true},
};
const qa_q2m_soldier_variant *qa_q2m_soldier_variant_of(const char *classname) {
  for (size_t i = 0; i < sizeof SOLDIER_VARIANTS / sizeof SOLDIER_VARIANTS[0]; i++)
    if (strcmp(SOLDIER_VARIANTS[i].classname, classname) == 0) return &SOLDIER_VARIANTS[i];
  return NULL;
}
static const qa_q2m_definition *built_in(qa_q2m_monsters *m, const char *classname, qa_q2_game_services *game) {
  bool rr = q2m_rr(game);
  for (size_t i = 0; i < m->builtin_count; i++) {
    const qa_q2m_definition *d = &m->builtin_storage[i];
    bool edition_match = (d->moves == (rr ? qa_q2m_rerelease_infantry_moves : qa_q2m_classic_infantry_moves)
                       || d->moves == (rr ? qa_q2m_rerelease_soldier_moves : qa_q2m_classic_soldier_moves));
    if (strcmp(d->classname, classname) == 0 && edition_match) return d;
  }
  (void)builtins();
  if (strcmp(classname, "monster_infantry") == 0) {
    qa_q2m_definition *d = alloc_builtin(m);
    if (!d) return NULL;
    d->classname = classname; d->kind = "infantry"; d->model = "models/monsters/infantry/tris.md2";
    d->health = 100; d->gib_health = rr ? -65 : -40; d->mass = 200; d->bounds = normal_bounds; d->scale = 1;
    d->initial_move = "infantry_move_stand";
    d->moves = rr ? qa_q2m_rerelease_infantry_moves : qa_q2m_classic_infantry_moves;
    d->move_count = rr ? qa_q2m_rerelease_infantry_move_count : qa_q2m_classic_infantry_move_count;
    d->callbacks = builtin_callbacks; d->callback_count = builtin_callback_count;
    d->stand = (qa_q2m_handler){NULL, qa_q2m_infantry_stand_handler};
    d->walk = (qa_q2m_handler){NULL, qa_q2m_infantry_walk_handler};
    d->run = (qa_q2m_handler){NULL, qa_q2m_infantry_run_handler};
    d->attack = (qa_q2m_handler){NULL, qa_q2m_infantry_attack_handler};
    d->sight = (qa_q2m_handler){NULL, qa_q2m_infantry_sight_handler};
    d->idle = (qa_q2m_handler){NULL, infantry_idle};
    d->reactions.pain = qa_q2m_infantry_pain_handler;
    d->reactions.die = qa_q2m_infantry_die_handler;
    d->duck = qa_q2m_infantry_duck_fn;
    d->sidestep = qa_q2m_infantry_sidestep;
    return d;
  }
  const qa_q2m_soldier_variant *variant = qa_q2m_soldier_variant_of(classname);
  if (variant != NULL && (!variant->rerelease_only || rr)) {
    qa_q2m_definition *d = alloc_builtin(m);
    if (!d) return NULL;
    d->classname = classname; d->kind = "soldier"; d->model = "models/monsters/soldier/tris.md2";
    d->health = variant->health;
    d->has_soldier = true; d->soldier_weapon = variant->weapon; d->soldier_skin = variant->skin; d->soldier_count = variant->count;
    d->gib_health = -30; d->mass = 100; d->bounds = normal_bounds; d->scale = 1;
    d->initial_move = "soldier_move_stand1";
    d->moves = rr ? qa_q2m_rerelease_soldier_moves : qa_q2m_classic_soldier_moves;
    d->move_count = rr ? qa_q2m_rerelease_soldier_move_count : qa_q2m_classic_soldier_move_count;
    d->callbacks = builtin_callbacks; d->callback_count = builtin_callback_count;
    d->stand = (qa_q2m_handler){NULL, qa_q2m_soldier_stand_handler};
    d->walk = (qa_q2m_handler){NULL, qa_q2m_soldier_walk_handler};
    d->run = (qa_q2m_handler){NULL, qa_q2m_soldier_run_handler};
    d->attack = (qa_q2m_handler){NULL, qa_q2m_soldier_attack_handler};
    d->sight = (qa_q2m_handler){NULL, qa_q2m_soldier_sight_handler};
    d->reactions.pain = qa_q2m_soldier_pain_handler;
    d->reactions.die = qa_q2m_soldier_die_handler;
    d->duck = qa_q2m_soldier_duck_fn;
    d->initialize = (qa_q2m_handler){NULL, soldier_initialize};
    d->sidestep = qa_q2m_soldier_sidestep;
    d->has_blind_fire_flag = true;
    d->blind_fire = variant->weapon != QA_Q2M_WEAPON_MACHINEGUN;
    return d;
  }
  return NULL;
}

void qa_q2m_place_triggered_monster(qa_q2_game_services *game, qa_owned_actor actor) {
  qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, actor.id);
  if (!body.has_value) qa_raise("Error", 0, 0, "Triggered monster has no shared body");
  qa_vec3 origin = qa_add3_double(body.value.origin, q2m_v3(0, 0, 1));
  qa_body_state next = body.value;
  next.origin = origin;
  qa_shared_body_table_write(game->host->bodies, actor, next);
  for (int i = 0; i < 1024; i++) {
    qa_trace_result trace = q2m_trace(game, origin, origin, &body.value.bounds, &actor.id, qa_q2m_monster_solid_mask(game));
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR) break;
    qa_actor_id target = trace.hit.actor;
    if (!qa_gameplay_authority_read(game->host->combat, target).has_value) break;
    (void)game->damage(game->ctx, target, NULL, actor.id, &actor.id, 100000, 0, q2m_zero(), origin, q2m_zero(), 21, 8, NULL);
    qa_trace_result remaining = q2m_trace(game, origin, origin, &body.value.bounds, &actor.id, qa_q2m_monster_solid_mask(game));
    if (remaining.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(remaining.hit.actor, target)) break;
  }
}

/* ---- Q2Monsters ---- */
static qa_q2m_actor_entry *entry_of(qa_q2m_monsters *m, qa_actor_id actor) {
  for (size_t i = 0; i < m->entry_count; i++)
    if (qa_same_actor(m->entries[i]->actor, actor)) return m->entries[i];
  return NULL;
}
static qa_q2m_context *lookup_context(void *ctx, qa_actor_id actor) {
  qa_q2m_actor_entry *e = entry_of(ctx, actor);
  return e ? &e->context : NULL;
}
static const qa_monster_mission *lookup_mission(void *ctx, qa_actor_id actor) {
  qa_q2m_monsters *m = ctx;
  return m->hooks.mission ? m->hooks.mission(m->hooks.ctx, actor) : NULL;
}
static void remove_entry(qa_q2m_monsters *m, qa_actor_id actor) {
  for (size_t i = 0; i < m->entry_count; i++)
    if (qa_same_actor(m->entries[i]->actor, actor)) {
      free(m->entries[i]->state);
      free(m->entries[i]);
      m->entries[i] = m->entries[--m->entry_count];
      return;
    }
}
static qa_q2m_pending_damage *pending_of(qa_q2m_monsters *m, qa_actor_id actor) {
  for (size_t i = 0; i < m->pending_count; i++)
    if (qa_same_actor(m->pending[i]->actor, actor)) return m->pending[i];
  return NULL;
}
static void pending_delete(qa_q2m_monsters *m, qa_actor_id actor) {
  for (size_t i = 0; i < m->pending_count; i++)
    if (qa_same_actor(m->pending[i]->actor, actor)) {
      free(m->pending[i]);
      m->pending[i] = m->pending[--m->pending_count];
      return;
    }
}
static void on_actor_release(void *ctx, qa_owned_actor actor) {
  qa_q2m_monsters *m = ctx;
  remove_entry(m, actor.id);
  pending_delete(m, actor.id);
  qa_q2m_perception_release(&m->perception, actor.id);
}
bool qa_q2m_monsters_init(qa_q2m_monsters *m, qa_q2m_weapons weapons, qa_q2m_monster_hooks hooks) {
  memset(m, 0, sizeof *m);
  m->weapons = weapons;
  m->hooks = hooks;
  qa_q2m_context_lookup lookup;
  lookup.ctx = m; lookup.get = lookup_context; lookup.mission = lookup_mission;
  return qa_q2m_perception_init(&m->perception, lookup);
}
void qa_q2m_monsters_destroy(qa_q2m_monsters *m) {
  /* C ownership: stop the registry calling into freed monsters. */
  if (m->connected) qa_session_actor_registry_off_release(m->release_registry, m->release_subscription);
  for (size_t i = 0; i < m->entry_count; i++) { free(m->entries[i]->state); free(m->entries[i]); }
  free(m->entries);
  for (size_t i = 0; i < m->pending_count; i++) free(m->pending[i]);
  free(m->pending);
  free(m->definitions);
  free(m->edition_definitions[0]); free(m->edition_definitions[1]);
  free(m->builtin_storage);
  free(m->merged_think); free(m->merged_use); free(m->merged_touch);
  free(m->merged_pain); free(m->merged_die); free(m->merged_blocked);
  free(m->power_armor_times);
  qa_q2m_perception_destroy(&m->perception);
  memset(m, 0, sizeof *m);
}
void qa_q2m_monsters_set_source_combat_rules(qa_q2m_monsters *m, qa_q2m_combat_rules rules,
                                             const qa_q2m_source_combat_hooks *hooks) {
  m->source_combat_rules = rules;
  m->source_combat_hooks = hooks;
  qa_q2m_perception_set_source_combat_rules(&m->perception, rules, hooks);
}
void qa_q2m_monsters_set_hint_paths(qa_q2m_monsters *m, const qa_q2m_hint_hooks *hooks) {
  m->hint_hooks = hooks;
  qa_q2m_perception_set_hint_paths(&m->perception, hooks);
}
void qa_q2m_monsters_set_route(qa_q2m_monsters *m, qa_actor_id actor, const qa_actor_id *goal, double pause_until) {
  qa_q2m_context *c = qa_q2m_monsters_context(m, actor);
  if (!c) qa_raise("Error", 0, 0, "Monster route has no source continuation");
  c->entity->has_goal = goal != NULL;
  if (goal) c->entity->goal = *goal;
  c->state->has_move_target = goal != NULL;
  if (goal) c->state->move_target = *goal;
  c->state->pause_time = pause_until;
  if (goal == NULL || pause_until > q2m_now(c->game)) c->stand(c->impl_ctx, c);
  else c->walk(c->impl_ctx, c);
  if (goal) {
    qa_body_state_result target = qa_shared_body_table_read(c->game->host->bodies, *goal);
    if (target.has_value)
      c->state->ideal_yaw = qa_vector_to_angles_with_rule(
          qa_sub3_double(target.value.origin, q2m_body(c->game, c->entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  }
}
qa_q2m_context *qa_q2m_monsters_context(qa_q2m_monsters *m, qa_actor_id actor) {
  qa_q2m_actor_entry *e = entry_of(m, actor);
  return e ? &e->context : NULL;
}
/* TS powerArmorTime.get(actor) ?? 0. */
static double power_armor_time(const qa_q2m_monsters *m, qa_actor_id actor) {
  for (size_t i = 0; i < m->power_armor_time_count; i++)
    if (qa_same_actor(m->power_armor_times[i].actor, actor)) return m->power_armor_times[i].until;
  return 0;
}
void qa_q2m_monsters_note_power_armor(qa_q2m_monsters *m, qa_actor_id actor, double now) {
  if (entry_of(m, actor) == NULL) return;
  for (size_t i = 0; i < m->power_armor_time_count; i++)
    if (qa_same_actor(m->power_armor_times[i].actor, actor)) { m->power_armor_times[i].until = now + 0.2; return; }
  if (!grow((void **)&m->power_armor_times, &m->power_armor_time_cap, m->power_armor_time_count, sizeof *m->power_armor_times)) abort();
  m->power_armor_times[m->power_armor_time_count++] = (qa_q2m_power_armor_time){actor, now + 0.2};
}
bool qa_q2m_monsters_current_sight_client(qa_q2m_monsters *m, qa_actor_id *out) {
  return qa_q2m_perception_current_sight_client(&m->perception, out);
}
void qa_q2m_monsters_found_target(qa_q2m_monsters *m, qa_q2m_context *context) {
  qa_q2m_perception_found_target(&m->perception, context);
}
void qa_q2m_monsters_hunt_target(qa_q2m_monsters *m, qa_q2m_context *context) {
  qa_q2m_perception_hunt_target(&m->perception, context);
}
void qa_q2m_monsters_begin_frame(qa_q2m_monsters *m, qa_q2_game_services *game) {
  qa_q2m_perception_begin_frame(&m->perception, game);
}
void qa_q2m_monsters_report_noise(qa_q2m_monsters *m, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  qa_q2m_perception_report_noise(&m->perception, actor, origin, secondary);
}
static qa_q2m_context *require_context(qa_q2m_monsters *m, qa_q2_entity *entity) {
  qa_q2m_context *c = qa_q2m_monsters_context(m, entity->actor.id);
  if (!c) qa_raise("Error", 0, 0, "Missing Q2 source monster context %s", entity->classname);
  return c;
}

/* context method implementations (impl_ctx = qa_q2m_monsters*) */
static qa_q2m_actor_entry *entry_of_context(qa_q2m_context *c) { return (qa_q2m_actor_entry *)c; }
static qa_q2m_combat_rules mth_rules(void *impl, qa_q2m_context *c) {
  (void)c;
  return ((qa_q2m_monsters *)impl)->source_combat_rules;
}
static qa_q2m_before_move_result mth_before_move(void *impl, qa_q2m_context *c, qa_vec3 displacement) {
  qa_q2m_monsters *m = impl;
  if (m->source_combat_hooks && m->source_combat_hooks->before_move)
    return m->source_combat_hooks->before_move(m->source_combat_hooks->ctx, c, displacement);
  qa_q2m_before_move_result r = {false, displacement};
  return r;
}
static bool mth_accepts(void *impl, qa_q2m_context *c, qa_vec3 origin) {
  qa_q2m_monsters *m = impl;
  return m->source_combat_hooks && m->source_combat_hooks->accepts_ground_move
    ? m->source_combat_hooks->accepts_ground_move(m->source_combat_hooks->ctx, c, origin) : true;
}
static bool mth_consume_blocked(void *impl, qa_q2m_context *c) {
  qa_q2m_monsters *m = impl;
  return m->source_combat_hooks && m->source_combat_hooks->consume_blocked
    ? m->source_combat_hooks->consume_blocked(m->source_combat_hooks->ctx, c) : false;
}
static bool mth_hint_run(void *impl, qa_q2m_context *c, double distance) {
  qa_q2m_monsters *m = impl;
  return m->hint_hooks && m->hint_hooks->run ? m->hint_hooks->run(m->hint_hooks->ctx, c, distance) : false;
}
static bool mth_hint_lost(void *impl, qa_q2m_context *c) {
  qa_q2m_monsters *m = impl;
  return m->hint_hooks && m->hint_hooks->check_lost ? m->hint_hooks->check_lost(m->hint_hooks->ctx, c) : false;
}
static void mth_schedule(void *impl, qa_q2m_context *c, double delay, const char *callback) {
  (void)impl;
  bool has_think = false; qa_q2_think think = qa_q2_think_resolve(c->game->source_callbacks, callback, &has_think);
  if (!has_think) qa_raise("Error", 0, 0, "Missing monster thinker %s", callback);
  c->game->schedule(c->game->ctx, c->entity, delay, think);
}
static void mth_set_move(void *impl, qa_q2m_context *c, const char *name, bool has_immediate, bool immediate) {
  (void)impl;
  if (!has_immediate) immediate = true;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  const qa_q2m_move *move = NULL;
  for (size_t i = 0; i < d->move_count; i++)
    if (strcmp(d->moves[i].name, name) == 0) { move = &d->moves[i]; break; }
  if (!move) qa_raise("Error", 0, 0, "%s: unknown source animation %s", c->entity->classname, name);
  if (q2m_rr(c->game) && !immediate) c->state->next_move = move;
  else { c->state->move = move; c->state->next_move = NULL; }
}
static void mth_stand(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  d->stand.fn(d->stand.ctx, c);
}
static void mth_walk(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  d->walk.fn(d->walk.ctx, c);
}
static void mth_run(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  d->run.fn(d->run.ctx, c);
}
static void mth_attack(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  d->attack.fn(d->attack.ctx, c);
}
static void mth_melee(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (d->melee.fn) d->melee.fn(d->melee.ctx, c);
}
static void mth_idle(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (d->idle.fn) d->idle.fn(d->idle.ctx, c);
}
static void mth_search(void *impl, qa_q2m_context *c) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (d->search.fn) d->search.fn(d->search.ctx, c);
}
static void mth_dispatch(void *impl, qa_q2m_context *c, const char *callback) {
  (void)impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (strcmp(callback, "$sight") == 0) {
    if (d->sight.fn) d->sight.fn(d->sight.ctx, c);
    return;
  }
  for (size_t i = 0; i < d->callback_count; i++)
    if (strcmp(d->callbacks[i].name, callback) == 0) {
      d->callbacks[i].handler.fn(d->callbacks[i].handler.ctx, c);
      return;
    }
  const qa_q2m_handler *shared = shared_callback(callback);
  if (!shared) qa_raise("Error", 0, 0, "%s: unknown source callback %s", c->entity->classname, callback);
  shared->fn(shared->ctx, c);
}
static bool mth_find_target(void *impl, qa_q2m_context *c) {
  return qa_q2m_perception_find_target(&((qa_q2m_monsters *)impl)->perception, c);
}
static bool default_check(void *ctx, qa_q2m_context *c) { (void)ctx; return qa_q2m_default_check_attack(c, NULL); }
static bool mth_check_attack(void *impl, qa_q2m_context *c, double distance) {
  (void)distance;
  qa_q2m_monsters *m = impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  qa_q2m_check_attack_fn check;
  if (d->check_attack) { check.ctx = d->check_attack_ctx; check.fn = d->check_attack; }
  else { check.ctx = NULL; check.fn = default_check; }
  return qa_q2m_perception_check_attack(&m->perception, c, check);
}
static bool mth_move_to_goal(void *impl, qa_q2m_context *c, double distance) {
  return qa_q2m_perception_move_to_goal(&((qa_q2m_monsters *)impl)->perception, c, distance);
}
static void mth_dodge(void *impl, qa_q2m_context *c, qa_actor_id attacker, double eta,
                      const qa_trace_result *trace, bool has_gravity, bool gravity) {
  (void)has_gravity;
  qa_q2m_monsters_dodge(impl, c->entity, c->game, attacker, eta, trace, gravity);
}
static bool monsters_blocked(qa_q2m_monsters *m, qa_q2m_context *c, double distance);
static bool mth_blocked(void *impl, qa_q2m_context *c, double distance) {
  qa_q2m_monsters *m = impl;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  /* Only the infantry and soldier families set monsterinfo.blocked; every other species leaves it null and is never blocked-checked. */
  if (d->blocked) return d->blocked(d->blocked_ctx, c, distance);
  return strcmp(c->state->kind, "infantry") == 0 || strcmp(c->state->kind, "soldier") == 0 ? monsters_blocked(m, c, distance) : false;
}
static const char *mth_platform_state(void *impl, qa_q2m_context *c, qa_actor_id actor) {
  (void)c;
  qa_q2m_monsters *m = impl;
  return m->hooks.platform_state ? m->hooks.platform_state(m->hooks.ctx, actor) : NULL;
}

static qa_q2m_context *attach_context(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game,
                                      const qa_q2m_definition *definition, qa_q2m_state *state) {
  qa_q2m_actor_entry *entry = malloc(sizeof *entry);
  if (!entry) return NULL;
  memset(entry, 0, sizeof *entry);
  entry->state = state;
  entry->definition = definition;
  entry->actor = entity->actor.id;
  qa_q2m_context *c = &entry->context;
  c->entity = entity; c->game = game; c->state = state; c->weapons = m->weapons;
  c->impl_ctx = m;
  c->source_combat_rules = mth_rules;
  c->before_source_move = mth_before_move;
  c->accepts_source_ground_move = mth_accepts;
  c->consume_source_blocked = mth_consume_blocked;
  c->run_hint_path = mth_hint_run;
  c->check_lost_hint_path = mth_hint_lost;
  c->schedule = mth_schedule;
  c->set_move = mth_set_move;
  c->stand = mth_stand; c->walk = mth_walk; c->run = mth_run; c->attack = mth_attack;
  c->dispatch = mth_dispatch;
  c->find_target = mth_find_target;
  c->check_attack = mth_check_attack;
  c->move_to_goal = mth_move_to_goal;
  c->dodge = mth_dodge;
  c->melee = mth_melee;
  c->blocked = mth_blocked;
  c->platform_state = mth_platform_state;
  c->idle = mth_idle; c->search = mth_search;
  /* Replace any existing entry for this actor (respawn). */
  remove_entry(m, entity->actor.id);
  if (!grow((void **)&m->entries, &m->entry_cap, m->entry_count, sizeof *m->entries)) { free(entry); return NULL; }
  m->entries[m->entry_count++] = entry;
  return c;
}

static void monsters_think(qa_q2m_context *c);
static void monsters_start(qa_q2m_monsters *m, qa_q2m_context *c);
static void monsters_trigger_spawn(qa_q2m_monsters *m, qa_q2m_context *c);
static void monsters_die(qa_q2m_monsters *m, qa_q2m_context *c, const qa_q2m_definition *d, qa_death_reaction reaction);
static void queue_pain(qa_q2m_monsters *m, qa_q2m_context *c, qa_pain_reaction reaction,
                       const qa_actor_id *inflictor, qa_vec3 point);
static void set_skin(qa_q2m_context *c);

static void src_think(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  monsters_think(require_context(ctx, e));
}
static void src_start(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  monsters_start(ctx, require_context(ctx, e));
}
static void src_trigger_spawn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  monsters_trigger_spawn(ctx, require_context(ctx, e));
}
static void src_dead_think(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  qa_q2m_monster_dead_think(require_context(ctx, e));
}
static void src_fly_check(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  qa_q2m_fly_check(require_context(ctx, e), true);
}
static void src_flies_on(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  qa_q2m_flies_on(require_context(ctx, e));
}
static void src_flies_off(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)g;
  qa_q2m_flies_off(require_context(ctx, e));
}
static void src_pain(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_pain_reaction reaction) {
  qa_q2m_monsters *m = ctx;
  qa_q2m_context *c = require_context(m, e);
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  qa_q2m_perception_react_to_damage(&m->perception, c, reaction.attacker);
  if (q2m_rr(g)) {
    const qa_actor_id *inflictor = reaction.attack ? reaction.attack->inflictor : NULL;
    queue_pain(m, c, reaction, inflictor, q2m_body(g, e).origin);
  } else if (!c->state->ducked) {
    if (d->reactions.pain) d->reactions.pain(d->reactions.ctx, c, reaction);
    if (g->options.skill == 3) c->state->pain_time = q2m_now(g) + 5;
  }
  set_skin(c);
  g->show(g->ctx, e);
}
static void src_die(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction reaction) {
  qa_q2m_monsters *m = ctx;
  qa_q2m_context *c = require_context(m, e);
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (q2m_rr(g)) {
    if (qa_q2m_health(g, &e->actor.id) < -999) qa_gameplay_authority_set_health(g->host->combat, e->actor, -999);
    e->has_enemy = reaction.pain.attacker != NULL;
    if (reaction.pain.attacker) e->enemy = *reaction.pain.attacker;
    e->flags = (double)(qa_js_to_int32(qa_js_to_uint32(e->flags) | (1u << 19)));
    queue_pain(m, c, reaction.pain, reaction.inflictor, reaction.point);
    return;
  }
  monsters_die(m, c, d, reaction);
}
static void src_use(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q2m_monsters *m = ctx;
  const qa_monster_mission *mission = m->hooks.mission ? m->hooks.mission(m->hooks.ctx, e->actor.id) : NULL;
  if (mission && mission->use(mission->ctx, activator)) return;
  qa_q2m_context *c = require_context(m, e);
  qa_q2_monster_target_result observed = activator ? g->monster_target(g->ctx, activator) : (qa_q2_monster_target_result){false, {0, false, false, false, 0, false, 0, false, 0}};
  qa_q2m_context *activator_context = activator ? qa_q2m_monsters_context(m, *activator) : NULL;
  if (e->has_enemy || c->state->dead || !activator || (observed.has_value && observed.value.notarget)
      || (!g->host->is_player(g->host->ctx, *activator) && !(activator_context && activator_context->state->good_guy)))
    return;
  e->enemy = *activator; e->has_enemy = true;
  qa_q2m_perception_found_target(&m->perception, c);
}
static qa_q2_use use_cb(void *ctx, void (*fn)(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *)) {
  qa_q2_use u = {ctx, fn};
  return u;
}
static void src_trigger_use(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q2m_monsters *m = ctx;
  g->schedule(g->ctx, e, !q2m_rr(g) ? 0.1 : g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, src_trigger_spawn});
  if (activator && g->host->is_player(g->host->ctx, *activator)) { e->enemy = *activator; e->has_enemy = true; }
  e->use = use_cb(m, src_use);
}

/* Callers (and the mission-pack and rerelease modules, which return these
 * arrays inside their own definitions) keep the previous result, so a merge
 * that produces the same entries keeps the existing array; only a changed
 * set replaces it. (TS returns a fresh object per getter call; GC keeps the
 * older ones alive.) The named entries are pointer-only structs. */
#define MERGE_KIND(field, type, member) \
  static bool merge_##field(qa_q2m_monsters *m, const qa_q2_callback_definitions *sources, size_t source_count) { \
    type *built = NULL; size_t built_count = 0, cap = 0; \
    for (size_t s = 0; s < source_count; s++) { \
      for (size_t i = 0; i < sources[s].member##_count; i++) { \
        const type *item = &sources[s].member[i]; \
        bool exists = false; \
        for (size_t j = 0; j < built_count; j++) { \
          if (strcmp(built[j].name, item->name) == 0) { \
            exists = true; \
            if (built[j].cb.fn != item->cb.fn || built[j].cb.ctx != item->cb.ctx) \
              qa_raise("Error", 0, 0, "Duplicate Q2 monster source callback %s", item->name); \
          } \
        } \
        if (exists) continue; \
        if (!grow((void **)&built, &cap, built_count, sizeof(type))) { free(built); return false; } \
        built[built_count++] = *item; \
      } \
    } \
    if (m->merged_##field != NULL && m->merged_##field##_count == built_count && \
        (built_count == 0 || memcmp(m->merged_##field, built, built_count * sizeof(type)) == 0)) { \
      free(built); \
      return true; \
    } \
    free(m->merged_##field); \
    m->merged_##field = built; m->merged_##field##_count = built_count; \
    return true; \
  }
MERGE_KIND(think, qa_q2_named_think, think)
MERGE_KIND(use, qa_q2_named_use, use)
MERGE_KIND(touch, qa_q2_named_touch, touch)
MERGE_KIND(pain, qa_q2_named_pain, pain)
MERGE_KIND(die, qa_q2_named_die, die)
MERGE_KIND(blocked, qa_q2_named_blocked, blocked)

qa_q2_callback_definitions qa_q2m_monsters_callbacks(qa_q2m_monsters *m) {
  qa_q2_named_think think_base[10];
  think_base[0] = qa_q2m_gib_callbacks().think[0];
  think_base[1] = qa_q2m_gib_callbacks().think[1];
  think_base[2] = (qa_q2_named_think){"monster_think", {m, src_think}};
  think_base[3] = (qa_q2_named_think){"monster_start_go", {m, src_start}};
  think_base[4] = (qa_q2_named_think){"monster_triggered_spawn", {m, src_trigger_spawn}};
  think_base[5] = (qa_q2_named_think){"monster_dead_think", {m, src_dead_think}};
  think_base[6] = (qa_q2_named_think){"M_FlyCheck", {m, src_fly_check}};
  think_base[7] = (qa_q2_named_think){"M_FliesOn", {m, src_flies_on}};
  think_base[8] = (qa_q2_named_think){"M_FliesOff", {m, src_flies_off}};
  size_t think_count = 9;
  qa_q2_named_use use_base[2] = {
    {"monster_use", {m, src_use}}, {"monster_triggered_spawn_use", {m, src_trigger_use}},
  };
  qa_q2_named_pain pain_base[1] = {{"monster_pain", {m, src_pain}}};
  qa_q2_named_die die_base[2];
  die_base[0] = qa_q2m_gib_callbacks().die[0];
  die_base[1] = (qa_q2_named_die){"monster_die", {m, src_die}};
  qa_q2_callback_definitions base;
  memset(&base, 0, sizeof base);
  base.think = think_base; base.think_count = think_count;
  base.use = use_base; base.use_count = 2;
  base.pain = pain_base; base.pain_count = 1;
  base.die = die_base; base.die_count = 2;
  base.touch = qa_q2m_gib_callbacks().touch; base.touch_count = qa_q2m_gib_callbacks().touch_count;
  size_t total_defs = m->definition_count + m->edition_counts[0] + m->edition_counts[1];
  size_t source_count = 1;
  qa_q2_callback_definitions *sources = malloc((total_defs + 1) * sizeof *sources);
  qa_q2_callback_definitions out;
  memset(&out, 0, sizeof out);
  if (!sources) return out;
  sources[0] = base;
  for (size_t i = 0; i < m->definition_count; i++)
    if (m->definitions[i]->has_source_callbacks) sources[source_count++] = m->definitions[i]->source_callbacks;
  for (int e = 0; e < 2; e++)
    for (size_t i = 0; i < m->edition_counts[e]; i++)
      if (m->edition_definitions[e][i]->has_source_callbacks) sources[source_count++] = m->edition_definitions[e][i]->source_callbacks;
  (void)merge_think(m, sources, source_count);
  (void)merge_use(m, sources, source_count);
  (void)merge_touch(m, sources, source_count);
  (void)merge_pain(m, sources, source_count);
  (void)merge_die(m, sources, source_count);
  (void)merge_blocked(m, sources, source_count);
  free(sources);
  out.think = m->merged_think; out.think_count = m->merged_think_count;
  out.use = m->merged_use; out.use_count = m->merged_use_count;
  out.touch = m->merged_touch; out.touch_count = m->merged_touch_count;
  out.pain = m->merged_pain; out.pain_count = m->merged_pain_count;
  out.die = m->merged_die; out.die_count = m->merged_die_count;
  out.blocked = m->merged_blocked; out.blocked_count = m->merged_blocked_count;
  return out;
}
static void connect_monsters(qa_q2m_monsters *m, qa_q2_game_services *game) {
  if (!m->connected) {
    m->release_registry = game->host->actors;
    m->release_subscription = qa_session_actor_registry_on_release(game->host->actors, on_actor_release, m);
    m->connected = true;
  }
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2m_monsters_callbacks(m));
  qa_q2m_perception_bind(&m->perception, game);
}
const qa_q2m_definition *qa_q2m_monsters_definition(qa_q2m_monsters *m, const char *classname, qa_q2_game_services *game) {
  int edition = q2m_rr(game) ? 1 : 0;
  for (size_t i = 0; i < m->edition_counts[edition]; i++)
    if (strcmp(m->edition_definitions[edition][i]->classname, classname) == 0) return m->edition_definitions[edition][i];
  for (size_t i = 0; i < m->definition_count; i++)
    if (strcmp(m->definitions[i]->classname, classname) == 0) return m->definitions[i];
  if (strcmp(classname, "turret_driver") == 0) {
    const qa_q2m_definition *infantry = built_in(m, "monster_infantry", game);
    if (!infantry) return NULL;
    for (size_t i = 0; i < m->builtin_count; i++) {
      const qa_q2m_definition *d = &m->builtin_storage[i];
      if (strcmp(d->classname, "turret_driver") == 0 && d->moves == infantry->moves) return d;
    }
    qa_q2m_definition *d = alloc_builtin(m);
    if (!d) return NULL;
    /* Re-fetch infantry: alloc uses preallocated storage, stable. */
    const qa_q2m_definition *inf = NULL;
    for (size_t i = 0; i < m->builtin_count; i++)
      if (strcmp(m->builtin_storage[i].classname, "monster_infantry") == 0 && m->builtin_storage[i].moves == infantry->moves)
        inf = &m->builtin_storage[i];
    if (!inf) return NULL;
    *d = *inf;
    d->classname = "turret_driver";
    d->gib_health = 0;
    d->has_view_height = true; d->view_height = 24;
    return d;
  }
  return built_in(m, classname, game);
}
void qa_q2m_monsters_register(qa_q2m_monsters *m, const qa_q2m_definition *definition, int edition) {
  const qa_q2m_definition ***list = edition < 0 ? &m->definitions : &m->edition_definitions[edition];
  size_t *count = edition < 0 ? &m->definition_count : &m->edition_counts[edition];
  size_t *cap = edition < 0 ? &m->definition_cap : &m->edition_caps[edition];
  for (size_t i = 0; i < *count; i++)
    if (strcmp((*list)[i]->classname, definition->classname) == 0)
      qa_raise("Error", 0, 0, "Duplicate Q2 monster definition %s", definition->classname);
  for (size_t mi = 0; mi < definition->move_count; mi++) {
    const qa_q2m_move *move = &definition->moves[mi];
    for (size_t fi = 0; fi < move->frame_count; fi++) {
      if (move->frames[fi].ai == QA_Q2M_AI_SOURCE) {
        bool found = false;
        for (size_t ai = 0; ai < definition->ai_count; ai++)
          if (strcmp(definition->ai[ai].name, move->frames[fi].ai_source_name) == 0) found = true;
        if (!found) qa_raise("Error", 0, 0, "Missing Q2 source AI %s", move->frames[fi].ai_source_name);
      }
    }
    if ((double)move->frame_count < move->last_frame - move->first_frame + 1)
      qa_raise("Error", 0, 0, "Q2 move %s has an incomplete frame table", move->name);
    const char *names[64];
    size_t name_count = 0;
    if (move->end && name_count < 64) names[name_count++] = move->end;
    for (size_t fi = 0; fi < move->frame_count; fi++)
      for (size_t ai2 = 0; ai2 < move->frames[fi].action_count; ai2++)
        if (!move->frames[fi].actions[ai2].is_nextframe && name_count < 64)
          names[name_count++] = move->frames[fi].actions[ai2].name;
    for (size_t ni = 0; ni < name_count; ni++) {
      bool found = shared_callback(names[ni]) != NULL;
      for (size_t ci = 0; ci < definition->callback_count && !found; ci++)
        if (strcmp(definition->callbacks[ci].name, names[ni]) == 0) found = true;
      if (!found) qa_raise("Error", 0, 0, "Q2 move %s references missing callback %s", move->name, names[ni]);
    }
  }
  if (!grow((void **)list, cap, *count, sizeof **list)) return;
  (*list)[(*count)++] = definition;
}

static void queue_pain(qa_q2m_monsters *m, qa_q2m_context *c, qa_pain_reaction reaction,
                       const qa_actor_id *inflictor, qa_vec3 point) {
  qa_actor_id actor = c->entity->actor.id;
  qa_q2m_pending_damage *previous = pending_of(m, actor);
  double damage = reaction.damage + (previous ? previous->reaction.pain.damage : 0);
  double kick = reaction.kick + (previous ? previous->reaction.pain.kick : 0);
  qa_q2m_pending_damage *entry = previous;
  if (!entry) {
    entry = malloc(sizeof *entry);
    if (!entry) return;
    memset(entry, 0, sizeof *entry);
    entry->actor = actor;
    if (!grow((void **)&m->pending, &m->pending_cap, m->pending_count, sizeof *m->pending)) { free(entry); return; }
    m->pending[m->pending_count++] = entry;
  }
  entry->has_attack = reaction.attack != NULL;
  if (reaction.attack) entry->attack = *reaction.attack;
  qa_death_reaction death;
  memset(&death, 0, sizeof death);
  death.pain.attack = entry->has_attack ? &entry->attack : NULL;
  death.pain.self = reaction.self;
  if (reaction.attacker) { entry->attacker_storage = *reaction.attacker; death.pain.attacker = &entry->attacker_storage; }
  death.pain.kick = kick;
  death.pain.damage = damage;
  if (inflictor) { entry->inflictor_storage = *inflictor; death.inflictor = &entry->inflictor_storage; }
  death.point = point;
  entry->reaction = death;
}
static void set_skin(qa_q2m_context *c) {
  if (c->state->gibbed || !q2m_rr(c->game)) return;
  double health = qa_q2m_health(c->game, &c->entity->actor.id);
  bool low = health < c->entity->max_health / 2;
  if (strcmp(c->state->kind, "soldier") == 0)
    c->entity->skin = (double)(qa_js_to_int32((qa_js_to_uint32(c->entity->skin) & ~1u) | (low ? 1u : 0u)));
  else if (strcmp(c->state->kind, "infantry") == 0) c->entity->skin = low ? 1 : 0;
}
static void monsters_die(qa_q2m_monsters *m, qa_q2m_context *c, const qa_q2m_definition *d, qa_death_reaction reaction) {
  qa_q2m_state *state = c->state;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (m->source_combat_hooks && m->source_combat_hooks->before_killed)
    m->source_combat_hooks->before_killed(m->source_combat_hooks->ctx, c);
  if (!state->dead) {
    if (state->has_commander) {
      qa_q2_entity *commander = game->entity(game->ctx, &state->commander);
      qa_q2m_context *commander_context = commander ? qa_q2m_monsters_context(m, commander->actor.id) : NULL;
      if (commander && commander_context) {
        qa_q2m_state *cs = commander_context->state;
        if (state->spawned_by == QA_Q2M_SPAWNED_CARRIER && strcmp(commander->classname, "monster_carrier") == 0)
          cs->monster_slots++;
        else if (state->spawned_by == QA_Q2M_SPAWNED_MEDIC && strcmp(commander->classname, "monster_medic_commander") == 0 && !q2m_rr(game))
          cs->monster_slots++;
        else if (state->spawned_by == QA_Q2M_SPAWNED_WIDOW
                   && strncmp(commander->classname, "monster_widow", 13) == 0 && cs->monster_used > 0)
          cs->monster_used--;
      }
    }
    const qa_monster_mission *mission = m->hooks.mission ? m->hooks.mission(m->hooks.ctx, entity->actor.id) : NULL;
    if (!mission && !state->good_guy && !state->do_not_count
        && (!q2m_rr(game) || (qa_js_to_uint32(entity->spawnflags) & 65536u) == 0)) {
      game->counters->killed_monsters++;
      if (game->options.mode == QA_Q2_MODE_COOP && reaction.pain.attacker != NULL &&
          game->host->is_player(game->host->ctx, *reaction.pain.attacker) && m->hooks.coop_kill != NULL)
        m->hooks.coop_kill(m->hooks.ctx, *reaction.pain.attacker);
    }
    entity->has_enemy = reaction.pain.attacker != NULL;
    if (reaction.pain.attacker) entity->enemy = *reaction.pain.attacker;
    if (reaction.pain.attacker != NULL) {
      qa_q2_entity *killer = game->entity(game->ctx, reaction.pain.attacker);
      if (killer != NULL && strcmp(killer->classname, "monster_medic") == 0) { entity->has_owner = true; entity->owner = *reaction.pain.attacker; }
    }
    if (!q2m_rr(game) && (strcmp(entity->motion, "push") == 0 || strcmp(entity->motion, "stop") == 0
                          || strcmp(entity->motion, "stationary") == 0)) {
      d->reactions.die(d->reactions.ctx, c, reaction);
      game->show(game->ctx, entity);
      return;
    }
    entity->touch = (qa_q2_touch){NULL, NULL};
    entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->flags) & ~3u));
    const char *item = qa_q2_spawn_get(&entity->spawn, "item");
    if (item && strlen(item) > 0 && !entity->item_dropped) {
      if (!m->hooks.drop_item) qa_raise("Error", 0, 0, "Monster authored item drop requires source item services");
      m->hooks.drop_item(m->hooks.ctx, entity->actor, game, item);
      entity->item_dropped = true;
    }
    if (!mission) {
      if (strlen(entity->death_target) > 0) entity->target = entity->death_target;
      if (strlen(entity->target) > 0) qa_q2_game_use_targets_entity(game, entity, reaction.pain.attacker, false);
    } else mission->killed(mission->ctx, reaction.pain.attacker);
  }
  d->reactions.die(d->reactions.ctx, c, reaction);
  /* g_monster.cpp refunds a rerelease medic child's slots only once it is gibbed, then forgets the commander,
   * so a revived child cannot refund twice. */
  if (q2m_rr(game) && state->gibbed && state->spawned_by == QA_Q2M_SPAWNED_MEDIC && state->has_commander) {
    qa_q2m_context *commander_context = qa_q2m_monsters_context(m, state->commander);
    if (commander_context != NULL) commander_context->state->monster_used -= state->monster_slots;
    state->has_commander = false;
  }
  game->show(game->ctx, entity);
}
void qa_q2m_monsters_process_pain(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2m_pending_damage *pending = pending_of(m, entity->actor.id);
  qa_q2m_context *c = qa_q2m_monsters_context(m, entity->actor.id);
  if (!pending || !c || pending->reaction.pain.damage == 0) return;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  entity->last_attack = pending->has_attack ? &pending->attack : NULL;
  qa_death_reaction reaction = pending->reaction;
  if (qa_q2m_health(game, &entity->actor.id) <= 0) {
    monsters_die(m, c, d, reaction);
    if (q2m_live(game, entity->actor.id) && qa_q2m_health(game, &entity->actor.id) > c->state->gib_health
        && entity->frame == c->state->move->last_frame) {
      entity->frame -= 1 + floor(q2m_rnd(game) * 2);
      qa_body_state body = q2m_body(game, entity);
      if (body.ground != NULL && strcmp(entity->motion, "toss") == 0 && c->state->locomotion != QA_Q2M_LOCO_STATIONARY) {
        qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
        changes.has_angles = true; changes.angles = body.angles;
        changes.angles.y = body.angles.y + (q2m_rnd(game) < 0.5 ? 4.5 : -4.5);
        q2m_move(game, entity, changes, false, false);
      }
    }
  } else if (d->reactions.pain) d->reactions.pain(d->reactions.ctx, c, reaction.pain);
  pending_delete(m, entity->actor.id);
  if (!q2m_live(game, entity->actor.id)) return;
  set_skin(c);
  if (strlen(entity->health_target) != 0) {
    const char *saved = entity->target;
    entity->target = entity->health_target;
    qa_q2_game_use_targets_entity(game, entity, entity->has_enemy ? &entity->enemy : NULL, false);
    entity->target = saved;
  }
  if (q2m_live(game, entity->actor.id)) game->show(game->ctx, entity);
}
void qa_q2m_monsters_end_frame(qa_q2m_monsters *m, qa_q2_game_services *game) {
  if (!q2m_rr(game) || m->pending_count == 0) return;
  /* Sort a snapshot by source slot (actors.source_of), as TS. */
  size_t count = m->entry_count;
  qa_q2m_context **ordered = malloc((count ? count : 1) * sizeof *ordered);
  if (!ordered) return;
  for (size_t i = 0; i < count; i++) ordered[i] = &m->entries[i]->context;
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++) {
      qa_actor_source_result sa = qa_session_actor_registry_source_of(game->host->actors, ordered[i]->entity->actor.id);
      qa_actor_source_result sb = qa_session_actor_registry_source_of(game->host->actors, ordered[j]->entity->actor.id);
      double ka = sa.has_value ? sa.slot : (double)ordered[i]->entity->actor.id.slot;
      double kb = sb.has_value ? sb.slot : (double)ordered[j]->entity->actor.id.slot;
      if (kb < ka) { qa_q2m_context *t = ordered[i]; ordered[i] = ordered[j]; ordered[j] = t; }
    }
  for (size_t i = 0; i < count; i++)
    if (q2m_live(game, ordered[i]->entity->actor.id) && (qa_js_to_uint32(ordered[i]->entity->server_flags) & 4u) != 0)
      qa_q2m_monsters_process_pain(m, ordered[i]->entity, game);
  free(ordered);
}

static void check_ground(qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state body = q2m_body(game, entity);
  if ((qa_js_to_uint32(entity->flags) & 3u) != 0) return;
  qa_q2_body_changes changes;
  memset(&changes, 0, sizeof changes);
  if (body.velocity.z * entity->gravity_vector.z < -100) {
    changes.has_ground = true; changes.ground_null = true;
    q2m_move(game, entity, changes, true, false);
    return;
  }
  qa_vec3 end = qa_add3_double(body.origin, q2m_v3(0, 0, 0.25 * entity->gravity_vector.z));
  qa_trace_result trace = q2m_trace(game, body.origin, end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
  bool steep = !trace.contact.has_plane
    || (entity->gravity_vector.z < 0 ? trace.contact.plane.normal.z < 0.7 : trace.contact.plane.normal.z > -0.7);
  if (!trace.start_solid && steep) {
    changes.has_ground = true; changes.ground_null = true;
    q2m_move(game, entity, changes, true, false);
    return;
  }
  if (!trace.start_solid && !trace.all_solid) {
    qa_actor_id ground;
    bool has_ground = qa_q2m_trace_ground_actor(trace, game, &ground);
    changes.has_origin = true; changes.origin = trace.end;
    changes.has_ground = true; changes.ground_null = !has_ground;
    if (has_ground) changes.ground = ground;
    changes.has_velocity = true; changes.velocity = body.velocity; changes.velocity.z = 0;
    q2m_move(game, entity, changes, true, false);
  }
}
static void categorize_position(qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2m_body(game, entity);
  qa_vec3 feet = body.origin;
  feet.z = body.origin.z + (entity->gravity_vector.z > 0 ? body.bounds.max.z - 1 : body.bounds.min.z + 1);
  double contents = game->host->point_contents(game->host->ctx, feet);
  qa_vec3 p26 = qa_add3_double(feet, q2m_v3(0, 0, 26));
  qa_vec3 p48 = qa_add3_double(feet, q2m_v3(0, 0, 48));
  state->water_level = (qa_js_to_uint32(contents) & qa_js_to_uint32(QA_Q2M_MASK_WATER)) == 0 ? 0
    : (qa_js_to_uint32(game->host->point_contents(game->host->ctx, p26)) & qa_js_to_uint32(QA_Q2M_MASK_WATER)) == 0 ? 1
    : (qa_js_to_uint32(game->host->point_contents(game->host->ctx, p48)) & qa_js_to_uint32(QA_Q2M_MASK_WATER)) == 0 ? 2 : 3;
  state->water_type = state->water_level == 0 ? 0 : contents;
}
static void world_damage(qa_q2_game_services *game, qa_actor_id target, qa_actor_id world, double amount,
                         qa_vec3 point, double mod, bool has_flags, double flags) {
  (void)game->damage(game->ctx, target, NULL, world, &world, amount, 0, q2m_zero(), point, q2m_zero(), mod, has_flags ? flags : 0, NULL);
}
static void world_effects(qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2m_body(game, entity);
  double water = state->water_level;
  uint32_t contents = qa_js_to_uint32(state->water_type);
  qa_actor_id world = game->host->world_actor(game->host->ctx);
  if (qa_q2m_health(game, &entity->actor.id) > 0) {
    if (state->locomotion == QA_Q2M_LOCO_SWIM ? water > 0 : water < 3)
      state->air_finished = q2m_now(game) + (state->locomotion == QA_Q2M_LOCO_SWIM ? 9 : 12);
    else if (state->air_finished < q2m_now(game) && state->pain_time < q2m_now(game)) {
      world_damage(game, entity->actor.id, world,
                   qa_js_min(15, 2 + 2 * floor(q2m_now(game) - state->air_finished)), body.origin, 17, true, 2);
      state->pain_time = q2m_now(game) + 1;
    }
  }
  if (water == 0) {
    if ((qa_js_to_uint32(entity->flags) & 8u) != 0) q2m_sound(game, entity, "player/watr_out.wav", true, 4, false, 0, false, 0);
    entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->flags) & ~8u));
    return;
  }
  if ((contents & 8u) != 0 && (qa_js_to_uint32(entity->flags) & 128u) == 0 && state->environmental_damage_time < q2m_now(game)) {
    state->environmental_damage_time = q2m_now(game) + 0.2;
    world_damage(game, entity->actor.id, world, 10 * water, body.origin, 19, false, 0);
  }
  if ((contents & 16u) != 0 && (qa_js_to_uint32(entity->flags) & 64u) == 0 && state->environmental_damage_time < q2m_now(game)) {
    state->environmental_damage_time = q2m_now(game) + 1;
    world_damage(game, entity->actor.id, world, 4 * water, body.origin, 18, false, 0);
  }
  if ((qa_js_to_uint32(entity->flags) & 8u) == 0) {
    if (!state->dead)
      q2m_sound(game, entity, (contents & 8u) != 0 ? (q2m_rnd(game) <= 0.5 ? "player/lava1.wav" : "player/lava2.wav")
                                                   : "player/watr_in.wav",
                true, 4, false, 0, false, 0);
    entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->flags) | 8u));
    state->environmental_damage_time = 0;
  }
}
static void move_frame(qa_q2m_monsters *m, qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  bool rr = q2m_rr(game);
  const qa_q2m_move *move = state->move;
  bool run_frame = !rr || state->next_move_time <= q2m_now(game);
  if (run_frame && state->next_move && state->next_move != state->move) {
    state->move = state->next_move; state->next_move = NULL; move = state->move;
  }
  if (!run_frame) run_frame = entity->frame < move->first_frame || entity->frame > move->last_frame;
  if (run_frame) {
    bool explicit = false;
    if (state->next_frame != 0 && state->next_frame >= move->first_frame && state->next_frame <= move->last_frame) {
      entity->frame = state->next_frame; state->next_frame = 0;
    } else {
      if (entity->frame == move->last_frame && move->end) {
        c->dispatch(c->impl_ctx, c, move->end);
        if (rr && state->next_move) {
          state->move = state->next_move; state->next_move = NULL;
          if (state->next_frame != 0) { entity->frame = state->next_frame; state->next_frame = 0; explicit = true; }
        }
        move = state->move;
        if (state->corpse || (qa_js_to_uint32(entity->server_flags) & 2u) != 0 || !q2m_live(game, entity->actor.id)) return;
      }
      if (entity->frame < move->first_frame || entity->frame > move->last_frame) {
        state->hold_frame = false; entity->frame = move->first_frame;
      } else if (!explicit && !state->hold_frame) {
        entity->frame++;
        if (entity->frame > move->last_frame) entity->frame = move->first_frame;
      }
    }
    state->next_move_time = qa_js_round((q2m_now(game) + 0.1) * 1000) / 1000;
    if (rr && state->next_frame != 0 && (state->next_frame < move->first_frame || state->next_frame > move->last_frame))
      state->next_frame = 0;
  }
  const qa_q2m_frame *frame = &move->frames[qa_q2m_record_at(move->frame_count, entity->frame - move->first_frame)];
  double distance = state->hold_frame ? 0 : frame->distance * state->scale * (rr ? game->host->frame_seconds(game->host->ctx) * 10 : 1);
  if (frame->ai != QA_Q2M_AI_SOURCE) qa_q2m_run_ai(c, frame->ai, distance);
  else {
    const qa_q2m_definition *d = entry_of_context(c)->definition;
    const qa_q2m_named_ai *handler = NULL;
    for (size_t i = 0; i < d->ai_count; i++)
      if (strcmp(d->ai[i].name, frame->ai_source_name) == 0) handler = &d->ai[i];
    if (!handler) qa_raise("Error", 0, 0, "Missing Q2 source AI %s", frame->ai_source_name);
    handler->fn(handler->ctx, c, distance);
  }
  (void)m;
  if (!q2m_live(game, entity->actor.id)) return;
  if (run_frame) {
    for (size_t i = 0; i < frame->action_count; i++) {
      const qa_q2m_action *action = &frame->actions[i];
      if (!action->is_nextframe) c->dispatch(c->impl_ctx, c, action->name);
      else state->next_frame = action->frame_is_next ? entity->frame + 1 : action->frame;
      if (!q2m_live(game, entity->actor.id)) return;
    }
  }
  if (rr && frame->lerp_frame != -1) {
    entity->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->render_flags) | (1u << 22)));
    entity->old_frame = frame->lerp_frame;
  }
}
static void check_dodge(qa_q2m_monsters *m, qa_q2m_context *c);
static void monsters_think(qa_q2m_context *c) {
  qa_q2m_monsters *m = c->impl_ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (q2m_rr(game)) {
    entity->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->render_flags) & ~((1u << 22) | (1u << 26))));
    entity->old_frame = -1;
    qa_q2_think prior = entity->think;
    qa_q2m_monsters_process_pain(m, entity, game);
    if (!q2m_live(game, entity->actor.id) || !qa_q2_think_eq(entity->think, prior)) return;
    check_dodge(m, c);
  }
  game->schedule(game->ctx, entity, !q2m_rr(game) ? 0.1 : game->host->frame_seconds(game->host->ctx),
                 (qa_q2_think){m, src_think});
  const qa_monster_mission *mission = m->hooks.mission ? m->hooks.mission(m->hooks.ctx, entity->actor.id) : NULL;
  if (mission && entity->has_enemy) {
    qa_monster_combat_route route = mission->combat_route(mission->ctx);
    if (route.goal) {
      state->combat_point = true;
      state->has_move_target = true; state->move_target = *route.goal;
      entity->goal = *route.goal; entity->has_goal = true;
    } else if (state->combat_point) {
      state->combat_point = false; state->has_move_target = false;
      entity->goal = entity->enemy; entity->has_goal = true;
    }
    if (route.stand_ground) state->stand_ground = true;
  }
  move_frame(m, c);
  if (!q2m_live(game, entity->actor.id) || state->gibbed) return;
  qa_linked_body_result linked = qa_shared_body_table_linked(game->host->bodies, entity->actor.id);
  double link_count = linked.has_value ? linked.value.link_count : 0;
  if (link_count != state->last_link_count) { state->last_link_count = link_count; check_ground(c); }
  categorize_position(c);
  world_effects(c);
  if (!q2m_live(game, entity->actor.id)) return;
  entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) & ~256u));
  entity->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->render_flags) & ~(1024u | 2048u | 4096u)));
  if (state->resurrecting) {
    entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) | 256u));
    entity->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->render_flags) | 1024u));
  }
  entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) & ~512u));
  if (power_armor_time(m, entity->actor.id) > q2m_now(game)) {
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
    if (combat.has_value && strcmp(combat.value.armor.powered.kind, "screen") == 0)
      entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) | 512u));
    else if (combat.has_value && strcmp(combat.value.armor.powered.kind, "shield") == 0) {
      entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) | 256u));
      entity->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->render_flags) | 2048u));
    }
  }
  game->show(game->ctx, entity);
}

void qa_q2m_monsters_dodge(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id attacker,
                            double eta, const qa_trace_result *trace, bool gravity) {
  qa_q2m_context *c = qa_q2m_monsters_context(m, entity->actor.id);
  if (!c) return;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (!d) return;
  if (d->dodge) { d->dodge(d->dodge_ctx, c, attacker, eta, trace, gravity); return; }
  qa_q2m_state *state = c->state;
  const bool classic = game->options.edition == QA_Q2_EDITION_CLASSIC;
  const bool infantry = strcmp(state->kind, "infantry") == 0;
  if (classic && !infantry && strcmp(state->kind, "soldier") != 0) return;
  const double r = q2m_rnd(game);
  if (classic) {
    if (r > 0.25) return;
    if (!entity->has_enemy) { entity->enemy = attacker; entity->has_enemy = true; }
    if (infantry) { c->set_move(c->impl_ctx, c, "infantry_move_duck", false, false); return; }
    if (game->options.skill == 0) { c->set_move(c->impl_ctx, c, "soldier_move_duck", false, false); return; }
    state->pause_time = q2m_now(game) + eta + 0.3;
    c->set_move(c->impl_ctx, c, q2m_rnd(game) > (game->options.skill == 1 ? 0.33 : 0.66) ? "soldier_move_duck" : "soldier_move_attack3",
                false, false);
    return;
  }
  if (qa_q2m_health(game, &entity->actor.id) < 1) return;
  const bool ducker = d->duck != NULL && !gravity, dodger = d->sidestep != NULL && !state->stand_ground;
  if (!ducker && !dodger) return;
  if (!entity->has_enemy) {
    entity->enemy = attacker; entity->has_enemy = true;
    qa_q2m_perception_found_target(&m->perception, c);
  }
  if (eta < game->host->frame_seconds(game->host->ctx) || eta > 2.5 || r > 0.5) return;
  const qa_body_state body = q2m_body(game, entity);
  const double height = body.origin.z + body.bounds.max.z - (ducker && trace != NULL ? 32 : 0);
  if (ducker && trace != NULL && !dodger && (trace->end.z <= height || state->ducked)) return;
  if (dodger) {
    if (state->dodging) return;
    if (!ducker || trace == NULL || trace->end.z <= height || state->ducked) {
      if (game->options.skill < 2 && q2m_rnd(game) >= (game->options.skill == 0 ? 0.25 : 0.5)) {
        state->dodge_time = q2m_now(game) + 0.8 + q2m_rnd(game) * 0.6;
        return;
      }
      state->lefty = trace == NULL
        ? q2m_rnd(game) < 0.5
        : qa_dot3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).right, qa_sub3_double(trace->end, body.origin)) >= 0;
      if (d->sidestep(d->sidestep_ctx, c)) {
        if (ducker && state->ducked) sh_duck_up(NULL, c);
        state->dodging = true;
        state->attack_state = QA_Q2M_ATTACK_SLIDING;
        state->dodge_time = q2m_now(game) + 0.4 + q2m_rnd(game) * 1.6;
      }
      return;
    }
  }
  if (ducker && trace != NULL && eta < 0.5) {
    if (state->next_duck_time > q2m_now(game)) return;
    qa_q2m_finish_dodge(c);
    if (d->duck(d->duck_ctx, c, eta)) {
      if (state->duck_wait < q2m_now(game)) state->duck_wait = q2m_now(game) + eta;
      sh_duck_down(NULL, c);
      if (game->options.skill == 0) state->duck_wait += 0.5 + q2m_rnd(game) * 0.5;
      else if (game->options.skill == 1) state->duck_wait += 0.1 + q2m_rnd(game) * 0.25;
    }
    state->dodge_time = q2m_now(game) + 0.2 + q2m_rnd(game) * 0.5;
  }
}
static void check_dodge(qa_q2m_monsters *m, qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  const qa_q2m_definition *d = entry_of_context(c)->definition;
  if (state->dead || state->dodge_time > q2m_now(game) || d == NULL || (d->dodge == NULL && d->duck == NULL && d->sidestep == NULL)) return;
  const qa_body_state body = q2m_body(game, entity);
  const qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  const qa_vec3 min = qa_add3_double(qa_add3_double(body.origin, body.bounds.min), q2m_v3(-512, -512, -512));
  const qa_vec3 max = qa_add3_double(qa_add3_double(body.origin, body.bounds.max), q2m_v3(512, 512, 512));
  size_t dodgeable_count = 0;
  qa_q2_entity *const *dodgeables = game->dodgeables(game->ctx, &dodgeable_count);
  for (size_t i = 0; i < dodgeable_count; i++) {
    qa_q2_entity *projectile = dodgeables[i];
    if (strcmp(projectile->solid, "none") == 0 || !projectile->has_owner) continue;
    const qa_body_state shot = q2m_body(game, projectile);
    const double speed = qa_hypot3(shot.velocity);
    const qa_vec3 shot_min = qa_add3_double(shot.origin, shot.bounds.min), shot_max = qa_add3_double(shot.origin, shot.bounds.max);
    if (speed < 4 || shot_min.x > max.x || shot_min.y > max.y || shot_min.z > max.z || shot_max.x < min.x || shot_max.y < min.y
        || shot_max.z < min.z || qa_dot3_double(qa_normalize3_double_hypot(qa_sub3_double(shot.origin, body.origin)), forward) <= 0.35)
      continue;
    qa_trace_result trace = q2m_trace(game, shot.origin, qa_add3_double(shot.origin, shot.velocity), &shot.bounds,
                                      &projectile->actor.id, projectile->clip_mask);
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->actor.id)) continue;
    qa_q2m_monsters_dodge(m, entity, game, projectile->owner, qa_hypot3(qa_sub3_double(trace.end, shot.origin)) / speed, &trace,
                          strcmp(projectile->motion, "bounce") == 0 || strcmp(projectile->motion, "toss") == 0);
    break;
  }
}
/* face_wall: turn toward the wall ahead so a jump up lands square on the ledge. */
static void face_wall(qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_body_state body = q2m_body(game, entity);
  const qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_trace_result trace = q2m_trace(game, body.origin, qa_add3_double(body.origin, qa_scale3_double(forward, 64)), NULL,
                                    &entity->actor.id, qa_q2m_monster_solid_mask(game));
  if (trace.fraction < 1 && !trace.all_solid && !trace.start_solid && trace.contact.has_plane) {
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(trace.contact.plane.normal, QA_VECTOR_TO_ANGLES_NATIVE_AI).y + 180;
    if (c->state->ideal_yaw > 360) c->state->ideal_yaw -= 360;
    qa_q2m_change_yaw(c);
  }
}
static bool starts_with_plat(const qa_q2_entity *entity) {
  return entity != NULL && strncmp(entity->classname, "func_plat", 9) == 0;
}
/* TS Q2Monsters.blocked: an infantry jumps up or down toward its enemy, and any monster rides a platform toward one above or below. */
static bool monsters_blocked(qa_q2m_monsters *m, qa_q2m_context *c, double distance) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  qa_body_state body = q2m_body(game, entity);
  if (!qa_q2m_enemy_body(c, &enemy) || (strcmp(state->kind, "soldier") == 0 && (state->dodging || state->ducked))) return false;
  const qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  const double mask = qa_q2m_monster_solid_mask(game);
  if (strcmp(state->kind, "infantry") == 0 && (qa_js_to_int32(entity->spawnflags) & 8) == 0 && state->jump_time <= q2m_now(game)) {
    const double self_min = body.origin.z + body.bounds.min.z, enemy_min = enemy.origin.z + enemy.bounds.min.z;
    const qa_vec3 ahead = qa_add3_double(body.origin, qa_scale3_double(forward, 48));
    const bool down = enemy_min < self_min - 18, up = enemy_min > self_min + 18;
    if (down || up) {
      const qa_vec3 start = up ? q2m_v3(ahead.x, ahead.y, body.origin.z + body.bounds.max.z + 40) : ahead;
      const qa_vec3 end = down ? q2m_v3(ahead.x, ahead.y, self_min - 193) : ahead;
      if (!down || q2m_trace(game, body.origin, ahead, &body.bounds, &entity->actor.id, mask).fraction == 1) {
        const double water_mask = (double)(qa_js_to_int32(mask) | qa_js_to_int32(QA_Q2M_MASK_WATER));
        qa_trace_result trace = q2m_trace(game, start, end, NULL, &entity->actor.id, water_mask);
        const uint32_t solid = qa_js_to_uint32(game->host->point_contents(game->host->ctx, qa_add3_double(trace.end, q2m_v3(0, 0, -1))));
        if (trace.fraction < 1 && !trace.all_solid && !trace.start_solid && (solid & (3u | 32u)) != 0 &&
            (up ? trace.end.z - self_min <= 40
                : self_min - trace.end.z >= 24 && enemy_min - trace.end.z <= 32 && trace.contact.has_plane &&
                      trace.contact.plane.normal.z >= 0.9)) {
          if ((solid & 32u) != 0) {
            qa_trace_result deep = q2m_trace(game, trace.end, end, NULL, &entity->actor.id, mask);
            if (qa_q2m_water_above_waist_at(game, deep.end, body.bounds.min.z)) return false;
          }
          if (up) face_wall(c);
          qa_q2m_finish_dodge(c);
          state->jump_time = q2m_now(game) + 3;
          c->set_move(c->impl_ctx, c, up ? "infantry_move_jump2" : "infantry_move_jump", false, false);
          return true;
        }
      }
    }
  }
  const bool above = enemy.origin.z + enemy.bounds.min.z >= body.origin.z + body.bounds.max.z,
             below = enemy.origin.z + enemy.bounds.max.z <= body.origin.z + body.bounds.min.z;
  if (!above && !below) return false;
  qa_q2_entity *platform = body.ground != NULL ? game->entity(game->ctx, body.ground) : NULL;
  if (!starts_with_plat(platform)) {
    const qa_vec3 start = qa_add3_double(body.origin, qa_scale3_double(forward, distance));
    qa_trace_result trace = q2m_trace(game, start, qa_add3_double(start, q2m_v3(0, 0, -384)), NULL, &entity->actor.id, mask);
    platform = trace.hit.kind == QA_TRACE_HIT_ACTOR ? game->entity(game->ctx, &trace.hit.actor) : NULL;
  }
  if (!starts_with_plat(platform) || platform->use.fn == NULL) return false;
  const char *endpoint = m->hooks.platform_state ? m->hooks.platform_state(m->hooks.ctx, platform->actor.id) : NULL;
  const bool on_platform = body.ground != NULL && qa_same_actor(*body.ground, platform->actor.id);
  const bool at_bottom = endpoint != NULL && strcmp(endpoint, "bottom") == 0, at_top = endpoint != NULL && strcmp(endpoint, "top") == 0;
  if (above ? (on_platform && at_bottom) || (!on_platform && at_top) : (on_platform && at_top) || (!on_platform && at_bottom)) {
    const qa_actor_id self = entity->actor.id;
    platform->use.fn(platform->use.ctx, platform, game, &self, &self);
    return true;
  }
  return false;
}

static void drop_to_floor(qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_body_state body = q2m_body(game, entity);
  double direction = entity->gravity_vector.z > 0 ? 1 : -1;
  bool offset = !q2m_rr(game)
    || q2m_trace(game, body.origin, body.origin, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game)).start_solid;
  qa_vec3 start = offset ? qa_add3_double(body.origin, q2m_v3(0, 0, -direction)) : body.origin;
  {
    qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = start;
    q2m_move(game, entity, changes, true, false);
  }
  qa_vec3 end = qa_add3_double(start, q2m_v3(0, 0, direction * 256));
  qa_trace_result trace = q2m_trace(game, start, end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
  if (trace.fraction != 1 && !trace.all_solid) {
    qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = trace.end;
    q2m_move(game, entity, changes, false, false);
    check_ground(c);
    categorize_position(c);
  }
}

static qa_q2m_context *admit(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game,
                             const qa_q2m_definition *definition, bool reviving, bool summoned) {
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH) { game->remove(game->ctx, entity); return NULL; }
  connect_monsters(m, game);
  bool rr = q2m_rr(game);
  const qa_q2m_move *initial_move = NULL;
  for (size_t i = 0; i < definition->move_count; i++)
    if (strcmp(definition->moves[i].name, definition->initial_move) == 0) initial_move = &definition->moves[i];
  if (!initial_move) qa_raise("Error", 0, 0, "Missing Q2 monster initial move %s", definition->initial_move);
  qa_q2m_context *prev_ctx = reviving ? qa_q2m_monsters_context(m, entity->actor.id) : NULL;
  const qa_q2m_state *previous = prev_ctx ? prev_ctx->state : NULL;
  qa_q2m_state *state = malloc(sizeof *state);
  if (!state) return NULL;
  memset(state, 0, sizeof *state);
  state->alt = qa_q2m_create_alternate_fly_state();
  state->kind = definition->kind;
  state->weapon = definition->has_soldier ? definition->soldier_weapon : QA_Q2M_WEAPON_MACHINEGUN;
  qa_q2m_locomotion locomotion = definition->has_locomotion ? definition->locomotion : QA_Q2M_LOCO_WALK;
  state->locomotion = locomotion;
  state->has_melee = definition->melee.fn != NULL;
  state->has_ranged_attack = !(definition->has_ranged_attack_flag && !definition->has_ranged_attack);
  state->has_idle = definition->idle.fn != NULL;
  state->has_search = definition->search.fn != NULL;
  state->blind_fire = definition->has_blind_fire_flag && definition->blind_fire;
  state->ignore_shots = previous ? previous->ignore_shots : false;
  state->do_not_count = summoned || (previous ? previous->do_not_count : false);
  state->spawned_by = previous ? previous->spawned_by : QA_Q2M_SPAWNED_NONE;
  state->has_commander = previous ? previous->has_commander : false;
  if (previous && previous->has_commander) state->commander = previous->commander;
  state->monster_slots = previous ? previous->monster_slots : 0;
  state->monster_used = previous ? previous->monster_used : 0;
  state->no_step = previous ? previous->no_step : false;
  state->move = initial_move;
  state->health_scaling = 1;
  state->scale = definition->scale;
  state->gib_health = definition->gib_health;
  state->can_take_damage = true;
  state->ideal_yaw = q2m_body(game, entity).angles.y;
  state->yaw_speed = definition->has_yaw_speed ? definition->yaw_speed : (locomotion == QA_Q2M_LOCO_WALK ? 20 : 10);
  state->combat_target = entity->combat_target;
  state->cocked = strcmp(definition->kind, "soldier") == 0;
  state->normal_height = definition->bounds.max.z;
  state->air_finished = q2m_now(game) + 12;
  qa_q2m_context *c = attach_context(m, entity, game, definition, state);
  if (!c) { free(state); return NULL; }
  entity->model = definition->model;
  entity->max_health = definition->health * (rr ? qa_q2_number_field(&entity->spawn, "health_multiplier", 1) : 1);
  entity->view_height = definition->has_view_height ? definition->view_height
                        : (rr ? 0 : locomotion == QA_Q2M_LOCO_SWIM ? 10 : 25);
  entity->server_flags = (double)(qa_js_to_int32((qa_js_to_uint32(entity->server_flags) | 4u) & ~2u));
  entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->flags) | (locomotion == QA_Q2M_LOCO_FLY ? 1u : locomotion == QA_Q2M_LOCO_SWIM ? 2u : 0u)));
  entity->clip_mask = qa_q2m_monster_solid_mask(game);
  entity->skin = definition->has_soldier ? definition->soldier_skin : 0;
  entity->count = definition->has_soldier ? definition->soldier_count : 0;
  entity->scale = rr ? qa_q2_number_field(&entity->spawn, "scale", 1) : 1;
  entity->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->render_flags) | (!rr ? 64u : 32768u)));
  state->scale *= entity->scale;
  {
    qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_bounds = true;
    changes.bounds.min = qa_scale3_double(definition->bounds.min, entity->scale);
    changes.bounds.max = qa_scale3_double(definition->bounds.max, entity->scale);
    q2m_move(game, entity, changes, true, false);
  }
  state->normal_height = q2m_body(game, entity).bounds.max.z;
  if (rr && entity->view_height == 0) entity->view_height = trunc(state->normal_height - 8);
  qa_combat_state_result existing = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
  if (!existing.has_value) {
    qa_combat_state combat;
    memset(&combat, 0, sizeof combat);
    combat.health = entity->max_health;
    combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    combat.armor.powered.kind = "none";
    combat.mass = definition->mass * entity->scale;
    combat.can_take_damage = true;
    qa_gameplay_authority_create_state(game->host->combat, entity->actor, combat, NULL, NULL);
  } else {
    qa_gameplay_authority_set_health(game->host->combat, entity->actor, entity->max_health);
    qa_armor_state armor;
    memset(&armor, 0, sizeof armor);
    armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    armor.powered.kind = "none";
    qa_gameplay_authority_set_armor(game->host->combat, entity->actor, armor);
    qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
    traits.has_mass = true; traits.mass = definition->mass * entity->scale;
    traits.has_can_take_damage = true; traits.can_take_damage = true;
    traits.has_invulnerable = true; traits.invulnerable = false;
    qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
  }
  entity->pain = (qa_q2_pain){m, src_pain};
  entity->die = (qa_q2_die){m, src_die};
  entity->use = use_cb(m, src_use);
  if (definition->initialize.fn) definition->initialize.fn(definition->initialize.ctx, c);
  if (!q2m_live(game, entity->actor.id)) return NULL;
  qa_combat_state_result initial_combat = qa_gameplay_authority_read(game->host->combat, entity->actor.id);
  if (!initial_combat.has_value) qa_raise("Error", 0, 0, "Missing initialized Q2 monster combat state %s", entity->classname);
  if (strcmp(initial_combat.value.armor.powered.kind, "none") != 0) {
    state->initial_power_armor_type = strcmp(initial_combat.value.armor.powered.kind, "screen") == 0
      ? QA_Q2M_ARMOR_SCREEN : QA_Q2M_ARMOR_SHIELD;
    state->max_power_armor_power = initial_combat.value.armor.powered.cells;
  } else {
    state->max_power_armor_power = qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power");
  }
  state->base_health = initial_combat.value.health;
  if (rr && (qa_js_to_uint32(entity->spawnflags) & 524288u) != 0) state->good_guy = true;
  if (!state->good_guy && (qa_js_to_uint32(entity->spawnflags) & 4u) != 0)
    entity->spawnflags = (double)(qa_js_to_int32((qa_js_to_uint32(entity->spawnflags) & ~4u) | 1u));
  if ((!reviving || !rr) && !state->good_guy && !state->do_not_count
      && (!rr || (qa_js_to_uint32(entity->spawnflags) & 65536u) == 0)) {
    const qa_monster_mission *mission = m->hooks.mission ? m->hooks.mission(m->hooks.ctx, entity->actor.id) : NULL;
    if (!mission) game->counters->total_monsters++;
    else mission->spawned(mission->ctx);
  }
  game->solid(game->ctx, entity, "box");
  game->motion(game->ctx, entity, locomotion == QA_Q2M_LOCO_STATIONARY ? "stationary" : "step");
  bool automatic = true;
  if (definition->start_mode) automatic = definition->start_mode(definition->start_mode_ctx, c) != QA_Q2M_START_MANUAL;
  if (automatic)
    entity->frame = state->move->first_frame + floor(q2m_rnd(game) * (state->move->last_frame - state->move->first_frame + 1));
  game->show(game->ctx, entity);
  if (automatic)
    game->schedule(game->ctx, entity, !rr ? 0.1 : game->host->frame_seconds(game->host->ctx), (qa_q2_think){m, src_start});
  if (definition->after_spawn.fn) definition->after_spawn.fn(definition->after_spawn.ctx, c);
  return c;
}


/* TS `${classname} in solid at ${JSON.stringify(origin)}`. */
static void start_in_solid(qa_q2_game_services *game, qa_q2_entity *entity) {
  qa_vec3 origin = q2m_body(game, entity).origin;
  char x[40], y[40], z[40], message[512];
  qa_net_js_number(origin.x, x, sizeof x); qa_net_js_number(origin.y, y, sizeof y); qa_net_js_number(origin.z, z, sizeof z);
  (void)snprintf(message, sizeof message, "%s in solid at {\"x\":%s,\"y\":%s,\"z\":%s}", entity->classname, x, y, z);
  game->host->diagnostic(game->host->ctx, message);
}
static void monsters_start(qa_q2m_monsters *m, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  bool spawn_dead = q2m_rr(game) && (qa_js_to_uint32(entity->spawnflags) & 65536u) != 0;
  if ((qa_js_to_uint32(entity->spawnflags) & 2u) == 0 && state->locomotion == QA_Q2M_LOCO_WALK && q2m_now(game) < 1
      && (!q2m_rr(game) || (qa_js_to_uint32(entity->spawnflags) & 262144u) == 0)) {
    drop_to_floor(c);
    /* walkmonster_start_go: a zero step relinks the monster, touches its triggers and reports a bad placement. */
    if (q2m_body(game, entity).ground != NULL && !qa_q2m_walk_move(c, 0, 0, true, true)) start_in_solid(game, entity);
  } else if (state->locomotion == QA_Q2M_LOCO_FLY && !qa_q2m_walk_move(c, 0, 0, true, true)) start_in_solid(game, entity);
  if (qa_q2m_health(game, &entity->actor.id) <= 0) return;
  const qa_monster_mission *mission = m->hooks.mission ? m->hooks.mission(m->hooks.ctx, entity->actor.id) : NULL;
  if (mission) {
    const qa_actor_id *route = mission->route(mission->ctx);
    entity->has_goal = route != NULL;
    if (route) entity->goal = *route;
    state->has_move_target = route != NULL;
    if (route) state->move_target = *route;
    qa_body_state_result target = {false, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {{0, 0, 0}, {0, 0, 0}}, NULL}};
    if (state->has_move_target) target = qa_shared_body_table_read(game->host->bodies, state->move_target);
    if (!target.has_value) {
      state->pause_time = q2m_now(game) + 100000000;
      if (!spawn_dead) c->stand(c->impl_ctx, c);
    } else {
      state->ideal_yaw = qa_vector_to_angles_with_rule(
          qa_sub3_double(target.value.origin, q2m_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_angles = true; changes.angles = q2m_body(game, entity).angles; changes.angles.y = state->ideal_yaw;
      q2m_move(game, entity, changes, true, false);
      if (!spawn_dead) c->walk(c->impl_ctx, c);
    }
  } else {
    if (strlen(entity->target) > 0) {
      size_t count = 0;
      qa_q2_entity *const *targets = game->targets(game->ctx, entity->target, &count);
      bool has_combat = false;
      for (size_t i = 0; i < count; i++) if (strcmp(targets[i]->classname, "point_combat") == 0) has_combat = true;
      if (has_combat) { state->combat_target = entity->target; entity->combat_target = entity->target; entity->target = ""; }
    }
    if (strlen(entity->target) > 0) {
      qa_q2_entity *target = game->pick_target(game->ctx, entity->target);
      entity->has_goal = target != NULL;
      if (target) entity->goal = target->actor.id;
      state->has_move_target = target != NULL;
      if (target) state->move_target = target->actor.id;
      if (!target) {
        char message[512];
        (void)snprintf(message, sizeof message, "%s: target %s not found", entity->classname, entity->target);
        game->host->diagnostic(game->host->ctx, message);
        entity->target = "";
        state->pause_time = q2m_now(game) + 100000000;
        if (!spawn_dead) c->stand(c->impl_ctx, c);
      } else if (strcmp(target->classname, "path_corner") == 0) {
        qa_vec3 a2 = q2m_body(game, target).origin;
        qa_vec3 b = q2m_body(game, entity).origin;
        state->ideal_yaw = qa_vector_to_angles_with_rule(
            qa_sub3_double(a2, b), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
        qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
        changes.has_angles = true; changes.angles = q2m_body(game, entity).angles; changes.angles.y = state->ideal_yaw;
        q2m_move(game, entity, changes, true, false);
        if (!spawn_dead) c->walk(c->impl_ctx, c);
        entity->target = "";
      } else {
        entity->has_goal = false; state->has_move_target = false;
        state->pause_time = q2m_now(game) + 100000000;
        if (!spawn_dead) c->stand(c->impl_ctx, c);
      }
    } else {
      state->pause_time = q2m_now(game) + 100000000;
      if (!spawn_dead) c->stand(c->impl_ctx, c);
    }
  }
  if (spawn_dead) {
    const qa_q2m_definition *d = entry_of_context(c)->definition;
    qa_vec3 origin = q2m_body(game, entity).origin;
    qa_gameplay_authority_set_health(game->host->combat, entity->actor, 0);
    qa_death_reaction reaction;
    memset(&reaction, 0, sizeof reaction);
    reaction.pain.self = entity->actor;
    reaction.pain.attacker = &entity->actor.id;
    reaction.inflictor = &entity->actor.id;
    d->reactions.die(d->reactions.ctx, c, reaction);
    if (!q2m_live(game, entity->actor.id) || state->gibbed) return;
    const qa_q2m_move *move = state->move;
    for (double frame_number = move->first_frame; frame_number < move->last_frame; frame_number++) {
      entity->frame = frame_number;
      const qa_q2m_frame *fr = &move->frames[qa_q2m_record_at(move->frame_count, frame_number - move->first_frame)];
      for (size_t a = 0; a < fr->action_count; a++) {
        if (!fr->actions[a].is_nextframe) c->dispatch(c->impl_ctx, c, fr->actions[a].name);
        else state->next_frame = fr->actions[a].frame_is_next ? entity->frame + 1 : fr->actions[a].frame;
        if (!q2m_live(game, entity->actor.id)) return;
      }
    }
    if (move->end) c->dispatch(c->impl_ctx, c, move->end);
    if (!q2m_live(game, entity->actor.id)) return;
    entity->frame = move->last_frame;
    qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = origin;
    q2m_move(game, entity, changes, false, false);
    game->show(game->ctx, entity);
    return;
  }
  if ((qa_js_to_uint32(entity->spawnflags) & 2u) != 0) {
    game->solid(game->ctx, entity, "none");
    game->motion(game->ctx, entity, "stationary");
    entity->visible = false;
    entity->server_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->server_flags) | 1u));
    qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
    traits.has_can_take_damage = true; traits.can_take_damage = false;
    qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
    game->show(game->ctx, entity);
    entity->use = use_cb(m, src_trigger_use);
    game->cancel(game->ctx, entity);
    return;
  }
  if (mission) mission->started(mission->ctx);
  game->schedule(game->ctx, entity, !q2m_rr(game) ? 0.1 : game->host->frame_seconds(game->host->ctx),
                 (qa_q2_think){m, src_think});
}
static void monsters_trigger_spawn(qa_q2m_monsters *m, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_place_triggered_monster(game, entity->actor);
  entity->spawnflags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->spawnflags) & ~2u));
  entity->server_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->server_flags) & ~1u));
  entity->visible = true;
  game->solid(game->ctx, entity, "box");
  game->motion(game->ctx, entity, "step");
  qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = true;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
  c->state->air_finished = q2m_now(game) + 12;
  monsters_start(m, c);
  qa_q2_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  if (entity->has_enemy && (qa_js_to_uint32(entity->spawnflags) & 1u) == 0
      && (qa_js_to_uint32((enemy_entity ? enemy_entity->flags : 0)) & 32u) == 0)
    qa_q2m_perception_found_target(&m->perception, c);
  else entity->has_enemy = false;
  entity->use = use_cb(m, src_use);
  game->show(game->ctx, entity);
}

bool qa_q2m_monsters_spawn(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  const qa_q2m_definition *d = qa_q2m_monsters_definition(m, entity->classname, game);
  if (!d) return false;
  (void)admit(m, entity, game, d, false, false);
  return true;
}
qa_q2m_context *qa_q2m_monsters_spawn_summoned(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  const qa_q2m_definition *d = qa_q2m_monsters_definition(m, entity->classname, game);
  return d ? admit(m, entity, game, d, false, true) : NULL;
}
qa_q2m_context *qa_q2m_monsters_respawn(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2m_actor_entry *entry = entry_of(m, entity->actor.id);
  const qa_q2m_definition *d = entry ? entry->definition : qa_q2m_monsters_definition(m, entity->classname, game);
  entity->has_enemy = false; entity->has_goal = false; entity->has_owner = false;
  entity->use = (qa_q2_use){NULL, NULL};
  return d ? admit(m, entity, game, d, true, false) : NULL;
}
void qa_q2m_monsters_resume_monster(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2m_context *c = qa_q2m_monsters_context(m, entity->actor.id);
  if (!c) return;
  if (!q2m_rr(game)) game->schedule(game->ctx, entity, 0.1, (qa_q2_think){m, src_think});
  else monsters_start(m, c);
}
qa_q2m_context *qa_q2m_monsters_spawn_infantry_driver(qa_q2m_monsters *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2m_context *c = qa_q2m_monsters_spawn_summoned(m, entity, game);
  if (c) c->state->stand_ground = true;
  return c;
}

/* External follower adapters over a live monster context. */
typedef struct path_env { qa_q2m_context *context; } path_env;
static bool pf_move_target(void *ctx, qa_actor_id *out) {
  path_env *e = ctx;
  if (!e->context->state->has_move_target) return false;
  *out = e->context->state->move_target;
  return true;
}
static bool pf_enemy(void *ctx, qa_actor_id *out) {
  path_env *e = ctx;
  if (!e->context->entity->has_enemy) return false;
  *out = e->context->entity->enemy;
  return true;
}
static bool pf_old_enemy(void *ctx, qa_actor_id *out) {
  path_env *e = ctx;
  if (!e->context->state->has_old_enemy) return false;
  *out = e->context->state->old_enemy;
  return true;
}
static bool pf_activator(void *ctx, qa_actor_id *out) {
  path_env *e = ctx;
  if (!e->context->entity->has_activator) return false;
  *out = e->context->entity->activator;
  return true;
}
/* TS touchPathCorner's own follower: advance sets goal = moveTarget = goal, then pauses or faces the next corner. */
static void pf_path_advance(void *ctx, const char *name, const qa_actor_id *goal, double pause_until) {
  (void)name;
  path_env *e = ctx;
  qa_q2m_context *c = e->context;
  c->entity->has_goal = goal != NULL; if (goal) c->entity->goal = *goal;
  c->state->has_move_target = goal != NULL; if (goal) c->state->move_target = *goal;
  if (pause_until != 0) { c->state->pause_time = pause_until; c->stand(c->impl_ctx, c); return; }
  qa_body_state_result target = goal == NULL ? (qa_body_state_result){0} : qa_shared_body_table_read(c->game->host->bodies, *goal);
  if (target.has_value)
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(target.value.origin, q2m_body(c->game, c->entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
/* TS touchCombatPoint's own follower. */
static void pf_combat_advance(void *ctx, const char *target, const qa_actor_id *goal, const qa_actor_id *move_target) {
  path_env *e = ctx;
  qa_q2m_context *c = e->context;
  c->entity->target = target;
  c->entity->has_goal = goal != NULL;
  if (goal) c->entity->goal = *goal;
  c->state->has_move_target = move_target != NULL;
  if (move_target) c->state->move_target = *move_target;
}
static void pf_combat_hold(void *ctx) {
  path_env *e = ctx;
  qa_q2m_context *c = e->context;
  c->state->pause_time = q2m_now(c->game) + 100000000; c->state->stand_ground = true;
  if (q2m_rr(c->game)) {
    c->state->reached_hold_combat = true;
    c->state->third_eye = true;
  }
  c->stand(c->impl_ctx, c);
}
static void pf_combat_finish(void *ctx) {
  path_env *e = ctx;
  qa_q2m_context *c = e->context;
  c->entity->target = "";
  if (!c->state->reached_hold_combat) {
    c->state->has_move_target = false;
    c->entity->has_goal = c->entity->has_enemy;
    if (c->entity->has_enemy) c->entity->goal = c->entity->enemy;
  }
  c->state->combat_point = false;
}

void qa_q2m_monsters_touch_path_corner(qa_q2m_monsters *m, qa_q2_entity *corner, qa_q2_game_services *game, qa_actor_id actor) {
  path_env env;
  qa_q2m_path_follower internal;
  const qa_q2m_path_follower *follower = NULL;
  qa_q2m_context *c = qa_q2m_monsters_context(m, actor);
  if (c) {
    env.context = c;
    internal.ctx = &env; internal.actor = c->entity->actor;
    internal.move_target = pf_move_target; internal.enemy = pf_enemy; internal.advance = pf_path_advance;
    follower = &internal;
  } else if (m->has_external_path && m->external_path.path)
    follower = m->external_path.path(m->external_path.ctx, actor);
  if (!follower) return;
  qa_actor_id move_target, enemy;
  if (!follower->move_target(follower->ctx, &move_target) || !qa_same_actor(move_target, corner->actor.id)
      || follower->enemy(follower->ctx, &enemy)) return;
  const char *path_target = qa_q2_spawn_get(&corner->spawn, "pathtarget");
  if (path_target != NULL) {
    const char *saved = corner->target; corner->target = path_target;
    qa_q2_game_use_targets_entity(game, corner, &actor, false);
    corner->target = saved;
  }
  qa_q2_entity *next = strlen(corner->target) > 0 ? game->pick_target(game->ctx, corner->target) : NULL;
  if (next != NULL && (qa_js_to_int32(next->spawnflags) & 1) != 0) {
    qa_body_state destination = q2m_body(game, next);
    qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, actor);
    if (!body.has_value) return;
    qa_vec3 origin = destination.origin;
    origin.z = destination.origin.z + destination.bounds.min.z - body.value.bounds.min.z;
    if (c == NULL) {
      qa_body_state moved = body.value; moved.origin = origin;
      qa_shared_body_table_write(game->host->bodies, follower->actor, moved);
      qa_shared_body_table_link(game->host->bodies, follower->actor, NULL);
    } else {
      qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_origin = true; changes.origin = origin;
      q2m_move(game, c->entity, changes, false, false);
    }
    qa_q2_presentation_event event; memset(&event, 0, sizeof event);
    event.kind = "entity-event"; event.has_actor = true; event.actor = actor; event.event = 7;
    game->host->emit(game->host->ctx, event);
    next = game->pick_target(game->ctx, next->target);
  }
  double now = q2m_now(game);
  follower->advance(follower->ctx, next != NULL ? next->targetname : "", next != NULL ? &next->actor.id : NULL,
      corner->wait != 0 ? now + corner->wait : next == NULL ? now + 100000000 : 0);
}
void qa_q2m_monsters_touch_combat_point(qa_q2m_monsters *m, qa_q2_entity *corner, qa_q2_game_services *game, qa_actor_id actor) {
  path_env env;
  qa_q2m_combat_follower internal;
  const qa_q2m_combat_follower *follower = NULL;
  qa_q2m_context *c = qa_q2m_monsters_context(m, actor);
  if (c) {
    env.context = c;
    memset(&internal, 0, sizeof internal);
    internal.ctx = &env;
    internal.move_target = pf_move_target; internal.enemy = pf_enemy;
    internal.old_enemy = pf_old_enemy; internal.activator = pf_activator;
    internal.walking = c->state->locomotion == QA_Q2M_LOCO_WALK;
    internal.advance = pf_combat_advance; internal.hold = pf_combat_hold; internal.finish = pf_combat_finish;
    follower = &internal;
  } else if (m->has_external_combat && m->external_combat.combat)
    follower = m->external_combat.combat(m->external_combat.ctx, actor);
  if (!follower) return;
  qa_actor_id move_target;
  if (!follower->move_target(follower->ctx, &move_target) || !qa_same_actor(move_target, corner->actor.id)) return;
  if (strlen(corner->target) > 0) {
    qa_q2_entity *target = game->pick_target(game->ctx, corner->target);
    follower->advance(follower->ctx, corner->target, target != NULL ? &target->actor.id : NULL,
        target != NULL ? &target->actor.id : &corner->actor.id);
    if (target == NULL) {
      char message[512];
      (void)snprintf(message, sizeof message, "point_combat target %s does not exist", corner->target);
      game->host->diagnostic(game->host->ctx, message);
    }
    corner->target = "";
  } else if ((qa_js_to_int32(corner->spawnflags) & 1) != 0 && follower->walking) follower->hold(follower->ctx);
  if (follower->move_target(follower->ctx, &move_target) && qa_same_actor(move_target, corner->actor.id)) follower->finish(follower->ctx);
  const char *path_target = qa_q2_spawn_get(&corner->spawn, "pathtarget");
  if (path_target != NULL) {
    const char *saved = corner->target; corner->target = path_target;
    qa_actor_id candidates[3]; bool has[3];
    has[0] = follower->enemy(follower->ctx, &candidates[0]);
    has[1] = follower->old_enemy(follower->ctx, &candidates[1]);
    has[2] = follower->activator(follower->ctx, &candidates[2]);
    qa_actor_id activator = actor;
    for (int i = 0; i < 3; i++)
      if (has[i] && game->host->is_player(game->host->ctx, candidates[i])) { activator = candidates[i]; break; }
    qa_q2_game_use_targets_entity(game, corner, &activator, false);
    corner->target = saved;
  }
}

/* ---- capture / restore ---- */
static qa_q2m_damage_checkpoint_result save_pending(qa_q2m_monsters *m, qa_actor_id actor) {
  qa_q2m_damage_checkpoint_result out;
  memset(&out, 0, sizeof out);
  qa_q2m_pending_damage *p = pending_of(m, actor);
  if (!p) return out;
  out.has_value = true;
  out.value.kick = p->reaction.pain.kick;
  out.value.damage = p->reaction.pain.damage;
  out.value.point = p->reaction.point;
  out.value.has_attacker = qa_q2_save_actor(p->reaction.pain.attacker, &out.value.attacker);
  out.value.has_inflictor = qa_q2_save_actor(p->reaction.inflictor, &out.value.inflictor);
  out.value.has_attack = p->has_attack;
  if (p->has_attack) out.value.attack = qa_q2_save_attack(p->attack);
  return out;
}
bool qa_q2m_monsters_capture(qa_q2m_monsters *m, qa_q2m_monsters_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  qa_q2m_actor_checkpoint *actors = m->entry_count ? malloc(m->entry_count * sizeof *actors) : NULL;
  if (m->entry_count && !actors) return false;
  for (size_t i = 0; i < m->entry_count; i++) {
    qa_q2m_actor_entry *e = m->entries[i];
    qa_q2m_context *c = &e->context;
    (void)qa_q2_save_actor(&c->entity->actor.id, &actors[i].actor);
    actors[i].definition = e->definition->classname;
    qa_q2m_state_checkpoint *sc = &actors[i].state;
    memset(sc, 0, sizeof *sc);
    sc->rest = *c->state;
    sc->move = c->state->move->name;
    sc->next_move = c->state->next_move ? c->state->next_move->name : NULL;
    sc->has_sound_target = c->state->has_sound_target;
    if (c->state->has_sound_target) {
      (void)qa_q2_save_actor(&c->state->sound_target.actor, &sc->sound_target.actor);
      (void)qa_q2_save_actor(&c->state->sound_target.owner, &sc->sound_target.owner);
      sc->sound_target.origin = c->state->sound_target.origin;
      sc->sound_target.time = c->state->sound_target.time;
    }
    sc->has_old_enemy = c->state->has_old_enemy && qa_q2_save_actor(&c->state->old_enemy, &sc->old_enemy);
    sc->has_move_target = c->state->has_move_target && qa_q2_save_actor(&c->state->move_target, &sc->move_target);
    sc->has_commander = c->state->has_commander && qa_q2_save_actor(&c->state->commander, &sc->commander);
    sc->rest.move = NULL; sc->rest.next_move = NULL;
    sc->rest.has_sound_target = false; sc->rest.has_old_enemy = false;
    sc->rest.has_move_target = false; sc->rest.has_commander = false;
    actors[i].pending_damage = save_pending(m, c->entity->actor.id);
  }
  out->actors = actors;
  out->actor_count = m->entry_count;
  return qa_q2m_perception_capture(&m->perception, &out->perception);
}
void qa_q2m_monsters_checkpoint_free(qa_q2m_monsters_checkpoint *cp) {
  free((void *)cp->actors);
  qa_q2m_perception_checkpoint_free(&cp->perception);
  memset(cp, 0, sizeof *cp);
}
static const qa_q2m_move *find_move(const qa_q2m_definition *d, const char *name) {
  for (size_t i = 0; i < d->move_count; i++)
    if (strcmp(d->moves[i].name, name) == 0) return &d->moves[i];
  qa_raise("Error", 0, 0, "%s checkpoint has unknown source animation %s", d->classname, name);
}
typedef struct restore_ref_env { qa_q2_game_services *game; } restore_ref_env;
static qa_actor_id restore_ref(void *ctx, qa_saved_actor_id actor) {
  restore_ref_env *e = ctx;
  return qa_session_actor_registry_reference_saved(e->game->host->actors, actor, NULL);
}
void qa_q2m_monsters_restore(qa_q2m_monsters *m, qa_q2_game_services *game, const qa_q2m_monsters_checkpoint *cp) {
  connect_monsters(m, game);
  /* TS contexts.clear(): the entities these contexts pointed at may already
   * be gone (a foundation restore rebuilds them), so do not read them. */
  for (size_t i = 0; i < m->entry_count; i++) { free(m->entries[i]->state); free(m->entries[i]); }
  m->entry_count = 0;
  while (m->pending_count) pending_delete(m, m->pending[0]->actor);
  qa_q2m_perception_restore(&m->perception, game, &cp->perception);
  for (size_t i = 0; i < cp->actor_count; i++) {
    const qa_q2m_actor_checkpoint *saved = &cp->actors[i];
    qa_owned_actor owner = qa_q2_restore_actor(game, saved->actor);
    qa_q2_entity *entity = game->entity(game->ctx, &owner.id);
    if (!entity) continue;
    const qa_q2m_definition *d = qa_q2m_monsters_definition(m, saved->definition, game);
    if (!d) qa_raise("Error", 0, 0, "Q2 monster checkpoint has no registered definition %s", saved->definition);
    const qa_q2m_state_checkpoint *s = &saved->state;
    const qa_q2m_move *move = find_move(d, s->move);
    const qa_q2m_move *next_move = s->next_move ? find_move(d, s->next_move) : NULL;
    qa_q2m_state *state = malloc(sizeof *state);
    if (!state) return;
    *state = s->rest;
    state->move = move;
    state->next_move = next_move;
    state->kind = d->kind;
    state->combat_target = qa_q2_intern(state->combat_target); /* outlives a decoded checkpoint */
    state->has_sound_target = s->has_sound_target;
    if (s->has_sound_target) {
      state->sound_target.actor = qa_session_actor_registry_reference_saved(game->host->actors, s->sound_target.actor, NULL);
      state->sound_target.owner = qa_session_actor_registry_reference_saved(game->host->actors, s->sound_target.owner, NULL);
      state->sound_target.origin = s->sound_target.origin;
      state->sound_target.time = s->sound_target.time;
    }
    state->has_old_enemy = s->has_old_enemy;
    if (s->has_old_enemy) state->old_enemy = qa_session_actor_registry_reference_saved(game->host->actors, s->old_enemy, NULL);
    state->has_move_target = s->has_move_target;
    if (s->has_move_target) state->move_target = qa_session_actor_registry_reference_saved(game->host->actors, s->move_target, NULL);
    state->has_commander = s->has_commander;
    if (s->has_commander) state->commander = qa_session_actor_registry_reference_saved(game->host->actors, s->commander, NULL);
    qa_q2m_context *c = attach_context(m, entity, game, d, state);
    if (d->restore.fn) d->restore.fn(d->restore.ctx, c);
    if (saved->pending_damage.has_value) {
      const qa_q2m_damage_checkpoint *p = &saved->pending_damage.value;
      qa_q2m_pending_damage *entry = malloc(sizeof *entry);
      if (!entry) return;
      memset(entry, 0, sizeof *entry);
      entry->actor = owner.id;
      qa_death_reaction reaction;
      memset(&reaction, 0, sizeof reaction);
      if (p->has_attacker) entry->attacker_storage = qa_session_actor_registry_reference_saved(game->host->actors, p->attacker, NULL);
      if (p->has_inflictor) entry->inflictor_storage = qa_session_actor_registry_reference_saved(game->host->actors, p->inflictor, NULL);
      reaction.pain.self = owner;
      reaction.pain.attacker = p->has_attacker ? &entry->attacker_storage : NULL;
      reaction.pain.kick = p->kick;
      reaction.pain.damage = p->damage;
      reaction.inflictor = p->has_inflictor ? &entry->inflictor_storage : NULL;
      reaction.point = p->point;
      entry->has_attack = p->has_attack;
      if (p->has_attack) {
        restore_ref_env env = {game};
        entry->attack = qa_q2_restore_attack(p->attack, restore_ref, &env, &entry->attack_attacker_storage,
                                             &entry->attack_inflictor_storage);
        reaction.pain.attack = &entry->attack;
      }
      entry->reaction = reaction;
      if (!grow((void **)&m->pending, &m->pending_cap, m->pending_count, sizeof *m->pending)) { free(entry); return; }
      m->pending[m->pending_count++] = entry;
    }
    entity->pain = (qa_q2_pain){m, src_pain};
    entity->die = (qa_q2_die){m, src_die};
  }
}






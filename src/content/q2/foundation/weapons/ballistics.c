/* Port of src/content/q2/foundation/weapons/ballistics.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/ballistics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static qa_vec3 zv(void) { return v3(0, 0, 0); }
static bool grow(void **arr, size_t *cap, size_t count, size_t size) {
  if (count < *cap) return true;
  size_t next = *cap ? *cap * 2 : 8;
  void *p = realloc(*arr, next * size);
  if (!p) return false;
  *arr = p; *cap = next; return true;
}
static double now_of(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static double rnd_of(qa_q2_game_services *g) { return g->host->random(g->host->ctx); }
static bool rerelease(qa_q2_game_services *g) { return g->options.edition == QA_Q2_EDITION_RERELEASE; }

static qa_vec3 trace_normal(qa_trace_result t) { return t.contact.has_plane ? t.contact.plane.normal : zv(); }
static double trace_contents(qa_trace_result t) { return strcmp(t.kind, "q1") == 0 ? 0 : t.contents; }
static bool trace_sky(qa_trace_result t) {
  if (strcmp(t.kind, "q2") == 0)
    return t.surface != NULL && ((qa_js_to_uint32(t.surface->flags) & 4) != 0 || strncmp(t.surface->name, "sky", 3) == 0);
  return strcmp(t.kind, "q3") == 0 && (qa_js_to_uint32(t.surface_flags) & 4) != 0;
}
static bool trace_hit_actor(qa_trace_result t, qa_actor_id *out) {
  if (t.hit.kind != QA_TRACE_HIT_ACTOR) return false;
  *out = t.hit.actor; return true;
}
static bool can_hurt(qa_q2_game_services *g, bool has, qa_actor_id actor) {
  if (!has) return false;
  qa_combat_state_result r = qa_gameplay_authority_read(g->host->combat, actor);
  return r.has_value && r.value.can_take_damage;
}
static double combat_health(qa_q2_game_services *g, qa_actor_id actor) {
  qa_combat_state_result r = qa_gameplay_authority_read(g->host->combat, actor);
  return r.has_value ? r.value.health : 0;
}
static bool centroid(qa_q2_game_services *g, qa_actor_id actor, qa_vec3 *out) {
  qa_body_state_result r = qa_shared_body_table_read(g->host->bodies, actor);
  if (!r.has_value) return false;
  *out = qa_add3_double(r.value.origin, qa_scale3_double(qa_add3_double(r.value.bounds.min, r.value.bounds.max), 0.5));
  return true;
}
static void effect(qa_q2_game_services *g, const char *name, qa_vec3 origin, qa_vec3 direction, double count, double color) {
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "effect"; e.effect = name; e.origin = origin; e.direction = direction; e.count = count; e.color = color;
  g->host->emit(g->host->ctx, e);
}
static const char *weapon_for_mod(double mod) {
  if (mod == QA_Q2W_MOD_BLASTER) return "q2:weapon_blaster";
  if (mod == QA_Q2W_MOD_SHOTGUN) return "q2:weapon_shotgun";
  if (mod == QA_Q2W_MOD_SUPERSHOTGUN) return "q2:weapon_supershotgun";
  if (mod == QA_Q2W_MOD_MACHINEGUN) return "q2:weapon_machinegun";
  if (mod == QA_Q2W_MOD_CHAINGUN) return "q2:weapon_chaingun";
  if (mod == QA_Q2W_MOD_HYPERBLASTER) return "q2:weapon_hyperblaster";
  return NULL;
}
static qa_trace_result do_trace(qa_q2_game_services *g, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                const qa_actor_id *ignore, const qa_actor_id *exclude, size_t exclude_count, double mask) {
  qa_q2_trace_request r;
  memset(&r, 0, sizeof r);
  r.start = start; r.end = end; r.bounds = bounds; r.ignore = ignore; r.mask = mask;
  r.exclude = exclude; r.exclude_count = exclude_count;
  return g->host->trace(g->host->ctx, r);
}
static void do_damage(qa_q2_game_services *g, qa_actor_id target, qa_q2_entity *inf_ent, qa_actor_id inf_actor,
                      const qa_actor_id *attacker, double amount, double kick, qa_vec3 dir, qa_vec3 point,
                      qa_vec3 normal, double mod, bool has_flags, double flags, const char *weapon) {
  (void)g->damage(g->ctx, target, inf_ent, inf_actor, attacker, amount, kick, dir, point, normal, mod, has_flags ? flags : 0, weapon);
}
static void do_radius(qa_q2_game_services *g, qa_q2_entity *inf, const qa_actor_id *attacker, double damage,
                      const qa_actor_id *ignore, double radius, double mod, bool has_flags, double flags, const char *weapon) {
  g->radius_damage(g->ctx, inf, attacker, damage, ignore, radius, mod, has_flags ? flags : 0, weapon);
}
static void move_parts(qa_q2_game_services *g, qa_q2_entity *e, qa_q2_body_changes c, bool has_link, bool link) {
  g->move(g->ctx, e, c, has_link ? link : true);
}

/* TS excluded ActorId[]: rerelease stops at 16, classic grows without bound. */
typedef struct exclusion { qa_actor_id fixed[16]; qa_actor_id *items; size_t count, cap; } exclusion;
static void exclusion_init(exclusion *x) { x->items = x->fixed; x->count = 0; x->cap = 16; }
static bool exclusion_has(const exclusion *x, qa_actor_id actor) {
  for (size_t i = 0; i < x->count; i++) if (qa_same_actor(x->items[i], actor)) return true;
  return false;
}
static void exclusion_push(exclusion *x, qa_actor_id actor) {
  if (x->count == x->cap) {
    size_t next = x->cap * 2;
    qa_actor_id *grown = x->items == x->fixed ? malloc(next * sizeof *grown) : realloc(x->items, next * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    if (x->items == x->fixed) memcpy(grown, x->fixed, sizeof x->fixed);
    x->items = grown; x->cap = next;
  }
  x->items[x->count++] = actor;
}
static void exclusion_destroy(exclusion *x) { if (x->items != x->fixed) free(x->items); }

/* ---- maps ---- */
static size_t find_double(qa_q2w_actor_double *a, size_t n, qa_actor_id actor) {
  for (size_t i = 0; i < n; i++) if (qa_same_actor(a[i].actor, actor)) return i;
  return n;
}
static bool set_double(qa_q2w_actor_double **a, size_t *n, size_t *cap, qa_actor_id actor, double value) {
  size_t i = find_double(*a, *n, actor);
  if (i < *n) { (*a)[i].value = value; return true; }
  if (!grow((void **)a, cap, *n, sizeof **a)) return false;
  (*a)[*n].actor = actor; (*a)[*n].value = value; (*n)++;
  return true;
}
static void del_double(qa_q2w_actor_double *a, size_t *n, qa_actor_id actor) {
  size_t i = find_double(a, *n, actor);
  if (i < *n) { a[i] = a[*n - 1]; (*n)--; }
}
qa_q2w_state *qa_q2w_ballistics_state(qa_q2w_ballistics *b, qa_actor_id actor) {
  for (size_t i = 0; i < b->state_count; i++) if (qa_same_actor(b->states[i].actor, actor)) return b->states[i].state;
  return NULL;
}
bool qa_q2w_ballistics_set_state(qa_q2w_ballistics *b, qa_actor_id actor, qa_q2w_state *state) {
  for (size_t i = 0; i < b->state_count; i++)
    if (qa_same_actor(b->states[i].actor, actor)) { free(b->states[i].state); b->states[i].state = state; return true; }
  if (!grow((void **)&b->states, &b->state_cap, b->state_count, sizeof *b->states)) return false;
  b->states[b->state_count].actor = actor; b->states[b->state_count].state = state; b->state_count++;
  return true;
}
const qa_q2w_input *qa_q2w_ballistics_input(qa_q2w_ballistics *b, qa_actor_id actor) {
  for (size_t i = 0; i < b->input_count; i++) if (qa_same_actor(b->inputs[i].actor, actor)) return &b->inputs[i].input;
  return NULL;
}
bool qa_q2w_ballistics_set_input(qa_q2w_ballistics *b, qa_actor_id actor, qa_q2w_input input) {
  for (size_t i = 0; i < b->input_count; i++)
    if (qa_same_actor(b->inputs[i].actor, actor)) { b->inputs[i].input = input; return true; }
  if (!grow((void **)&b->inputs, &b->input_cap, b->input_count, sizeof *b->inputs)) return false;
  b->inputs[b->input_count].actor = actor; b->inputs[b->input_count].input = input; b->input_count++;
  return true;
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q2w_ballistics *b = ctx;
  for (size_t i = 0; i < b->state_count; i++)
    if (qa_same_actor(b->states[i].actor, actor.id)) { free(b->states[i].state); b->states[i] = b->states[--b->state_count]; break; }
  for (size_t i = 0; i < b->input_count; i++)
    if (qa_same_actor(b->inputs[i].actor, actor.id)) { b->inputs[i] = b->inputs[--b->input_count]; break; }
  del_double(b->blaster_causes, &b->blaster_cause_count, actor.id);
  del_double(b->silencer_charges, &b->silencer_count, actor.id);
}
void qa_q2w_ballistics_track_actors(qa_q2w_ballistics *b, qa_q2_game_services *game) {
  void *registry = game->host->actors;
  for (size_t i = 0; i < b->release_count; i++) if (b->release_registries[i] == registry) return;
  if (!grow((void **)&b->release_registries, &b->release_cap, b->release_count, sizeof(void *))) return;
  if (!grow((void **)&b->release_subscriptions, &b->release_subscription_cap, b->release_count, sizeof(int64_t))) return;
  b->release_subscriptions[b->release_count] = qa_session_actor_registry_on_release(game->host->actors, on_release, b);
  b->release_registries[b->release_count++] = registry;
}

/* forward: projectile callbacks */
static void grenade_explode(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
static void grenade_update(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
static void bfg_think(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
static void bfg_explode(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
static void blaster_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact);
static void grenade_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact);
static void rocket_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact);
static void bfg_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact);

bool qa_q2w_ballistics_init(qa_q2w_ballistics *b, qa_q2w_hooks hooks) {
  memset(b, 0, sizeof *b);
  b->hooks = hooks;
  qa_q2_think free_t = QA_Q2_FREE_ENTITY;
  b->cb_think[0] = (qa_q2_named_think){"G_FreeEdict", free_t};
  b->cb_think[1] = (qa_q2_named_think){"Grenade_Explode", {b, grenade_explode}};
  b->cb_think[2] = (qa_q2_named_think){"grenade_think", {b, grenade_update}};
  b->cb_think[3] = (qa_q2_named_think){"bfg_think", {b, bfg_think}};
  b->cb_think[4] = (qa_q2_named_think){"bfg_explode", {b, bfg_explode}};
  b->cb_touch[0] = (qa_q2_named_touch){"blaster_touch", {b, blaster_touch}};
  b->cb_touch[1] = (qa_q2_named_touch){"Grenade_Touch", {b, grenade_touch}};
  b->cb_touch[2] = (qa_q2_named_touch){"rocket_touch", {b, rocket_touch}};
  b->cb_touch[3] = (qa_q2_named_touch){"bfg_touch", {b, bfg_touch}};
  b->cb_die[0] = (qa_q2_named_die){"q2_weapon_debris_die", QA_Q2_FREE_ENTITY_DIE};
  return true;
}
void qa_q2w_ballistics_destroy(qa_q2w_ballistics *b) {
  for (size_t i = 0; i < b->release_count; i++)
    qa_session_actor_registry_off_release(b->release_registries[i], b->release_subscriptions[i]);
  free(b->release_subscriptions);
  for (size_t i = 0; i < b->state_count; i++) free(b->states[i].state);
  free(b->states); free(b->inputs);
  free(b->blaster_causes); free(b->silencer_charges); free(b->release_registries);
  memset(b, 0, sizeof *b);
}
qa_q2_callback_definitions qa_q2w_ballistics_callbacks(qa_q2w_ballistics *b) {
  qa_q2_callback_definitions d;
  memset(&d, 0, sizeof d);
  d.think = b->cb_think; d.think_count = 5;
  d.touch = b->cb_touch; d.touch_count = 4;
  d.die = b->cb_die; d.die_count = 1;
  return d;
}
void qa_q2w_ballistics_register_callbacks(qa_q2w_ballistics *b, qa_q2_game_services *game) {
  qa_q2w_ballistics_track_actors(b, game);
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2w_ballistics_callbacks(b));
}
bool qa_q2w_ballistics_capture_projectiles(qa_q2w_ballistics *b, qa_q2w_cause_entry **out, size_t *out_count) {
  *out = NULL; *out_count = b->blaster_cause_count;
  if (b->blaster_cause_count == 0) return true;
  *out = malloc(b->blaster_cause_count * sizeof **out);
  if (!*out) return false;
  for (size_t i = 0; i < b->blaster_cause_count; i++) {
    (void)qa_q2_save_actor(&b->blaster_causes[i].actor, &(*out)[i].actor);
    (*out)[i].means_of_death = b->blaster_causes[i].value;
  }
  return true;
}
void qa_q2w_free_cause_entries(qa_q2w_cause_entry *entries) { free(entries); }
void qa_q2w_ballistics_restore_projectiles(qa_q2w_ballistics *b, qa_q2_game_services *game, const qa_q2w_cause_entry *causes, size_t count) {
  qa_q2w_ballistics_register_callbacks(b, game);
  b->blaster_cause_count = 0;
  for (size_t i = 0; i < count; i++)
    (void)set_double(&b->blaster_causes, &b->blaster_cause_count, &b->blaster_cause_cap,
                     qa_q2_restore_actor(game, causes[i].actor).id, causes[i].means_of_death);
}
double qa_q2w_ballistics_silencer_shots(qa_q2w_ballistics *b, qa_actor_id actor) {
  size_t i = find_double(b->silencer_charges, b->silencer_count, actor);
  return i < b->silencer_count ? b->silencer_charges[i].value : 0;
}
void qa_q2w_ballistics_grant_silencer(qa_q2w_ballistics *b, qa_actor_id actor, qa_q2_game_services *game, double charges) {
  if (!qa_session_actor_registry_is_live(game->host->actors, actor)) qa_raise("Error", 0, 0, "Silencer owner is not live");
  if (!(charges >= 0 && charges <= 9007199254740991.0 && trunc(charges) == charges))
    qa_range_error("Silencer charges must be a nonnegative integer");
  qa_q2w_ballistics_track_actors(b, game);
  (void)set_double(&b->silencer_charges, &b->silencer_count, &b->silencer_cap, actor,
                   qa_q2w_ballistics_silencer_shots(b, actor) + charges);
}
void qa_q2w_ballistics_reset_silencer(qa_q2w_ballistics *b, qa_actor_id actor) {
  del_double(b->silencer_charges, &b->silencer_count, actor);
}
double qa_q2w_ballistics_shot_mask(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game) {
  const qa_q2w_input *in = qa_q2w_ballistics_input(b, self.id);
  return qa_q2w_actor_shot_mask(game, in == NULL || in->players_collide);
}
void qa_q2w_ballistics_player_noise(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 origin, const char *kind) {
  qa_q2w_ballistics_player_noise_for_actor(b, self.id, game, origin, kind);
}
void qa_q2w_ballistics_player_noise_for_actor(qa_q2w_ballistics *b, qa_actor_id owner, qa_q2_game_services *game, qa_vec3 origin, const char *kind) {
  if (!qa_session_actor_registry_is_live(game->host->actors, owner) || !game->host->is_player(game->host->ctx, owner)) return;
  qa_q2w_ballistics_track_actors(b, game);
  double charges = qa_q2w_ballistics_silencer_shots(b, owner);
  bool weapon = strcmp(kind, "weapon") == 0;
  bool impact = strcmp(kind, "impact") == 0;
  if (weapon) {
    if (rerelease(game)) {
      qa_q2w_event e; memset(&e, 0, sizeof e);
      e.kind = QA_Q2W_EVENT_INVISIBILITY_REVEAL; e.actor = &owner; e.until = now_of(game) + (charges > 0 ? 0.4 : 2);
      b->hooks.emit(b->hooks.ctx, e);
    }
    if (charges > 0) {
      (void)set_double(&b->silencer_charges, &b->silencer_count, &b->silencer_cap, owner, charges - 1);
      return;
    }
  }
  const qa_q2w_input *in = qa_q2w_ballistics_input(b, owner);
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH || (in && in->notarget)) return;
  /* FL_DISGUISED: only a weapon shot is noticed, and it names the shooter as the next target for half a second. */
  qa_q2_entity *entity = game->entity(game->ctx, &owner);
  const bool disguised = entity != NULL && (qa_js_to_int32(entity->flags) & 0x8000) != 0;
  if (disguised) {
    if (!weapon) return;
    game->has_disguise_violation = true;
    game->disguise_violation_actor = owner;
    game->disguise_violation_until = now_of(game) + 0.5;
  } else {
    qa_q2_monster_target_result mt = game->monster_target(game->ctx, &owner);
    if (mt.has_value && mt.value.notarget) return;
  }
  b->hooks.noise(b->hooks.ctx, owner, origin, impact);
}
static void impact_noise(qa_q2w_ballistics *b, qa_q2_entity *projectile, qa_q2_game_services *game) {
  if (!projectile->has_owner) return;
  qa_q2w_ballistics_player_noise_for_actor(b, projectile->owner, game, game->body(game->ctx, projectile->actor).origin, "impact");
}

void qa_q2w_ballistics_check_dodge(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, double speed) {
  if (rerelease(game) || !game->host->is_player(game->host->ctx, self.id)) return;
  if (game->options.skill == 0 && rnd_of(game) > 0.25) return;
  qa_trace_result trace = do_trace(game, start, qa_add3_double(start, qa_scale3_double(direction, 8192)), NULL, &self.id, NULL, 0,
                                   qa_q2w_ballistics_shot_mask(b, self, game));
  qa_actor_id target;
  if (!trace_hit_actor(trace, &target) || !game->host->is_monster(game->host->ctx, target) || combat_health(game, target) <= 0) return;
  qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, target);
  if (!body.has_value) return;
  qa_vec3 to_self = qa_normalize3_double_hypot(qa_sub3_double(game->body(game->ctx, self).origin, body.value.origin));
  if (qa_dot3_double(to_self, qa_math_angle_vectors_rule(body.value.angles, QA_ANGLE_RULE_NATIVE).forward) <= 0.3) return;
  b->hooks.dodge(b->hooks.ctx, target, self.id,
                 (qa_hypot3(qa_sub3_double(trace.end, start)) - body.value.bounds.max.x) / speed, trace);
}

static qa_vec3 spread_end(qa_q2_game_services *g, qa_vec3 origin, qa_vec3 aim, double hspread, double vspread, double factor) {
  qa_angle_vectors axes = qa_math_angle_vectors_rule(qa_vector_to_angles_with_rule(aim, QA_VECTOR_TO_ANGLES_NATIVE), QA_ANGLE_RULE_NATIVE);
  double r = (rnd_of(g) * 2 - 1) * hspread * factor;
  double u = (rnd_of(g) * 2 - 1) * vspread * factor;
  return qa_add3_double(qa_add3_double(qa_add3_double(origin, qa_scale3_double(axes.forward, 8192)),
                                       qa_scale3_double(axes.right, r)), qa_scale3_double(axes.up, u));
}

static void fire_lead(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                      double damage, double kick, double hspread, double vspread, double mod, const char *impact) {
  bool rr = rerelease(game);
  double shot_mask = qa_q2w_ballistics_shot_mask(b, self, game);
  bool has_water = (qa_js_to_uint32(game->host->point_contents(game->host->ctx, start)) & qa_js_to_uint32(QA_Q2W_WATER_MASK)) != 0;
  qa_vec3 water_start = start;
  uint32_t mask = qa_js_to_uint32(shot_mask) | (!has_water ? (uint32_t)QA_Q2W_WATER_MASK : 0u);
  qa_vec3 from = game->body(game->ctx, self).origin, end = start;
  qa_actor_id excluded[16];
  size_t excluded_count = 0;
  bool initial = true;
  qa_trace_result trace;
  memset(&trace, 0, sizeof trace);
  for (;;) {
    trace = do_trace(game, from, end, NULL, &self.id, excluded, excluded_count, (double)(initial && !rr ? qa_js_to_uint32(shot_mask) : mask));
    if (trace.fraction == 1 && initial) { initial = false; excluded_count = 0; from = start; end = spread_end(game, start, direction, hspread, vspread, 1); continue; }
    if (trace.fraction == 1) break;
    if ((qa_js_to_uint32(trace_contents(trace)) & qa_js_to_uint32(QA_Q2W_WATER_MASK)) != 0 && (mask & qa_js_to_uint32(QA_Q2W_WATER_MASK)) != 0) {
      water_start = trace.end; has_water = true;
      if (qa_hypot3(qa_sub3_double(start, trace.end)) != 0) {
        uint32_t c = qa_js_to_uint32(trace_contents(trace));
        const char *brown = rr ? "brwater" : "*brwater";
        double color = (c & 32) != 0 ? (strcmp(trace.kind, "q2") == 0 && trace.surface && strcmp(trace.surface->name, brown) == 0 ? 3 : 2) : (c & 16) != 0 ? 4 : 5;
        effect(game, "splash", trace.end, trace_normal(trace), 8, color);
        end = spread_end(game, trace.end, qa_sub3_double(end, start), hspread, vspread, 2);
      }
      mask &= ~qa_js_to_uint32(QA_Q2W_WATER_MASK);
      if (!rr) from = trace.end;
      continue;
    }
    qa_actor_id actor;
    bool has_actor = trace_hit_actor(trace, &actor);
    if ((rr || !trace_sky(trace)) && can_hurt(game, has_actor, actor)) {
      do_damage(game, actor, NULL, self.id, &self.id, damage, kick, direction, trace.end, trace_normal(trace), mod, true, 16, weapon_for_mod(mod));
      qa_combat_state_result state = qa_gameplay_authority_read(game->host->combat, actor);
      bool listed = false;
      for (size_t i = 0; i < excluded_count; i++) if (qa_same_actor(excluded[i], actor)) listed = true;
      if (rr && ((qa_js_to_uint32(trace_contents(trace)) & 0x4000000u) != 0 ||
                 (game->host->is_monster(game->host->ctx, actor) && state.has_value && state.value.health <= 0)) && !listed) {
        if (excluded_count == 16) break;
        excluded[excluded_count++] = actor;
        continue;
      }
    } else if (!trace_sky(trace)) {
      effect(game, impact, trace.end, trace_normal(trace), 0, 0);
      qa_q2w_ballistics_player_noise(b, self, game, trace.end, "impact");
    }
    break;
  }
  if (has_water) {
    qa_vec3 pos = qa_add3_double(trace.end, qa_scale3_double(qa_normalize3_double_hypot(qa_sub3_double(trace.end, water_start)), -2));
    qa_actor_id hit_actor;
    bool has_hit = trace_hit_actor(trace, &hit_actor);
    qa_vec3 water_end = (qa_js_to_uint32(game->host->point_contents(game->host->ctx, pos)) & qa_js_to_uint32(QA_Q2W_WATER_MASK)) != 0
      ? pos : do_trace(game, pos, water_start, NULL, has_hit ? &hit_actor : NULL, NULL, 0, QA_Q2W_WATER_MASK).end;
    qa_q2w_event e; memset(&e, 0, sizeof e);
    e.kind = QA_Q2W_EVENT_BEAM; e.effect = "bubble-trail"; e.actor = &self.id; e.start = water_start; e.end = water_end; e.duration = 0;
    b->hooks.emit(b->hooks.ctx, e);
  }
}
void qa_q2w_ballistics_fire_bullet(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                   double damage, double kick, double hspread, double vspread, double mod) {
  fire_lead(b, self, game, start, direction, damage, kick, hspread, vspread, mod, "gunshot");
}
void qa_q2w_ballistics_fire_shotgun(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                    double damage, double kick, double hspread, double vspread, double count, double mod) {
  for (double pellet = 0; pellet < count; pellet++) fire_lead(b, self, game, start, direction, damage, kick, hspread, vspread, mod, "shotgun");
}

void qa_q2w_ballistics_fire_rail(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, double damage, double kick) {
  qa_vec3 end = qa_add3_double(start, qa_scale3_double(direction, 8192));
  exclusion excluded;
  exclusion_init(&excluded);
  qa_vec3 from = start;
  qa_actor_id ignore = self.id;
  bool water = false;
  uint32_t mask = qa_js_to_uint32(qa_q2w_ballistics_shot_mask(b, self, game)) | 8u | 16u;
  qa_trace_result trace;
  memset(&trace, 0, sizeof trace);
  for (;;) {
    trace = do_trace(game, from, end, NULL, &ignore, excluded.items, excluded.count, (double)mask);
    if (trace.fraction == 1) break;
    if ((qa_js_to_uint32(trace_contents(trace)) & mask & (8u | 16u)) != 0) { mask &= ~(8u | 16u); water = true; }
    else {
      qa_actor_id actor;
      if (!trace_hit_actor(trace, &actor)) break;
      if (!qa_same_actor(actor, self.id) && can_hurt(game, true, actor))
        do_damage(game, actor, NULL, self.id, &self.id, damage, kick, direction, trace.end, trace_normal(trace), QA_Q2W_MOD_RAILGUN, true, 0, "q2:weapon_railgun");
      qa_q2_weapon_target_result entity = game->weapon_target(game->ctx, actor);
      bool pierce = game->host->is_monster(game->host->ctx, actor) || game->host->is_player(game->host->ctx, actor)
        || (entity.has_value && strcmp(entity.value.solid, "box") == 0)
        || (rerelease(game) && ((entity.has_value && entity.value.damageable_target) ||
            (entity.has_value && strcmp(entity.value.solid, "none") == 0) ||
            (entity.has_value && strcmp(entity.value.solid, "trigger") == 0) ||
            !qa_session_actor_registry_is_live(game->host->actors, actor)));
      if (!pierce || exclusion_has(&excluded, actor)) break;
      if (rerelease(game) && excluded.count == 16) break;
      exclusion_push(&excluded, actor);
      if (!rerelease(game)) ignore = actor;
    }
    if (!rerelease(game)) from = trace.end;
  }
  exclusion_destroy(&excluded);
  qa_q2w_event e; memset(&e, 0, sizeof e);
  e.kind = QA_Q2W_EVENT_BEAM; e.effect = "rail"; e.actor = &self.id; e.start = start; e.end = trace.end;
  b->hooks.emit(b->hooks.ctx, e);
  if (water && !rerelease(game)) { e.effect = "rail-water"; b->hooks.emit(b->hooks.ctx, e); }
  qa_q2w_ballistics_player_noise(b, self, game, trace.end, "impact");
}

bool qa_q2w_ballistics_fire_hit(qa_q2w_ballistics *b, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 aim, double damage, double kick) {
  if (!self->has_enemy) qa_raise("Error", 0, 0, "Q2 fireHit requires an enemy");
  qa_actor_id enemy = self->enemy;
  qa_body_state_result target_body = qa_shared_body_table_read(game->host->bodies, enemy);
  if (!target_body.has_value) return false;
  qa_body_state body = game->body(game->ctx, self->actor);
  qa_vec3 delta = qa_sub3_double(target_body.value.origin, body.origin);
  double range = qa_hypot3(delta), side = aim.y;
  bool rr = rerelease(game);
  qa_vec3 min = qa_add3_double(target_body.value.origin, target_body.value.bounds.min);
  qa_vec3 max = qa_add3_double(target_body.value.origin, target_body.value.bounds.max);
  if (rr) {
    qa_vec3 smin = qa_add3_double(body.origin, body.bounds.min), smax = qa_add3_double(body.origin, body.bounds.max);
    double dx = qa_js_max(0, qa_js_max(min.x - smax.x, smin.x - max.x));
    double dy = qa_js_max(0, qa_js_max(min.y - smax.y, smin.y - max.y));
    double dz = qa_js_max(0, qa_js_max(min.z - smax.z, smin.z - max.z));
    range = qa_hypot3(v3(dx, dy, dz)); /* TS Math.hypot */
  }
  if (range > aim.x) return false;
  if (side > body.bounds.min.x && side < body.bounds.max.x) { if (!rr) range -= target_body.value.bounds.max.x; }
  else side = side < 0 ? target_body.value.bounds.min.x : target_body.value.bounds.max.x;
  qa_vec3 point = rr ? v3(qa_js_max(min.x, qa_js_min(max.x, body.origin.x)), qa_js_max(min.y, qa_js_min(max.y, body.origin.y)), qa_js_max(min.z, qa_js_min(max.z, body.origin.z)))
                     : qa_add3_double(body.origin, qa_scale3_double(delta, range));
  qa_actor_id target = enemy;
  double mask = qa_q2w_ballistics_shot_mask(b, self->actor, game);
  qa_trace_result traces[2];
  size_t trace_count = 0;
  traces[trace_count++] = do_trace(game, body.origin, point, NULL, &self->actor.id, NULL, 0, mask);
  if (rr) traces[trace_count++] = do_trace(game, point, target_body.value.origin, NULL, &self->actor.id, NULL, 0, mask);
  for (size_t i = 0; i < trace_count; i++) {
    if (traces[i].fraction < 1) {
      qa_actor_id actor;
      bool has = trace_hit_actor(traces[i], &actor);
      if (!can_hurt(game, has, actor)) return false;
      target = (game->host->is_monster(game->host->ctx, actor) || game->host->is_player(game->host->ctx, actor)) ? enemy : actor;
    }
  }
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 impact = qa_add3_double(qa_add3_double(qa_add3_double(body.origin, qa_scale3_double(axes.forward, range)),
                                                 qa_scale3_double(axes.right, side)), qa_scale3_double(axes.up, aim.z));
  do_damage(game, target, self, self->actor.id, &self->actor.id, damage, trunc(kick / 2),
            qa_sub3_double(impact, target_body.value.origin), impact, zv(), QA_Q2W_MOD_HIT, true, 8, NULL);
  if (!game->host->is_monster(game->host->ctx, target) && !game->host->is_player(game->host->ctx, target)) return false;
  qa_owned_actor owned; bool has_owned = qa_session_actor_registry_resolve_owned(game->host->actors, enemy, &owned);
  qa_body_state_result current = qa_shared_body_table_read(game->host->bodies, enemy);
  if (has_owned && current.has_value) {
    qa_vec3 center = qa_add3_double(current.value.origin, qa_scale3_double(qa_add3_double(current.value.bounds.min, current.value.bounds.max), 0.5));
    qa_vec3 velocity = qa_add3_double(current.value.velocity, qa_scale3_double(qa_normalize3_double_hypot(qa_sub3_double(center, impact)), kick));
    qa_body_state next = current.value;
    next.velocity = velocity;
    if (velocity.z > 0) next.ground = NULL;
    qa_shared_body_table_write(game->host->bodies, owned, next);
  }
  return true;
}

static qa_q2_entity *projectile_for_actor(qa_q2w_ballistics *b, qa_actor_id owner, qa_q2_game_services *game, const char *classname,
                                           qa_vec3 start, qa_vec3 direction, double speed, const char *model, double fx, double clip_mask) {
  qa_q2w_ballistics_register_callbacks(b, game);
  qa_q2_entity *p = game->create(game->ctx, classname, NULL, 0);
  p->owner = owner; p->has_owner = true; p->model = model; p->effects = fx;
  p->clip_mask = clip_mask; p->projectile = true;
  p->dodgeable = rerelease(game);
  if (p->dodgeable) qa_q2_game_mark_dodgeable(game, p);
  p->movedir = direction;
  qa_q2_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_origin = true; c.origin = start;
  c.has_angles = true; c.angles = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE);
  c.has_velocity = true; c.velocity = qa_scale3_double(direction, speed);
  c.has_bounds = true; c.bounds = (qa_bounds){zv(), zv()};
  move_parts(game, p, c, true, false);
  return p;
}
static qa_q2_entity *projectile(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, const char *classname,
                                 qa_vec3 start, qa_vec3 direction, double speed, const char *model, double fx) {
  return projectile_for_actor(b, self.id, game, classname, start, direction, speed, model, fx,
                              qa_q2w_ballistics_shot_mask(b, self, game));
}
static qa_weapon_trajectory_update_result launch_behavior(qa_q2w_ballistics *b, qa_q2_entity *p, qa_q2_game_services *game,
                                                          const char *weapon, const char *role) {
  qa_weapon_trajectory_update_result none = {false, {zv(), zv(), zv()}};
  (void)b;
  if (!p->has_owner || !game->host->is_player(game->host->ctx, p->owner)) return none;
  if (game->host->weapon_behavior == NULL) return none;
  qa_weapon_behavior_launch in;
  memset(&in, 0, sizeof in);
  in.projectile = p->actor; in.shooter = p->owner; in.weapon = weapon; in.role = role;
  in.time_seconds = now_of(game); in.body = game->body(game->ctx, p->actor);
  qa_weapon_trajectory_update_result update = game->host->weapon_behavior->launch(game->host->weapon_behavior->ctx, in);
  if (update.has_value) game->project_trajectory(game->ctx, p, update.value);
  return update;
}
static void loop_sound(qa_q2_entity *p, qa_q2_game_services *game, const char *path, bool start) {
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "sound"; e.has_actor = true; e.actor = p->actor.id; e.origin = game->body(game->ctx, p->actor).origin;
  e.path = path; e.channel = 0; e.volume = 1; e.attenuation = 1; e.reliable = false; e.loop = start ? "start" : "stop";
  game->host->emit(game->host->ctx, e);
}
static qa_q2_think free_think(void) { return QA_Q2_FREE_ENTITY; }
static void blaster_impact(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, bool has_other, qa_actor_id other, qa_vec3 plane, bool sky_hit);

qa_q2_entity *qa_q2w_ballistics_fire_blaster(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                              double damage, double speed, double fx, bool hyper, double mod) {
  qa_vec3 dir = !rerelease(game) ? qa_normalize3_double_hypot(direction) : direction;
  qa_q2_entity *bolt = projectile(b, self, game, "bolt", start, dir, speed, "models/objects/laser/tris.md2", fx);
  bolt->damage = damage; bolt->server_flags = (double)(qa_js_to_int32(bolt->server_flags) | 2);
  (void)set_double(&b->blaster_causes, &b->blaster_cause_count, &b->blaster_cause_cap, bolt->actor.id, mod);
  bolt->touch = (qa_q2_touch){b, blaster_touch};
  game->schedule(game->ctx, bolt, 2, free_think());
  game->solid(game->ctx, bolt, "box");
  game->motion(game->ctx, bolt, "fly-missile");
  qa_weapon_trajectory_update_result trajectory = launch_behavior(b, bolt, game, hyper ? "q2:weapon_hyperblaster" : "q2:weapon_blaster", "bolt");
  qa_vec3 launch_origin = game->body(game->ctx, bolt->actor).origin;
  qa_vec3 launch_direction = !trajectory.has_value ? dir : qa_normalize3_double_hypot(trajectory.value.velocity);
  game->show(game->ctx, bolt);
  loop_sound(bolt, game, "misc/lasfly.wav", true);
  qa_q2w_ballistics_check_dodge(b, self, game, start, dir, speed);
  qa_trace_result trace = do_trace(game, game->body(game->ctx, self).origin, launch_origin, NULL, &bolt->actor.id, NULL, 0, bolt->clip_mask);
  if (trace.fraction < 1) {
    qa_q2_body_changes c;
    memset(&c, 0, sizeof c);
    c.has_origin = true;
    c.origin = !rerelease(game) ? qa_add3_double(launch_origin, qa_scale3_double(launch_direction, -10))
                                : qa_add3_double(trace.end, trace_normal(trace));
    move_parts(game, bolt, c, false, false);
    qa_actor_id hit_actor;
    bool has_hit = trace_hit_actor(trace, &hit_actor);
    blaster_impact(b, bolt, game, has_hit, hit_actor,
                   !rerelease(game) ? zv() : trace_normal(trace), rerelease(game) && trace_sky(trace));
  }
  return bolt;
}
qa_q2_entity *qa_q2w_ballistics_fire_blaster_default(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                                      double damage, double speed, double fx, bool hyper) {
  return qa_q2w_ballistics_fire_blaster(b, self, game, start, direction, damage, speed, fx, hyper,
                                        hyper ? QA_Q2W_MOD_HYPERBLASTER : QA_Q2W_MOD_BLASTER);
}

static qa_q2_entity *launch_grenade(qa_q2w_ballistics *b, qa_actor_id owner, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                     double damage, double speed, double timer, double radius, bool hand, bool held, bool monster,
                                     double owner_gravity, double clip_mask, const qa_q2w_grenade_adjustment *adjustment) {
  bool rr = rerelease(game);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE), QA_ANGLE_RULE_NATIVE);
  /* TS `models/objects/${model}/tris.md2` (four possible values). */
  static const char *const models[] = {"models/objects/grenade/tris.md2", "models/objects/grenade2/tris.md2",
                                       "models/objects/grenade3/tris.md2", "models/objects/grenade4/tris.md2"};
  const char *model_str = models[hand ? (rr ? 2 : 1) : (rr && !monster ? 3 : 0)];
  qa_q2_entity *gr = projectile_for_actor(b, owner, game, hand ? (rr ? "hand_grenade" : "hgrenade") : "grenade", start, direction, speed,
                                           model_str, 32 + (rr && monster && !hand ? 137438953472.0 : 0), clip_mask);
  gr->damage_radius = radius; gr->damage = damage; gr->speed = speed;
  gr->spawnflags = hand ? (held ? 3 : 1) : 0;
  double gravity = rr ? owner_gravity / 800 : 1;
  double up = (adjustment ? adjustment->up : 200 + (rnd_of(game) * 2 - 1) * 10) * gravity;
  double right = adjustment ? adjustment->right : (rnd_of(game) * 2 - 1) * 10;
  qa_q2_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_velocity = true;
  c.velocity = qa_add3_double(qa_add3_double(qa_scale3_double(direction, speed), qa_scale3_double(axes.up, up)),
                              qa_scale3_double(axes.right, right));
  move_parts(game, gr, c, true, false);
  if (rr && (hand || monster)) {
    double ax = (rnd_of(game) * 2 - 1) * 360, ay = (rnd_of(game) * 2 - 1) * 360, az = (rnd_of(game) * 2 - 1) * 360;
    gr->angular_velocity = v3(ax, ay, az);
  } else if (rr) gr->angular_velocity = zv();
  else gr->angular_velocity = v3(300, 300, 300);
  gr->touch = (qa_q2_touch){b, grenade_touch};
  if (rr && !hand && !monster) {
    gr->timestamp = now_of(game) + timer;
    gr->render_flags = (double)(qa_js_to_int32(qa_js_to_uint32(gr->render_flags) | 1u));
    qa_q2_body_changes ac;
    memset(&ac, 0, sizeof ac);
    ac.has_angles = true;
    ac.angles = qa_vector_to_angles_with_rule(game->body(game->ctx, gr->actor).velocity, QA_VECTOR_TO_ANGLES_NATIVE);
    move_parts(game, gr, ac, true, false);
    game->schedule(game->ctx, gr, game->host->frame_seconds(game->host->ctx), (qa_q2_think){b, grenade_update});
  } else {
    game->schedule(game->ctx, gr, qa_js_max(0, timer), (qa_q2_think){b, grenade_explode});
  }
  if (hand) loop_sound(gr, game, "weapons/hgrenc1b.wav", true);
  if (hand && timer <= 0) {
    grenade_explode(b, gr, game);
  } else {
    if (hand) {
      qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, owner);
      if (!body.has_value) qa_raise("Error", 0, 0, "Q2 grenade thrower has no shared body");
      qa_q2_presentation_event e;
      memset(&e, 0, sizeof e);
      e.kind = "sound"; e.has_actor = true; e.actor = owner; e.origin = body.value.origin; e.path = "weapons/hgrent1a.wav";
      e.channel = 1; e.volume = 1; e.attenuation = 1; e.loop = "once";
      game->host->emit(game->host->ctx, e);
    }
    game->solid(game->ctx, gr, "box");
    game->motion(game->ctx, gr, "bounce");
    (void)launch_behavior(b, gr, game, hand ? "q2:ammo_grenades" : "q2:weapon_grenadelauncher", "grenade");
    game->show(game->ctx, gr);
  }
  return gr;
}
qa_q2_entity *qa_q2w_ballistics_fire_grenade(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                              double damage, double speed, double timer, double radius, bool hand, bool held, bool monster,
                                              const qa_q2w_grenade_adjustment *adjustment) {
  const qa_q2w_input *in = qa_q2w_ballistics_input(b, self.id);
  return launch_grenade(b, self.id, game, start, direction, damage, speed, timer, radius, hand, held, monster,
                        adjustment ? adjustment->gravity : (in ? in->gravity : 800),
                        qa_q2w_ballistics_shot_mask(b, self, game), adjustment);
}
qa_q2_entity *qa_q2w_ballistics_fire_hand_grenade(qa_q2w_ballistics *b, qa_actor_id owner, qa_q2_game_services *game, qa_q2w_hand_grenade_launch spec) {
  return launch_grenade(b, owner, game, spec.start, spec.direction, spec.damage, spec.speed, spec.timer, spec.radius,
                        true, spec.held, false, spec.gravity, qa_q2w_actor_shot_mask(game, spec.players_collide), NULL);
}
qa_q2_entity *qa_q2w_ballistics_fire_rocket(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                             double damage, double speed, double radius, double radius_damage) {
  qa_q2_entity *rocket = projectile(b, self, game, "rocket", start, direction, speed, "models/objects/rocket/tris.md2", 16);
  rocket->damage = damage; rocket->damage_radius = radius; rocket->radius_damage = radius_damage;
  rocket->touch = (qa_q2_touch){b, rocket_touch};
  game->schedule(game->ctx, rocket, 8000 / speed, free_think());
  game->solid(game->ctx, rocket, "box");
  game->motion(game->ctx, rocket, "fly-missile");
  (void)launch_behavior(b, rocket, game, "q2:weapon_rocketlauncher", "rocket");
  game->show(game->ctx, rocket);
  loop_sound(rocket, game, "weapons/rockfly.wav", true);
  qa_q2w_ballistics_check_dodge(b, self, game, start, direction, speed);
  return rocket;
}
static void debris(qa_q2_entity *source, qa_q2_game_services *game) {
  qa_q2_entity *piece = game->create(game->ctx, "debris", NULL, 0);
  qa_body_state body = game->body(game->ctx, source->actor);
  double rx = rnd_of(game) * 2 - 1, ry = rnd_of(game) * 2 - 1, rz = rnd_of(game) * 2 - 1;
  piece->model = "models/objects/debris2/tris.md2";
  qa_q2_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_origin = true; c.origin = body.origin;
  c.has_velocity = true;
  c.velocity = qa_add3_double(body.velocity, qa_scale3_double(v3(100 * rx, 100 * ry, 100 + 100 * rz), 2));
  move_parts(game, piece, c, true, false);
  {
    double ax = rnd_of(game) * 600, ay = rnd_of(game) * 600, az = rnd_of(game) * 600;
    piece->angular_velocity = v3(ax, ay, az);
  }
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  state.armor.powered.kind = "none";
  state.can_take_damage = true;
  qa_gameplay_authority_create_state(game->host->combat, piece->actor, state, NULL, NULL);
  piece->die = QA_Q2_FREE_ENTITY_DIE;
  game->motion(game->ctx, piece, "bounce");
  game->solid(game->ctx, piece, "none");
  game->show(game->ctx, piece);
  game->schedule(game->ctx, piece, 5 + rnd_of(game) * 5, free_think());
}
qa_q2_entity *qa_q2w_ballistics_fire_bfg(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                           double damage, double speed, double radius) {
  qa_q2_entity *bfg = projectile(b, self, game, "bfg blast", start, direction, speed, "sprites/s_bfg1.sp2", 128 | 8192);
  bfg->dodgeable = false; bfg->damage = damage; bfg->damage_radius = radius;
  bfg->touch = (qa_q2_touch){b, bfg_touch};
  qa_q2w_ballistics_check_dodge(b, self, game, start, direction, speed);
  game->schedule(game->ctx, bfg, game->host->frame_seconds(game->host->ctx), (qa_q2_think){b, bfg_think});
  game->solid(game->ctx, bfg, "box");
  game->motion(game->ctx, bfg, "fly-missile");
  (void)launch_behavior(b, bfg, game, "q2:weapon_bfg", "energy");
  game->show(game->ctx, bfg);
  loop_sound(bfg, game, "weapons/bfg__l1a.wav", true);
  return bfg;
}

static bool bfg_target(qa_q2_game_services *game, qa_actor_id actor) {
  qa_q2_weapon_target_result wt = game->weapon_target(game->ctx, actor);
  return game->host->is_monster(game->host->ctx, actor) || game->host->is_player(game->host->ctx, actor)
      || (wt.has_value && wt.value.bfg_explobox)
      || (rerelease(game) && wt.has_value && wt.value.damageable_target);
}
static void bfg_ambient(qa_q2w_ballistics *b, qa_q2_entity *e, qa_q2_game_services *game) {
  double theta = rnd_of(game) * 2 * 3.141592653589793;
  double phi = acos(rnd_of(game) * 2 - 1);
  qa_vec3 direction = v3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
  qa_vec3 start = game->body(game->ctx, e->actor).origin;
  qa_trace_result trace = do_trace(game, start, qa_add3_double(start, qa_scale3_double(direction, 256)), NULL, &e->actor.id, NULL, 0, 1 | 16 | 8);
  if (trace.fraction < 1) {
    qa_q2w_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = QA_Q2W_EVENT_BEAM; ev.effect = "bfg-lightning"; ev.actor = &e->actor.id; ev.start = start; ev.end = trace.end; ev.duration = 0.3;
    b->hooks.emit(b->hooks.ctx, ev);
  }
}

static void grenade_explode(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2w_ballistics *b = ctx;
  bool hand = (qa_js_to_uint32(e->spawnflags) & 1u) != 0, held = (qa_js_to_uint32(e->spawnflags) & 2u) != 0;
  impact_noise(b, e, g);
  qa_body_state body = g->body(g->ctx, e->actor);
  if (e->has_enemy) {
    qa_vec3 center;
    qa_body_state_result target_body = qa_shared_body_table_read(g->host->bodies, e->enemy);
    if (centroid(g, e->enemy, &center) && target_body.has_value) {
      double points = trunc(e->damage - 0.5 * qa_hypot3(qa_sub3_double(body.origin, center)));
      do_damage(g, e->enemy, e, e->actor.id, e->has_owner ? &e->owner : NULL, points, points,
                qa_sub3_double(target_body.value.origin, body.origin), body.origin, zv(),
                hand ? QA_Q2W_MOD_HAND_GRENADE : QA_Q2W_MOD_GRENADE, true, 1, hand ? "q2:ammo_grenades" : "q2:weapon_grenadelauncher");
    }
  }
  do_radius(g, e, e->has_owner ? &e->owner : NULL, e->damage, e->has_enemy ? &e->enemy : NULL, e->damage_radius,
            held ? QA_Q2W_MOD_HELD_GRENADE : hand ? QA_Q2W_MOD_HAND_GRENADE_SPLASH : QA_Q2W_MOD_GRENADE_SPLASH,
            true, 0, hand ? "q2:ammo_grenades" : "q2:weapon_grenadelauncher");
  bool wet = (qa_js_to_uint32(g->host->point_contents(g->host->ctx, body.origin)) & qa_js_to_uint32(QA_Q2W_WATER_MASK)) != 0;
  const char *name = body.ground == NULL ? (wet ? "rocket-explosion-water" : "rocket-explosion")
                                         : (wet ? "grenade-explosion-water" : "grenade-explosion");
  effect(g, name, qa_add3_double(body.origin, qa_scale3_double(body.velocity, -0.02)), zv(), 0, 0);
  g->remove(g->ctx, e);
}
static void grenade_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2w_ballistics *b = ctx;
  bool hand = (qa_js_to_uint32(e->spawnflags) & 1u) != 0;
  if (e->has_owner && qa_same_actor(contact.other, e->owner)) return;
  if ((qa_js_to_uint32((contact.surface ? contact.surface->native_flags : 0)) & 4u) != 0) { g->remove(g->ctx, e); return; }
  if (!can_hurt(g, true, contact.other)) {
    g->sound(g->ctx, e->actor, hand ? (rnd_of(g) > 0.5 ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav") : "weapons/grenlb1b.wav", 2, 1, 1);
    return;
  }
  e->enemy = contact.other; e->has_enemy = true;
  grenade_explode(b, e, g);
}
static void grenade_update(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2w_ballistics *b = ctx;
  if (now_of(g) >= e->timestamp) { grenade_explode(b, e, g); return; }
  qa_body_state body = g->body(g->ctx, e->actor);
  double vsq = qa_dot3_double(body.velocity, body.velocity);
  if (vsq != 0) {
    double fraction = qa_js_min(1, vsq / (e->speed * e->speed));
    qa_vec3 angles = qa_vector_to_angles_with_rule(body.velocity, QA_VECTOR_TO_ANGLES_NATIVE);
    qa_q2_body_changes c;
    memset(&c, 0, sizeof c);
    c.has_angles = true;
    c.angles = v3(qa_lerp_angle_with_rule(body.angles.x, angles.x, fraction, QA_LERP_ANGLE_DELTA), angles.y,
                  body.angles.z + g->host->frame_seconds(g->host->ctx) * 360 * fraction);
    move_parts(g, e, c, false, false);
  }
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){b, grenade_update});
}
static void rocket_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2w_ballistics *b = ctx;
  if (e->has_owner && qa_same_actor(contact.other, e->owner)) return;
  if ((qa_js_to_uint32((contact.surface ? contact.surface->native_flags : 0)) & 4u) != 0) { g->remove(g->ctx, e); return; }
  impact_noise(b, e, g);
  qa_body_state body = g->body(g->ctx, e->actor);
  qa_vec3 plane = contact.plane ? contact.plane->normal : zv();
  if (can_hurt(g, true, contact.other))
    do_damage(g, contact.other, e, e->actor.id, e->has_owner ? &e->owner : NULL, e->damage, 0, body.velocity, body.origin, plane,
              QA_Q2W_MOD_ROCKET, true, 0, "q2:weapon_rocketlauncher");
  else if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER && contact.surface != NULL
           && (qa_js_to_uint32(contact.surface->native_flags) & (8u | 16u | 32u | 64u)) == 0) {
    double count = floor(rnd_of(g) * 5);
    for (double i = 0; i < count; i++) debris(e, g);
  }
  do_radius(g, e, e->has_owner ? &e->owner : NULL, e->radius_damage, &contact.other, e->damage_radius,
            QA_Q2W_MOD_ROCKET_SPLASH, true, 0, "q2:weapon_rocketlauncher");
  bool wet = (qa_js_to_uint32(g->host->point_contents(g->host->ctx, body.origin)) & qa_js_to_uint32(QA_Q2W_WATER_MASK)) != 0;
  effect(g, wet ? "rocket-explosion-water" : "rocket-explosion",
         !rerelease(g) ? qa_add3_double(body.origin, qa_scale3_double(body.velocity, -0.02)) : qa_add3_double(body.origin, plane), zv(), 0, 0);
  g->remove(g->ctx, e);
}
static void bfg_explode(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2w_ballistics *b = ctx;
  if (rerelease(g)) bfg_ambient(b, e, g);
  if (e->frame == 0) {
    size_t count = 0;
    const qa_actor_id *actors = g->host->nearby(g->host->ctx, g->body(g->ctx, e->actor).origin, e->damage_radius, &count);
    for (size_t i = 0; i < count; i++) {
      qa_actor_id actor = actors[i];
      if ((e->has_owner && qa_same_actor(actor, e->owner)) || !can_hurt(g, true, actor) || !g->can_damage(g->ctx, actor, e->actor)) continue;
      qa_owned_actor owner;
      bool has_owner = e->has_owner && qa_session_actor_registry_resolve_owned(g->host->actors, e->owner, &owner);
      if (has_owner && qa_shared_body_table_read(g->host->bodies, owner.id).has_value
          && !g->can_damage(g->ctx, actor, owner)) continue;
      if (rerelease(g) && (!bfg_target(g, actor) || !b->hooks.can_target(b->hooks.ctx, e->has_owner ? &e->owner : NULL, actor))) continue;
      qa_vec3 center;
      qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, actor);
      if (!centroid(g, actor, &center) || !body.has_value) continue;
      double distance = qa_hypot3(qa_sub3_double(g->body(g->ctx, e->actor).origin, center));
      double points = trunc(e->damage * (1 - sqrt(distance / e->damage_radius)));
      if (!rerelease(g)) effect(g, "bfg-explosion", body.value.origin, zv(), 0, 0);
      qa_actor_id inf_actor = e->actor.id;
      const qa_actor_id * attacker = e->has_owner ? &e->owner : NULL;
      qa_vec3 dir = g->body(g->ctx, e->actor).velocity;
      qa_vec3 point = !rerelease(g) ? body.value.origin : center;
      qa_vec3 normal = zv();
      do_damage(g, actor, e, inf_actor, attacker, points, 0, dir,
                point, normal, QA_Q2W_MOD_BFG_EFFECT, true, 4, "q2:weapon_bfg");
      if (rerelease(g)) {
        qa_q2w_event ev; memset(&ev, 0, sizeof ev);
        ev.kind = QA_Q2W_EVENT_BEAM; ev.effect = "bfg-zap"; ev.actor = &e->actor.id;
        ev.start = g->body(g->ctx, e->actor).origin; ev.end = center;
        b->hooks.emit(b->hooks.ctx, ev);
      }
    }
  }
  e->frame++;
  g->show(g->ctx, e);
  g->schedule(g->ctx, e, 0.1, e->frame == 5 ? free_think() : (qa_q2_think){b, bfg_explode});
}
static void bfg_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2w_ballistics *b = ctx;
  if (e->has_owner && qa_same_actor(contact.other, e->owner)) return;
  if ((qa_js_to_uint32((contact.surface ? contact.surface->native_flags : 0)) & 4u) != 0) { g->remove(g->ctx, e); return; }
  impact_noise(b, e, g);
  qa_body_state body = g->body(g->ctx, e->actor);
  double energy = rerelease(g) ? 4 : 0;
  if (can_hurt(g, true, contact.other))
    do_damage(g, contact.other, e, e->actor.id, e->has_owner ? &e->owner : NULL, 200, 0, body.velocity, body.origin,
              contact.plane ? contact.plane->normal : zv(), QA_Q2W_MOD_BFG_BLAST, true, energy, "q2:weapon_bfg");
  do_radius(g, e, e->has_owner ? &e->owner : NULL, 200, &contact.other, 100, QA_Q2W_MOD_BFG_BLAST, true, energy, "q2:weapon_bfg");
  g->sound(g->ctx, e->actor, "weapons/bfg__x1b.wav", 2, 1, 1);
  loop_sound(e, g, "weapons/bfg__l1a.wav", false);
  e->touch = (qa_q2_touch){NULL, NULL};
  e->enemy = contact.other; e->has_enemy = true;
  e->model = "sprites/s_bfg3.sp2"; e->frame = 0;
  e->effects = (double)(qa_js_to_int32(qa_js_to_uint32(e->effects) & ~8192u));
  qa_q2_body_changes c;
  memset(&c, 0, sizeof c);
  c.has_origin = true; c.origin = qa_add3_double(body.origin, qa_scale3_double(body.velocity, -g->host->frame_seconds(g->host->ctx)));
  c.has_velocity = true; c.velocity = zv();
  move_parts(g, e, c, false, false);
  g->solid(g->ctx, e, "none");
  g->motion(g->ctx, e, "stationary");
  g->show(g->ctx, e);
  effect(g, "bfg-bigexplosion", g->body(g->ctx, e->actor).origin, zv(), 0, 0);
  g->schedule(g->ctx, e, 0.1, (qa_q2_think){b, bfg_explode});
}
static void bfg_think(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2w_ballistics *b = ctx;
  if (rerelease(g)) bfg_ambient(b, e, g);
  qa_vec3 origin = g->body(g->ctx, e->actor).origin;
  size_t count = 0;
  const qa_actor_id *actors = g->host->nearby(g->host->ctx, origin, 256, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    if (qa_same_actor(actor, e->actor.id) || (e->has_owner && qa_same_actor(actor, e->owner)) || !can_hurt(g, true, actor) || !bfg_target(g, actor)) continue;
    if (rerelease(g) && !b->hooks.can_target(b->hooks.ctx, e->has_owner ? &e->owner : NULL, actor)) continue;
    qa_vec3 center;
    if (!centroid(g, actor, &center)) continue;
    if (rerelease(g) && do_trace(g, origin, center, NULL, NULL, NULL, 0, 3).fraction < 1) continue;
    qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(center, origin));
    qa_vec3 end = qa_add3_double(origin, qa_scale3_double(direction, 2048));
    exclusion excluded;
    exclusion_init(&excluded);
    qa_vec3 from = origin;
    qa_actor_id ignore = e->actor.id;
    qa_trace_result trace;
    memset(&trace, 0, sizeof trace);
    double mask = (double)(qa_js_to_int32((1u | 0x2000000u | 0x4000000u) | (rerelease(g) ? (uint32_t)QA_Q2W_PLAYER_CONTENTS : 0u)));
    for (;;) {
      trace = do_trace(g, from, end, NULL, &ignore, excluded.items, excluded.count, mask);
      if (trace.fraction == 1) break;
      qa_actor_id target;
      bool has_target = trace_hit_actor(trace, &target);
      qa_q2_weapon_target_result wt = has_target ? g->weapon_target(g->ctx, target) : (qa_q2_weapon_target_result){false, {0, false, false, false}};
      if (has_target && !(e->has_owner && qa_same_actor(target, e->owner)) && can_hurt(g, true, target)
          && !(wt.has_value && wt.value.laser_immune))
        do_damage(g, target, e, e->actor.id, e->has_owner ? &e->owner : NULL,
                  g->options.mode == QA_Q2_MODE_DEATHMATCH ? 5 : 10, 1, direction, trace.end, zv(),
                  QA_Q2W_MOD_BFG_LASER, true, 4, "q2:weapon_bfg");
      if (!has_target || (!g->host->is_monster(g->host->ctx, target) && !g->host->is_player(g->host->ctx, target)
          && !(rerelease(g) && wt.has_value && wt.value.damageable_target))) {
        effect(g, "laser-sparks", trace.end, trace_normal(trace), 4, e->skin);
        break;
      }
      if (exclusion_has(&excluded, target) || (rerelease(g) && excluded.count == 16)) break;
      exclusion_push(&excluded, target);
      if (!rerelease(g)) { from = trace.end; ignore = target; }
    }
    exclusion_destroy(&excluded);
    qa_q2w_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = QA_Q2W_EVENT_BEAM; ev.effect = "bfg-laser"; ev.actor = &e->actor.id; ev.start = origin;
    ev.end = g->options.edition == QA_Q2_EDITION_RERELEASE ? center : trace.end;
    b->hooks.emit(b->hooks.ctx, ev);
  }
  g->schedule(g->ctx, e, 0.1, (qa_q2_think){b, bfg_think});
}
static void blaster_impact(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, bool has_other, qa_actor_id other, qa_vec3 plane, bool sky_hit) {
  qa_actor_id inf_actor;
  const qa_actor_id * attacker;
  double amount;
  qa_vec3 dir;
  qa_vec3 point;
  qa_q2w_ballistics *b = ctx;
  size_t i = find_double(b->blaster_causes, b->blaster_cause_count, e->actor.id);
  if (i >= b->blaster_cause_count) qa_raise("Error", 0, 0, "Q2 blaster projectile lost its source cause");
  double mod = b->blaster_causes[i].value;
  if (has_other && e->has_owner && qa_same_actor(other, e->owner)) return;
  if (sky_hit) { g->remove(g->ctx, e); return; }
  impact_noise(b, e, g);
  if (can_hurt(g, has_other, other))
    (inf_actor = e->actor.id, attacker = e->has_owner ? &e->owner : NULL, amount = e->damage, dir = g->body(g->ctx, e->actor).velocity, point = g->body(g->ctx, e->actor).origin, do_damage(g, other, e, inf_actor, attacker, amount, 1, dir,
              point, plane, mod, true, 4,
              mod == QA_Q2W_MOD_HYPERBLASTER ? "q2:weapon_hyperblaster" : mod == QA_Q2W_MOD_BLASTER ? "q2:weapon_blaster" : NULL));
  else effect(g, "blaster", g->body(g->ctx, e->actor).origin, plane, 0, 0);
  g->remove(g->ctx, e);
}
static void blaster_touch(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  blaster_impact(ctx, e, g, true, contact.other, contact.plane ? contact.plane->normal : zv(),
                 (qa_js_to_uint32((contact.surface ? contact.surface->native_flags : 0)) & 4u) != 0);
}




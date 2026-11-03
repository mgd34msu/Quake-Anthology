/* Row-review regressions for the Q2 mission-pack entity, projectile, sphere
 * and weapon classes (missionpacks/entities/movers.ts, entities/xatrix.ts,
 * projectiles/bolts.ts, mines.ts, nuke.ts, trap.ts, spheres.ts) and for
 * the mission-pack monster builders' ownership. Every expected value comes
 * from running the TypeScript classes under bun over the same scripted
 * game (scratch oracle oracle/mp2.ts). Every fake checks a sentinel ctx.
 * The game runs on the one Q2 family: a scripted qa_q2_game_services over
 * a qa_q2_foundation_host whose actors, bodies, callbacks, combat and
 * inventory are the real shared world tables, as in TS.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation.h"
#include "qa/content/q2/missionpacks.h"
#include "qa/content/q2/rerelease/entities.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/core/random/q2_rerelease.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/world/actors.h"
#include "qa/world/gameplay.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                              \
  do {                                                                                           \
    checks++;                                                                                    \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

/* ---- sentinel contexts ---- */
static char GAME_CTX, HOST_CTX, LINK_CTX, GAMEPLAY_CTX, BEHAVIOR_CTX;
static char ENTITY_HOOKS_CTX, MOVERS_CTX, PROJECTILE_HOOKS_CTX, WEAPON_HOOKS_CTX, SPHERE_HOOKS_CTX;
static int ctx_errors = 0;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) ctx_errors++; } while (0)

static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }

/* ---- scripted world over the shared tables ---- */
#define SLOTS 128
#define MAXE 64
typedef struct world {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q2_source_callbacks *source_callbacks;
  qa_q2_counters counters;
  bool live_known[SLOTS]; qa_owned_actor live[SLOTS];
  qa_q2_entity *ents[MAXE]; size_t n;
  int64_t next_slot;
  bool player[SLOTS], monster[SLOTS];
  qa_actor_id nearby[16]; size_t nearby_n;
  qa_actor_id players[16]; size_t players_n;
  double now;
  bool controls;
  char log[64][64]; size_t log_n;
  struct { const char *kind; int64_t actor; char text[64]; qa_vec3 start, end; double width, color; bool visible; } emitted[32];
  size_t emitted_n;
  struct { char effect[32]; qa_vec3 end; } beams[8]; size_t beams_n;
  struct { int64_t target, attacker; double amount, kick, mod; } damage[16]; size_t damage_n;
  qa_q2mp_player_effect effects[8]; size_t effects_n;
} world;
static world W;
static bool W_open;
static qa_q2_foundation_host HOST;
static qa_q2_game_services GAME;
static qa_weapon_behavior_projectile_port BEHAVIOR;

/* The registry hands out the lowest free slot; allocate fillers up to the
 * wanted slot and release them so scripted slot numbers stay exact. */
static qa_owned_actor ensure_live(int64_t slot) {
  if (W.live_known[slot] && qa_session_actor_registry_is_live(W.actors, W.live[slot].id)) return W.live[slot];
  qa_owned_actor fillers[SLOTS]; size_t filler_n = 0;
  for (;;) {
    qa_owned_actor actor = qa_session_actor_registry_allocate(W.actors, "q2:test", "q2:actor");
    if (actor.id.slot == slot) {
      for (size_t i = 0; i < filler_n; i++) qa_session_actor_registry_release(W.actors, fillers[i]);
      W.live_known[slot] = true; W.live[slot] = actor;
      return actor;
    }
    if (actor.id.slot > slot || filler_n == SLOTS) abort();
    fillers[filler_n++] = actor;
  }
}
/* The live actor at slot, or an id the registry does not own (TS: an actor
 * id that is not live in this session). */
static const char FOREIGN = 0;
static qa_actor_id aid(int64_t slot) {
  if (W.live_known[slot] && qa_session_actor_registry_is_live(W.actors, W.live[slot].id)) return W.live[slot].id;
  qa_actor_id id; memset(&id, 0, sizeof id); id.slot = slot; id.generation = 1; id.token = &FOREIGN;
  return id;
}
static qa_owned_actor owned(int64_t slot) { return ensure_live(slot); }

static void logf_(const char *text) { if (W.log_n < 64) snprintf(W.log[W.log_n++], 64, "%s", text); }
static qa_q2_entity *find_entity(qa_actor_id id) {
  for (size_t i = 0; i < W.n; i++) if (qa_same_actor(W.ents[i]->actor.id, id)) return W.ents[i];
  return NULL;
}
static void write_body(int64_t slot, qa_body_state state) {
  qa_owned_actor actor = ensure_live(slot);
  if (qa_shared_body_table_read(W.bodies, actor.id).has_value) qa_shared_body_table_write(W.bodies, actor, state);
  else qa_shared_body_table_create_body(W.bodies, actor, state);
}
static qa_q2_entity *add_entity(int64_t slot, const char *classname) {
  qa_q2_entity *e = malloc(sizeof *e);
  if (!e) abort();
  qa_q2_spawn_fields spawn; memset(&spawn, 0, sizeof spawn);
  spawn.ordinal = (double)slot; spawn.classname = classname;
  qa_q2_entity_init(e, owned(slot), spawn);
  W.ents[W.n++] = e;
  qa_body_state empty; memset(&empty, 0, sizeof empty);
  write_body(slot, empty);
  return e;
}
static void set_body(int64_t slot, qa_vec3 origin, qa_vec3 min, qa_vec3 max) {
  qa_body_state state; memset(&state, 0, sizeof state);
  state.origin = origin; state.bounds.min = min; state.bounds.max = max;
  write_body(slot, state);
}
static void set_combat(int64_t slot, double health, double mass, bool can_take) {
  qa_owned_actor actor = ensure_live(slot);
  if (qa_gameplay_authority_read(W.combat, actor.id).has_value) {
    qa_gameplay_authority_set_health(W.combat, actor, health);
    qa_combat_trait_changes t; memset(&t, 0, sizeof t);
    t.has_mass = true; t.mass = mass; t.has_can_take_damage = true; t.can_take_damage = can_take;
    qa_gameplay_authority_set_traits(W.combat, actor, t);
    return;
  }
  qa_combat_state state; memset(&state, 0, sizeof state);
  state.health = health; state.mass = mass; state.can_take_damage = can_take;
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE; state.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(W.combat, actor, state, NULL, NULL);
}
static void set_player(int64_t slot) { W.player[slot] = true; (void)ensure_live(slot); }
/* TS host.actors.observations(): the scripted actor is now live. */
static void observe(int64_t slot) { (void)ensure_live(slot); }
static bool entity_present(const char *classname) {
  for (size_t i = 0; i < W.n; i++) if (strcmp(W.ents[i]->classname, classname) == 0) return true;
  return false;
}

/* game services */
static qa_q2_entity *const *g_entities(void *ctx, size_t *n) { EXPECT_CTX(ctx, GAME_CTX); *n = W.n; return W.ents; }
static qa_q2_entity *g_create(void *ctx, const char *classname, const qa_q2_field_pair *values, size_t count) {
  EXPECT_CTX(ctx, GAME_CTX); (void)values; (void)count;
  return add_entity(W.next_slot++, classname);
}
static qa_q2_entity *g_spawn(void *ctx, qa_q2_spawn_fields fields) {
  qa_q2_entity *e = g_create(ctx, fields.classname, NULL, 0);
  qa_q2_spawn_fields_free(&fields); /* spawn owns fields */
  return e;
}
static void g_remove(void *ctx, qa_q2_entity *e) {
  EXPECT_CTX(ctx, GAME_CTX);
  char line[64]; snprintf(line, sizeof line, "remove %s", e->classname); logf_(line);
  for (size_t i = 0; i < W.n; i++)
    if (W.ents[i] == e) {
      /* A compacting live array: the next entity slides into this index. */
      memmove(&W.ents[i], &W.ents[i + 1], (W.n - i - 1) * sizeof W.ents[0]);
      W.n--;
      qa_owned_actor actor = e->actor;
      qa_q2_entity_destroy(e);
      free(e);
      if (!W.player[actor.id.slot]) qa_session_actor_registry_release(W.actors, actor);
      return;
    }
}
static qa_q2_entity *g_entity(void *ctx, const qa_actor_id *actor) { EXPECT_CTX(ctx, GAME_CTX); return actor ? find_entity(*actor) : NULL; }
static qa_body_state g_body(void *ctx, qa_owned_actor actor) {
  EXPECT_CTX(ctx, GAME_CTX);
  qa_body_state_result body = qa_shared_body_table_read(W.bodies, actor.id);
  qa_body_state empty; memset(&empty, 0, sizeof empty);
  return body.has_value ? body.value : empty;
}
static void g_move(void *ctx, qa_q2_entity *e, qa_q2_body_changes c, bool link) {
  EXPECT_CTX(ctx, GAME_CTX); (void)link;
  char line[64];
  snprintf(line, sizeof line, "move %s%s%s%s", e->classname, c.has_velocity ? " v" : "", c.has_angles ? " a" : "", c.has_origin ? " o" : "");
  logf_(line);
  qa_body_state b = g_body(ctx, e->actor);
  if (c.has_origin) b.origin = c.origin;
  if (c.has_angles) b.angles = c.angles;
  if (c.has_velocity) b.velocity = c.velocity;
  if (c.has_bounds) b.bounds = c.bounds;
  qa_shared_body_table_write(W.bodies, e->actor, b);
}
static void g_link(void *ctx, qa_q2_entity *e) { EXPECT_CTX(ctx, GAME_CTX); (void)e; }
static void g_solid(void *ctx, qa_q2_entity *e, const char *s) { EXPECT_CTX(ctx, GAME_CTX); e->solid = s; }
static void g_motion(void *ctx, qa_q2_entity *e, const char *k) {
  EXPECT_CTX(ctx, GAME_CTX); e->motion = k;
  char line[64]; snprintf(line, sizeof line, "motion %s", e->classname); logf_(line);
}
static void g_schedule(void *ctx, qa_q2_entity *e, double delay, qa_q2_think think) {
  EXPECT_CTX(ctx, GAME_CTX); (void)think;
  char line[64]; snprintf(line, sizeof line, "schedule %s %s", e->classname, QA_JS_NUMBER(delay)); logf_(line);
}
static void g_cancel(void *ctx, qa_q2_entity *e) { EXPECT_CTX(ctx, GAME_CTX); (void)e; }
static void g_use_targets(void *ctx, qa_authored_target e, const qa_actor_id *a, bool i) { EXPECT_CTX(ctx, GAME_CTX); (void)e; (void)a; (void)i; }
static qa_q2_entity *const *g_targets(void *ctx, const char *name, size_t *n) { EXPECT_CTX(ctx, GAME_CTX); (void)name; *n = 0; return NULL; }
static void g_show(void *ctx, qa_q2_entity *e) { EXPECT_CTX(ctx, GAME_CTX); (void)e; }
static void g_sound(void *ctx, qa_owned_actor a, const char *path, double c, double v, double at) {
  EXPECT_CTX(ctx, GAME_CTX); (void)a; (void)c; (void)v; (void)at;
  char line[64]; snprintf(line, sizeof line, "sound %s", path); logf_(line);
}
static qa_damage_outcome g_damage(void *ctx, qa_actor_id target, qa_q2_entity *inflictor, qa_actor_id inflictor_id,
                                  const qa_actor_id *attacker, double amount, double knockback, qa_vec3 direction,
                                  qa_vec3 point, qa_vec3 normal, double mod, double flags, qa_item_id weapon) {
  EXPECT_CTX(ctx, GAME_CTX);
  (void)inflictor; (void)inflictor_id; (void)direction; (void)point; (void)normal; (void)flags; (void)weapon;
  if (W.damage_n < 16) {
    W.damage[W.damage_n].target = target.slot; W.damage[W.damage_n].attacker = attacker ? attacker->slot : -1;
    W.damage[W.damage_n].amount = amount; W.damage[W.damage_n].kick = knockback; W.damage[W.damage_n].mod = mod;
    W.damage_n++;
  }
  qa_damage_outcome o; memset(&o, 0, sizeof o); return o;
}
static void g_radius(void *ctx, qa_q2_entity *inflictor, const qa_actor_id *attacker, double damage, const qa_actor_id *ignore,
                     double radius, double mod, double flags, qa_item_id weapon) {
  EXPECT_CTX(ctx, GAME_CTX); (void)inflictor; (void)attacker; (void)damage; (void)ignore; (void)radius; (void)mod; (void)flags; (void)weapon;
}
static bool g_can_damage(void *ctx, qa_actor_id t, qa_owned_actor i) { EXPECT_CTX(ctx, GAME_CTX); (void)t; (void)i; return true; }

/* host */
static double h_now(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return W.now; }
static double h_frame(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return 0.1; }
static double h_random(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return 0.5; }
static void h_touch_triggers(void *ctx, qa_owned_actor a) { EXPECT_CTX(ctx, HOST_CTX); (void)a; }
static qa_trace_result h_trace(void *ctx, qa_q2_trace_request r) {
  EXPECT_CTX(ctx, HOST_CTX);
  qa_trace_result t; memset(&t, 0, sizeof t);
  t.kind = "q2"; t.fraction = 1; t.end = r.end; t.hit.kind = QA_TRACE_HIT_NONE;
  return t;
}
static double h_point_contents(void *ctx, qa_vec3 p) { EXPECT_CTX(ctx, HOST_CTX); (void)p; return 0; }
static bool h_in_phs(void *ctx, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(ctx, HOST_CTX); (void)a; (void)b; return true; }
static const qa_actor_id *h_nearby(void *ctx, qa_vec3 o, double r, size_t *n) { EXPECT_CTX(ctx, HOST_CTX); (void)o; (void)r; *n = W.nearby_n; return W.nearby; }
static const qa_actor_id *h_players(void *ctx, size_t *n) { EXPECT_CTX(ctx, HOST_CTX); *n = W.players_n; return W.players; }
static qa_actor_id h_world(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return aid(0); }
static bool h_is_player(void *ctx, qa_actor_id a) { EXPECT_CTX(ctx, HOST_CTX); return a.slot > 0 && a.slot < SLOTS && W.player[a.slot]; }
static bool h_is_monster(void *ctx, qa_actor_id a) { EXPECT_CTX(ctx, HOST_CTX); return a.slot > 0 && a.slot < SLOTS && W.monster[a.slot]; }
static void h_emit(void *ctx, qa_q2_presentation_event e) {
  EXPECT_CTX(ctx, HOST_CTX);
  if (W.emitted_n >= 32) return;
  W.emitted[W.emitted_n].kind = e.kind;
  W.emitted[W.emitted_n].actor = e.has_actor ? e.actor.slot : -1;
  snprintf(W.emitted[W.emitted_n].text, 64, "%s", e.text ? e.text : "");
  W.emitted[W.emitted_n].start = e.start; W.emitted[W.emitted_n].end = e.end;
  W.emitted[W.emitted_n].width = e.width; W.emitted[W.emitted_n].color = e.color; W.emitted[W.emitted_n].visible = e.visible;
  W.emitted_n++;
}
static void h_diagnostic(void *ctx, const char *m) { EXPECT_CTX(ctx, HOST_CTX); (void)m; }
static bool w_controls(void *ctx, qa_actor_id projectile) { EXPECT_CTX(ctx, BEHAVIOR_CTX); (void)projectile; return W.controls; }
static qa_weapon_trajectory_update_result w_launch(void *ctx, qa_weapon_behavior_launch in) {
  EXPECT_CTX(ctx, BEHAVIOR_CTX); (void)in; qa_weapon_trajectory_update_result r; memset(&r, 0, sizeof r); return r;
}
static qa_bounds link_bounds(void *ctx, qa_owned_actor a, qa_body_state b) {
  EXPECT_CTX(ctx, LINK_CTX); (void)a;
  qa_bounds r = {qa_add3_double(b.origin, b.bounds.min), qa_add3_double(b.origin, b.bounds.max)}; return r;
}
static void link_noop(void *ctx, qa_linked_body b) { EXPECT_CTX(ctx, LINK_CTX); (void)b; }
static void unlink_noop(void *ctx, qa_actor_id a) { EXPECT_CTX(ctx, LINK_CTX); (void)a; }
static void gp_impulse(void *ctx, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { EXPECT_CTX(ctx, GAMEPLAY_CTX); (void)a; (void)i; (void)m; }
static void gp_before(void *ctx, qa_owned_actor a, qa_damage_decision d) { EXPECT_CTX(ctx, GAMEPLAY_CTX); (void)a; (void)d; }
static void gp_confirmed(void *ctx, qa_damage_outcome o) { EXPECT_CTX(ctx, GAMEPLAY_CTX); (void)o; }

static void close_world(void) {
  if (!W_open) return;
  for (size_t i = 0; i < W.n; i++) { qa_q2_entity_destroy(W.ents[i]); free(W.ents[i]); }
  qa_q2_source_callbacks_destroy(W.source_callbacks);
  qa_shared_inventory_table_destroy(W.inventory);
  qa_gameplay_authority_destroy(W.combat);
  qa_actor_callback_table_destroy(W.callbacks);
  qa_shared_body_table_destroy(W.bodies);
  qa_session_actor_registry_close(W.actors);
  qa_session_actor_registry_destroy(W.actors);
  qa_identity_owner_destroy(&W.identities);
  W_open = false;
}
static void reset(qa_q2_mode mode, qa_q2_edition edition, double now) {
  close_world();
  memset(&W, 0, sizeof W);
  W_open = true;
  W.next_slot = 10; W.now = now;
  qa_identity_owner_create(&W.identities, "mp2");
  W.actors = qa_session_actor_registry_create(&W.identities, 4096);
  qa_body_link_hooks link; memset(&link, 0, sizeof link);
  link.ctx = &LINK_CTX; link.absolute_bounds = link_bounds; link.on_link = link_noop; link.on_unlink = unlink_noop;
  W.bodies = qa_shared_body_table_create(W.actors, link);
  W.callbacks = qa_actor_callback_table_create(W.actors);
  qa_gameplay_hooks gameplay; memset(&gameplay, 0, sizeof gameplay);
  gameplay.ctx = &GAMEPLAY_CTX; gameplay.impulse = gp_impulse; gameplay.before_reaction = gp_before; gameplay.confirmed = gp_confirmed;
  W.combat = qa_gameplay_authority_create(W.actors, W.callbacks, gameplay);
  W.inventory = qa_shared_inventory_table_create(W.actors);
  W.source_callbacks = qa_q2_source_callbacks_create();
  memset(&HOST, 0, sizeof HOST);
  HOST.ctx = &HOST_CTX;
  HOST.actors = W.actors; HOST.bodies = W.bodies; HOST.callbacks = W.callbacks;
  HOST.combat = W.combat; HOST.inventory = W.inventory;
  HOST.now = h_now; HOST.frame_seconds = h_frame; HOST.random = h_random; HOST.touch_triggers = h_touch_triggers;
  HOST.trace = h_trace; HOST.point_contents = h_point_contents; HOST.in_phs = h_in_phs; HOST.nearby = h_nearby;
  HOST.players = h_players; HOST.world_actor = h_world; HOST.is_player = h_is_player; HOST.is_monster = h_is_monster;
  HOST.emit = h_emit; HOST.diagnostic = h_diagnostic;
  memset(&GAME, 0, sizeof GAME);
  GAME.ctx = &GAME_CTX; GAME.host = &HOST;
  GAME.source_callbacks = W.source_callbacks; GAME.counters = &W.counters;
  GAME.options.edition = edition; GAME.options.mode = mode; GAME.options.map_name = "test";
  GAME.entities = g_entities;
  GAME.spawn = g_spawn; GAME.create = g_create; GAME.remove = g_remove; GAME.entity = g_entity; GAME.body = g_body;
  GAME.move = g_move; GAME.link = g_link; GAME.solid = g_solid; GAME.motion = g_motion; GAME.schedule = g_schedule;
  GAME.cancel = g_cancel; GAME.use_targets = g_use_targets; GAME.targets = g_targets; GAME.show = g_show;
  GAME.sound = g_sound; GAME.damage = g_damage; GAME.radius_damage = g_radius; GAME.can_damage = g_can_damage;
}

/* mission-pack hooks: hooks.movers is the real Q2MoverModule. */
static void mv_path_corner(void *ctx, qa_q2_entity *c, qa_q2_game_services *g, qa_actor_id o) { EXPECT_CTX(ctx, MOVERS_CTX); (void)c; (void)g; (void)o; }
static void mv_combat_point(void *ctx, qa_q2_entity *p, qa_q2_game_services *g, qa_actor_id o) { EXPECT_CTX(ctx, MOVERS_CTX); (void)p; (void)g; (void)o; }
static qa_q2_mover_module *MOVERS;
static void eh_teleport(void *ctx, qa_actor_id a, qa_vec3 o, qa_vec3 an) { EXPECT_CTX(ctx, ENTITY_HOOKS_CTX); (void)a; (void)o; (void)an; }
static void eh_anger(void *ctx, qa_q2_entity *m, qa_q2_entity *t, qa_q2_game_services *g) { EXPECT_CTX(ctx, ENTITY_HOOKS_CTX); (void)m; (void)t; (void)g; }
static void eh_emit(void *ctx, qa_q2mp_entity_event e) { EXPECT_CTX(ctx, ENTITY_HOOKS_CTX); (void)e; }
static qa_q2mp_entity_hooks entity_hooks(void) {
  if (MOVERS == NULL) {
    qa_q2_mover_hooks mh = {&MOVERS_CTX, mv_path_corner, mv_combat_point};
    MOVERS = qa_q2_create_mover_module(mh);
    if (MOVERS == NULL) abort();
  }
  qa_q2mp_entity_hooks h; memset(&h, 0, sizeof h);
  h.movers = MOVERS;
  h.ctx = &ENTITY_HOOKS_CTX; h.teleport_player = eh_teleport; h.target_anger = eh_anger; h.emit = eh_emit;
  return h;
}
static void wh_emit(void *ctx, qa_q2w_event e) {
  EXPECT_CTX(ctx, WEAPON_HOOKS_CTX);
  if (e.kind == QA_Q2W_EVENT_BEAM && W.beams_n < 8) { snprintf(W.beams[W.beams_n].effect, 32, "%s", e.effect); W.beams[W.beams_n].end = e.end; W.beams_n++; }
}
static void wh_noise(void *ctx, qa_actor_id a, qa_vec3 o, bool s) { EXPECT_CTX(ctx, WEAPON_HOOKS_CTX); (void)a; (void)o; (void)s; }
static void wh_dodge(void *ctx, qa_actor_id m, qa_actor_id a, double eta, qa_trace_result t) { EXPECT_CTX(ctx, WEAPON_HOOKS_CTX); (void)m; (void)a; (void)eta; (void)t; }
static bool wh_can_target(void *ctx, const qa_actor_id *a, qa_actor_id t) { EXPECT_CTX(ctx, WEAPON_HOOKS_CTX); (void)a; (void)t; return true; }
static void wh_ammo(void *ctx, qa_actor_id a, qa_item_id i) { EXPECT_CTX(ctx, WEAPON_HOOKS_CTX); (void)a; (void)i; }
static struct qa_q2m_context *ph_monster(void *ctx, qa_actor_id a) { EXPECT_CTX(ctx, PROJECTILE_HOOKS_CTX); (void)a; return NULL; }
static void ph_effect(void *ctx, qa_q2mp_player_effect e) { EXPECT_CTX(ctx, PROJECTILE_HOOKS_CTX); if (W.effects_n < 8) W.effects[W.effects_n++] = e; }
static bool sh_intermission(void *ctx) { EXPECT_CTX(ctx, SPHERE_HOOKS_CTX); return false; }
static void sh_effect(void *ctx, qa_q2mp_player_effect e) { EXPECT_CTX(ctx, SPHERE_HOOKS_CTX); if (W.effects_n < 8) W.effects[W.effects_n++] = e; }

static qa_q2w_ballistics BALLISTICS;
static qa_q2mp_projectiles PROJECTILES;
static void make_projectiles(void) {
  qa_q2w_hooks wh; memset(&wh, 0, sizeof wh);
  wh.ctx = &WEAPON_HOOKS_CTX; wh.emit = wh_emit; wh.noise = wh_noise; wh.dodge = wh_dodge;
  wh.can_target = wh_can_target; wh.ammo_changed = wh_ammo;
  CHECK(qa_q2w_ballistics_init(&BALLISTICS, wh));
  qa_q2mp_projectile_hooks ph; memset(&ph, 0, sizeof ph);
  ph.base = &BALLISTICS; ph.ctx = &PROJECTILE_HOOKS_CTX; ph.monster = ph_monster; ph.player_effect = ph_effect;
  qa_q2mp_projectiles_init(&PROJECTILES, ph);
}
static void free_projectiles(void) { qa_q2w_ballistics_destroy(&BALLISTICS); }
static bool log_is(const char *const *expected, size_t count) {
  if (getenv("MP2_DEBUG")) for (size_t i = 0; i < W.log_n; i++) fprintf(stderr, "log[%zu] %s\n", i, W.log[i]);
  if (W.log_n != count) return false;
  for (size_t i = 0; i < count; i++) if (strcmp(W.log[i], expected[i]) != 0) return false;
  return true;
}
/* TS sourceCallbacks.<kind>.resolve(name) !== null, without aborting the
 * test binary when the name is missing (resolve throws in TS). */
typedef struct resolve_probe { const char *kind, *name; } resolve_probe;
static void probe_resolve(void *ctx) {
  resolve_probe *p = ctx;
  bool has = false;
  if (strcmp(p->kind, "think") == 0) (void)qa_q2_think_resolve(W.source_callbacks, p->name, &has);
  else if (strcmp(p->kind, "touch") == 0) (void)qa_q2_touch_resolve(W.source_callbacks, p->name, &has);
  else (void)qa_q2_die_resolve(W.source_callbacks, p->name, &has);
  if (!has) qa_range_error("unresolved");
}
static bool registered(const char *kind, const char *name) {
  resolve_probe p = {kind, name};
  char message[128];
  return qa_range_error_catch(probe_resolve, &p, message, sizeof message);
}

/* 1. plat2_hit_bottom removes every owned bad_area (TS iterates a snapshot;
 * the old live loop skipped the area sliding into a removed index).
 * Oracle: remaining [func_plat2, info_null], style 1, count 0. */
static void test_plat_bottom_removes_all_areas(void) {
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  qa_q2mp_rogue_movers movers; qa_q2mp_rogue_movers_init(&movers, entity_hooks());
  qa_q2_entity *plat = add_entity(1, "func_plat2"); plat->flags = 1024;
  for (int64_t slot = 2; slot <= 5; slot++) {
    qa_q2_entity *e = add_entity(slot, slot == 4 ? "info_null" : "bad_area");
    if (slot != 4) { e->owner = plat->actor.id; e->has_owner = true; }
  }
  movers.bottom.fn(movers.bottom.ctx, plat, &GAME);
  CHECK(W.n == 2 && entity_present("func_plat2") && entity_present("info_null") && !entity_present("bad_area"));
  CHECK(plat->style == 1 && plat->count == 0);
}

/* 2. secret_touch centerprints through game.host.emit. Oracle:
 * [{kind: centerprint, actor: 7, text: "secret!"}], timestamp 12. */
static void test_secret_touch_centerprint(void) {
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  qa_q2mp_rogue_movers movers; qa_q2mp_rogue_movers_init(&movers, entity_hooks());
  qa_q2_entity *door = add_entity(1, "func_door_secret2"); door->message = "secret!";
  set_player(7); set_combat(7, 100, 200, true);
  qa_touch_contact contact; memset(&contact, 0, sizeof contact);
  contact.self = door->actor; contact.other = aid(7);
  movers.secret_touch.fn(movers.secret_touch.ctx, door, &GAME, contact);
  CHECK(W.emitted_n == 1 && strcmp(W.emitted[0].kind, "centerprint") == 0 && W.emitted[0].actor == 7 &&
        strcmp(W.emitted[0].text, "secret!") == 0);
  CHECK(door->timestamp == 12);
}

/* 3. use_nuke over adjacent monsters. Oracle: remaining [misc_nuke, player],
 * one damage {target 4, attacker 1, 100000, kick 1, mod 39}. */
static void test_nuke_use_removes_adjacent_monsters(void) {
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  qa_q2mp_xatrix_entities xatrix; qa_q2mp_xatrix_entities_init(&xatrix, entity_hooks());
  qa_q2_entity *nuke = add_entity(1, "misc_nuke");
  add_entity(2, "monster_soldier"); W.monster[2] = true;
  add_entity(3, "monster_soldier"); W.monster[3] = true;
  add_entity(4, "player"); set_player(4);
  add_entity(5, "monster_gekk"); W.monster[5] = true;
  xatrix.nuke_use.fn(xatrix.nuke_use.ctx, nuke, &GAME, NULL, NULL);
  CHECK(W.n == 2 && entity_present("misc_nuke") && entity_present("player"));
  CHECK(W.damage_n == 1 && W.damage[0].target == 4 && W.damage[0].attacker == 1 && W.damage[0].amount == 100000 &&
        W.damage[0].kick == 1 && W.damage[0].mod == 39);
  CHECK(nuke->use.fn == NULL);
}

/* 4. target_mal_laser spawned off emits an invisible beam through the host.
 * Oracle: {beam, actor 1, start = end = (1,2,3), width 16, color 4076007664, visible false}. */
static void test_mal_laser_off_beam(void) {
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  qa_q2mp_xatrix_entities xatrix; qa_q2mp_xatrix_entities_init(&xatrix, entity_hooks());
  qa_q2_entity *laser = add_entity(1, "target_mal_laser"); laser->spawnflags = 2 | 64;
  set_body(1, v3(1, 2, 3), v3(0, 0, 0), v3(0, 0, 0));
  CHECK(qa_q2mp_xatrix_entities_spawn(&xatrix, laser, &GAME));
  CHECK(W.emitted_n == 1);
  if (W.emitted_n == 1) {
    CHECK(strcmp(W.emitted[0].kind, "beam") == 0 && W.emitted[0].actor == 1 && !W.emitted[0].visible);
    CHECK(W.emitted[0].start.x == 1 && W.emitted[0].start.y == 2 && W.emitted[0].start.z == 3);
    CHECK(W.emitted[0].end.x == 1 && W.emitted[0].end.y == 2 && W.emitted[0].end.z == 3);
    CHECK(W.emitted[0].width == 16 && W.emitted[0].color == 4076007664.0);
  }
}

/* 5. heat_think, tracker_fly and rerelease/heat_think leave steering to a
 * weapon behavior that controls the trajectory. Oracle logs below. */
static void run_steering(bool controls) {
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  memset(&BEHAVIOR, 0, sizeof BEHAVIOR);
  BEHAVIOR.ctx = &BEHAVIOR_CTX; BEHAVIOR.launch = w_launch; BEHAVIOR.controls_trajectory = w_controls;
  HOST.weapon_behavior = &BEHAVIOR;
  W.controls = controls;
  make_projectiles();
  qa_q2mp_bolts *bolts = &PROJECTILES.base.base.base;
  qa_q2_entity *rocket = add_entity(1, "rocket");
  set_player(7); set_combat(7, 100, 200, true); set_body(7, v3(100, 0, 0), v3(0, 0, 0), v3(0, 0, 0));
  W.nearby[W.nearby_n++] = aid(7);
  bolts->heat_think.fn(bolts->heat_think.ctx, rocket, &GAME);
  qa_q2_entity *tracker = add_entity(2, "tracker"); tracker->enemy = aid(7); tracker->has_enemy = true; tracker->speed = 300;
  bolts->tracker_fly.fn(bolts->tracker_fly.ctx, tracker, &GAME);
  qa_q2_entity *rr = add_entity(3, "rocket"); rr->accel = 0.075; rr->speed = 500; rr->movedir = v3(1, 0, 0);
  GAME.options.edition = QA_Q2_EDITION_RERELEASE;
  bolts->heat_think_rerelease.fn(bolts->heat_think_rerelease.ctx, rr, &GAME);
  CHECK(rocket->has_enemy && rocket->enemy.slot == 7 && rr->has_enemy && rr->enemy.slot == 7);
  free_projectiles();
}
static void test_steering_controlled_trajectory(void) {
  static const char *const free_log[] = {"move rocket v a", "motion rocket", "schedule rocket 0.1",
    "move tracker v a", "motion tracker", "schedule tracker 0.1",
    "move rocket a", "sound weapons/railgr1a.wav", "move rocket v", "motion rocket", "schedule rocket 0.1"};
  static const char *const controlled_log[] = {"schedule rocket 0.1", "schedule tracker 0.1",
    "sound weapons/railgr1a.wav", "schedule rocket 0.1"};
  run_steering(false);
  CHECK(log_is(free_log, sizeof free_log / sizeof free_log[0]));
  run_steering(true);
  CHECK(log_is(controlled_log, sizeof controlled_log / sizeof controlled_log[0]));
}

/* 6. tesla_think_active scans every observed actor, not only Q2 entities.
 * Oracle: damage [{target 7, attacker null, 3, kick 8, mod 45}],
 * beams [{bfg-lightning, end (50,0,10)}]. */
static void test_tesla_observations(void) {
  reset(QA_Q2_MODE_DEATHMATCH, QA_Q2_EDITION_CLASSIC, 10);
  make_projectiles();
  qa_q2mp_mines *mines = &PROJECTILES.base.base;
  qa_q2_entity *tesla = add_entity(1, "tesla"); tesla->damage = 3; tesla->timestamp = 100;
  qa_q2_entity *field = add_entity(2, "tesla trigger");
  set_body(2, v3(0, 0, 0), v3(-128, -128, 0), v3(128, 128, 128));
  tesla->team_chain = field->actor.id; tesla->has_team_chain = true;
  set_player(7); set_combat(7, 100, 200, true); set_body(7, v3(50, 0, 10), v3(-16, -16, -24), v3(16, 16, 32));
  observe(1); observe(2); observe(7);
  mines->tesla_active.fn(mines->tesla_active.ctx, tesla, &GAME);
  CHECK(W.damage_n == 1 && W.damage[0].target == 7 && W.damage[0].attacker == -1 && W.damage[0].amount == 3 &&
        W.damage[0].kick == 8 && W.damage[0].mod == 45);
  CHECK(W.beams_n == 1 && strcmp(W.beams[0].effect, "bfg-lightning") == 0 && W.beams[0].end.x == 50 &&
        W.beams[0].end.y == 0 && W.beams[0].end.z == 10);
  free_projectiles();
}

/* 7. `this.callbacks` is virtual: spawnBadArea through a full projectile
 * chain registers the most-derived set (trap and nuke names included).
 * Oracle (bun: new Q2SourceCallbacks().register(new
 * Q2MissionPackProjectiles(hooks).callbacks), then each kind's
 * functions.keys() and trajectories.keys()): exactly these names, in this
 * order; use/pain/blocked empty. */
static bool names_are(const char *kind, const char *const *expected, size_t count) {
  const char *names[64];
  size_t n = qa_q2_source_callbacks_names(W.source_callbacks, kind, names, 64);
  if (getenv("MP2_DEBUG")) for (size_t i = 0; i < n && i < 64; i++) fprintf(stderr, "%s[%zu] %s\n", kind, i, names[i]);
  if (n != count) return false;
  for (size_t i = 0; i < count; i++) if (strcmp(names[i], expected[i]) != 0) return false;
  return true;
}
static void test_registration_most_derived(void) {
  static const char *const think[] = {"missionpack.free", "ionripper_sparks", "heat_think", "rerelease/heat_think",
    "tracker_fly", "tracker_pain_daemon_think", "Prox_Explode", "prox_open", "prox_seek", "Prox_Think", "tesla_think",
    "tesla_activate", "tesla_think_active", "Nuke_Think", "Nuke_Quake", "Trap_Think", "rerelease/Trap_Think",
    "Trap_Gib_Think"};
  static const char *const touch[] = {"ionripper_touch", "plasma_touch", "flechette_touch", "blaster2_touch",
    "tracker_touch", "prox_land", "Prox_Field_Touch", "tesla_lava", "badarea_touch", "nuke_bounce"};
  static const char *const die[] = {"prox_die", "tesla_die", "nuke_die", "trap_die"};
  static const char *const trajectory[] = {"ionripper_touch", "plasma_touch", "flechette_touch", "tracker_touch"};
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  make_projectiles();
  (void)qa_q2mp_mines_spawn_bad_area(&PROJECTILES.base.base, &GAME, v3(-1, -1, -1), v3(1, 1, 1), 0, false, aid(0));
  CHECK(names_are("think", think, sizeof think / sizeof think[0]));
  CHECK(names_are("touch", touch, sizeof touch / sizeof touch[0]));
  CHECK(names_are("die", die, sizeof die / sizeof die[0]));
  CHECK(names_are("trajectory", trajectory, sizeof trajectory / sizeof trajectory[0]));
  CHECK(names_are("use", NULL, 0) && names_are("pain", NULL, 0) && names_are("blocked", NULL, 0));
  /* resolve() still finds each registered name (the old check). */
  for (size_t i = 0; i < sizeof think / sizeof think[0]; i++) CHECK(registered("think", think[i]));
  for (size_t i = 0; i < sizeof touch / sizeof touch[0]; i++) CHECK(registered("touch", touch[i]));
  for (size_t i = 0; i < sizeof die / sizeof die[0]; i++) CHECK(registered("die", die[i]));
  free_projectiles();
}

/* 8. A dopple sphere's pain resets its lifetime to now + 15; an owned one
 * keeps max(wait, now + 15). Oracle (both editions, wait 100, now 10):
 * owned 100, dopple 25; enemy 8; effects 16 / 67108872. */
static void test_sphere_pain_dopple_wait(void) {
  for (int edition = 0; edition < 2; edition++)
    for (int kind = 0; kind < 2; kind++)
      for (int dopple = 0; dopple < 2; dopple++) {
        reset(QA_Q2_MODE_SINGLEPLAYER, edition ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC, 10);
        make_projectiles();
        qa_q2mp_sphere_hooks sh = {false, &SPHERE_HOOKS_CTX, sh_intermission, sh_effect};
        qa_q2mp_spheres spheres; qa_q2mp_spheres_init(&spheres, &PROJECTILES, sh);
        qa_q2_entity *sphere = add_entity(1, "sphere"); sphere->wait = 100;
        sphere->spawnflags = (kind ? 2 : 4) | (dopple ? (edition ? 0x10000 : 0x100) : 0);
        qa_actor_id attacker = aid(8);
        qa_pain_reaction reaction; memset(&reaction, 0, sizeof reaction);
        reaction.self = sphere->actor; reaction.attacker = &attacker;
        qa_q2_pain pain = kind ? spheres.hunter_pain : spheres.vengeance_pain;
        pain.fn(pain.ctx, sphere, &GAME, reaction);
        CHECK(sphere->wait == (dopple ? 25 : 100));
        CHECK(sphere->has_enemy && sphere->enemy.slot == 8);
        CHECK(sphere->effects == (kind ? 67108872 : 16));
        free_projectiles();
      }
}

/* 9. The hunter camera's owner.flags |= 0x4000 is a signed int32 OR.
 * Oracle: flags 2^32 + 1 -> 16385; viewHeight 8; one sphere-camera effect. */
static void test_hunter_camera_flags(void) {
  reset(QA_Q2_MODE_SINGLEPLAYER, QA_Q2_EDITION_CLASSIC, 10);
  make_projectiles();
  qa_q2mp_sphere_hooks sh = {true, &SPHERE_HOOKS_CTX, sh_intermission, sh_effect};
  qa_q2mp_spheres spheres; qa_q2mp_spheres_init(&spheres, &PROJECTILES, sh);
  qa_q2_entity *owner = add_entity(1, "player"); set_player(1);
  set_body(1, v3(0, 0, 0), v3(-16, -16, -24), v3(16, 16, 32));
  owner->flags = 4294967297.0; owner->view_height = 22;
  set_combat(1, 0, 200, true);
  qa_q2_entity *sphere = add_entity(2, "sphere"); set_body(2, v3(0, 0, 40), v3(0, 0, 0), v3(0, 0, 0));
  sphere->spawnflags = 2; sphere->owner = owner->actor.id; sphere->has_owner = true; sphere->wait = 100;
  set_body(8, v3(500, 0, 0), v3(0, 0, 0), v3(0, 0, 0));
  qa_actor_id attacker = aid(8);
  qa_pain_reaction reaction; memset(&reaction, 0, sizeof reaction);
  reaction.self = sphere->actor; reaction.attacker = &attacker;
  spheres.hunter_pain.fn(spheres.hunter_pain.ctx, sphere, &GAME, reaction);
  CHECK(owner->flags == 16385);
  CHECK(owner->view_height == 8);
  size_t cameras = 0;
  for (size_t i = 0; i < W.effects_n; i++) if (W.effects[i].kind == QA_Q2MP_EFFECT_SPHERE_CAMERA) cameras++;
  CHECK(cameras == 1);
  free_projectiles();
}

/* 10. Nuke explosion: target.flags |= 0x10000 stores a signed int32.
 * Oracle: 2^31 -> -2147418112; 2^32 + 4 -> 65540; damage [10000, 10000]. */
static void test_nuke_flags_int32(void) {
  reset(QA_Q2_MODE_COOP, QA_Q2_EDITION_CLASSIC, 10);
  make_projectiles();
  qa_q2mp_nuke *nuke = &PROJECTILES.base;
  qa_q2_entity *bomb = add_entity(1, "nuke"); bomb->damage = 400; bomb->damage_radius = 512; bomb->wait = 5;
  qa_q2_entity *first = add_entity(2, "player"); set_player(2); set_combat(2, 100, 200, true);
  set_body(2, v3(10, 0, 0), v3(0, 0, 0), v3(0, 0, 0)); first->flags = 2147483648.0;
  qa_q2_entity *second = add_entity(3, "player"); set_player(3); set_combat(3, 100, 200, true);
  set_body(3, v3(20, 0, 0), v3(0, 0, 0), v3(0, 0, 0)); second->flags = 4294967300.0;
  W.players[W.players_n++] = aid(2); W.players[W.players_n++] = aid(3);
  W.nearby[W.nearby_n++] = aid(2); W.nearby[W.nearby_n++] = aid(3);
  nuke->nuke_think.fn(nuke->nuke_think.ctx, bomb, &GAME);
  CHECK(first->flags == -2147418112.0);
  CHECK(second->flags == 65540);
  CHECK(W.damage_n == 2 && W.damage[0].amount == 10000 && W.damage[1].amount == 10000);
  free_projectiles();
}


/* 11. The rerelease chainfist sweeps game.host.actors.observations(), so a
 * deathmatch player with no Q2 entity is hit. Oracle: damage [{target 7,
 * attacker 1, 15, kick 50, mod 40}], frame 6, emptySoundTime 10.5, log
 * [sound weapons/sawslice.wav]. */
static void test_chainfist_rerelease_observations(void) {
  reset(QA_Q2_MODE_DEATHMATCH, QA_Q2_EDITION_RERELEASE, 10);
  make_projectiles();
  qa_q2w_hooks wh; memset(&wh, 0, sizeof wh);
  wh.ctx = &WEAPON_HOOKS_CTX; wh.emit = wh_emit; wh.noise = wh_noise; wh.dodge = wh_dodge;
  wh.can_target = wh_can_target; wh.ammo_changed = wh_ammo;
  qa_q2w_weapons weapons;
  CHECK(qa_q2w_weapons_init(&weapons, wh));
  qa_q2mp_weapons mp; qa_q2mp_weapons_init(&mp, &PROJECTILES);
  qa_q2mp_weapons_register(&mp, &weapons, QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_RERELEASE);
  const qa_q2w_extension *chainfist = NULL;
  for (size_t i = 0; i < weapons.extension_count; i++)
    if (strcmp(weapons.extensions[i].definition->name, "chainfist") == 0) chainfist = &weapons.extensions[i];
  CHECK(chainfist != NULL);
  set_body(1, v3(0, 0, 0), v3(-16, -16, -24), v3(16, 16, 32));
  set_combat(7, 100, 200, true); set_body(7, v3(30, 0, 0), v3(-16, -16, -24), v3(16, 16, 32));
  observe(1); observe(7);
  qa_q2w_state state; memset(&state, 0, sizeof state);
  state.frame = 5; state.empty_sound_time = 0; state.primary_handoff = QA_Q2W_HANDOFF_ACTIVE; state.weapon = "chainfist";
  qa_q2w_context context; memset(&context, 0, sizeof context);
  context.self.actor = owned(1); context.self.view_height = 22; context.game = &GAME; context.state = &state;
  context.definition = chainfist ? chainfist->definition : NULL; context.now = 10; context.rerelease = true;
  context.input.attack = true; context.input.hand = QA_Q2W_HAND_RIGHT; context.input.players_collide = true;
  if (chainfist) chainfist->fire(chainfist->ctx, &context, &weapons);
  CHECK(W.damage_n == 1 && W.damage[0].target == 7 && W.damage[0].attacker == 1 && W.damage[0].amount == 15 &&
        W.damage[0].kick == 50 && W.damage[0].mod == 40);
  CHECK(state.frame == 6 && state.empty_sound_time == 10.5);
  CHECK(W.log_n == 1 && strcmp(W.log[0], "sound weapons/sawslice.wav") == 0);
  qa_q2w_weapons_destroy(&weapons);
  free_projectiles();
}

/* ================= rerelease (qa_q2_* host) ================= */
static char RR_CTX, RR_HOOKS_CTX, RR_PLAYER_HOOKS_CTX, RR_LINK_CTX;
typedef struct rr_world {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_q2_foundation_host host;
  qa_q2_game_services game;
  qa_q2_counters counters;
  qa_q2_entity **entities; size_t entity_count, entity_cap;
  qa_actor_id players[80]; size_t players_n;
  qa_q2_rerelease_random random;
  int host_random_calls, use_targets_calls;
  size_t centerprints, waiting_texts, wait_texts;
  qa_actor_id last_actor; char last_text[64];
  char log[8][64]; size_t log_n;
} rr_world;
static rr_world *RW;
static void rr_log(const char *text) { if (RW->log_n < 8) snprintf(RW->log[RW->log_n++], 64, "%s", text); }
static double rr_now_fn(void *c) { EXPECT_CTX(c, RR_CTX); return 10; }
static double rr_frame_fn(void *c) { EXPECT_CTX(c, RR_CTX); return 0.1; }
static double rr_random_fn(void *c) { EXPECT_CTX(c, RR_CTX); RW->host_random_calls++; return 0.99; }
static const qa_actor_id *rr_players_fn(void *c, size_t *n) { EXPECT_CTX(c, RR_CTX); *n = RW->players_n; return RW->players; }
static void rr_emit_fn(void *c, qa_q2_presentation_event e) {
  EXPECT_CTX(c, RR_CTX);
  if (strcmp(e.kind, "centerprint") != 0) return;
  RW->centerprints++;
  if (strcmp(e.text, "waiting") == 0) RW->waiting_texts++;
  if (strcmp(e.text, "wait") == 0) RW->wait_texts++;
  RW->last_actor = e.actor; snprintf(RW->last_text, 64, "%s", e.text);
}
static void rr_diag_fn(void *c, const char *m) { EXPECT_CTX(c, RR_CTX); (void)m; }
static bool rr_is_player_fn(void *c, qa_actor_id a) {
  EXPECT_CTX(c, RR_CTX);
  for (size_t i = 0; i < RW->players_n; i++) if (qa_same_actor(RW->players[i], a)) return true;
  return false;
}
static qa_q2_entity *const *rr_entities_fn(void *c, size_t *n) { EXPECT_CTX(c, RR_CTX); *n = RW->entity_count; return RW->entities; }
static qa_q2_entity *rr_entity_fn(void *c, const qa_actor_id *a) {
  EXPECT_CTX(c, RR_CTX);
  if (!a) return NULL;
  for (size_t i = 0; i < RW->entity_count; i++) if (qa_same_actor(RW->entities[i]->actor.id, *a)) return RW->entities[i];
  return NULL;
}
static qa_body_state rr_body_fn(void *c, qa_owned_actor a) {
  EXPECT_CTX(c, RR_CTX);
  qa_body_state_result b = qa_shared_body_table_read(RW->bodies, a.id);
  qa_body_state empty; memset(&empty, 0, sizeof empty);
  return b.has_value ? b.value : empty;
}
static void rr_remove_fn(void *c, qa_q2_entity *e) {
  EXPECT_CTX(c, RR_CTX);
  char line[64]; snprintf(line, sizeof line, "remove %s", e->classname); rr_log(line);
  for (size_t i = 0; i < RW->entity_count; i++)
    if (RW->entities[i] == e) {
      memmove(&RW->entities[i], &RW->entities[i + 1], (RW->entity_count - i - 1) * sizeof RW->entities[0]);
      RW->entity_count--;
      qa_q2_entity_destroy(e); free(e);
      return;
    }
}
static void rr_schedule_fn(void *c, qa_q2_entity *e, double d, qa_q2_think t) {
  EXPECT_CTX(c, RR_CTX); (void)t;
  char line[64]; snprintf(line, sizeof line, "schedule %s %s", e->classname, QA_JS_NUMBER(d)); rr_log(line);
}
static void rr_use_targets_fn(void *c, qa_authored_target e, const qa_actor_id *a, bool d) { EXPECT_CTX(c, RR_CTX); (void)e; (void)a; (void)d; RW->use_targets_calls++; }
static void rr_solid_fn(void *c, qa_q2_entity *e, const char *s) { EXPECT_CTX(c, RR_CTX); e->solid = s; }
static void rr_motion_fn(void *c, qa_q2_entity *e, const char *k) { EXPECT_CTX(c, RR_CTX); e->motion = k; }
static void rr_move_fn(void *c, qa_q2_entity *e, qa_q2_body_changes ch, bool link) { EXPECT_CTX(c, RR_CTX); (void)e; (void)ch; (void)link; }
static void rr_show_fn(void *c, qa_q2_entity *e) { EXPECT_CTX(c, RR_CTX); (void)e; }
static qa_bounds rr_link_bounds(void *c, qa_owned_actor a, qa_body_state b) {
  EXPECT_CTX(c, RR_LINK_CTX); (void)a;
  qa_bounds r = {qa_add3_double(b.origin, b.bounds.min), qa_add3_double(b.origin, b.bounds.max)}; return r;
}
static void rr_link_noop(void *c, qa_linked_body b) { EXPECT_CTX(c, RR_LINK_CTX); (void)b; }
static void rr_unlink_noop(void *c, qa_actor_id a) { EXPECT_CTX(c, RR_LINK_CTX); (void)a; }
/* rerelease hooks */
static void rh_emit(void *c, qa_q2_rerelease_event e) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)e; }
static const char *rh_style(void *c, double s) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)s; return "m"; }
static qa_q2rr_player_identity rh_identity(void *c, qa_actor_id a) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)a; qa_q2rr_player_identity id = {0, ""}; return id; }
static bool rh_clip(void *c, qa_q2_entity *t, qa_actor_id p, qa_q2_game_services *g) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)t; (void)p; (void)g; return true; }
static qa_q2rr_navigation rh_nav(void *c, qa_vec3 s, qa_vec3 g, const qa_actor_id *a) {
  EXPECT_CTX(c, RR_HOOKS_CTX); (void)s; (void)g; (void)a; qa_q2rr_navigation n = {"no-navigation", 0, NULL, 0}; return n;
}
static bool rh_search(void *c, const qa_actor_id *a) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)a; return false; }
static bool rh_grounded(void *c, qa_actor_id a) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)a; return true; }
static void rh_push(void *c, qa_actor_id a, qa_vec3 v) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)a; (void)v; }
static void rh_grav(void *c, qa_actor_id a, double g) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)a; (void)g; }
static void rh_world_grav(void *c, double g) { EXPECT_CTX(c, RR_HOOKS_CTX); (void)g; }
static qa_q2_rerelease_hooks rr_hooks(void) {
  qa_q2_rerelease_hooks h; memset(&h, 0, sizeof h);
  h.ctx = &RR_HOOKS_CTX; h.emit = rh_emit; h.light_style = rh_style; h.player_identity = rh_identity;
  h.clip_trigger = rh_clip; h.navigation = rh_nav; h.monsters_searching = rh_search; h.grounded_on_world = rh_grounded;
  h.push_player = rh_push; h.set_actor_gravity = rh_grav; h.set_world_gravity = rh_world_grav;
  return h;
}
/* player hooks */
static qa_q2_player_movement ph_movement(void *c, qa_actor_id a) {
  EXPECT_CTX(c, RR_PLAYER_HOOKS_CTX); (void)a; qa_q2_player_movement m; memset(&m, 0, sizeof m); return m;
}
static void ph_set_movement(void *c, qa_actor_id a, qa_q2_player_movement_change ch) { EXPECT_CTX(c, RR_PLAYER_HOOKS_CTX); (void)a; (void)ch; }
static void ph_emit(void *c, qa_q2_player_event e) { EXPECT_CTX(c, RR_PLAYER_HOOKS_CTX); (void)e; }
static void ph_noise(void *c, qa_actor_id a, qa_vec3 o) { EXPECT_CTX(c, RR_PLAYER_HOOKS_CTX); (void)a; (void)o; }
static bool ph_banned(void *c, const char *a) { EXPECT_CTX(c, RR_PLAYER_HOOKS_CTX); (void)a; return false; }

static qa_q2_entity *rr_add(const char *classname, qa_vec3 origin, qa_vec3 min, qa_vec3 max) {
  qa_owned_actor actor = qa_session_actor_registry_allocate(RW->actors, "q2:test", classname);
  qa_q2_entity *e = calloc(1, sizeof *e);
  if (!e) abort();
  qa_q2_spawn_fields fields; memset(&fields, 0, sizeof fields);
  fields.ordinal = -1; fields.classname = classname;
  qa_q2_entity_init(e, actor, fields);
  qa_body_state body; memset(&body, 0, sizeof body);
  body.origin = origin; body.bounds.min = min; body.bounds.max = max;
  qa_shared_body_table_create_body(RW->bodies, actor, body);
  if (RW->entity_count == RW->entity_cap) {
    RW->entity_cap = RW->entity_cap ? RW->entity_cap * 2 : 16;
    RW->entities = realloc(RW->entities, RW->entity_cap * sizeof *RW->entities);
    if (!RW->entities) abort();
  }
  RW->entities[RW->entity_count++] = e;
  return e;
}
static void rr_setup(qa_q2_mode mode) {
  RW = calloc(1, sizeof *RW);
  if (!RW) abort();
  qa_identity_owner_create(&RW->identities, "test");
  RW->actors = qa_session_actor_registry_create(&RW->identities, 65536);
  qa_body_link_hooks link; memset(&link, 0, sizeof link);
  link.ctx = &RR_LINK_CTX; link.absolute_bounds = rr_link_bounds; link.on_link = rr_link_noop; link.on_unlink = rr_unlink_noop;
  RW->bodies = qa_shared_body_table_create(RW->actors, link);
  RW->callbacks = qa_actor_callback_table_create(RW->actors);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  RW->combat = qa_gameplay_authority_create(RW->actors, RW->callbacks, hooks);
  RW->host.ctx = &RR_CTX;
  RW->host.actors = RW->actors; RW->host.bodies = RW->bodies; RW->host.callbacks = RW->callbacks; RW->host.combat = RW->combat;
  RW->host.now = rr_now_fn; RW->host.frame_seconds = rr_frame_fn; RW->host.random = rr_random_fn;
  RW->host.players = rr_players_fn; RW->host.emit = rr_emit_fn; RW->host.diagnostic = rr_diag_fn;
  RW->host.is_player = rr_is_player_fn;
  RW->game.ctx = &RR_CTX; RW->game.host = &RW->host; RW->game.counters = &RW->counters;
  RW->game.options.mode = mode; RW->game.options.edition = QA_Q2_EDITION_RERELEASE; RW->game.options.map_name = "test";
  RW->game.entities = rr_entities_fn; RW->game.entity = rr_entity_fn; RW->game.body = rr_body_fn;
  RW->game.remove = rr_remove_fn; RW->game.schedule = rr_schedule_fn; RW->game.use_targets = rr_use_targets_fn;
  RW->game.solid = rr_solid_fn; RW->game.motion = rr_motion_fn; RW->game.move = rr_move_fn; RW->game.show = rr_show_fn;
}
static void rr_teardown(void) {
  for (size_t i = 0; i < RW->entity_count; i++) { qa_q2_entity_destroy(RW->entities[i]); free(RW->entities[i]); }
  free(RW->entities);
  qa_gameplay_authority_destroy(RW->combat);
  qa_actor_callback_table_destroy(RW->callbacks);
  qa_shared_body_table_destroy(RW->bodies);
  qa_session_actor_registry_close(RW->actors);
  qa_session_actor_registry_destroy(RW->actors);
  qa_identity_owner_destroy(&RW->identities);
  free(RW);
  RW = NULL;
}
/* Initialized in place: the base Q2Players holds this object as its subclass self. */
static void rr_players(qa_q2_rerelease_players *players) {
  qa_q2_player_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &RR_PLAYER_HOOKS_CTX; hooks.movement = ph_movement; hooks.set_movement = ph_set_movement;
  hooks.emit = ph_emit; hooks.noise = ph_noise; hooks.banned = ph_banned;
  CHECK(qa_q2_rerelease_players_init(players, NULL, NULL, hooks, rr_hooks(), NULL, NULL, 0));
}
/* 70 eligible coop players: the relay must see all of them (the old port
 * capped the eligible list at 64). */
static void add_relay_players(qa_q2_rerelease_players *players, size_t outside_from) {
  for (size_t i = 0; i < 70; i++) {
    qa_vec3 origin = i >= outside_from ? v3(1000, 0, 0) : v3(0, 0, 0);
    qa_q2_entity *player = rr_add("player", origin, v3(-16, -16, -24), v3(16, 16, 32));
    RW->players[RW->players_n++] = player->actor.id;
    qa_combat_state state; memset(&state, 0, sizeof state);
    state.health = 100; state.can_take_damage = true; state.armor.regular.kind = QA_REGULAR_ARMOR_NONE; state.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(RW->combat, player->actor, state, NULL, NULL);
    qa_q2_base_player_state *st = malloc(sizeof *st);
    if (!st) abort();
    qa_q2_base_player_state_init(st, (double)i, 0);
    st->dead = false; st->spectator = false; st->noclip = false;
    qa_q2_players_set_state(players->base, player->actor.id, st);
    CHECK(qa_q2_players_get_state(players->base, player->actor.id) == st);
  }
}
/* Oracle (rr.ts): think with 69 inside / 1 outside: no fire, no remove;
 * log [schedule trigger_coop_relay 1]; 70 centerprints, 69 "wait", the
 * last to the 70th player with "waiting"; timestamp 15. */
static void test_coop_relay_think_over_64_players(void) {
  rr_setup(QA_Q2_MODE_COOP);
  qa_q2_rerelease_players players;
  rr_players(&players);
  qa_q2_rerelease_entities entities;
  CHECK(qa_q2_rerelease_entities_init(&entities, &players, rr_hooks(), NULL));
  qa_q2_entity *relay = rr_add("trigger_coop_relay", v3(0, 0, 0), v3(-100, -100, -100), v3(100, 100, 100));
  relay->message = "wait"; relay->map = "waiting"; relay->wait = 1;
  add_relay_players(&players, 69);
  entities.coop_relay_think.fn(entities.coop_relay_think.ctx, relay, &RW->game);
  CHECK(RW->use_targets_calls == 0);
  CHECK(RW->log_n == 1 && strcmp(RW->log[0], "schedule trigger_coop_relay 1") == 0);
  CHECK(RW->centerprints == 70 && RW->wait_texts == 69);
  CHECK(qa_same_actor(RW->last_actor, RW->players[69]) && strcmp(RW->last_text, "waiting") == 0);
  CHECK(relay->timestamp == 15);
  qa_q2_rerelease_entities_destroy(&entities);
  qa_q2_rerelease_players_destroy(&players);
  rr_teardown();
}
/* Oracle (rr.ts): use with all 70 outside: no relay; 71 centerprints, 70
 * "waiting", the last to the activator with "wait"; timestamp 15. */
static void test_coop_relay_use_over_64_players(void) {
  rr_setup(QA_Q2_MODE_COOP);
  qa_q2_rerelease_players players;
  rr_players(&players);
  qa_q2_rerelease_entities entities;
  CHECK(qa_q2_rerelease_entities_init(&entities, &players, rr_hooks(), NULL));
  qa_q2_entity *relay = rr_add("trigger_coop_relay", v3(0, 0, 0), v3(-10, -10, -10), v3(10, 10, 10));
  relay->message = "wait"; relay->map = "waiting";
  add_relay_players(&players, 0);
  qa_actor_id activator = RW->players[0];
  entities.coop_relay_use.fn(entities.coop_relay_use.ctx, relay, &RW->game, NULL, &activator);
  CHECK(RW->use_targets_calls == 0 && RW->log_n == 0);
  CHECK(RW->centerprints == 71 && RW->waiting_texts == 70);
  CHECK(qa_same_actor(RW->last_actor, activator) && strcmp(RW->last_text, "wait") == 0);
  CHECK(relay->timestamp == 15);
  qa_q2_rerelease_entities_destroy(&entities);
  qa_q2_rerelease_players_destroy(&players);
  rr_teardown();
}
/* The intermission camera picks authored[rereleaseRandom.integer(4) % n]
 * from the shared rerelease stream, never host.random (which returns 0.99
 * here: floor(0.99 * 4) % 3 would pick the first camera). Oracle (rr.ts,
 * cameras at x = 100, 200, 300): seed 1 -> 200, seed 3 -> 300, seed 7 ->
 * 100; host.random calls 0. */
static void test_intermission_camera_rerelease_random(void) {
  static const struct { uint32_t seed; double x; } cases[] = {{1, 200}, {3, 300}, {7, 100}};
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    rr_setup(QA_Q2_MODE_DEATHMATCH);
    for (int i = 0; i < 3; i++) rr_add("info_player_intermission", v3(100 * (i + 1), 0, 0), v3(0, 0, 0), v3(0, 0, 0));
    qa_q2_rerelease_random_init(&RW->random, cases[c].seed);
    RW->host.rerelease_random = &RW->random;
    qa_q2_rerelease_players players;
  rr_players(&players);
    qa_q2_rerelease_players_begin_rerelease_intermission(&players, &RW->game, "next", NULL, 0);
    CHECK(players.has_intermission_camera && players.intermission_camera_origin.x == cases[c].x);
    CHECK(RW->host_random_calls == 0);
    qa_q2_rerelease_players_destroy(&players);
    rr_teardown();
  }
}

int main(void) {
  test_plat_bottom_removes_all_areas();
  test_secret_touch_centerprint();
  test_nuke_use_removes_adjacent_monsters();
  test_mal_laser_off_beam();
  test_steering_controlled_trajectory();
  test_tesla_observations();
  test_registration_most_derived();
  test_sphere_pain_dopple_wait();
  test_hunter_camera_flags();
  test_nuke_flags_int32();
  test_chainfist_rerelease_observations();
  test_coop_relay_think_over_64_players();
  test_coop_relay_use_over_64_players();
  test_intermission_camera_rerelease_random();
  close_world();
  qa_q2_mover_module_destroy(MOVERS);
  CHECK(ctx_errors == 0);
  printf("test_q2_rows_mp2: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

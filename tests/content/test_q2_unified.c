/*
 * The Q2 game is one family: qa_q2_entity / qa_q2_game_services (TS
 * Q2Entity / Q2GameServices, implemented by Q2EntityServices) over one
 * qa_q2_foundation_host (TS Q2FoundationHost). Mission-pack entities run
 * on the same game the base entities use: xatrix target_mal_laser calls
 * q2TargetLaserThink (qa_q2_target_laser_think_fn) directly, and rogue
 * func_force_wall calls killQ2Box (qa_q2_kill_box) directly.
 *
 * Expected values come from the same scenario run in TS under bun:
 * Q2EntityServices over SessionActorRegistry/SharedBodyTable/
 * GameplayAuthority with createQ2CombatPolicy and nativeVictimArmor,
 * modules [new Q2MissionPackEntities("xatrix"|"rogue", hooks)], the same
 * trace stub (a live monster blocks at fraction 0.25 unless ignored or
 * excluded; a zero-length trace is clear; otherwise world at 0.5 with
 * plane normal (0,-1,0)).
 * Every callback checks its sentinel ctx. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/scenery.h"
#include "qa/content/q2/missionpacks/entities/index.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/world/gameplay/policies.h"

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

/* Distinct sentinel contexts: each callback asserts it got its own. */
static int HOST_SENTINEL, LINK_SENTINEL, COMBAT_SENTINEL, POLICY_SENTINEL, ARMOR_SENTINEL, HOOKS_SENTINEL;
static int ctx_failures = 0;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) { ctx_failures++; fprintf(stderr, "FAIL ctx %s:%d\n", __FILE__, __LINE__); } } while (0)

typedef struct trace_record { qa_vec3 start, end; bool has_ignore; qa_actor_id ignore; size_t exclude; double mask; } trace_record;
typedef struct world {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_combat_policy policy;
  qa_victim_armor_policy armor;
  qa_shared_inventory_table *inventory;
  qa_q2_foundation_host host;
  qa_q2mp_entities pack;
  qa_q2_callback_definitions pack_callbacks;
  qa_q2_spawn_module module;
  qa_q2_entity_services *services;
  qa_q2_game_services *game;
  double now;
  bool has_victim; qa_actor_id victim;
  qa_owned_actor scheduled[16]; double due[16]; size_t scheduled_count;
  qa_q2_presentation_event emits[16]; size_t emit_count;
  trace_record traces[16]; size_t trace_count;
  int diagnostics, pack_events;
} world;
static world *W;

/* ---- host (TS Q2FoundationHost) ---- */
static double h_now(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return W->now; }
static double h_frame(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.1; }
static double h_gravity(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 800; }
static double h_random(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.5; }
static void h_schedule(void *c, qa_owned_actor actor, const double *due) {
  EXPECT_CTX(c, HOST_SENTINEL);
  for (size_t i = 0; i < W->scheduled_count; i++) {
    if (qa_same_actor(W->scheduled[i].id, actor.id)) {
      if (due == NULL) { W->scheduled[i] = W->scheduled[--W->scheduled_count]; W->due[i] = W->due[W->scheduled_count]; }
      else W->due[i] = *due;
      return;
    }
  }
  if (due != NULL) { W->scheduled[W->scheduled_count] = actor; W->due[W->scheduled_count++] = *due; }
}
static void h_touch_triggers(void *c, qa_owned_actor a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static bool victim_blocks(qa_q2_trace_request r) {
  if (!W->has_victim) return false;
  qa_combat_state_result state = qa_gameplay_authority_read(W->combat, W->victim);
  if (!state.has_value || state.value.health <= 0) return false;
  for (size_t i = 0; i < r.exclude_count; i++) if (qa_same_actor(r.exclude[i], W->victim)) return false;
  return r.ignore == NULL || !qa_same_actor(*r.ignore, W->victim);
}
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  EXPECT_CTX(c, HOST_SENTINEL);
  trace_record *t = &W->traces[W->trace_count++];
  t->start = r.start; t->end = r.end; t->has_ignore = r.ignore != NULL; if (r.ignore) t->ignore = *r.ignore;
  t->exclude = r.exclude_count; t->mask = r.mask;
  qa_trace_result out;
  memset(&out, 0, sizeof out);
  if (victim_blocks(r)) {
    out.fraction = 0.25;
    out.end = (qa_vec3){r.start.x + (r.end.x - r.start.x) * 0.25, r.start.y + (r.end.y - r.start.y) * 0.25,
                        r.start.z + (r.end.z - r.start.z) * 0.25};
    out.hit.kind = QA_TRACE_HIT_ACTOR; out.hit.actor = W->victim;
    return out;
  }
  if (r.start.x == r.end.x && r.start.y == r.end.y && r.start.z == r.end.z) {
    out.fraction = 1; out.end = r.end; out.hit.kind = QA_TRACE_HIT_NONE;
    return out;
  }
  out.fraction = 0.5;
  out.end = (qa_vec3){(r.start.x + r.end.x) / 2, (r.start.y + r.end.y) / 2, (r.start.z + r.end.z) / 2};
  out.hit.kind = QA_TRACE_HIT_WORLD;
  out.contact.has_plane = true; out.contact.plane.normal = (qa_vec3){0, -1, 0};
  out.contents = 1;
  return out;
}
static double h_contents(void *c, qa_vec3 p) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)b; return true; }
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) { EXPECT_CTX(c, HOST_SENTINEL); (void)o; (void)r; *n = 0; return NULL; }
static const qa_actor_id *h_players(void *c, size_t *n) { EXPECT_CTX(c, HOST_SENTINEL); *n = 0; return NULL; }
static bool h_is_player(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; return false; }
static bool h_is_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); return W->has_victim && qa_same_actor(a, W->victim); }
static qa_bounds h_inline_bounds(void *c, double m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; qa_bounds b; memset(&b, 0, sizeof b); return b; }
static void h_set_solid(void *c, qa_owned_actor a, const char *s, const double *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)s; (void)m; }
static void h_set_motion(void *c, qa_q2_motion m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }
static void h_portal(void *c, double p, bool o) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) { EXPECT_CTX(c, HOST_SENTINEL); if (W->emit_count < 16) W->emits[W->emit_count++] = e; }
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; qa_q2_player_view_result r; memset(&r, 0, sizeof r); r.has_value = true; return r; }
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_SENTINEL); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; W->diagnostics++; }

/* ---- world tables ---- */
static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) { EXPECT_CTX(c, LINK_SENTINEL); return qa_translated_body_bounds(a, b); }
static void link_on(void *c, qa_linked_body b) { EXPECT_CTX(c, LINK_SENTINEL); (void)b; }
static void link_off(void *c, qa_actor_id a) { EXPECT_CTX(c, LINK_SENTINEL); (void)a; }
static void combat_impulse(void *c, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)i; (void)m; }
static void combat_before(void *c, qa_owned_actor a, qa_damage_decision d) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)d; }
static void combat_confirmed(void *c, qa_damage_outcome o) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)o; }
static qa_q2_combat_context policy_context(void *c, qa_damage_request r, qa_combat_state t, const qa_combat_state *a) {
  EXPECT_CTX(c, POLICY_SENTINEL); (void)r; (void)t; (void)a;
  qa_q2_combat_context out;
  memset(&out, 0, sizeof out);
  out.arithmetic = "binary64"; out.monster = true; out.has_enemy = true; out.friendly_fire = true;
  return out;
}
static qa_victim_armor_context armor_context(void *c, qa_damage_request r) {
  EXPECT_CTX(c, ARMOR_SENTINEL); (void)r;
  qa_victim_armor_context out;
  memset(&out, 0, sizeof out);
  out.screen_facing_dot = 1; out.arithmetic = "binary64";
  return out;
}

/* ---- mission-pack entity hooks (TS Q2MissionPackEntityHooks) ---- */
static void mp_teleport(void *c, qa_actor_id a, qa_vec3 o, qa_vec3 g) { EXPECT_CTX(c, HOOKS_SENTINEL); (void)a; (void)o; (void)g; }
static void mp_anger(void *c, qa_q2_entity *m, qa_q2_entity *t, qa_q2_game_services *g) { EXPECT_CTX(c, HOOKS_SENTINEL); (void)m; (void)t; (void)g; }
static void mp_emit(void *c, qa_q2mp_entity_event e) { EXPECT_CTX(c, HOOKS_SENTINEL); (void)e; W->pack_events++; }
static bool module_spawn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  if (c != &W->pack) ctx_failures++;
  return qa_q2mp_entities_spawn(c, e, g);
}

static void world_create(qa_q2mp_pack pack) {
  W = calloc(1, sizeof *W);
  qa_identity_owner_create(&W->identities, "q2-unified");
  W->actors = qa_session_actor_registry_create(&W->identities, 4096);
  qa_body_link_hooks link = {&LINK_SENTINEL, link_bounds, link_on, link_off};
  W->bodies = qa_shared_body_table_create(W->actors, link);
  W->callbacks = qa_actor_callback_table_create(W->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &COMBAT_SENTINEL; hooks.impulse = combat_impulse; hooks.before_reaction = combat_before; hooks.confirmed = combat_confirmed;
  W->combat = qa_gameplay_authority_create(W->actors, W->callbacks, hooks);
  W->inventory = qa_shared_inventory_table_create(W->actors);
  qa_q2_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = "q2:combat"; policy.ctx = &POLICY_SENTINEL; policy.context = policy_context;
  W->armor = policy.armor = qa_native_victim_armor(armor_context, &ARMOR_SENTINEL);
  W->policy = qa_create_q2_combat_policy(policy);
  (void)qa_gameplay_authority_register(W->combat, W->policy);

  qa_q2_foundation_host *h = &W->host;
  h->ctx = &HOST_SENTINEL;
  h->actors = W->actors; h->bodies = W->bodies; h->callbacks = W->callbacks; h->combat = W->combat; h->inventory = W->inventory;
  h->now = h_now; h->frame_seconds = h_frame; h->gravity = h_gravity; h->random = h_random; h->schedule = h_schedule;
  h->touch_triggers = h_touch_triggers; h->trace = h_trace; h->point_contents = h_contents; h->in_pvs = h_true2;
  h->in_phs = h_true2; h->areas_connected = h_true2; h->nearby = h_nearby; h->players = h_players;
  h->is_player = h_is_player; h->is_monster = h_is_monster; h->inline_model_bounds = h_inline_bounds;
  h->set_solid = h_set_solid; h->set_motion = h_set_motion; h->set_area_portal = h_portal; h->emit = h_emit;
  h->player_view_state = h_view; h->key_consumed = h_key; h->prepare_level_change = h_prepare;
  h->transition = h_transition; h->diagnostic = h_diagnostic;

  qa_q2mp_entity_hooks mp;
  memset(&mp, 0, sizeof mp);
  mp.ctx = &HOOKS_SENTINEL; mp.teleport_player = mp_teleport; mp.target_anger = mp_anger; mp.emit = mp_emit;
  qa_q2mp_entities_init(&W->pack, pack, mp);
  W->pack_callbacks = qa_q2mp_entities_callbacks(&W->pack);
  W->module.ctx = &W->pack; W->module.callbacks = &W->pack_callbacks; W->module.spawn = module_spawn;

  qa_q2_game_options options;
  memset(&options, 0, sizeof options);
  options.edition = QA_Q2_EDITION_CLASSIC; options.map_name = "unified"; options.skill = 1;
  options.mode = QA_Q2_MODE_SINGLEPLAYER; options.max_clients = 1; options.provider = "q2:official";
  options.campaign = "q2:base"; options.combat_provider = "q2:combat"; options.inventory_provider = "q2:inventory";
  options.movement_provider = "q2:movement";
  W->services = qa_q2_entity_services_create(&W->host, options, &W->module, 1);
  W->game = qa_q2_entity_services_game(W->services);
}
static void world_destroy(void) {
  qa_q2_entity_services_destroy(W->services);
  qa_shared_inventory_table_destroy(W->inventory);
  qa_gameplay_authority_destroy(W->combat);
  qa_combat_policy_free(W->policy);
  qa_victim_armor_policy_free(W->armor);
  qa_actor_callback_table_destroy(W->callbacks);
  qa_shared_body_table_destroy(W->bodies);
  qa_session_actor_registry_close(W->actors);
  qa_session_actor_registry_destroy(W->actors);
  qa_identity_owner_destroy(&W->identities);
  free(W);
  W = NULL;
}
static void advance(double t) {
  W->now = t;
  qa_owned_actor due[16];
  size_t n = 0;
  for (size_t i = 0; i < W->scheduled_count;) {
    if (W->due[i] <= t) { due[n++] = W->scheduled[i]; W->scheduled[i] = W->scheduled[--W->scheduled_count]; W->due[i] = W->due[W->scheduled_count]; }
    else i++;
  }
  for (size_t i = 0; i < n; i++) {
    if (!qa_session_actor_registry_is_live(W->actors, due[i].id)) continue;
    qa_frame_context frame;
    memset(&frame, 0, sizeof frame);
    frame.frame = qa_js_round(t * 10);
    frame.time.kind = QA_SOURCE_TIME_SECONDS; frame.time.value = t;
    frame.elapsed.kind = QA_SOURCE_TIME_SECONDS; frame.elapsed.value = 0.1;
    frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
    (void)qa_actor_callback_table_think(W->callbacks, due[i], frame);
  }
}
static qa_q2_entity *add_victim(qa_vec3 origin) {
  char text[64];
  snprintf(text, sizeof text, "%s %s %s", QA_JS_NUMBER(origin.x), QA_JS_NUMBER(origin.y), QA_JS_NUMBER(origin.z));
  qa_q2_field_pair pair = {"origin", text};
  qa_q2_entity *entity = W->game->create(W->game->ctx, "monster_dummy", &pair, 1);
  qa_q2_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_bounds = true; changes.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  W->game->move(W->game->ctx, entity, changes, true);
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = 100; state.mass = 200; state.can_take_damage = true;
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE; state.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(W->combat, entity->actor, state, NULL, NULL);
  W->has_victim = true; W->victim = entity->actor.id;
  return entity;
}
static qa_q2_entity *spawn(double ordinal, const char *classname, const char *const *pairs, size_t count) {
  qa_q2_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  fields.ordinal = ordinal; fields.classname = classname;
  if (!qa_q2_spawn_set(&fields, "classname", classname)) abort();
  for (size_t i = 0; i < count; i++) if (!qa_q2_spawn_set(&fields, pairs[2 * i], pairs[2 * i + 1])) abort();
  fields.classname = qa_q2_spawn_get(&fields, "classname");
  return W->game->spawn(W->game->ctx, fields);
}
static double health(qa_actor_id actor) {
  qa_combat_state_result r = qa_gameplay_authority_read(W->combat, actor);
  return r.has_value ? r.value.health : NAN;
}
static bool vec_eq(qa_vec3 a, double x, double y, double z) { return a.x == x && a.y == y && a.z == z; }

static void test_xatrix_laser(void) {
  world_create(QA_Q2MP_PACK_XATRIX);
  qa_q2_entity *victim = add_victim((qa_vec3){0, 200, 0});
  static const char *const pairs[] = {"origin", "0 0 0", "angles", "0 90 0", "spawnflags", "1", "dmg", "7"};
  qa_q2_entity *laser = spawn(3, "target_mal_laser", pairs, 4);
  /* TS after-spawn 2147483649 0.2 4 0 {6.123233995736766e-17,1,0} 0.1 0.1 */
  CHECK(qa_js_to_uint32(laser->spawnflags) == 2147483649u);
  CHECK(laser->has_next_think && laser->next_think == 0.2);
  CHECK(laser->frame == 4 && laser->skin == 0);
  CHECK(vec_eq(laser->movedir, 6.123233995736766e-17, 1, 0));
  CHECK(laser->wait == 0.1 && laser->delay == 0.1);
  W->emit_count = 0;
  advance(0.2);
  /* TS: health 93; spawnflags 2147483649 next 0.4 */
  CHECK(health(victim->actor.id) == 93);
  CHECK(qa_js_to_uint32(laser->spawnflags) == 2147483649u);
  CHECK(laser->has_next_think && laser->next_think == 0.4);
  /* TS traces: two, the second past the monster with it excluded. */
  CHECK(W->trace_count == 2);
  if (W->trace_count == 2) {
    trace_record *a = &W->traces[0], *b = &W->traces[1];
    CHECK(vec_eq(a->start, 0, 0, 0) && vec_eq(a->end, 1.2540383223268897e-13, 2048, 0));
    CHECK(a->has_ignore && qa_same_actor(a->ignore, laser->actor.id) && a->exclude == 0 && a->mask == 100663297);
    CHECK(vec_eq(b->start, 3.135095805817224e-14, 512, 0) && vec_eq(b->end, 1.2540383223268897e-13, 2048, 0));
    CHECK(b->has_ignore && qa_same_actor(b->ignore, victim->actor.id) && b->exclude == 1 && b->mask == 100663297);
  }
  /* TS emits: laser-sparks effect then the visible beam. */
  CHECK(W->emit_count == 2);
  if (W->emit_count == 2) {
    qa_q2_presentation_event *e = &W->emits[0], *beam = &W->emits[1];
    CHECK(strcmp(e->kind, "effect") == 0 && strcmp(e->effect, "q2:laser-sparks") == 0);
    CHECK(vec_eq(e->origin, 7.837739514543061e-14, 1280, 0) && vec_eq(e->direction, 0, -1, 0));
    CHECK(e->count == 8 && e->color == 0);
    CHECK(strcmp(beam->kind, "beam") == 0 && beam->has_actor && qa_same_actor(beam->actor, laser->actor.id));
    CHECK(vec_eq(beam->start, 0, 0, 0) && vec_eq(beam->end, 7.837739514543061e-14, 1280, 0));
    CHECK(beam->width == 4 && beam->color == 0 && beam->visible);
  }
  CHECK(W->diagnostics == 0 && W->pack_events == 0);
  world_destroy();
}

static void test_rogue_force_wall_kill_box(void) {
  world_create(QA_Q2MP_PACK_ROGUE);
  static const char *const pairs[] = {"origin", "10 20 30"};
  qa_q2_entity *wall = spawn(4, "func_force_wall", pairs, 1);
  qa_q2_entity *victim = add_victim((qa_vec3){10, 20, 30});
  /* TS wall 1 none {10,20,30} {10,20,30} 208 */
  CHECK(wall->wait == 1 && strcmp(wall->solid, "none") == 0);
  CHECK(vec_eq(wall->pos1, 10, 20, 30) && vec_eq(wall->pos2, 10, 20, 30) && wall->style == 208);
  W->emit_count = 0; W->trace_count = 0;
  CHECK(wall->use.fn != NULL);
  if (wall->use.fn) wall->use.fn(wall->use.ctx, wall, W->game, NULL, NULL);
  /* TS: wait 0 solid brush next 0.1; health -999 */
  CHECK(wall->wait == 0 && strcmp(wall->solid, "brush") == 0);
  CHECK(wall->has_next_think && wall->next_think == 0.1);
  CHECK(health(victim->actor.id) == -999);
  CHECK(W->trace_count == 2);
  for (size_t i = 0; i < W->trace_count; i++) {
    trace_record *t = &W->traces[i];
    CHECK(vec_eq(t->start, 10, 20, 30) && vec_eq(t->end, 10, 20, 30));
    CHECK(t->has_ignore && qa_same_actor(t->ignore, wall->actor.id) && t->exclude == 0 && t->mask == 33619971);
  }
  CHECK(W->emit_count == 0 && W->pack_events == 0 && W->diagnostics == 0);
  /* TS direct killQ2Box: true, -999, 2 traces. */
  qa_q2_entity *second = add_victim((qa_vec3){0, 0, 0});
  W->trace_count = 0;
  CHECK(qa_q2_kill_box(wall, W->game));
  CHECK(health(second->actor.id) == -999);
  CHECK(W->trace_count == 2);
  world_destroy();
}

int main(void) {
  test_xatrix_laser();
  test_rogue_force_wall_kill_box();
  CHECK(ctx_failures == 0);
  printf("test_q2_unified: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

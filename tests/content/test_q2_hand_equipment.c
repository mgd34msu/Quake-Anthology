/*
 * Port of the offhand cases of tests/gameplay/foundation/q2/weapons/
 * hand-equipment.test.ts that run on Q2HandGrenadeEquipment alone (the
 * native-weapon and combat-policy cases need the Q2 combat policy and are
 * covered with the weapons port). Expected values are the TS test's, which
 * pass under bun. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/equipment/hand_grenades.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* Sentinel contexts: every leaf checks it receives the context its author passed. */
static int HOST_CTX, WEAPON_CTX, LINK_CTX, PROJECT_CTX;
#define EXPECT_CTX(ctx, sentinel) CHECK((ctx) == &(sentinel))

typedef enum modifier { MOD_NONE, MOD_DAMAGE, MOD_RETIRE_SOURCE, MOD_SOURCE, MOD_CADENCE } modifier;
typedef struct fixture {
  qa_q2_edition edition;
  double now;
  modifier mod;
  int calls;
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_body_table *bodies;
  qa_shared_inventory_table *inventory;
  qa_gameplay_authority *combat;
  qa_q2_foundation_host host;
  qa_q2_entity_services *services;
  qa_q2_game_services *game;
  qa_q2w_weapons weapons;
  qa_q2_hand_grenade_equipment *equipment;
  qa_owned_actor world, player;
  qa_hand_grenade_equipment_input hand;
} fixture;
static fixture *F;

static const qa_hand_grenade_loadout LOADOUT = {true, 1, 50, false};

static double h_now(void *c) { EXPECT_CTX(c, HOST_CTX); return F->now; }
static double h_frame(void *c) { EXPECT_CTX(c, HOST_CTX); return 0.1; }
static double h_gravity(void *c) { EXPECT_CTX(c, HOST_CTX); return 800; }
static double h_random(void *c) { EXPECT_CTX(c, HOST_CTX); return 0.5; }
static void h_schedule(void *c, qa_owned_actor a, const double *d) { EXPECT_CTX(c, HOST_CTX); (void)a; (void)d; }
static void h_touch(void *c, qa_owned_actor a) { EXPECT_CTX(c, HOST_CTX); (void)a; }
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  EXPECT_CTX(c, HOST_CTX);
  qa_trace_result t = {0};
  t.kind = "q2"; t.fraction = 1; t.end = r.end;
  return t;
}
static double h_contents(void *c, qa_vec3 p) { EXPECT_CTX(c, HOST_CTX); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(c, HOST_CTX); (void)a; (void)b; return true; }
static const qa_actor_id *h_players(void *c, size_t *n) {
  EXPECT_CTX(c, HOST_CTX);
  static qa_actor_id ids[1];
  ids[0] = F->player.id; *n = 1;
  return ids;
}
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) { (void)o; (void)r; return h_players(c, n); }
static qa_actor_id h_world(void *c) { EXPECT_CTX(c, HOST_CTX); return F->world.id; }
static bool h_is_player(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); return qa_same_actor(a, F->player.id); }
static bool h_is_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); (void)a; return false; }
static qa_bounds h_inline(void *c, double m) { EXPECT_CTX(c, HOST_CTX); (void)m; qa_bounds b = {0}; return b; }
static void h_solid(void *c, qa_owned_actor a, const char *s, const double *m) { EXPECT_CTX(c, HOST_CTX); (void)a; (void)s; (void)m; }
static void h_motion(void *c, qa_q2_motion m) { EXPECT_CTX(c, HOST_CTX); (void)m; }
static void h_portal(void *c, double p, bool o) { EXPECT_CTX(c, HOST_CTX); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) { EXPECT_CTX(c, HOST_CTX); (void)e; }
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); (void)a; qa_q2_player_view_result r = {0}; return r; }
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_CTX); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_CTX); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_CTX); qa_raise("Error", 0, 0, "%s", m); }
static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state s) { EXPECT_CTX(c, LINK_CTX); return qa_translated_body_bounds(a, s); }
static void link_noop(void *c, qa_linked_body b) { EXPECT_CTX(c, LINK_CTX); (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { EXPECT_CTX(c, LINK_CTX); (void)a; }

static void w_emit(void *c, qa_q2w_event e) { EXPECT_CTX(c, WEAPON_CTX); (void)e; }
static void w_noise(void *c, qa_actor_id a, qa_vec3 o, bool s) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)o; (void)s; }
static void w_dodge(void *c, qa_actor_id m, qa_actor_id a, double e, qa_trace_result t) { EXPECT_CTX(c, WEAPON_CTX); (void)m; (void)a; (void)e; (void)t; }
static void w_ammo(void *c, qa_actor_id a, qa_item_id i) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)i; }
static bool w_target(void *c, const qa_actor_id *a, qa_actor_id t) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)t; return true; }
static double w_quad(void *c, qa_actor_id a) { EXPECT_CTX(c, WEAPON_CTX); (void)a; return 3; }
static double w_source(void *c, qa_actor_id a) {
  EXPECT_CTX(c, WEAPON_CTX);
  CHECK(qa_same_actor(a, F->player.id));
  F->calls++;
  if (F->mod == MOD_RETIRE_SOURCE) qa_session_actor_registry_release(F->actors, F->player);
  return F->mod == MOD_DAMAGE ? 2 : 6;
}
static double w_interval(void *c, qa_actor_id a, double s) {
  EXPECT_CTX(c, WEAPON_CTX); (void)s;
  CHECK(qa_same_actor(a, F->player.id));
  F->calls++;
  qa_session_actor_registry_release(F->actors, F->player);
  return 0.05;
}

/* TS project: eye = origin + 22 z; classic adds z after forward/right;
 * rerelease aims the muzzle at the eye trace end. */
static qa_q2w_projection project(void *c, qa_vec3 angles, qa_vec3 offset) {
  EXPECT_CTX(c, PROJECT_CTX);
  qa_body_state_result body = qa_shared_body_table_read(F->bodies, F->player.id);
  if (!body.has_value) qa_raise("Error", 0, 0, "Missing test actor body");
  qa_angle_vectors axes = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 eye = qa_q2_add(body.value.origin, (qa_vec3){0, 0, 22});
  qa_vec3 start = qa_q2_add(qa_q2_add(eye, qa_q2_scale(axes.forward, offset.x)), qa_q2_scale(axes.right, offset.y));
  if (F->edition == QA_Q2_EDITION_CLASSIC) return (qa_q2w_projection){qa_q2_add(start, (qa_vec3){0, 0, offset.z}), axes.forward};
  qa_vec3 muzzle = qa_q2_add(start, qa_q2_scale(axes.up, offset.z));
  qa_q2_trace_request request = {0};
  request.start = eye; request.end = qa_q2_add(eye, qa_q2_scale(axes.forward, 8192)); request.mask = 0x6000003;
  qa_trace_result trace = F->host.trace(F->host.ctx, request);
  return (qa_q2w_projection){muzzle, qa_q2_normalize(qa_q2_subtract(trace.end, muzzle))};
}

static void fixture_open(fixture *f, qa_q2_edition edition, modifier mod) {
  memset(f, 0, sizeof *f);
  F = f;
  f->edition = edition; f->mod = mod;
  CHECK(qa_identity_owner_create(&f->identities, "q2-weapons"));
  f->actors = qa_session_actor_registry_create(&f->identities, 1024);
  f->callbacks = qa_actor_callback_table_create(f->actors);
  qa_body_link_hooks link = {0};
  link.ctx = &LINK_CTX; link.absolute_bounds = link_bounds; link.on_link = link_noop; link.on_unlink = unlink_noop;
  f->bodies = qa_shared_body_table_create(f->actors, link);
  f->inventory = qa_shared_inventory_table_create(f->actors);
  qa_gameplay_hooks gameplay = {0};
  f->combat = qa_gameplay_authority_create(f->actors, f->callbacks, gameplay);
  f->world = qa_session_actor_registry_allocate_at_source(f->actors, "q2:game", 0, "q2:worldspawn");
  qa_body_state world_body = {0};
  qa_shared_body_table_create_body(f->bodies, f->world, world_body);
  f->player = qa_session_actor_registry_allocate(f->actors, "q3:character", "q3:sarge");
  qa_body_state body = {0};
  body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  qa_shared_body_table_create_body(f->bodies, f->player, body);
  qa_combat_state combat = {0};
  combat.health = 100; combat.mass = 200; combat.can_take_damage = true; combat.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(f->combat, f->player, combat, NULL, NULL);
  qa_inventory_entry railgun = {.item = "q3:weapon/railgun", .count = 1, .capacity = 1};
  qa_shared_inventory_table_create_entries(f->inventory, f->player, &railgun, 1);
  qa_q2_foundation_host *h = &f->host;
  h->ctx = &HOST_CTX;
  h->actors = f->actors; h->bodies = f->bodies; h->callbacks = f->callbacks; h->combat = f->combat; h->inventory = f->inventory;
  h->now = h_now; h->frame_seconds = h_frame; h->gravity = h_gravity; h->random = h_random; h->schedule = h_schedule;
  h->touch_triggers = h_touch; h->trace = h_trace; h->point_contents = h_contents; h->in_pvs = h_true2; h->in_phs = h_true2;
  h->areas_connected = h_true2; h->nearby = h_nearby; h->players = h_players; h->world_actor = h_world;
  h->is_player = h_is_player; h->is_monster = h_is_monster; h->inline_model_bounds = h_inline; h->set_solid = h_solid;
  h->set_motion = h_motion; h->set_area_portal = h_portal; h->emit = h_emit; h->player_view_state = h_view;
  h->key_consumed = h_key; h->prepare_level_change = h_prepare; h->transition = h_transition; h->diagnostic = h_diagnostic;
  qa_q2_game_options options = {0};
  options.edition = edition; options.map_name = "weapon-check"; options.skill = 1; options.mode = QA_Q2_MODE_SINGLEPLAYER;
  options.max_clients = 1; options.provider = "q2:game"; options.campaign = "q2:campaign";
  options.combat_provider = "q2:combat"; options.inventory_provider = "q2:inventory"; options.movement_provider = "q1:movement";
  f->services = qa_q2_entity_services_create(h, options, NULL, 0);
  CHECK(f->services != NULL);
  f->game = qa_q2_entity_services_game(f->services);
  qa_q2w_hooks wh = {0};
  wh.ctx = &WEAPON_CTX; wh.emit = w_emit; wh.noise = w_noise; wh.dodge = w_dodge; wh.ammo_changed = w_ammo; wh.can_target = w_target;
  if (mod == MOD_DAMAGE) { wh.quad_multiplier = w_quad; wh.source_damage_multiplier = w_source; }
  if (mod == MOD_SOURCE || mod == MOD_RETIRE_SOURCE) wh.source_damage_multiplier = w_source;
  if (mod == MOD_CADENCE) wh.firing_interval = w_interval;
  CHECK(qa_q2w_weapons_init(&f->weapons, wh));
  f->equipment = qa_q2_hand_grenade_equipment_create(f->game, &f->weapons.base);
  CHECK(f->equipment != NULL);
  qa_q2_hand_grenade_equipment_configure(f->equipment, f->player.id, LOADOUT);
  f->hand = (qa_hand_grenade_equipment_input){.gravity = 800, .players_collide = true, .held = true,
    .lifecycle = QA_Q2W_ALIVE, .project = {&PROJECT_CTX, project}};
}
static void fixture_close(fixture *f) {
  qa_q2_hand_grenade_equipment_destroy(f->equipment);
  qa_q2_entity_services_destroy(f->services);
  qa_q2w_weapons_destroy(&f->weapons);
  qa_shared_inventory_table_destroy(f->inventory);
  qa_gameplay_authority_destroy(f->combat);
  qa_actor_callback_table_destroy(f->callbacks);
  qa_shared_body_table_destroy(f->bodies);
  qa_session_actor_registry_close(f->actors);
  qa_session_actor_registry_destroy(f->actors);
  qa_identity_owner_destroy(&f->identities);
}
static void step(fixture *f, double seconds, qa_hand_grenade_equipment_input in) {
  f->now = seconds;
  qa_q2_hand_grenade_equipment_step(f->equipment, f->player.id, in);
}
static qa_hand_grenade_equipment_input with(qa_hand_grenade_equipment_input in, bool pressed, bool held, bool released) {
  in.pressed = pressed; in.held = held; in.released = released;
  return in;
}
static qa_q2_entity *entity_named(fixture *f, const char *classname) {
  size_t n = 0;
  qa_q2_entity *const *all = f->game->entities(f->game->ctx, &n);
  for (size_t i = 0; i < n; i++) if (strcmp(all[i]->classname, classname) == 0) return all[i];
  return NULL;
}
static size_t entity_count(fixture *f) {
  size_t n = 0;
  (void)f->game->entities(f->game->ctx, &n);
  return n;
}
static double grenades(fixture *f) { return qa_shared_inventory_table_count(f->inventory, f->player.id, QA_HAND_GRENADE_AMMO); }
static bool action_is(fixture *f, qa_q2w_hand_action_kind kind) {
  qa_hand_grenade_equipment_state state;
  return qa_q2_hand_grenade_equipment_state_of(f->equipment, f->player.id, &state) && state.action.kind == kind;
}
static bool has_state(fixture *f) {
  qa_hand_grenade_equipment_state state;
  return qa_q2_hand_grenade_equipment_state_of(f->equipment, f->player.id, &state);
}

/* "offhand grenade reuses primary damage hooks only at emission and keeps independent stacking" */
static void test_damage_hooks(void) {
  for (int no_stack = 0; no_stack < 2; no_stack++) {
    fixture f;
    fixture_open(&f, QA_Q2_EDITION_CLASSIC, MOD_DAMAGE);
    step(&f, 0, with(f.hand, true, true, false));
    for (int frame = 1; frame <= 11; frame++) step(&f, frame / 10.0, f.hand);
    CHECK(f.calls == 0);
    qa_hand_grenade_equipment_input powers = f.hand;
    powers.quad_until = 10; powers.double_until = 10; powers.no_stack_double = no_stack;
    step(&f, 1.2, with(powers, false, false, true));
    CHECK(f.calls == 0);
    step(&f, 1.3, with(powers, false, false, false));
    qa_q2_entity *grenade = entity_named(&f, "hgrenade");
    CHECK(grenade != NULL && grenade->damage == 125 * 3 * 2 * (no_stack ? 1 : 2));
    CHECK(f.calls == 1);
    step(&f, 1.4, with(f.hand, false, false, false));
    CHECK(f.calls == 1);
    fixture_close(&f);
  }
}

/* "complete source multiplier is independent of Quad timers and cannot emit after owner retirement" */
static void test_source_multiplier(void) {
  for (int retire = 0; retire < 2; retire++) {
    fixture f;
    fixture_open(&f, QA_Q2_EDITION_RERELEASE, retire ? MOD_RETIRE_SOURCE : MOD_SOURCE);
    step(&f, 0, with(f.hand, true, true, false));
    for (int frame = 1; frame <= 11; frame++) step(&f, frame / 10.0, f.hand);
    CHECK(f.calls == 0);
    step(&f, 1.2, with(f.hand, false, false, true));
    CHECK(f.calls == 1);
    qa_q2_entity *grenade = entity_named(&f, "hand_grenade");
    if (retire) { CHECK(grenade == NULL); CHECK(!has_state(&f)); }
    else CHECK(grenade != NULL && grenade->damage == 750);
    fixture_close(&f);
  }
}

/* "cadence source retirement cannot republish an equipment action" */
static void test_cadence_retirement(void) {
  fixture f;
  fixture_open(&f, QA_Q2_EDITION_CLASSIC, MOD_CADENCE);
  step(&f, 0, f.hand);
  CHECK(f.calls == 0);
  step(&f, 0, with(f.hand, true, true, false));
  CHECK(f.calls == 1);
  CHECK(!has_state(&f));
  CHECK(entity_count(&f) == 0);
  fixture_close(&f);
}

typedef struct configure_call { fixture *f; qa_hand_grenade_loadout loadout; } configure_call;
static void configure_fn(void *ctx) {
  configure_call *c = ctx;
  qa_q2_hand_grenade_equipment_configure(c->f->equipment, c->f->player.id, c->loadout);
}

/* "disable refunds only unprimed ammunition, pre-removal resolves primed ammunition, release cleanup is actor independent" */
static void test_disable_and_removal(void) {
  fixture unprimed;
  fixture_open(&unprimed, QA_Q2_EDITION_CLASSIC, MOD_NONE);
  step(&unprimed, 0, with(unprimed.hand, true, true, false));
  qa_hand_grenade_loadout disabled = LOADOUT;
  disabled.enabled = false;
  qa_q2_hand_grenade_equipment_configure(unprimed.equipment, unprimed.player.id, disabled);
  step(&unprimed, 0.1, unprimed.hand);
  CHECK(grenades(&unprimed) == 1);
  fixture_close(&unprimed);

  fixture primed;
  fixture_open(&primed, QA_Q2_EDITION_RERELEASE, MOD_NONE);
  step(&primed, 0, with(primed.hand, true, true, false));
  for (int frame = 1; frame <= 10; frame++) step(&primed, frame / 10.0, primed.hand);
  configure_call call = {&primed, LOADOUT};
  call.loadout.infinite_ammo = true;
  qa_caught_error caught;
  CHECK(!qa_try(configure_fn, &call, &caught) && strstr(caught.message, "reserved") != NULL);
  qa_hand_grenade_equipment_input removing = primed.hand;
  removing.lifecycle = QA_Q2W_REMOVING;
  step(&primed, 2, removing);
  CHECK(grenades(&primed) == 0);
  size_t n = 0;
  qa_q2_entity *const *all = primed.game->entities(primed.game->ctx, &n);
  CHECK(n >= 1 && all[0]->spawnflags == 1);
  qa_session_actor_registry_release(primed.actors, primed.player);
  CHECK(!has_state(&primed));
  fixture_close(&primed);

  fixture removed;
  fixture_open(&removed, QA_Q2_EDITION_CLASSIC, MOD_NONE);
  step(&removed, 0, with(removed.hand, true, true, false));
  qa_session_actor_registry_release(removed.actors, removed.player);
  CHECK(!has_state(&removed));
  fixture_close(&removed);
}

/* "cooking checkpoint restores the debited shared inventory without replaying
 * effects or allowance" (equipment half; the persistence round trip and its
 * corrupt-frame check belong to persistence/q2-hand-grenades). */
static void test_cooking_checkpoint(void) {
  fixture original;
  fixture_open(&original, QA_Q2_EDITION_RERELEASE, MOD_NONE);
  step(&original, 0, with(original.hand, true, true, false));
  for (int frame = 1; frame <= 10; frame++) step(&original, frame / 10.0, original.hand);
  qa_hand_grenades_checkpoint saved = qa_q2_hand_grenade_equipment_capture(original.equipment);
  CHECK(saved.actor_count == 1 && saved.actors[0].state.action.kind == QA_Q2W_HA_COOKING);
  qa_hand_grenade_checkpoint_entry entry = saved.actors[0];
  fixture_close(&original);

  fixture restored;
  fixture_open(&restored, QA_Q2_EDITION_RERELEASE, MOD_NONE);
  /* The original fixture's player had spent its one grenade; same actor slot here. */
  qa_inventory_entry spent = {.item = QA_HAND_GRENADE_AMMO, .count = 0, .capacity = 50};
  qa_shared_inventory_table_configure(restored.inventory, restored.player, spent, NULL, NULL);
  /* TS: both fixtures' fresh sessions give the player the same slot and
   * generation. C keys session generations by token address, and a reused
   * address continues the old map, so name the restored player directly. */
  CHECK(entry.actor.slot == (double)restored.player.id.slot);
  entry.actor.generation = (double)restored.player.id.generation;
  qa_hand_grenades_checkpoint cp = {1, QA_Q2_EDITION_RERELEASE, &entry, 1};
  qa_q2_hand_grenade_equipment_restore(restored.equipment, cp);
  CHECK(entity_count(&restored) == 0);
  CHECK(grenades(&restored) == 0);
  qa_q2_hand_grenade_equipment_configure(restored.equipment, restored.player.id, LOADOUT);
  CHECK(action_is(&restored, QA_Q2W_HA_COOKING));
  step(&restored, 2, with(restored.hand, false, false, true));
  qa_q2_entity *grenade = entity_named(&restored, "hand_grenade");
  CHECK(grenade != NULL && grenade->has_next_think && grenade->next_think == 4.2);
  CHECK(grenades(&restored) == 0);
  step(&restored, 2.1, with(restored.hand, false, false, false));
  CHECK(entity_count(&restored) == 1);
  fixture_close(&restored);
}

int main(void) {
  test_damage_hooks();
  test_source_multiplier();
  test_cadence_retirement();
  test_disable_and_removal();
  test_cooking_checkpoint();
  if (failures) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("test_q2_hand_equipment: %d checks, 0 failures\n", checks);
  return 0;
}

/*
 * TS drift packet D4 (src/content/q2) checks. Each test reproduces a TS test
 * added or changed after the C port's baseline; expected values are the TS
 * assertions, confirmed by running the TS tests under bun:
 *   tests/gameplay/foundation/q2/foundation.test.ts (func_button touch,
 *     g_instagib, g_infinite_ammo),
 *   tests/gameplay/foundation/q2/monsters/monsters.test.ts (rerelease Xatrix
 *     soldier variants),
 *   tests/gameplay/q2-missionpacks/gekk-jump.test.ts and gekk-water.test.ts,
 *   tests/content/q2/rerelease-ammo.test.ts,
 *   tests/content/q2/rerelease-soldier-variants.test.ts and
 *   tests/content/q2/rerelease-missionpack-moves.test.ts (the corpus tests read
 *     q2/rerelease/baseq2/game_x64.dll under QTS_TEST_CORPUS and are skipped
 *     without it, as in TS).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "qa/content/q2/base/monsters/chick.h"
#include "qa/content/q2/base/monsters/jorg.h"
#include "qa/content/q2/base/player/spawns.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/infinite_ammo.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/rerelease_ammo.h"
#include "qa/content/q2/foundation/runtime.h"
#include "qa/content/q2/foundation/targets.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/missionpacks/monsters/gekk.h"
#include "qa/content/q2/missionpacks/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_carrier.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_flyer.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_hover.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_stalker.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_turret.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_widow.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_widow2.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_fixbot.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_gekk.h"
#include "qa/content/q2/rerelease/monsters/tables/missionpack.h"
#include "qa/guest/pe/format.h"
#include "qa/world/gameplay/policies.h"

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static int HOST_SENTINEL, LINK_SENTINEL, COMBAT_SENTINEL, POLICY_SENTINEL, ARMOR_SENTINEL, ITEM_SENTINEL, MOVER_SENTINEL;
static int ctx_failures = 0;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) { ctx_failures++; fprintf(stderr, "FAIL ctx %s:%d\n", __FILE__, __LINE__); } } while (0)

/* ---------------- world fixture (TS targetGame) ---------------- */
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
  qa_q2_item_module *items;
  qa_q2_mover_module *movers;
  qa_q2m_monsters monsters;
  qa_q2_callback_definitions monster_callbacks;
  qa_q2_spawn_module modules[4];
  qa_q2_foundation *foundation;
  qa_q2_game_services *game;
  bool has_player; qa_actor_id player;
  qa_actor_id beam_actors[8]; bool beam_visible[8]; size_t beam_count;
} world;
static world *W;

static double h_now(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0; }
static double h_frame(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.1; }
static double h_gravity(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 800; }
static double h_random(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.5; }
static void h_schedule(void *c, qa_owned_actor actor, const double *due) { EXPECT_CTX(c, HOST_SENTINEL); (void)actor; (void)due; }
static void h_touch_triggers(void *c, qa_owned_actor a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  EXPECT_CTX(c, HOST_SENTINEL);
  qa_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = 1; out.end = r.end; out.hit.kind = QA_TRACE_HIT_NONE;
  return out;
}
static double h_contents(void *c, qa_vec3 p) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)b; return true; }
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) { EXPECT_CTX(c, HOST_SENTINEL); (void)o; (void)r; *n = 0; return NULL; }
static const qa_actor_id *h_players(void *c, size_t *n) {
  EXPECT_CTX(c, HOST_SENTINEL);
  *n = W->has_player ? 1 : 0;
  return W->has_player ? &W->player : NULL;
}
static bool h_is_player(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); return W->has_player && qa_same_actor(a, W->player); }
static bool h_is_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; return false; }
static qa_bounds h_inline_bounds(void *c, double m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; qa_bounds b; memset(&b, 0, sizeof b); return b; }
static void h_set_solid(void *c, qa_owned_actor a, const char *s, const double *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)s; (void)m; }
static void h_set_motion(void *c, qa_q2_motion m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }
static void h_portal(void *c, double p, bool o) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) {
  EXPECT_CTX(c, HOST_SENTINEL);
  if (e.kind != NULL && strcmp(e.kind, "beam") == 0 && e.has_actor && W->beam_count < 8) {
    W->beam_actors[W->beam_count] = e.actor;
    W->beam_visible[W->beam_count++] = e.visible;
  }
}
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; qa_q2_player_view_result r; memset(&r, 0, sizeof r); r.has_value = true; return r; }
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_SENTINEL); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }
static qa_q2_monster_target_result h_monster_target(void *c, qa_actor_id a) {
  EXPECT_CTX(c, HOST_SENTINEL); (void)a;
  qa_q2_monster_target_result r; memset(&r, 0, sizeof r);
  return r;
}

static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) { EXPECT_CTX(c, LINK_SENTINEL); return qa_translated_body_bounds(a, b); }
static void link_on(void *c, qa_linked_body b) { EXPECT_CTX(c, LINK_SENTINEL); (void)b; }
static void link_off(void *c, qa_actor_id a) { EXPECT_CTX(c, LINK_SENTINEL); (void)a; }
static void combat_impulse(void *c, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)i; (void)m; }
static void combat_before(void *c, qa_owned_actor a, qa_damage_decision d) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)d; }
static void combat_confirmed(void *c, qa_damage_outcome o) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)o; }
static qa_q2_combat_context policy_context(void *c, qa_damage_request r, qa_combat_state t, const qa_combat_state *a) {
  EXPECT_CTX(c, POLICY_SENTINEL); (void)t; (void)a;
  qa_q2_combat_context out;
  memset(&out, 0, sizeof out);
  out.arithmetic = "binary64"; out.player = W->has_player && qa_same_actor(r.target, W->player); out.no_knockback = true;
  return out;
}
static qa_victim_armor_context armor_context(void *c, qa_damage_request r) {
  EXPECT_CTX(c, ARMOR_SENTINEL); (void)r;
  qa_victim_armor_context out;
  memset(&out, 0, sizeof out);
  out.screen_facing_dot = 1; out.arithmetic = "binary64";
  return out;
}
static void ih_weapon_picked(void *c, qa_actor_id p, qa_item_id i, bool f) { EXPECT_CTX(c, ITEM_SENTINEL); (void)p; (void)i; (void)f; }
static void ih_silencer(void *c, qa_actor_id p, double n) { EXPECT_CTX(c, ITEM_SENTINEL); (void)p; (void)n; }
static void ih_power_armor(void *c, qa_actor_id p, const char *k) { EXPECT_CTX(c, ITEM_SENTINEL); (void)p; (void)k; }
static void mh_path_corner(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id o) { EXPECT_CTX(c, MOVER_SENTINEL); (void)e; (void)g; (void)o; }
static void mh_combat_point(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id o) { EXPECT_CTX(c, MOVER_SENTINEL); (void)e; (void)g; (void)o; }
static bool monsters_spawn(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2m_monsters_spawn(c, e, g); }

static qa_q2_game_options base_options(void) {
  qa_q2_game_options options;
  memset(&options, 0, sizeof options);
  options.edition = QA_Q2_EDITION_CLASSIC; options.map_name = "base1"; options.skill = 1;
  options.mode = QA_Q2_MODE_SINGLEPLAYER; options.max_clients = 4; options.provider = "q2:official";
  options.campaign = "q2:base"; options.combat_provider = "q2:combat"; options.inventory_provider = "q2:inventory";
  options.movement_provider = "q1:movement";
  return options;
}

static void world_create(qa_q2_game_options options) {
  W = calloc(1, sizeof *W);
  qa_identity_owner_create(&W->identities, "q2-foundation");
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
  h->transition = h_transition; h->diagnostic = h_diagnostic; h->monster_target = h_monster_target;

  qa_q2_item_hooks item_hooks;
  memset(&item_hooks, 0, sizeof item_hooks);
  item_hooks.ctx = &ITEM_SENTINEL; item_hooks.weapon_picked = ih_weapon_picked; item_hooks.silencer = ih_silencer;
  item_hooks.power_armor = ih_power_armor;
  W->items = qa_q2_create_item_module(item_hooks);
  qa_q2_mover_hooks mover_hooks = {&MOVER_SENTINEL, mh_path_corner, mh_combat_point};
  W->movers = qa_q2_create_mover_module(mover_hooks);
  qa_q2m_weapons weapons;
  memset(&weapons, 0, sizeof weapons);
  qa_q2m_monster_hooks monster_hooks;
  memset(&monster_hooks, 0, sizeof monster_hooks);
  if (W->items == NULL || W->movers == NULL || !qa_q2m_monsters_init(&W->monsters, weapons, monster_hooks)) abort();
  W->monster_callbacks = qa_q2m_monsters_callbacks(&W->monsters);
  W->modules[0] = qa_q2_create_target_module();
  W->modules[1] = qa_q2_item_module_as_spawn(W->items);
  W->modules[2] = qa_q2_mover_module_as_spawn(W->movers);
  W->modules[3] = (qa_q2_spawn_module){&W->monsters, &W->monster_callbacks, NULL, monsters_spawn};
  W->foundation = qa_q2_entity_services_create(&W->host, options, W->modules, 4);
  W->game = qa_q2_entity_services_game(W->foundation);

  qa_owned_actor player = qa_session_actor_registry_allocate_at_source(W->actors, "q2:official", 1, "q3:character");
  W->has_player = true; W->player = player.id;
  qa_body_state body;
  memset(&body, 0, sizeof body);
  qa_shared_body_table_create_body(W->bodies, player, body);
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = 100; state.mass = 200; state.can_take_damage = true;
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE; state.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(W->combat, player, state, NULL, NULL);
  qa_shared_inventory_table_create_entries(W->inventory, player, NULL, 0);
  (void)qa_q2_entity_services_attach_player(W->foundation, player);
  qa_q2_item_module_configure_player(W->items, player, W->game, true);
}
static void world_destroy(void) {
  qa_q2_entity_services_destroy(W->foundation);
  qa_q2m_monsters_destroy(&W->monsters);
  qa_q2_mover_module_destroy(W->movers);
  qa_q2_item_module_destroy(W->items);
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
static qa_q2_entity *spawn(const char *classname, double ordinal, const char *const *pairs, size_t pair_count) {
  qa_q2_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  fields.classname = classname;
  fields.ordinal = ordinal;
  qa_q2_spawn_set(&fields, "classname", classname);
  for (size_t i = 0; i + 1 < pair_count * 2; i += 2) qa_q2_spawn_set(&fields, pairs[i], pairs[i + 1]);
  /* The spawned entity owns the fields. */
  return W->game->spawn(W->game->ctx, fields);
}

/* TS foundation.test.ts "a touch-fired func_button keeps its touch callback and has no blocked handler". */
static void test_button_touch(void) {
  world_create(base_options());
  static const char *const pairs[] = {"target", "t4", "wait", "1", "speed", "100", "lip", "0"};
  qa_q2_entity *button = spawn("func_button", 5, pairs, 4);
  CHECK(button != NULL);
  if (button != NULL) {
    CHECK(button->touch.fn != NULL);
    qa_touch_contact contact;
    memset(&contact, 0, sizeof contact);
    contact.self = button->actor; contact.other = W->player;
    if (button->touch.fn != NULL) button->touch.fn(button->touch.ctx, button, W->game, contact);
    CHECK(button->touch.fn != NULL);
    CHECK(button->blocked.fn == NULL);
  }
  world_destroy();
}

/* TS foundation.test.ts "rerelease g_instagib removes pickups, starts players with a railgun and 99 slugs, and refuses weapon drops". */
static void test_instagib(void) {
  qa_q2_game_options options = base_options();
  options.edition = QA_Q2_EDITION_RERELEASE; options.mode = QA_Q2_MODE_DEATHMATCH; options.instagib = true;
  world_create(options);
  static const char *const pairs[] = {"origin", "0 0 0"};
  qa_q2_entity *pickup = spawn("item_health", 40, pairs, 1);
  CHECK(pickup != NULL);
  if (pickup != NULL) {
    qa_actor_id id = pickup->actor.id;
    CHECK(W->game->entity(W->game->ctx, &id) == NULL);
  }
  CHECK(qa_shared_inventory_table_count(W->inventory, W->player, "q2:weapon_blaster") == 0);
  CHECK(qa_shared_inventory_table_count(W->inventory, W->player, "q2:weapon_railgun") == 1);
  CHECK(qa_shared_inventory_table_count(W->inventory, W->player, "q2:ammo_slugs") == 99);
  qa_q2_entity *owner = W->game->entity(W->game->ctx, &W->player);
  CHECK(owner != NULL);
  qa_q2_drop_options drop = {0};
  if (owner != NULL) CHECK(qa_q2_item_module_drop(W->items, owner, W->game, "q2:weapon_railgun", drop) == NULL);
  world_destroy();
}

/* TS foundation.test.ts "rerelease g_infinite_ammo keeps trap and tesla ammo finite and refuses ammo drops". */
static void test_infinite_ammo(void) {
  qa_q2_game_options rerelease = base_options();
  rerelease.edition = QA_Q2_EDITION_RERELEASE; rerelease.mode = QA_Q2_MODE_DEATHMATCH; rerelease.deathmatch_flags = 8192;
  CHECK(qa_q2_no_infinite_ammo(&rerelease, "q2:ammo_trap"));
  CHECK(qa_q2_no_infinite_ammo(&rerelease, "q2:ammo_tesla"));
  CHECK(!qa_q2_no_infinite_ammo(&rerelease, "q2:ammo_slugs"));
  qa_q2_game_options classic = base_options();
  CHECK(!qa_q2_no_infinite_ammo(&classic, "q2:ammo_trap"));
}

/* TS monsters.test.ts "rerelease Xatrix soldiers spawn as soldier-family variants with their own skin, count and health". */
static void test_soldier_variants_spawn(void) {
  qa_q2_game_options options = base_options();
  options.edition = QA_Q2_EDITION_RERELEASE;
  world_create(options);
  static const struct { const char *classname; double health, skin, count; qa_q2m_weapon_kind weapon; } expected[] = {
    {"monster_soldier_ripper", 50, 6, 0, QA_Q2M_WEAPON_BLASTER},
    {"monster_soldier_hypergun", 60, 8, 2, QA_Q2M_WEAPON_SHOTGUN},
    {"monster_soldier_lasergun", 70, 10, 4, QA_Q2M_WEAPON_MACHINEGUN},
  };
  static const char *const pairs[] = {"origin", "0 0 0"};
  for (size_t i = 0; i < 3; i++) {
    qa_q2_entity *entity = spawn(expected[i].classname, 50 + (double)i, pairs, 1);
    CHECK(entity != NULL);
    if (entity == NULL) continue;
    qa_q2m_context *context = qa_q2m_monsters_context(&W->monsters, entity->actor.id);
    CHECK(context != NULL);
    qa_combat_state_result combat = qa_gameplay_authority_read(W->combat, entity->actor.id);
    CHECK(combat.has_value && combat.value.health == expected[i].health);
    CHECK(entity->skin == expected[i].skin);
    CHECK(entity->count == expected[i].count);
    if (context != NULL) {
      CHECK(context->state->weapon == expected[i].weapon);
      CHECK(strcmp(context->state->kind, "soldier") == 0);
    }
    CHECK(strcmp(entity->model, "models/monsters/soldier/tris.md2") == 0);
  }
  world_destroy();
}

/* TS entity-services.test.ts "classic coop spawn fixups use named think callbacks that survive a checkpoint after firing":
 * the fixup think is a named source callback both before and after it fires, so a capture names it. */
static void test_coop_spawn_callbacks(void) {
  static const struct { const char *map, *classname, *think; } cases[] = {
    {"jail2", "info_player_coop", "SP_FixCoopSpots"},
    {"security", "info_player_start", "SP_CreateCoopSpots"},
  };
  for (size_t i = 0; i < 2; i++) {
    world_create(base_options());
    qa_q2_game_options options = base_options();
    options.map_name = cases[i].map; options.mode = QA_Q2_MODE_COOP;
    qa_q2_spawn_module spawns = qa_q2_get_player_spawns();
    qa_q2_entity_services *services = qa_q2_entity_services_create(&W->host, options, &spawns, 1);
    qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(W->actors, "q2:map", 3, "q2:authored");
    qa_body_state body;
    memset(&body, 0, sizeof body);
    qa_shared_body_table_create_body(W->bodies, actor, body);
    qa_q2_spawn_fields fields;
    memset(&fields, 0, sizeof fields);
    fields.classname = cases[i].classname; fields.ordinal = 3;
    qa_q2_entity *spot = qa_q2_entity_services_attach(services, actor, fields);
    qa_q2_game_services *game = qa_q2_entity_services_game(services);
    CHECK(qa_q2_player_spawns_spawn(spot, game));
    CHECK(spot->think.fn != NULL);
    const char *name = qa_q2_think_name(qa_q2_entity_services_callbacks(services), spot->think, true);
    CHECK(name != NULL && strcmp(name, cases[i].think) == 0);
    /* The think fires (advance(0.2)); the capture still names the entity's callback. */
    qa_q2_think think = spot->think;
    think.fn(think.ctx, spot, game);
    qa_q2_foundation_checkpoint checkpoint = qa_q2_entity_services_capture(services);
    CHECK(checkpoint.entity_count >= 1);
    qa_q2_entity_services_destroy(services);
    world_destroy();
  }
}

/* TS q2-missionpacks monsters.test.ts "monster dabeam hides its beam before its actor is freed". The fixture's
 * scheduler is driven by hand: the beam's scheduled thinks run in order as the TS advance(0.1) and advance(0.2). */
static void test_dabeam_hides_beam(void) {
  world_create(base_options());
  static const char *const pairs[] = {"origin", "0 0 0"};
  qa_q2_entity *owner = spawn("monster_soldier", 60, pairs, 1);
  CHECK(owner != NULL);
  if (owner != NULL) {
    qa_q2_entity *beam = qa_q2mpm_monster_dabeam(owner, W->game, &W->player, (qa_vec3){0, 0, 24}, (qa_vec3){0, 0, 0}, 5, false);
    qa_actor_id id = beam->actor.id;
    qa_q2_think hit = beam->think;
    CHECK(hit.fn != NULL);
    hit.fn(hit.ctx, beam, W->game);
    size_t shown = 0;
    for (size_t i = 0; i < W->beam_count; i++) if (qa_same_actor(W->beam_actors[i], id)) shown++;
    CHECK(shown == 1 && W->beam_visible[W->beam_count - 1]);
    qa_q2_think release = beam->think;
    CHECK(release.fn != NULL);
    release.fn(release.ctx, beam, W->game);
    size_t seen = 0;
    bool last_visible = true;
    for (size_t i = 0; i < W->beam_count; i++)
      if (qa_same_actor(W->beam_actors[i], id)) { seen++; last_visible = W->beam_visible[i]; }
    CHECK(seen == 2 && !last_visible);
    CHECK(!qa_session_actor_registry_is_live(W->actors, id));
  }
  world_destroy();
}

/* TS q2-base monsters.test.ts "A head gib drops the dying actor's attached models, as ThrowHead does" and
 * "The Iron Maiden corpse box sits on the floor, not under it". */
static void test_head_gib_and_chick_corpse(void) {
  world_create(base_options());
  qa_q2bm_jorg_definition jorg;
  qa_q2bm_create_jorg_definition(&jorg, &W->monsters);
  qa_q2m_monsters_register(&W->monsters, &jorg.definition, -1);
  qa_q2m_monsters_register(&W->monsters, &qa_q2bm_chick_definition, -1);
  static const char *const pairs[] = {"origin", "0 0 0"};
  qa_q2_entity *boss = spawn("monster_jorg", 70, pairs, 1);
  CHECK(boss != NULL);
  if (boss != NULL) {
    CHECK(boss->model2 != NULL && boss->model2[0] != '\0');
    qa_q2m_gib_options head = {true, false, false, false};
    (void)qa_q2m_throw_gib(boss, W->game, "models/objects/gibs/gear/tris.md2", 0, head);
    CHECK(boss->model2 != NULL && boss->model2[0] == '\0');
  }
  qa_q2_entity *chick = spawn("monster_chick", 71, pairs, 1);
  CHECK(chick != NULL);
  qa_q2m_context *context = chick != NULL ? qa_q2m_monsters_context(&W->monsters, chick->actor.id) : NULL;
  CHECK(context != NULL);
  if (context != NULL) {
    const qa_q2m_handler *dead = NULL;
    for (size_t i = 0; i < qa_q2bm_chick_definition.callback_count; i++)
      if (strcmp(qa_q2bm_chick_definition.callbacks[i].name, "chick_dead") == 0) dead = &qa_q2bm_chick_definition.callbacks[i].handler;
    CHECK(dead != NULL);
    if (dead != NULL) dead->fn(dead->ctx, context);
    qa_body_state body = W->game->body(W->game->ctx, chick->actor);
    CHECK(body.bounds.min.x == -16 && body.bounds.min.y == -16 && body.bounds.min.z == 0);
    CHECK(body.bounds.max.x == 16 && body.bounds.max.y == 16 && body.bounds.max.z == 16);
  }
  world_destroy();
}

/* TS weapons.test.ts "a shot whose ammo was dropped mid-fire spends what is left and never throws or goes negative",
 * at the consume step: each base weapon with ammo and quantity 1 (not grenades) fires with its ammo already gone,
 * and a multi-round debit larger than the count spends what is left. */
static void wh_ammo_changed(void *c, qa_actor_id a, qa_item_id i) { (void)c; (void)a; (void)i; }
static void test_consume_after_drop(void) {
  const qa_q2_edition editions[2] = {QA_Q2_EDITION_CLASSIC, QA_Q2_EDITION_RERELEASE};
  for (size_t e = 0; e < 2; e++) {
    qa_q2_game_options options = base_options();
    options.edition = editions[e];
    world_create(options);
    qa_q2w_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.ammo_changed = wh_ammo_changed;
    qa_q2w_weapons weapons;
    CHECK(qa_q2w_weapons_init(&weapons, hooks));
    qa_owned_actor player;
    CHECK(qa_session_actor_registry_resolve_owned(W->actors, W->player, &player));
    for (size_t i = 0; i < qa_q2w_base_weapon_count; i++) {
      const qa_q2w_definition *definition = &qa_q2w_base_weapons[i];
      if (definition->ammo == NULL || definition->quantity != 1 || strcmp(definition->name, "grenades") == 0) continue;
      qa_inventory_entry entry = {.item = definition->ammo, .count = 0, .capacity = 200};
      qa_shared_inventory_table_configure(W->inventory, player, entry, NULL, NULL);
      qa_q2w_state state;
      qa_q2w_state_init(&state, definition->name);
      qa_q2w_context context;
      memset(&context, 0, sizeof context);
      context.self = (qa_q2w_owner){player, 22};
      context.game = W->game; context.state = &state; context.definition = definition;
      context.rerelease = editions[e] == QA_Q2_EDITION_RERELEASE;
      qa_q2w_weapons_consume(&weapons, &context, 1, true);
      CHECK(qa_shared_inventory_table_count(W->inventory, W->player, definition->ammo) == 0);
      entry.count = 1;
      qa_shared_inventory_table_configure(W->inventory, player, entry, NULL, NULL);
      qa_q2w_weapons_consume(&weapons, &context, 3, true);
      CHECK(qa_shared_inventory_table_count(W->inventory, W->player, definition->ammo) == 0);
    }
    qa_q2w_weapons_destroy(&weapons);
    world_destroy();
  }
}

/* ---------------- gekk (TS gekk-jump.test.ts, gekk-water.test.ts) ---------------- */
static const qa_q2m_move *find_move(const qa_q2m_move *moves, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (strcmp(moves[i].name, name) == 0) return &moves[i];
  return NULL;
}
static bool has_callback(const qa_q2m_definition *d, const char *name) {
  for (size_t i = 0; i < d->callback_count; i++) if (strcmp(d->callbacks[i].name, name) == 0) return true;
  return false;
}
static bool action_is(const qa_q2m_frame *frame, const char *name) {
  return frame->action_count == 1 && !frame->actions[0].is_nextframe && strcmp(frame->actions[0].name, name) == 0;
}

static void test_gekk_jump(void) {
  qa_q2mpm_arena arena;
  memset(&arena, 0, sizeof arena);
  qa_q2m_monsters monsters;
  memset(&monsters, 0, sizeof monsters);
  qa_q2m_definition definition = qa_q2mpm_create_gekk_definition(&arena, &monsters);
  const qa_q2m_move *jump = find_move(definition.moves, definition.move_count, "gekk_move_jump");
  const qa_q2m_move *jump2 = find_move(definition.moves, definition.move_count, "gekk_move_jump2");
  CHECK(jump != NULL && jump2 != NULL);
  const qa_q2m_move *both[2] = {jump, jump2};
  for (size_t i = 0; i < 2; i++) {
    const qa_q2m_move *move = both[i];
    if (move == NULL) continue;
    CHECK(move->first_frame == 74 && move->last_frame == 81);
    CHECK(move->end != NULL && strcmp(move->end, "gekk_run") == 0);
    CHECK(move->frame_count == 8);
    CHECK(move->frame_count == 8 && action_is(&move->frames[6], "gekk_jump_wait_land"));
  }
  if (jump != NULL) CHECK(action_is(&jump->frames[0], "gekk_jump_down"));
  if (jump2 != NULL) {
    CHECK(action_is(&jump2->frames[0], "gekk_jump_up"));
    static const double distances[8] = {-8, -8, 0, 0, 0, 0, 0, 0};
    for (size_t f = 0; f < 8; f++) CHECK(jump2->frames[f].distance == distances[f]);
  }
  CHECK(definition.blocked != NULL);
  CHECK(has_callback(&definition, "gekk_jump_up") && has_callback(&definition, "gekk_jump_down") && has_callback(&definition, "gekk_jump_wait_land"));
  qa_q2mpm_arena_destroy(&arena);
}

/* A gekk at the origin whose enemy stands `distance` units away along +x. */
typedef struct gekk_scene {
  qa_q2_edition edition;
  double flags, spawnflags, distance, water_level;
  bool submerged;
  double rolls[4]; size_t roll_count, roll_at;
} gekk_scene;
static gekk_scene *G;
static const char *G_moves[8];
static size_t G_move_count;
static int G_moved;
static qa_q2_entity G_entity, G_enemy;
static qa_q2m_state G_state;
static qa_q2m_move G_current_move;
static const qa_bounds G_bounds = {{-16, -16, -24}, {16, 16, 24}};

static qa_body_state gs_body(void *ctx, qa_owned_actor actor) {
  (void)ctx;
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.bounds = G_bounds;
  if (!qa_same_actor(actor.id, G_entity.actor.id)) body.origin.x = G->distance + 32;
  return body;
}
static qa_q2_entity *gs_entity(void *ctx, const qa_actor_id *actor) { (void)ctx; (void)actor; return &G_enemy; }
static qa_q2_monster_target_result gs_monster_target(void *ctx, const qa_actor_id *actor) {
  (void)ctx; (void)actor;
  qa_q2_monster_target_result r;
  memset(&r, 0, sizeof r);
  r.has_value = true; r.value.view_height = 22;
  return r;
}
static void gs_move(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) { (void)ctx; (void)entity; (void)changes; (void)link; G_moved++; }
static double gs_contents(void *ctx, qa_vec3 point) { (void)ctx; (void)point; return G->submerged ? 56 : 0; }
static double gs_now(void *ctx) { (void)ctx; return 100; }
static double gs_random(void *ctx) { (void)ctx; return G->roll_at < G->roll_count ? G->rolls[G->roll_at++] : 0; }
static void gs_set_move(void *impl, qa_q2m_context *c, const char *name, bool has_immediate, bool immediate) {
  (void)impl; (void)c; (void)has_immediate; (void)immediate;
  if (G_move_count < 8) G_moves[G_move_count++] = name;
}

static qa_shared_body_table *gs_bodies;
static qa_session_actor_registry *gs_actors;
static qa_identity_owner gs_identities;
static qa_q2m_context gekk_context(gekk_scene *scene, qa_q2_game_services *game, qa_q2_foundation_host *host) {
  G = scene;
  G_move_count = 0;
  G_moved = 0;
  memset(&G_entity, 0, sizeof G_entity);
  memset(&G_enemy, 0, sizeof G_enemy);
  memset(&G_state, 0, sizeof G_state);
  memset(&G_current_move, 0, sizeof G_current_move);
  G_current_move.name = "gekk_move_swim_loop";
  qa_owned_actor gekk = qa_session_actor_registry_allocate(gs_actors, "q2:test", "q2:gekk");
  qa_owned_actor enemy = qa_session_actor_registry_allocate(gs_actors, "q2:test", "q2:enemy");
  qa_body_state enemy_body;
  memset(&enemy_body, 0, sizeof enemy_body);
  enemy_body.origin.x = scene->distance + 32; enemy_body.bounds = G_bounds;
  qa_shared_body_table_create_body(gs_bodies, enemy, enemy_body);
  G_entity.actor = gekk;
  G_entity.flags = scene->flags; G_entity.spawnflags = scene->spawnflags;
  G_entity.has_enemy = true; G_entity.enemy = enemy.id;
  G_enemy.actor = enemy;
  G_state.water_level = scene->water_level;
  G_state.locomotion = QA_Q2M_LOCO_SWIM;
  G_state.move = &G_current_move;
  memset(host, 0, sizeof *host);
  host->actors = gs_actors; host->bodies = gs_bodies;
  host->point_contents = gs_contents; host->now = gs_now; host->random = gs_random;
  memset(game, 0, sizeof *game);
  game->host = host;
  game->options.edition = scene->edition;
  game->body = gs_body; game->entity = gs_entity; game->monster_target = gs_monster_target; game->move = gs_move;
  qa_q2m_context context;
  memset(&context, 0, sizeof context);
  context.entity = &G_entity; context.game = game; context.state = &G_state; context.set_move = gs_set_move;
  return context;
}
static gekk_scene scene_of(qa_q2_edition edition, double flags, double distance, bool submerged, double water_level) {
  gekk_scene scene;
  memset(&scene, 0, sizeof scene);
  scene.edition = edition; scene.flags = flags; scene.distance = distance; scene.submerged = submerged; scene.water_level = water_level;
  return scene;
}
static void gekk_attack(qa_q2m_definition *definition, gekk_scene *scene) {
  qa_q2_game_services game;
  qa_q2_foundation_host host;
  qa_q2m_context context = gekk_context(scene, &game, &host);
  definition->attack.fn(definition->attack.ctx, &context);
}
static bool moves_are(const char *const *expected, size_t count) {
  if (G_move_count != count) return false;
  for (size_t i = 0; i < count; i++) if (strcmp(G_moves[i], expected[i]) != 0) return false;
  return true;
}

static void test_gekk_water(void) {
  qa_identity_owner_create(&gs_identities, "gekk");
  gs_actors = qa_session_actor_registry_create(&gs_identities, 256);
  qa_body_link_hooks link = {&LINK_SENTINEL, link_bounds, link_on, link_off};
  gs_bodies = qa_shared_body_table_create(gs_actors, link);
  qa_q2mpm_arena arena;
  memset(&arena, 0, sizeof arena);
  qa_q2m_monsters monsters;
  memset(&monsters, 0, sizeof monsters);
  qa_q2m_definition definition = qa_q2mpm_create_gekk_definition(&arena, &monsters);

  /* A swimming rerelease gekk stays in the water while its enemy is submerged within RANGE_NEAR. */
  gekk_scene scene = scene_of(QA_Q2_EDITION_RERELEASE, 2, 440, true, 2);
  gekk_attack(&definition, &scene);
  CHECK(G_move_count == 0);
  CHECK(((int)G_entity.flags & 2) == 2);
  CHECK(G_state.next_frame == 0);

  /* ...and leaves the water through its attack when the enemy is on land or too far. */
  const double distances[2] = {300, 441};
  const bool submerged[2] = {false, true};
  static const char *const leap[] = {"gekk_move_leapatk"};
  for (size_t i = 0; i < 2; i++) {
    scene = scene_of(QA_Q2_EDITION_RERELEASE, 2 | 4, distances[i], submerged[i], 2);
    gekk_attack(&definition, &scene);
    CHECK(moves_are(leap, 1));
    CHECK(G_entity.flags == 4);
    CHECK(G_state.locomotion == QA_Q2M_LOCO_WALK);
    CHECK(G_state.next_frame == 75);
    CHECK(G_moved == 0);
  }

  /* A rerelease gekk on land picks spit, run or leap by range, rolls and spawnflags. */
  static const char *const spit[] = {"gekk_move_spit"};
  static const char *const run[] = {"gekk_move_run_start"};
  scene = scene_of(QA_Q2_EDITION_RERELEASE, 0, 940, false, 0);
  scene.rolls[0] = 0.6; scene.roll_count = 1;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(spit, 1));
  scene = scene_of(QA_Q2_EDITION_RERELEASE, 0, 940, false, 0);
  scene.rolls[0] = 0.5; scene.roll_count = 1;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(run, 1));
  CHECK(G_state.attack_finished == 102);
  scene = scene_of(QA_Q2_EDITION_RERELEASE, 0, 939, false, 0);
  scene.rolls[0] = 0.8; scene.roll_count = 1;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(spit, 1));
  scene = scene_of(QA_Q2_EDITION_RERELEASE, 0, 939, false, 0);
  scene.rolls[0] = 0.7; scene.rolls[1] = 0.9; scene.roll_count = 2;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(run, 1));
  CHECK(fabs(G_state.attack_finished - 101.4) < 5e-7);
  CHECK(scene.roll_at == 2);
  scene = scene_of(QA_Q2_EDITION_RERELEASE, 0, 939, false, 0);
  scene.rolls[0] = 0.7; scene.rolls[1] = 0.7; scene.roll_count = 2;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(leap, 1));
  scene = scene_of(QA_Q2_EDITION_RERELEASE, 0, 939, false, 0);
  scene.spawnflags = 16;
  scene.rolls[0] = 0.7; scene.rolls[1] = 0.1; scene.roll_count = 2;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(run, 1));
  CHECK(fabs(G_state.attack_finished - 101.4) < 5e-7);
  CHECK(scene.roll_at == 1);

  /* The classic gekk attack keeps its 1998 water rule: no attack while swimming or wet. */
  scene = scene_of(QA_Q2_EDITION_CLASSIC, 2, 100, false, 2);
  scene.rolls[0] = 0.9; scene.rolls[1] = 0.9; scene.roll_count = 2;
  gekk_attack(&definition, &scene);
  CHECK(G_move_count == 0);
  scene = scene_of(QA_Q2_EDITION_CLASSIC, 0, 100, false, 1);
  scene.rolls[0] = 0.9; scene.rolls[1] = 0.9; scene.roll_count = 2;
  gekk_attack(&definition, &scene);
  CHECK(G_move_count == 0);
  scene = scene_of(QA_Q2_EDITION_CLASSIC, 0, 100, false, 0);
  scene.rolls[0] = 0.1; scene.rolls[1] = 0.1; scene.roll_count = 2;
  gekk_attack(&definition, &scene);
  CHECK(moves_are(leap, 1));

  /* Rerelease gekks swim and stand underwater on frames 291-322 and test water at WATER_WAIST. */
  const qa_q2m_move *classic = find_move(definition.moves, definition.move_count, "gekk_move_swim_loop");
  CHECK(classic != NULL && classic->first_frame == 242);
  bool swim_action = false;
  for (size_t f = 0; classic != NULL && f < classic->frame_count; f++)
    for (size_t a = 0; a < classic->frames[f].action_count; a++)
      if (!classic->frames[f].actions[a].is_nextframe && strcmp(classic->frames[f].actions[a].name, "gekk_swim") == 0) swim_action = true;
  CHECK(swim_action);
  qa_q2rm_mission_pack_moves moves = qa_q2rm_rerelease_mission_pack_moves("monster_gekk", definition.moves, definition.move_count);
  CHECK(moves.moves != NULL);
  static const char *const underwater[] = {"gekk_move_swim_loop", "gekk_move_standunderwater"};
  for (size_t i = 0; i < 2 && moves.moves != NULL; i++) {
    const qa_q2m_move *move = find_move(moves.moves, moves.count, underwater[i]);
    CHECK(move != NULL && move->first_frame == 291 && move->last_frame == 322 && move->frame_count == 32);
    for (size_t f = 0; move != NULL && f < move->frame_count; f++) CHECK(move->frames[f].action_count == 0);
  }
  const qa_q2m_move *loop = moves.moves != NULL ? find_move(moves.moves, moves.count, "gekk_move_swim_loop") : NULL;
  CHECK(loop != NULL && loop->end != NULL && strcmp(loop->end, "gekk_swim_loop") == 0);
  qa_q2rm_mission_pack_moves_free(&moves);
  static const char *const swim_start[] = {"gekk_move_swim_start"};
  const struct { qa_q2_edition edition; double level, spawnflags; const char *const *expected; } levels[] = {
    {QA_Q2_EDITION_CLASSIC, 1, 0, swim_start},
    {QA_Q2_EDITION_RERELEASE, 1, 0, run},
    {QA_Q2_EDITION_RERELEASE, 2, 0, swim_start},
    {QA_Q2_EDITION_RERELEASE, 3, 32, run},
  };
  for (size_t i = 0; i < 4; i++) {
    scene = scene_of(levels[i].edition, 0, 100, false, levels[i].level);
    scene.spawnflags = levels[i].spawnflags;
    qa_q2_game_services game;
    qa_q2_foundation_host host;
    qa_q2m_context context = gekk_context(&scene, &game, &host);
    definition.run.fn(definition.run.ctx, &context);
    CHECK(moves_are(levels[i].expected, 1));
  }
  qa_q2mpm_arena_destroy(&arena);
  qa_shared_body_table_destroy(gs_bodies);
  qa_session_actor_registry_close(gs_actors);
  qa_session_actor_registry_destroy(gs_actors);
  qa_identity_owner_destroy(&gs_identities);
}

/* TS rerelease-ammo.test.ts "rerelease mission pack ammo uses the rerelease capacities and classic keeps the original ones". */
static double mission_capacity(qa_q2mp_pack pack, qa_q2_edition edition, const char *item) {
  qa_q2mp_weapon_inventory_entry entries[64];
  size_t count = qa_q2mp_mission_weapon_inventory_edition(pack, edition, entries, 64);
  for (size_t i = 0; i < count; i++) if (strcmp(entries[i].item, item) == 0) return entries[i].capacity;
  return -1;
}
static void test_mission_ammo_capacities(void) {
  CHECK(mission_capacity(QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_CLASSIC, "q2:ammo_tesla") == 50);
  CHECK(mission_capacity(QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_CLASSIC, "q2:ammo_disruptor") == 100);
  CHECK(mission_capacity(QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_RERELEASE, "q2:ammo_tesla") == 5);
  CHECK(mission_capacity(QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_RERELEASE, "q2:ammo_disruptor") == 12);
  CHECK(mission_capacity(QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_RERELEASE, "q2:ammo_flechettes") == 200);
  CHECK(mission_capacity(QA_Q2MP_PACK_ROGUE, QA_Q2_EDITION_RERELEASE, "q2:ammo_prox") == 50);
  CHECK(mission_capacity(QA_Q2MP_PACK_XATRIX, QA_Q2_EDITION_RERELEASE, "q2:ammo_trap") == 5);
  CHECK(mission_capacity(QA_Q2MP_PACK_XATRIX, QA_Q2_EDITION_RERELEASE, "q2:ammo_magslug") == 50);
  CHECK(qa_q2mp_mission_ammo_pack_capacity("ammo_disruptor", false, QA_Q2_EDITION_RERELEASE, 150) == 21);
  CHECK(qa_q2mp_mission_ammo_pack_capacity("ammo_disruptor", true, QA_Q2_EDITION_RERELEASE, 200) == 30);
  CHECK(qa_q2mp_mission_ammo_pack_capacity("ammo_flechettes", true, QA_Q2_EDITION_RERELEASE, 200) == 300);
  CHECK(qa_q2mp_mission_ammo_pack_capacity("ammo_disruptor", false, QA_Q2_EDITION_CLASSIC, 150) == 150);
}

/* ---------------- game_x64.dll corpus tests ---------------- */
typedef struct dll_image { uint8_t *bytes; size_t len; qa_pe_file pe; } dll_image;
static bool load_dll(dll_image *image) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL) return false;
  char path[1024];
  snprintf(path, sizeof path, "%s/q2/rerelease/baseq2/game_x64.dll", root);
  FILE *file = fopen(path, "rb");
  if (file == NULL) return false;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  image->bytes = malloc((size_t)size);
  image->len = (size_t)size;
  bool ok = image->bytes != NULL && fread(image->bytes, 1, image->len, file) == image->len;
  fclose(file);
  if (!ok) { free(image->bytes); return false; }
  qa_parse_pe(&image->pe, image->bytes, image->len);
  return true;
}
static void free_dll(dll_image *image) { qa_pe_file_free(&image->pe); free(image->bytes); }
static bool dll_sha256_is(const dll_image *image, const char *hex) {
  unsigned char digest[32];
  unsigned int length = 0;
  if (EVP_Digest(image->bytes, image->len, digest, &length, EVP_sha256(), NULL) != 1 || length != 32) return false;
  char text[65];
  for (size_t i = 0; i < 32; i++) snprintf(text + i * 2, 3, "%02x", digest[i]);
  return strcmp(text, hex) == 0;
}
static int32_t i32_at(const dll_image *image, size_t at) {
  uint32_t v = (uint32_t)image->bytes[at] | (uint32_t)image->bytes[at + 1] << 8 | (uint32_t)image->bytes[at + 2] << 16 | (uint32_t)image->bytes[at + 3] << 24;
  return (int32_t)v;
}
static int16_t i16_at(const dll_image *image, size_t at) { return (int16_t)(uint16_t)((uint16_t)image->bytes[at] | (uint16_t)image->bytes[at + 1] << 8); }
static uint64_t u64_at(const dll_image *image, size_t at) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | image->bytes[at + (size_t)i];
  return v;
}
static float f32_at(const dll_image *image, size_t at) {
  uint32_t v = (uint32_t)i32_at(image, at);
  float f;
  memcpy(&f, &v, sizeof f);
  return f;
}
static long rva_to_offset(const dll_image *image, uint32_t rva) {
  for (size_t i = 0; i < image->pe.section_count; i++) {
    const qa_pe_section *s = &image->pe.sections[i];
    if (rva >= s->rva && rva < s->rva + s->raw_size) return (long)(s->raw_offset + rva - s->rva);
  }
  return -1;
}

static int32_t gitem_tag(const dll_image *image, const char *classname) {
  size_t needle_len = strlen(classname) + 2;
  char needle[64];
  needle[0] = '\0';
  memcpy(needle + 1, classname, needle_len - 2);
  needle[needle_len - 1] = '\0';
  long at = -1;
  for (size_t i = 0; i + needle_len <= image->len; i++)
    if (memcmp(image->bytes + i, needle, needle_len) == 0) { at = (long)i + 1; break; }
  CHECK(at > 0);
  if (at <= 0) return -1;
  const qa_pe_section *section = NULL;
  for (size_t i = 0; i < image->pe.section_count; i++) {
    const qa_pe_section *s = &image->pe.sections[i];
    if ((size_t)at >= s->raw_offset && (size_t)at < (size_t)s->raw_offset + s->raw_size) section = s;
  }
  if (section == NULL) return -1;
  uint64_t address = image->pe.preferred_base + (uint64_t)(section->rva + (uint32_t)at - section->raw_offset);
  for (size_t i = 0; i < image->pe.section_count; i++) {
    const qa_pe_section *data = &image->pe.sections[i];
    if (strcmp(data->name, ".data") != 0) continue;
    for (size_t offset = data->raw_offset; offset + 8 <= (size_t)data->raw_offset + data->raw_size; offset += 8)
      if (u64_at(image, offset) == address) return i32_at(image, offset + 0x88);
  }
  return -1;
}
#define MAX_AMMO_OFFSET 0xbd0
static void ammo_defaults(const dll_image *image, int32_t out[12]) {
  for (int i = 0; i < 12; i++) out[i] = -1;
  long start = rva_to_offset(image, 0xd7760);
  int32_t ax = 0, cx = 0;
  for (long i = start; i < start + (0xd77dc - 0xd7760); i++) {
    uint8_t b = image->bytes[i];
    if (b == 0xb8) ax = i32_at(image, (size_t)i + 1);
    else if (b == 0xb9) cx = i32_at(image, (size_t)i + 1);
    else if (b == 0xc7 && image->bytes[i + 1] == 0x83) {
      double slot = (i32_at(image, (size_t)i + 2) - MAX_AMMO_OFFSET) / 2.0;
      if (slot >= 0 && slot < 11 && slot == floor(slot)) {
        out[(int)slot] = i16_at(image, (size_t)i + 6);
        out[(int)slot + 1] = i16_at(image, (size_t)i + 8);
      }
    } else if (b == 0x66 && image->bytes[i + 1] == 0x89 && (image->bytes[i + 2] == 0x83 || image->bytes[i + 2] == 0x8b)) {
      double slot = (i32_at(image, (size_t)i + 3) - MAX_AMMO_OFFSET) / 2.0;
      if (slot >= 0 && slot < 12 && slot == floor(slot)) out[(int)slot] = image->bytes[i + 2] == 0x83 ? ax : cx;
    }
  }
}
/* Each `movzx eax,[base+d]; cmp ax,reg; cmovl; mov [base+d],ax` raises slot d to the constant last loaded into reg; -1 = none. */
static void ammo_ceilings(const dll_image *image, uint32_t rva, int32_t out[12]) {
  for (int i = 0; i < 12; i++) out[i] = -1;
  long start = rva_to_offset(image, rva);
  int32_t r9 = 0, cx = 0, dx = 0;
  double slot = -1;
  for (long i = start; i < start + 0x1b0; i++) {
    const uint8_t *b = image->bytes + i;
    if (b[0] == 0x41 && b[1] == 0xb9) r9 = i32_at(image, (size_t)i + 2);
    else if (b[0] == 0xb9 && b[1] != 0x00 && b[2] == 0) cx = i32_at(image, (size_t)i + 1);
    else if (b[0] == 0xba && b[2] == 0) dx = i32_at(image, (size_t)i + 1);
    else if (b[0] == 0x0f && b[1] == 0xb7 && b[2] >= 0x80 && b[2] < 0x88) slot = (i32_at(image, (size_t)i + 3) - MAX_AMMO_OFFSET) / 2.0;
    else if (b[0] == 0x66 && b[1] == 0x41 && b[2] == 0x3b && b[3] == 0xc1 && slot >= 0) { if (slot < 12 && slot == floor(slot)) out[(int)slot] = r9; }
    else if (b[0] == 0x66 && b[1] == 0x3b && b[2] == 0xc1 && slot >= 0) { if (slot < 12 && slot == floor(slot)) out[(int)slot] = cx; }
    else if (b[0] == 0x66 && b[1] == 0x3b && b[2] == 0xc2 && slot >= 0) { if (slot < 12 && slot == floor(slot)) out[(int)slot] = dx; }
  }
}
static const char *const AMMO_CLASSNAMES[12] = {"ammo_bullets", "ammo_shells", "ammo_rockets", "ammo_grenades", "ammo_cells", "ammo_slugs",
                                                "ammo_magslug", "ammo_trap", "ammo_flechettes", "ammo_tesla", "ammo_disruptor", "ammo_prox"};
#define DLL_SHA256 "045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd"

/* TS rerelease-ammo.test.ts "rerelease ammo capacities equal what game_x64.dll stores and clamps". */
static void test_corpus_ammo(const dll_image *image) {
  CHECK(dll_sha256_is(image, DLL_SHA256));
  bool tags[12] = {false};
  for (size_t i = 0; i < 12; i++) {
    const qa_q2_ammo_limits *limits = qa_q2_rerelease_ammo_limits(AMMO_CLASSNAMES[i]);
    CHECK(limits != NULL);
    if (limits == NULL) continue;
    tags[(int)limits->tag] = true;
    CHECK(gitem_tag(image, AMMO_CLASSNAMES[i]) == limits->tag);
  }
  for (size_t i = 0; i < 12; i++) CHECK(tags[i]);
  int32_t initial[12];
  ammo_defaults(image, initial);
  for (size_t i = 0; i < 12; i++) CHECK(initial[i] > 0);
  for (size_t i = 0; i < 12; i++) {
    const qa_q2_ammo_limits *limits = qa_q2_rerelease_ammo_limits(AMMO_CLASSNAMES[i]);
    if (limits != NULL) CHECK(initial[(int)limits->tag] == limits->capacity);
  }
  int32_t bandolier[12], pack[12];
  ammo_ceilings(image, 0x667b0, bandolier);
  ammo_ceilings(image, 0x66960, pack);
  for (size_t i = 0; i < 12; i++) {
    const qa_q2_ammo_limits *limits = qa_q2_rerelease_ammo_limits(AMMO_CLASSNAMES[i]);
    if (limits == NULL) continue;
    CHECK(bandolier[(int)limits->tag] == (limits->has_bandolier ? limits->bandolier : -1));
    CHECK(pack[(int)limits->tag] == (limits->has_pack ? limits->pack : -1));
  }
}

/* TS rerelease-soldier-variants.test.ts. */
static bool find_bytes(const dll_image *image, const uint8_t *needle, size_t length, long from, long to) {
  for (long i = from; i + (long)length <= to; i++) if (memcmp(image->bytes + i, needle, length) == 0) return true;
  return false;
}
static void test_corpus_soldier_variants(const dll_image *image) {
  static const struct { const char *classname; uint32_t rva, health; } spawns[] = {
    {"monster_soldier_ripper", 0xd0820, 0x178904},
    {"monster_soldier_hypergun", 0xd0930, 0x178920},
    {"monster_soldier_lasergun", 0xd0a50, 0x178950},
  };
  static const qa_q2m_weapon_kind weapons[3] = {QA_Q2M_WEAPON_BLASTER, QA_Q2M_WEAPON_SHOTGUN, QA_Q2M_WEAPON_MACHINEGUN};
  for (size_t i = 0; i < 3; i++) {
    const qa_q2m_soldier_variant *variant = qa_q2m_soldier_variant_of(spawns[i].classname);
    CHECK(variant != NULL);
    if (variant == NULL) continue;
    long start = rva_to_offset(image, spawns[i].rva), end = start + 0x140;
    int32_t skin = (int32_t)variant->skin, count = (int32_t)variant->count, gib = -30;
    uint8_t skin_store[7] = {0xc7, 0x43, 0x3c};
    memcpy(skin_store + 3, &skin, 4);
    uint8_t count_store[10] = {0xc7, 0x83, 0xb8, 0x07, 0x00, 0x00};
    memcpy(count_store + 6, &count, 4);
    uint8_t gib_store[10] = {0xc7, 0x83, 0x80, 0x07, 0x00, 0x00};
    memcpy(gib_store + 6, &gib, 4);
    CHECK(find_bytes(image, skin_store, 7, start, end));
    CHECK(find_bytes(image, count_store, 10, start, end));
    CHECK(find_bytes(image, gib_store, 10, start, end));
    CHECK(f32_at(image, (size_t)rva_to_offset(image, spawns[i].health)) == variant->health);
    CHECK(variant->rerelease_only);
    bool fallback = false;
    for (size_t k = 0; k < QA_Q2MPM_ORIGINAL_FALLBACK_COUNT; k++)
      if (strcmp(qa_q2mpm_original_mission_pack_fallbacks[k], spawns[i].classname) == 0) fallback = true;
    CHECK(!fallback);
    CHECK(variant->weapon == weapons[i]);
  }
}

/* TS rerelease-missionpack-moves.test.ts: an mmove_t is {firstframe, lastframe, mframe_t *, endfunc, sidestep_scale, lerp}
 * (32 bytes), an mframe_t {aifunc, dist, thinkfunc, lerp_frame} (32 bytes). */
typedef struct dll_frame { uint32_t ai, think; float dist; } dll_frame;
typedef struct dll_move { uint32_t rva; int32_t first, last; uint32_t end; dll_frame *frames; size_t frame_count; } dll_move;
static dll_move *scan_moves(const dll_image *image, size_t *out_count) {
  const uint64_t base = image->pe.preferred_base;
  const qa_pe_section *text = NULL;
  for (size_t i = 0; i < image->pe.section_count; i++) if (strcmp(image->pe.sections[i].name, ".text") == 0) text = &image->pe.sections[i];
  if (text == NULL) { *out_count = 0; return NULL; }
#define IN_TEXT(v) ((v) >= base + text->rva && (v) < base + text->rva + text->virtual_size)
  const uint64_t data_start = base + 0x13b000, data_end = base + 0x299000;
  dll_move *moves = NULL;
  size_t count = 0, cap = 0;
  for (size_t si = 0; si < image->pe.section_count; si++) {
    const qa_pe_section *section = &image->pe.sections[si];
    if (strcmp(section->name, ".rdata") != 0 && strcmp(section->name, ".data") != 0) continue;
    for (size_t offset = section->raw_offset; offset + 32 <= (size_t)section->raw_offset + section->raw_size; offset += 8) {
      int32_t first = i32_at(image, offset), last = i32_at(image, offset + 4);
      if (first < 0 || last < first || last > 400) continue;
      uint64_t frame_address = u64_at(image, offset + 8), end = u64_at(image, offset + 16);
      if (frame_address < data_start || frame_address >= data_end || (end != 0 && !IN_TEXT(end))) continue;
      long start = rva_to_offset(image, (uint32_t)(frame_address - base));
      if (start < 0) continue;
      dll_frame *frames = malloc((size_t)(last - first + 1) * sizeof *frames);
      bool valid = true;
      for (int32_t index = 0; index <= last - first && valid; index++) {
        size_t at = (size_t)start + (size_t)index * 32;
        if (at + 32 > image->len) { valid = false; break; }
        uint64_t ai = u64_at(image, at), think = u64_at(image, at + 16);
        float dist = f32_at(image, at + 8);
        int32_t lerp = i32_at(image, at + 24);
        if ((ai != 0 && !IN_TEXT(ai)) || (think != 0 && !IN_TEXT(think)) || lerp < -1 || lerp > 1000 || !(fabsf(dist) < 500)) valid = false;
        else frames[index] = (dll_frame){ai == 0 ? 0 : (uint32_t)(ai - base), think == 0 ? 0 : (uint32_t)(think - base), dist};
      }
      if (!valid) { free(frames); continue; }
      if (count == cap) { cap = cap ? cap * 2 : 256; moves = realloc(moves, cap * sizeof *moves); }
      moves[count++] = (dll_move){(uint32_t)(section->rva + offset - section->raw_offset), first, last, end == 0 ? 0 : (uint32_t)(end - base),
                                  frames, (size_t)(last - first + 1)};
    }
  }
#undef IN_TEXT
  *out_count = count;
  return moves;
}
static const struct { const char *name; uint32_t address; } STANDARD_AI[] = {
  {"stand", 0x52570}, {"walk", 0x52990}, {"run", 0x54b00}, {"charge", 0x52b90}, {"move", 0x52560}, {"none", 0}};
static const struct { const char *name; uint32_t address; } CALLBACK_ADDRESS[] = {
  {"monster_footstep", 0x9ddc0}, {"stalker_swing_attack", 0x10a950}, {"stalker_jump_straightup", 0x10aeb0},
  {"stalker_jump_wait_land", 0x10b150}, {"stalker_jump_up", 0x488e0}, {"stalker_jump_down", 0x48760},
  {"stalker_idle_noise", 0x1099f0}, {"BossExplode", 0xfa720}, {"carrier_prep_spawn", 0x1072f0},
  {"carrier_start_spawn", 0x1077c0}, {"carrier_ready_spawn", 0x107390}, {"carrier_spawn_check", 0x107330},
  {"widow2_step", 0x113950}, {"widow2_attack_beam", 0x115640}, {"hover_dying", 0xb8740}, {"gekk_shrink", 0x48590},
  {"change_to_roam", 0x41640}};
static const struct { const char *name; uint32_t address; } END_ADDRESS[] = {
  {"carrier_run", 0x107da0}, {"carrier_dead", 0x1088b0}, {"stalker_dead", 0x10b430}, {"hover_dead", 0xb9520}, {"gekk_dead", 0x48350}};
#define LOOKUP(table, key, out) \
  do { for (size_t _k = 0; _k < sizeof table / sizeof table[0]; _k++) if (strcmp(table[_k].name, key) == 0) { out = &table[_k].address; break; } } while (0)

static bool move_matches(const qa_q2m_move *move, const dll_move *candidate) {
  if (candidate->first != move->first_frame || candidate->last != move->last_frame) return false;
  if ((candidate->end != 0) != (move->end != NULL)) return false;
  const uint32_t *known = NULL;
  if (move->end != NULL) LOOKUP(END_ADDRESS, move->end, known);
  if (known != NULL && *known != candidate->end) return false;
  for (size_t index = 0; index < candidate->frame_count; index++) {
    if (index >= move->frame_count) return false;
    const qa_q2m_frame *source = &move->frames[index];
    const dll_frame *frame = &candidate->frames[index];
    if (fabs((double)frame->dist - source->distance) > 1e-3) return false;
    if (source->ai != QA_Q2M_AI_SOURCE) {
      const uint32_t *expected = NULL;
      LOOKUP(STANDARD_AI, qa_q2m_ai_name(source->ai), expected);
      if (expected == NULL || *expected != frame->ai) return false;
    } else {
      if (frame->ai == 0) return false;
      for (size_t k = 0; k < sizeof STANDARD_AI / sizeof STANDARD_AI[0]; k++) if (STANDARD_AI[k].address == frame->ai) return false;
    }
    if ((frame->think != 0) != (source->action_count > 0)) return false;
    if (source->action_count > 0 && !source->actions[0].is_nextframe) {
      const uint32_t *address = NULL;
      LOOKUP(CALLBACK_ADDRESS, source->actions[0].name, address);
      if (address != NULL && *address != frame->think) return false;
    }
  }
  return true;
}
static const struct { const char *name; uint32_t address; } PINNED[] = {
  {"gekk_move_death1", 0x146ed0}, {"gekk_move_swim_loop", 0x146da8}, {"gekk_move_standunderwater", 0x147098},
  {"fixbot_move_stand2", 0x146c70}, {"stalker_move_run", 0x15c2f0}, {"stalker_move_walk", 0x15c2c8},
  {"stalker_move_false_death_end", 0x15c240}, {"stalker_move_swing_l", 0x15c268}, {"stalker_move_swing_r", 0x15c310},
  {"stalker_move_jump_straightup", 0x15c288}, {"stalker_move_jump_up", 0x15c1e0}, {"stalker_move_jump_down", 0x15c330},
  {"stalker_move_death", 0x15c3b0}, {"hover_move_death1", 0x154398}, {"carrier_move_death", 0x15c178},
  {"carrier_move_spawn", 0x15c0d0}, {"widow2_move_walk", 0x15d568}, {"widow2_move_run", 0x15e450},
  {"widow2_move_attack_pre_beam", 0x15d548}};
static const char *const ABSENT[] = {
  "monster_fixbot:fixbot_move_pickup", "monster_fixbot:fixbot_move_attack1", "monster_fixbot:fixbot_move_land",
  "monster_fixbot:fixbot_move_death1", "monster_fixbot:fixbot_move_backward", "monster_stalker:stalker_move_dodge_run",
  "monster_daedalus:hover_move_start_attack2", "monster_daedalus:hover_move_end_attack2", "monster_kamikaze:flyer_move_start",
  "monster_kamikaze:flyer_move_stop", "monster_kamikaze:flyer_move_rollright", "monster_kamikaze:flyer_move_rollleft",
  "monster_kamikaze:flyer_move_defense", "monster_kamikaze:flyer_move_bankright", "monster_kamikaze:flyer_move_bankleft"};
static bool is_absent(const char *classname, const char *move) {
  char key[160];
  snprintf(key, sizeof key, "%s:%s", classname, move);
  for (size_t i = 0; i < sizeof ABSENT / sizeof ABSENT[0]; i++) if (strcmp(ABSENT[i], key) == 0) return true;
  return false;
}
static bool candidate_matches(const qa_q2m_move *move, const dll_move *dll, size_t dll_count) {
  const uint32_t *rva = NULL;
  LOOKUP(PINNED, move->name, rva);
  for (size_t i = 0; i < dll_count; i++)
    if ((rva == NULL || dll[i].rva == *rva) && move_matches(move, &dll[i])) return true;
  return false;
}
static void test_corpus_missionpack_moves(const dll_image *image) {
  CHECK(dll_sha256_is(image, DLL_SHA256));
  size_t dll_count = 0;
  dll_move *dll = scan_moves(image, &dll_count);
  const struct { const char *classname; const qa_q2m_move *moves; size_t count; } species[] = {
    {"monster_gekk", qa_q2mpm_xatrix_gekk_moves, qa_q2mpm_xatrix_gekk_move_count},
    {"monster_fixbot", qa_q2mpm_xatrix_fixbot_moves, qa_q2mpm_xatrix_fixbot_move_count},
    {"monster_stalker", qa_q2mpm_rogue_stalker_moves, qa_q2mpm_rogue_stalker_move_count},
    {"monster_daedalus", qa_q2mpm_rogue_hover_moves, qa_q2mpm_rogue_hover_move_count},
    {"monster_carrier", qa_q2mpm_rogue_carrier_moves, qa_q2mpm_rogue_carrier_move_count},
    {"monster_widow", qa_q2mpm_rogue_widow_moves, qa_q2mpm_rogue_widow_move_count},
    {"monster_widow2", qa_q2mpm_rogue_widow2_moves, qa_q2mpm_rogue_widow2_move_count},
    {"monster_turret", qa_q2mpm_rogue_turret_moves, qa_q2mpm_rogue_turret_move_count},
    {"monster_kamikaze", qa_q2mpm_rogue_flyer_moves, qa_q2mpm_rogue_flyer_move_count},
  };
  size_t missing = 0, absent_found = 0;
  /* "rerelease mission pack monster moves equal the mmove_t tables in game_x64.dll". */
  for (size_t s = 0; s < sizeof species / sizeof species[0]; s++) {
    bool fallback = false;
    for (size_t k = 0; k < QA_Q2MPM_ORIGINAL_FALLBACK_COUNT; k++)
      if (strcmp(qa_q2mpm_original_mission_pack_fallbacks[k], species[s].classname) == 0) fallback = true;
    CHECK(fallback);
    qa_q2rm_mission_pack_moves rerelease = qa_q2rm_rerelease_mission_pack_moves(species[s].classname, species[s].moves, species[s].count);
    const qa_q2m_move *moves = rerelease.moves != NULL ? rerelease.moves : species[s].moves;
    size_t count = rerelease.moves != NULL ? rerelease.count : species[s].count;
    for (size_t m = 0; m < count; m++) {
      if (candidate_matches(&moves[m], dll, dll_count)) continue;
      if (is_absent(species[s].classname, moves[m].name)) absent_found++;
      else { missing++; fprintf(stderr, "missing %s:%s\n", species[s].classname, moves[m].name); }
    }
    qa_q2rm_mission_pack_moves_free(&rerelease);
  }
  CHECK(missing == 0);
  CHECK(absent_found == sizeof ABSENT / sizeof ABSENT[0]);
  /* "the classic mission pack tables differ from the DLL exactly at the moves the overrides replace". */
  for (size_t s = 0; s < sizeof species / sizeof species[0]; s++) {
    size_t spec_count = 0, replacement_count = 0;
    const qa_q2rm_move_callbacks *specs = NULL;
    const qa_q2m_move *replacements = NULL;
    for (size_t m = 0; m < species[s].count; m++) {
      const qa_q2m_move *move = &species[s].moves[m];
      if (is_absent(species[s].classname, move->name)) continue;
      bool changed = false;
      for (size_t c = 0; c < sizeof species / sizeof species[0] && !changed; c++) {
        specs = qa_q2rm_rerelease_mission_pack_callbacks(species[c].classname, &spec_count);
        for (size_t k = 0; k < spec_count; k++) if (strcmp(specs[k].move, move->name) == 0) changed = true;
        replacements = qa_q2rm_rerelease_mission_pack_replacements(species[c].classname, &replacement_count);
        for (size_t k = 0; k < replacement_count; k++) if (strcmp(replacements[k].name, move->name) == 0) changed = true;
      }
      bool equal = candidate_matches(move, dll, dll_count);
      if (changed == equal) fprintf(stderr, "classic %s equal=%d changed=%d\n", move->name, equal, changed);
      CHECK(equal == !changed);
    }
  }
  for (size_t i = 0; i < dll_count; i++) free(dll[i].frames);
  free(dll);
}

int main(void) {
  test_button_touch();
  test_instagib();
  test_infinite_ammo();
  test_soldier_variants_spawn();
  test_coop_spawn_callbacks();
  test_dabeam_hides_beam();
  test_head_gib_and_chick_corpse();
  test_consume_after_drop();
  test_gekk_jump();
  test_gekk_water();
  test_mission_ammo_capacities();
  dll_image image;
  if (load_dll(&image)) {
    test_corpus_ammo(&image);
    test_corpus_soldier_variants(&image);
    test_corpus_missionpack_moves(&image);
    free_dll(&image);
  } else printf("skip game_x64.dll corpus tests (QTS_TEST_CORPUS has no q2/rerelease/baseq2/game_x64.dll)\n");
  CHECK(ctx_failures == 0);
  printf("test_q2_drift_d4: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

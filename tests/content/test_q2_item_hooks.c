/*
 * TS Q2ItemHooks.pickedUp and Q2ItemHooks.edition (src/content/q2/foundation/
 * items.ts): touch reports each completed pickup with the TS usable test, and
 * a rerelease module applies the game_x64.dll rows (applyRereleaseRows).
 *
 * Reproduces tests/gameplay/q3/base/peer-input-regressions.test.ts "rerelease
 * item rows carry the game_x64.dll values for body armor". The pickedUp and
 * pickup-icon expectations come from the same scenario run in TS under bun:
 * Q2Foundation over SessionActorRegistry/SharedBodyTable/GameplayAuthority/
 * SharedInventoryTable with the single item module, one player at source
 * slot 1 configured with configurePlayer(player, game, true), then
 * load('{item_quad} {ammo_bullets} {item_armor_shard} {weapon_shotgun}') and
 * touch of each spawned entity in order. Both editions printed
 *   pickedUp: 0:q2:item_quad:true 0:q2:ammo_bullets:false
 *             0:q2:item_armor_shard:false 0:q2:weapon_shotgun:true
 *   icons classic   p_quad a_bullets i_jacketarmor w_shotgun
 *   icons rerelease p_quad a_bullets i_armor_shard w_shotgun
 *   body armor paths classic sound/misc/ar1_pkup.wav, rerelease ar3_pkup.wav.
 * Every callback checks its sentinel ctx. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/runtime.h"
#include "qa/world/gameplay/policies.h"

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static int HOST_SENTINEL, LINK_SENTINEL, COMBAT_SENTINEL, POLICY_SENTINEL, ARMOR_SENTINEL, ITEM_SENTINEL;
static int ctx_failures = 0;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) { ctx_failures++; fprintf(stderr, "FAIL ctx %s:%d\n", __FILE__, __LINE__); } } while (0)

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
  qa_q2_spawn_module module;
  qa_q2_foundation *foundation;
  qa_q2_game_services *game;
  bool has_player; qa_actor_id player;
  char picked[8][64]; size_t picked_count;
  char icons[8][32]; size_t icon_count;
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
  if (e.kind != NULL && strcmp(e.kind, "pickup") == 0 && W->icon_count < 8)
    snprintf(W->icons[W->icon_count++], sizeof W->icons[0], "%s", e.icon);
}
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; qa_q2_player_view_result r; memset(&r, 0, sizeof r); r.has_value = true; return r; }
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_SENTINEL); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }

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

/* ---- item hooks (TS Q2ItemHooks) ---- */
static void ih_weapon_picked(void *c, qa_actor_id p, qa_item_id i, bool f) { EXPECT_CTX(c, ITEM_SENTINEL); (void)p; (void)i; (void)f; }
static void ih_silencer(void *c, qa_actor_id p, double n) { EXPECT_CTX(c, ITEM_SENTINEL); (void)p; (void)n; }
static void ih_power_armor(void *c, qa_actor_id p, const char *k) { EXPECT_CTX(c, ITEM_SENTINEL); (void)p; (void)k; }
static void ih_picked_up(void *c, qa_actor_id player, qa_item_id item, bool usable) {
  EXPECT_CTX(c, ITEM_SENTINEL);
  if (W->picked_count < 8)
    snprintf(W->picked[W->picked_count++], sizeof W->picked[0], "%lld:%s:%s", (long long)player.slot, item, usable ? "true" : "false");
}
static bool module_spawn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  if (c != W->items) ctx_failures++;
  return qa_q2_item_module_spawn(c, e, g);
}

static void world_create(qa_q2_edition edition) {
  W = calloc(1, sizeof *W);
  qa_identity_owner_create(&W->identities, "q2-items");
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

  qa_q2_item_hooks item_hooks;
  memset(&item_hooks, 0, sizeof item_hooks);
  item_hooks.ctx = &ITEM_SENTINEL; item_hooks.weapon_picked = ih_weapon_picked; item_hooks.silencer = ih_silencer;
  item_hooks.power_armor = ih_power_armor; item_hooks.picked_up = ih_picked_up; item_hooks.edition = edition;
  W->items = qa_q2_create_item_module(item_hooks);
  if (W->items == NULL) abort();
  W->module = qa_q2_item_module_as_spawn(W->items);
  W->module.spawn = module_spawn;

  qa_q2_game_options options;
  memset(&options, 0, sizeof options);
  options.edition = edition; options.map_name = "base1"; options.skill = 1;
  options.mode = QA_Q2_MODE_SINGLEPLAYER; options.max_clients = 4; options.provider = "q2:official";
  options.campaign = "q2:base"; options.combat_provider = "q2:combat"; options.inventory_provider = "q2:inventory";
  options.movement_provider = "q1:movement";
  W->foundation = qa_q2_entity_services_create(&W->host, options, &W->module, 1);
  W->game = qa_q2_entity_services_game(W->foundation);
}
static void world_destroy(void) {
  qa_q2_entity_services_destroy(W->foundation);
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

static bool paths_contain(qa_q2_item_module *m, const char *classname, const char *path) {
  size_t n = 0;
  const char *const *paths = qa_q2_item_module_resource_paths(m, &classname, 1, &n);
  for (size_t i = 0; i < n; i++) if (strcmp(paths[i], path) == 0) return true;
  return false;
}

static void test_edition(qa_q2_edition edition) {
  world_create(edition);
  bool rerelease = edition == QA_Q2_EDITION_RERELEASE;
  /* TS peer-input-regressions: body armor pickup sound by edition. */
  CHECK(paths_contain(W->items, "item_armor_body", rerelease ? "sound/misc/ar3_pkup.wav" : "sound/misc/ar1_pkup.wav"));
  CHECK(!paths_contain(W->items, "item_armor_body", rerelease ? "sound/misc/ar1_pkup.wav" : "sound/misc/ar3_pkup.wav"));
  CHECK(paths_contain(W->items, "item_armor_shard", "sound/misc/ar2_pkup.wav"));

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

  qa_q2_spawn_report report = qa_q2_foundation_load(W->foundation,
      "{ \"classname\" \"item_quad\" } { \"classname\" \"ammo_bullets\" } { \"classname\" \"item_armor_shard\" } "
      "{ \"classname\" \"weapon_shotgun\" }", NULL, 0);
  CHECK(report.spawned_count == 4);
  qa_q2_entity *spawned[4];
  for (size_t i = 0; i < report.spawned_count && i < 4; i++) spawned[i] = report.spawned[i];
  for (size_t i = 0; i < report.spawned_count && i < 4; i++) qa_q2_item_module_touch(W->items, spawned[i], W->game, player.id);

  static const char *const picked[] = {"0:q2:item_quad:true", "0:q2:ammo_bullets:false", "0:q2:item_armor_shard:false",
                                       "0:q2:weapon_shotgun:true"};
  CHECK(player.id.slot == 0);
  CHECK(W->picked_count == 4);
  for (size_t i = 0; i < 4 && i < W->picked_count; i++) CHECK(strcmp(W->picked[i], picked[i]) == 0);
  const char *const icons[] = {"p_quad", "a_bullets", rerelease ? "i_armor_shard" : "i_jacketarmor", "w_shotgun"};
  CHECK(W->icon_count == 4);
  for (size_t i = 0; i < 4 && i < W->icon_count; i++) CHECK(strcmp(W->icons[i], icons[i]) == 0);
  world_destroy();
}

int main(void) {
  test_edition(QA_Q2_EDITION_CLASSIC);
  test_edition(QA_Q2_EDITION_RERELEASE);
  CHECK(ctx_failures == 0);
  printf("test_q2_item_hooks: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

/*
 * Port of key cases from tests/composition/ (q1-supply, q2-q3-supply,
 * q3-q2-supply profile data + q1Q2PickupSelect) and direct checks of
 * the expansion transforms, loadouts, and Q1 source clients.
 * GPL-2.0-or-later.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/composition/expansion_source_supply.h"
#include "qa/content/composition/expansion_supply.h"
#include "qa/content/composition/q1/clients.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/composition/q2_q1_supply.h"
#include "qa/content/composition/q2_q3_supply.h"
#include "qa/content/composition/q3_q1_supply.h"
#include "qa/content/composition/q3_q2_supply.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static const qa_pickup_supply_mapping *find_row(const qa_pickup_supply_mapping *rows, size_t count,
                                                const char *source) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(rows[i].source, source) == 0) return &rows[i];
  return NULL;
}
static bool row_has(const qa_pickup_supply_mapping *row, const char *destination) {
  if (row == NULL) return false;
  for (size_t i = 0; i < row->destination_count; i++)
    if (strcmp(row->destinations[i], destination) == 0) return true;
  return false;
}
static bool owners_has(const qa_pickup_supply_profile *profile, bool ammo, const char *item, const char *source) {
  const qa_pickup_supply_owner *owners = ammo ? profile->ammo_owners : profile->weapon_owners;
  size_t count = ammo ? profile->ammo_owner_count : profile->weapon_owner_count;
  if (owners == NULL) return false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(owners[i].item, item) == 0 && strcmp(owners[i].source, source) == 0) return true;
  return false;
}

/* tests/composition/q1-supply.test.ts: pickup rank cases. */
static void test_pickup_select(void) {
  CHECK(!qa_composition_q1_q2_pickup_select("q2:weapon_hyperblaster", "q2:weapon_shotgun", QA_PICKUP_SELECTION_BETTER));
  CHECK(qa_composition_q1_q2_pickup_select("q2:weapon_shotgun", "q2:weapon_hyperblaster", QA_PICKUP_SELECTION_BETTER));
  CHECK(!qa_composition_q1_q2_pickup_select("q2:weapon_shotgun", "q2:weapon_shotgun", QA_PICKUP_SELECTION_BETTER));
  CHECK(qa_composition_q1_q2_pickup_select("q2:weapon_hyperblaster", "q2:weapon_shotgun", QA_PICKUP_SELECTION_ALWAYS));
  CHECK(!qa_composition_q1_q2_pickup_select("q2:weapon_shotgun", "q2:weapon_hyperblaster", QA_PICKUP_SELECTION_NEVER));
  CHECK(qa_composition_q1_q2_pickup_select(NULL, "q2:weapon_blaster", QA_PICKUP_SELECTION_BETTER));
  CHECK_ABORTS((void)qa_composition_q1_q2_pickup_select(NULL, "q2:weapon_boomer", QA_PICKUP_SELECTION_BETTER));
  CHECK_ABORTS((void)qa_composition_q1_q2_pickup_select("q3:weapon/shotgun", "q2:weapon_shotgun", QA_PICKUP_SELECTION_BETTER));
}

static void test_profiles(void) {
  const qa_pickup_supply_profile *p = &QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q1-base-q2-supply") == 0);
  CHECK(p->ammo_count == 4 && p->weapon_count == 8 && p->weapon_owners == NULL && p->ammo_owners == NULL);
  const qa_pickup_supply_mapping *row = find_row(p->ammo, p->ammo_count, "q1:ammo/rockets");
  CHECK(row != NULL && row->destination_count == 2 && row_has(row, "q2:ammo_rockets") && row_has(row, "q2:ammo_grenades"));
  /* TS test: every Q1_Q2 weapon destination is a base Q2 weapon. */
  for (size_t i = 0; i < p->weapon_count; i++)
    for (size_t d = 0; d < p->weapons[i].destination_count; d++)
      CHECK(qa_composition_q1_q2_pickup_select(NULL, p->weapons[i].destinations[d], QA_PICKUP_SELECTION_BETTER));

  p = &QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q2-base-q1-supply") == 0);
  CHECK(p->weapon_owner_count == 8 && owners_has(p, false, "q1:weapon/lightning", "q2:weapon_hyperblaster"));
  row = find_row(p->ammo, p->ammo_count, "q2:ammo_cells");
  CHECK(row_has(row, "q2:ammo_cells") && row_has(row, "q1:ammo/cells"));

  p = &QA_COMPOSITION_Q2_HIPNOTIC_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q2-hipnotic-supply") == 0);
  CHECK(p->weapon_owner_count == 11 && owners_has(p, false, "q1:weapon/hipnotic:mjolnir", "q2:weapon_bfg"));
  row = find_row(p->weapons, p->weapon_count, "q2:weapon_grenadelauncher");
  CHECK(row_has(row, "q1:weapon/hipnotic:proximity"));

  p = &QA_COMPOSITION_Q1_HIPNOTIC_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q1-hipnotic-supply") == 0);
  row = find_row(p->ammo, p->ammo_count, "q1:ammo/cells");
  CHECK(row != NULL && row->destination_count == 1 && row_has(row, "q1:ammo/cells"));
  row = find_row(p->weapons, p->weapon_count, "q1:weapon/lightning");
  CHECK(row != NULL && row->destination_count == 3 && row_has(row, "q1:weapon/hipnotic:laser"));

  p = &QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q2-base-q3-supply") == 0);
  row = find_row(p->ammo, p->ammo_count, "q2:ammo_grenades");
  CHECK(row_has(row, "q2:ammo_grenades") && row_has(row, "q3:ammo/grenadelauncher"));
  row = find_row(p->weapons, p->weapon_count, "q2:weapon_hyperblaster");
  CHECK(row_has(row, "q3:weapon/plasmagun") && row_has(row, "q3:weapon/lightning"));

  p = &QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE;
  CHECK(p->ammo_owner_count == 6 && owners_has(p, true, "q2:ammo_slugs", "q3:ammo/railgun"));
  row = find_row(p->weapons, p->weapon_count, "q3:weapon/machinegun");
  CHECK(row_has(row, "q2:weapon_machinegun") && row_has(row, "q2:weapon_chaingun"));

  p = &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE;
  CHECK(p->ammo_owner_count == 4 && owners_has(p, true, "q1:ammo/cells", "q3:ammo/lightning"));
  p = &QA_COMPOSITION_Q3_HIPNOTIC_SUPPLY_PROFILE;
  row = find_row(p->weapons, p->weapon_count, "q3:weapon/bfg");
  CHECK(row_has(row, "q1:weapon/hipnotic:mjolnir"));

  p = &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q1-base-q3-supply") == 0 && p->weapon_count == 8);

  qa_composition_supply_loadout loadout = qa_composition_q1_q2_supply_loadout();
  CHECK(strcmp(loadout.weapon, "q2:weapon_shotgun") == 0 && loadout.inventory_count == 3);
  CHECK(strcmp(loadout.inventory[2].item, "q2:ammo_shells") == 0 && loadout.inventory[2].count == 25);
  loadout = qa_composition_q3_q2_supply_loadout();
  CHECK(strcmp(loadout.weapon, "q2:weapon_machinegun") == 0 && loadout.inventory_count == 4);
  CHECK(loadout.inventory[3].count == 100);
}

static qa_inventory_entry SPAWN_AMMO[] = {
    {"q3:weapon/gauntlet", 5, 1, false, {0}}, {"q3:weapon/shotgun", 0, 1, false, {0}},
    {"q3:ammo/shotgun", 7, 200, false, {0}}, {"q3:ammo/machinegun", 9, 200, false, {0}}};
static qa_arsenal_state fake_spawn(void *ctx, qa_provider_id provider, const char *product, bool mission_pack) {
  (void)ctx;
  (void)mission_pack;
  qa_arsenal_state state = {0};
  state.provider = provider;
  state.active_weapon = "q3:weapon/machinegun";
  state.state.kind = strcmp(product, "foreign") == 0 ? "q1" : "q3";
  state.ammo = SPAWN_AMMO;
  state.ammo_count = 4;
  return state;
}

static void test_q1_q3_loadout(void) {
  qa_inventory_entry ammo[4];
  qa_arsenal_state out;
  CHECK(qa_composition_q1_q3_supply_loadout("q3:game", NULL, fake_spawn, NULL, ammo, 4, &out));
  CHECK(strcmp(out.active_weapon, "q3:weapon/shotgun") == 0);
  CHECK(out.state.source_weapon == 3);
  CHECK(ammo[0].count == 1 && ammo[1].count == 1 && ammo[2].count == 25 && ammo[3].count == 0);
  CHECK(!qa_composition_q1_q3_supply_loadout("q3:game", NULL, fake_spawn, NULL, ammo, 3, &out));
  CHECK_ABORTS((void)qa_composition_q1_q3_supply_loadout("q3:game", "foreign", fake_spawn, NULL, ammo, 4, &out));
}

static void test_expansions(void) {
  qa_composition_owned_profile owned;
  qa_composition_expansion_supply_kind rogue[] = {QA_COMPOSITION_EXPANSION_Q2_ROGUE};
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE, rogue, 1, &owned));
  qa_pickup_supply_profile view = qa_composition_owned_profile_view(&owned);
  CHECK(strcmp(view.id, "composition:q1-base-q2-supply/q2-rogue") == 0);
  CHECK(view.weapon_owners == NULL && view.ammo_owners == NULL);
  const qa_pickup_supply_mapping *row = find_row(view.weapons, view.weapon_count, "q1:weapon/nailgun");
  CHECK(row_has(row, "q2:weapon_machinegun") && row_has(row, "q2:weapon_etf_rifle"));
  row = find_row(view.ammo, view.ammo_count, "q1:ammo/nails");
  CHECK(row_has(row, "q2:ammo_bullets") && row_has(row, "q2:ammo_flechettes"));
  /* Rockets: first stays q2:ammo_rockets; grenades expand to prox/tesla. */
  row = find_row(view.ammo, view.ammo_count, "q1:ammo/rockets");
  CHECK(row != NULL && strcmp(row->destinations[0], "q2:ammo_rockets") == 0 && row_has(row, "q2:ammo_prox") &&
        row_has(row, "q2:ammo_tesla"));
  qa_composition_owned_profile_destroy(&owned);

  qa_composition_expansion_supply_kind q1rogue[] = {QA_COMPOSITION_EXPANSION_Q1_ROGUE};
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE, q1rogue, 1, &owned));
  view = qa_composition_owned_profile_view(&owned);
  CHECK(view.weapon_owners != NULL && view.ammo_owners == NULL);
  CHECK(owners_has(&view, false, "q1:weapon/rogue:plasma", "q2:weapon_hyperblaster"));
  CHECK(owners_has(&view, false, "q1:weapon/lightning", "q2:weapon_hyperblaster"));
  qa_composition_owned_profile_destroy(&owned);

  qa_composition_expansion_supply_kind mp[] = {QA_COMPOSITION_EXPANSION_Q3_MISSIONPACK};
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE, mp, 1, &owned));
  view = qa_composition_owned_profile_view(&owned);
  CHECK(owners_has(&view, true, "q3:ammo/nailgun", "q3:ammo/machinegun"));
  CHECK(view.ammo_owner_count == 7);
  qa_composition_owned_profile_destroy(&owned);

  /* Empty expansion list still rewrites the id with a trailing slash. */
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE, NULL, 0, &owned));
  CHECK(strcmp(owned.id, "composition:q1-base-q2-supply/") == 0);
  qa_composition_owned_profile_destroy(&owned);

  CHECK(qa_composition_expansion_source_supply(&QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE, &owned));
  view = qa_composition_owned_profile_view(&owned);
  CHECK(strcmp(view.id, "composition:q2-base-q1-supply/expansion-sources") == 0);
  row = find_row(view.ammo, view.ammo_count, "q2:ammo_magslug");
  CHECK(row_has(row, "q1:ammo/nails"));
  row = find_row(view.weapons, view.weapon_count, "q2:weapon_phalanx");
  CHECK(row_has(row, "q1:weapon/supernailgun"));
  CHECK(owners_has(&view, true, "q1:ammo/shells", "q2:ammo_shells"));
  /* q1:ammo/nails is fed by bullets and slugs: no unique owner. */
  CHECK(!owners_has(&view, true, "q1:ammo/nails", "q2:ammo_bullets"));
  CHECK(!owners_has(&view, true, "q1:ammo/nails", "q2:ammo_slugs"));
  qa_composition_owned_profile_destroy(&owned);
}

/* --- Q1 source clients over a fake game/services -------------------------- */
static int token_a, token_b;
static qa_owned_actor actor_a, actor_b;
static char last_team[16];
static char last_auto[16];
static int emit_count;
static bool observer_seen;

static bool fake_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  (void)ctx;
  if (actor.token == &token_a) { *out = actor_a; return true; }
  if (actor.token == &token_b) { *out = actor_b; return true; }
  return false;
}
static void fake_assert_owned(void *ctx, qa_owned_actor actor) { (void)ctx; (void)actor; }
static bool fake_resolve_saved(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out) {
  (void)ctx;
  if (slot == 1 && generation == 1) { *out = actor_a; return true; }
  if (slot == 2 && generation == 1) { *out = actor_b; return true; }
  return false;
}
static void fake_on_release(void *ctx, qa_comp_q1_actor_release_fn callback, void *callback_ctx) {
  (void)ctx; (void)callback; (void)callback_ctx;
}
static void fake_set_team(void *ctx, qa_owned_actor actor, const char *team) {
  (void)ctx; (void)actor;
  (void)snprintf(last_team, sizeof last_team, "%s", team != NULL ? team : "(null)");
}
static void fake_set_auto(void *ctx, qa_actor_id actor, const char *value) {
  (void)ctx; (void)actor;
  (void)snprintf(last_auto, sizeof last_auto, "%s", value);
}
static double fake_cvar(void *ctx, const char *name) { (void)ctx; (void)name; return 0; }
static void fake_set_cvar(void *ctx, const char *name, const char *value) { (void)ctx; (void)name; (void)value; }
static void fake_emit(void *ctx, const qa_comp_q1_event *event) { (void)ctx; (void)event; emit_count++; }
static qa_comp_q1_selected_player fake_selected(void *ctx, qa_actor_id actor) {
  (void)ctx; (void)actor;
  qa_comp_q1_selected_player player = {0};
  player.water_type = "empty";
  return player;
}
static void fake_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  (void)ctx; (void)actor;
  observer_seen = enabled;
}
static void fake_noop_actor(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }

static void test_clients(void) {
  actor_a.id = (qa_actor_id){{NULL, NULL}, 1, 1, &token_a};
  actor_a.owner = "q1:game";
  actor_a.token = &token_a;
  actor_b.id = (qa_actor_id){{NULL, NULL}, 2, 1, &token_b};
  actor_b.owner = "q1:game";
  actor_b.token = &token_b;
  qa_comp_q1_services services = {0};
  services.cvar = fake_cvar;
  services.set_cvar = fake_set_cvar;
  services.emit = fake_emit;
  services.selected_player = fake_selected;
  services.set_observer = fake_set_observer;
  services.disconnect = fake_noop_actor;
  qa_comp_q1_client_game game = {0};
  game.edition = "rerelease";
  game.has_max_clients = true;
  game.max_clients = 4;
  game.resolve_owned = fake_resolve_owned;
  game.assert_owned = fake_assert_owned;
  game.resolve_saved = fake_resolve_saved;
  game.on_release = fake_on_release;
  game.set_traits_team = fake_set_team;
  game.set_auto_switch = fake_set_auto;

  qa_comp_q1_source_clients clients;
  qa_comp_q1_source_clients_init(&clients, game, QA_COMP_Q1_ID1, &services);
  const qa_comp_userinfo_entry info[] = {{"name", "Ranger"}, {"topcolor", "99"}, {"bottomcolor", "4"},
                                         {"qts_weapon_autoswitch", "bogus"}};
  qa_comp_q1_client_admission admission = {0, info, 4};
  emit_count = 0;
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_attach(&clients, actor_a, &admission);
  CHECK(client != NULL);
  CHECK(strcmp(qa_comp_q1_source_client_name(client), "Ranger") == 0);
  CHECK(qa_comp_q1_source_client_shirt(client) == 13); /* clamp 99 -> 13 */
  CHECK(qa_comp_q1_source_client_pants(client) == 4);
  CHECK(client->team == 5);
  CHECK(strcmp(last_team, "5") == 0);
  CHECK(strcmp(last_auto, "always") == 0); /* bogus autoswitch -> always */
  CHECK(emit_count == 1);
  CHECK(qa_comp_q1_source_clients_get(&clients, actor_b.id) == NULL);

  qa_comp_q1_source_clients_spawned(&clients, actor_a.id);
  CHECK(client->team == -1); /* rerelease id1 non-coop */
  qa_comp_q1_source_clients_add_score(&clients, actor_a.id, 16777217);
  CHECK(client->frags == 16777216); /* Math.fround staging */
  qa_comp_q1_source_clients_set_observer(&clients, actor_a.id, true);
  CHECK(observer_seen && client->observer);

  size_t length = 0;
  uint8_t *bytes = qa_comp_q1_source_clients_capture(&clients, &length);
  CHECK(bytes != NULL && length > 0);
  client->frags = 0;
  clients.red_captures = 3;
  CHECK(qa_comp_q1_source_clients_restore(&clients, bytes, length));
  client = qa_comp_q1_source_clients_require(&clients, actor_a.id);
  CHECK(client->frags == 16777216 && client->observer && client->team == -1);
  CHECK(clients.red_captures == 0);
  free(bytes);

  admission.slot = 0;
  CHECK_ABORTS((void)qa_comp_q1_source_clients_attach(&clients, actor_b, &admission));
  admission.slot = 4;
  CHECK_ABORTS((void)qa_comp_q1_source_clients_attach(&clients, actor_b, &admission));
  CHECK_ABORTS((void)qa_comp_q1_source_clients_require(&clients, actor_b.id));
  qa_comp_q1_source_clients_destroy(&clients);
}

int main(void) {
  test_pickup_select();
  test_profiles();
  test_q1_q3_loadout();
  test_expansions();
  test_clients();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

/* Phase B3 checks for the SharedSimulation port (runtime.ts players,
 * admission, travel and checkpoint region, shared_simulation_players.c).
 *
 * Expected values come from the TS methods run under bun on the same inputs:
 * tests/app/oracles/shared_simulation_b3.ts calls the SharedSimulation
 * .prototype methods on fake `this` records
 * (`bun tests/app/oracles/shared_simulation_b3.ts`). Methods that need a
 * constructed simulation (the constructor calls phase B1 and B2 methods) are
 * exercised on a zeroed simulation record holding only the members the method
 * reads. Listener callbacks are checked with sentinel contexts. Linked through
 * the normal Makefile rule.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"
#include "qa/foundation/support.h"

static int failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)

typedef struct fixture {
  qa_app_shared_simulation *sim;
  qa_identity_owner owner;
  qa_owned_actor actor;
  qa_client_id client;
  qa_app_sim_movement_player *player;
  qa_q1_services *game;
  qa_comp_q1_source_composition *composition;
} fixture;

/* A zeroed simulation with the members the B3 player and client methods read:
 * the actor registry, one admitted movement player and the mod client maps. */
static void fixture_init(fixture *f) {
  memset(f, 0, sizeof *f);
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  CHECK(sim != NULL);
  CHECK(qa_identity_owner_create(&f->owner, "b3-session"));
  sim->options.identity = f->owner;
  sim->actors = qa_session_actor_registry_create(&f->owner, 64);
  qa_ss_map_init(&sim->player_states, QA_SS_KEY_OWNED, sizeof(qa_app_sim_movement_player *));
  qa_ss_map_init(&sim->native_movement_players, QA_SS_KEY_OWNED, sizeof(qa_app_sim_movement_player *));
  qa_ss_map_init(&sim->mod_client_admissions, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->mod_client_commands, QA_SS_KEY_ACTOR, sizeof(qa_mod_client_command_receipt));
  qa_ss_map_init(&sim->restored_native_inventory_selections, QA_SS_KEY_ACTOR, sizeof(qa_ss_optional_item));
  f->actor = qa_session_actor_registry_allocate(sim->actors, "q1:campaign", "q1:character");
  f->client = qa_identity_owner_client(&f->owner, 4, 2);
  f->player = calloc(1, sizeof *f->player);
  f->player->actor = f->actor; f->player->client = f->client;
  qa_ss_map_set(&sim->player_states, &f->actor, &f->player);
  f->sim = sim;
}
/* source.kind "q1" with a composition holding the player's client record. */
static void fixture_q1(fixture *f, qa_cvar_registry *cvars) {
  f->game = calloc(1, sizeof *f->game);
  f->game->host.actors = f->sim->actors;
  f->composition = calloc(1, sizeof *f->composition);
  f->composition->clients.game = f->game;
  f->composition->clients.records = calloc(1, sizeof *f->composition->clients.records);
  f->composition->clients.records[0].actor = f->actor;
  f->composition->clients.record_count = 1; f->composition->clients.record_capacity = 1;
  f->sim->source.kind = QA_SS_SOURCE_Q1;
  f->sim->source.q1_composition = f->composition;
  f->sim->source.q1_cvars = cvars;
}
static void fixture_free(fixture *f) {
  if (f->composition) {
    qa_comp_q1_source_client *record = &f->composition->clients.records[0];
    for (size_t i = 0; i < record->userinfo_count; i++) { free((void *)record->userinfo[i].key); free((void *)record->userinfo[i].value); }
    free(record->userinfo);
    free(f->composition->clients.records); free(f->composition); free(f->game);
  }
  qa_app_shared_simulation *sim = f->sim;
  qa_ss_map *maps[] = {&sim->player_states, &sim->native_movement_players, &sim->mod_client_admissions, &sim->mod_client_commands,
                       &sim->restored_native_inventory_selections};
  for (size_t i = 0; i < sizeof maps / sizeof *maps; i++) qa_ss_map_free(maps[i]);
  free(sim->mod_client_listeners);
  qa_session_actor_registry_destroy(sim->actors);
  free(f->player); free(sim);
}
static qa_cvar_registry *make_cvars(fixture *f, const char *pausable) {
  qa_cvar_registry_options options; memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.context.session = f->owner.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  if (pausable) { qa_cvar_snapshot snapshot; if (qa_cvar_registry_register(cvars, "pausable", pausable, 0, &snapshot)) qa_cvar_read_free(&snapshot); }
  return cvars;
}

/* toggleQ1Pause / sourcePlayerName / sourcePlayerUserinfo / storePlayerUserinfo (q2Userinfo parsing). */
static void test_pause(void) {
  static const struct { const char *userinfo; const char *pausable; const char *first, *second, *text; } cases[] = {
    {NULL, NULL, "Server paused the game\n", "Server unpaused the game\n", ""},
    {NULL, "0", "Server paused the game\n", "Server unpaused the game\n", ""},
    {"\\name\\Ranger\\topcolor\\4", "1", "Ranger paused the game\n", "Ranger unpaused the game\n", "\\name\\Ranger\\topcolor\\4"},
    {"\\name\\Ranger\\topcolor\\4", "0", "Pause not allowed.\n", "Pause not allowed.\n", "\\name\\Ranger\\topcolor\\4"},
    {"\\skin\\male/grunt\\fov\\90", NULL, "unconnected paused the game\n", "unconnected unpaused the game\n", "\\skin\\male/grunt\\fov\\90"},
    {"name\\Ranger\\name\\Second\\odd", "1", "Ranger paused the game\n", "Ranger unpaused the game\n", "\\name\\Ranger"},
    {"\\name\\\\x\\y", "1", " paused the game\n", " unpaused the game\n", "\\name\\\\x\\y"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    fixture f; fixture_init(&f);
    qa_cvar_registry *cvars = make_cvars(&f, cases[i].pausable);
    fixture_q1(&f, cvars);
    const qa_actor_id *actor = NULL;
    if (cases[i].userinfo) { qa_app_shared_sim_store_player_userinfo(f.sim, f.actor.id, cases[i].userinfo); actor = &f.actor.id; }
    char first[128]; snprintf(first, sizeof first, "%s", qa_app_shared_sim_toggle_q1_pause(f.sim, actor));
    const char *second = qa_app_shared_sim_toggle_q1_pause(f.sim, actor);
    CHECK(strcmp(first, cases[i].first) == 0);
    CHECK(strcmp(second, cases[i].second) == 0);
    CHECK(!qa_app_shared_sim_q1_paused(f.sim));
    if (actor) CHECK(strcmp(qa_app_shared_sim_source_player_userinfo(f.sim, *actor), cases[i].text) == 0);
    qa_cvar_registry_destroy(cvars);
    fixture_free(&f);
  }
}

/* sharedCarryOf over the SimulationTravel player state kinds. */
static void test_shared_carry(void) {
  qa_inventory_entry inventory[1]; memset(inventory, 0, sizeof inventory);
  inventory[0].item = "q2:ammo_shells"; inventory[0].count = 10; inventory[0].capacity = 100;
  qa_armor_state armor; memset(&armor, 0, sizeof armor); armor.regular.kind = QA_REGULAR_ARMOR_Q2; armor.powered.kind = "none";
  qa_q1_travel_state q1; memset(&q1, 0, sizeof q1); q1.health = 33; q1.armor = armor; q1.inventory = inventory; q1.inventory_count = 1;
  qa_q2_base_player_carry q2; memset(&q2, 0, sizeof q2); q2.health = 120; q2.armor = armor; q2.inventory = inventory; q2.inventory_count = 1;
  qa_app_sim_travel_player quakec; memset(&quakec, 0, sizeof quakec); quakec.state_kind = "quakec";
  qa_app_sim_travel_player shared; memset(&shared, 0, sizeof shared);
  shared.state_kind = "shared"; shared.health = 75; shared.armor = armor; shared.inventory = inventory; shared.inventory_count = 1;
  qa_app_sim_travel_player q1_player; memset(&q1_player, 0, sizeof q1_player); q1_player.state_kind = "q1"; q1_player.carry = &q1;
  qa_app_sim_travel_player q2_player; memset(&q2_player, 0, sizeof q2_player); q2_player.state_kind = "q2"; q2_player.carry = &q2;
  qa_ss_shared_carry out;
  CHECK(!qa_app_shared_sim_shared_carry_of(NULL, &out));
  CHECK(!qa_app_shared_sim_shared_carry_of(&quakec, &out));
  CHECK(qa_app_shared_sim_shared_carry_of(&shared, &out) && out.health == 75 && out.inventory_count == 1);
  CHECK(qa_app_shared_sim_shared_carry_of(&q1_player, &out) && out.health == 33 && out.inventory_count == 1);
  CHECK(qa_app_shared_sim_shared_carry_of(&q2_player, &out) && out.health == 120 && out.inventory_count == 1);
}

/* takeLevelChange: returns the change once, then null. */
static void test_level_change(void) {
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  sim->has_level_change = true; sim->level_change_map = qa_ss_strdup("base2"); sim->level_change_server_flags = 4;
  qa_ss_level_change change;
  CHECK(qa_app_shared_sim_take_level_change(sim, &change));
  CHECK(strcmp(change.map, "base2") == 0 && change.server_flags == 4 && !change.has_landmark);
  free(change.map);
  CHECK(!qa_app_shared_sim_take_level_change(sim, &change));
  CHECK(sim->level_change_map == NULL);
  free(sim);
}

/* takeTransitions: campaign intents gain the source match gates. */
static qa_source_match_player_result no_match_player(void *ctx, qa_actor_id actor) {
  (void)ctx; (void)actor; qa_source_match_player_result result; memset(&result, 0, sizeof result); return result;
}
static void test_transitions(void) {
  fixture f; fixture_init(&f);
  qa_app_shared_simulation *sim = f.sim;
  sim->source_match = qa_mod_match_state_create(sim->actors, no_match_player, NULL);
  CHECK(qa_event_ring_init(&sim->transitions, 8, sizeof(qa_transition_intent)));
  CHECK(qa_event_ring_open_cursor(&sim->transitions, &sim->transitions_cursor, false));
  qa_mission_gate gate; memset(&gate, 0, sizeof gate);
  qa_transition_intent intents[3]; memset(intents, 0, sizeof intents);
  intents[0].kind = "campaign-level"; intents[0].map = "q1:e1m2"; intents[0].spawn_point = ""; intents[0].gates = &gate; intents[0].gate_count = 1;
  intents[1].kind = "match-end";
  intents[2].kind = "campaign-complete";
  for (int i = 0; i < 3; i++) qa_event_ring_admit(&sim->transitions, &intents[i]);
  size_t count;
  const qa_transition_intent *taken = qa_app_shared_sim_take_transitions(sim, &count);
  size_t gates; qa_mod_match_state_gates(sim->source_match, &gates);
  /* Oracle: transitions campaign-level:1,match-end:0,campaign-complete:0 0 (a match with no objectives). */
  CHECK(count == 3 && gates == 0);
  CHECK(strcmp(taken[0].kind, "campaign-level") == 0 && taken[0].gate_count == 1);
  CHECK(strcmp(taken[1].kind, "match-end") == 0 && taken[1].gate_count == 0);
  CHECK(strcmp(taken[2].kind, "campaign-complete") == 0 && taken[2].gate_count == 0);
  CHECK(qa_event_ring_pending(&sim->transitions, &sim->transitions_cursor) == 0);
  qa_event_ring_destroy(&sim->transitions);
  qa_mod_match_state_destroy(sim->source_match);
  fixture_free(&f);
}

/* notifyClientEvent: admission dedupe, listener snapshot order, disconnect cleanup; sentinel contexts. */
typedef struct listener_log { int sentinel; char seen[256]; } listener_log;
static listener_log first_log = {11, ""}, second_log = {22, ""};
static void first_listener(void *ctx, qa_mod_client_event event) {
  CHECK(ctx == &first_log && first_log.sentinel == 11);
  strcat(first_log.seen, "first:"); strcat(first_log.seen, event.kind); strcat(first_log.seen, ",");
}
static void second_listener(void *ctx, qa_mod_client_event event) {
  CHECK(ctx == &second_log && second_log.sentinel == 22);
  strcat(first_log.seen, "second:"); strcat(first_log.seen, event.kind); strcat(first_log.seen, ",");
}
static void test_notify(void) {
  fixture f; fixture_init(&f);
  qa_app_shared_simulation *sim = f.sim;
  qa_app_shared_sim_add_mod_client_listener(sim, first_listener, &first_log);
  qa_app_shared_sim_add_mod_client_listener(sim, second_listener, &second_log);
  qa_mod_client_command_receipt receipt; memset(&receipt, 0, sizeof receipt);
  qa_ss_map_set(&sim->mod_client_commands, &f.actor.id, &receipt);
  qa_app_shared_sim_notify_client_event(sim, "admitted", f.actor.id);
  qa_app_shared_sim_notify_client_event(sim, "admitted", f.actor.id);
  qa_app_shared_sim_notify_client_event(sim, "userinfo", f.actor.id);
  qa_app_shared_sim_notify_client_event(sim, "disconnecting", f.actor.id);
  /* Oracle: first:admitted,second:admitted,cursors,first:userinfo,second:userinfo,first:disconnecting,second:disconnecting 0 0
   * (restoreNativeInventoryCursors has no saved cursor here, so it records nothing in C). */
  CHECK(strcmp(first_log.seen, "first:admitted,second:admitted,first:userinfo,second:userinfo,first:disconnecting,second:disconnecting,") == 0);
  CHECK(sim->mod_client_admissions.count == 0 && sim->mod_client_commands.count == 0);
  fixture_free(&f);
}

int main(void) {
  test_pause();
  test_shared_carry();
  test_level_change();
  test_transitions();
  test_notify();
  if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
  puts("test_shared_simulation_b3: ok");
  return 0;
}

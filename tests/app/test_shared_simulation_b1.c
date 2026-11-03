/* Phase B1 checks for the SharedSimulation port (runtime.ts "worlds and
 * rules", docs/runtime_layout.md). The expected values were printed by
 * tests/app/oracles/shared_simulation_b1.ts, which runs the real runtime.ts
 * methods and guest-monsters.ts under bun on stub `this` objects carrying
 * the same inputs as the C fixtures below.
 *
 * The full constructor needs the phase B2/B3 methods, so each check builds a
 * zeroed simulation with only the members the method under test reads.
 * Callbacks the methods receive from their callers check a sentinel ctx.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"

static int failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)
#define CHECK_VEC(v, X, Y, Z) CHECK((v).x == (X) && (v).y == (Y) && (v).z == (Z))

static const char *const entities =
  "{ \"classname\" \"worldspawn\" }\n"
  "{ \"classname\" \"info_player_deathmatch\" \"origin\" \"0 0 24\" }\n"
  "{ \"classname\" \"info_player_deathmatch\" \"origin\" \"512 0 24\" }\n"
  "{ \"classname\" \"info_player_start\" \"origin\" \"64 64 0\" }\n"
  "{ \"classname\" \"info_player_deathmatch\" \"origin\" \" 1 2 3\" }\n"
  "{ \"classname\" \"info_player_deathmatch\" \"origin\" \"-256 768 40\" }\n"
  "{ \"classname\" \"item_health\" \"origin\" \"100 100 8\" }\n"
  "{ \"classname\" \"item_health_large\" \"origin\" \"-100 300 8\" }\n"
  "{ \"classname\" \"info_flag_blue\" \"origin\" \"900 900 16\" }\n"
  "{ \"classname\" \"monster_soldier\" \"origin\" \"10 20 30\" \"angle\" \"90\" }\n"
  "{ \"classname\" \"monster_tank\" \"origin\" \"1 2 3\" \"targetname\" \"t1\" }\n"
  "{ \"classname\" \"monster_gunner\" \"origin\" \"4 5 6\" \"spawnflags\" \"2\" }\n"
  "{ \"classname\" \"monster_berserk\" \"origin\" \"7 8 9\" \"spawnflags\" \"256\" }\n"
  "{ \"classname\" \"monster_infantry\" \"origin\" \"1 1 1\" \"item\" \"item_armor_shard\" }\n"
  "{ \"classname\" \"dm_dball_goal\" \"origin\" \"300 0 0\" }\n"
  "{ \"classname\" \"dm_dball_goal\" \"origin\" \"-300 0 0\" }\n"
  "{ \"classname\" \"dm_dball_team1_start\" \"origin\" \"-200 10 0\" }";

/* A zeroed simulation with the maps the B1 methods under test touch. */
static qa_app_shared_simulation *fake_sim(const char *match, const char *map_provider, qa_ss_source_kind source, const char *text) {
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  if (!sim) abort();
  qa_ss_map_init(&sim->player_states, QA_SS_KEY_OWNED, sizeof(qa_app_sim_movement_player *));
  qa_ss_map_init(&sim->lmctf_spot_flags, QA_SS_KEY_STRING, sizeof(qa_vec3));
  qa_ss_map_init(&sim->deathball_goals, QA_SS_KEY_NUMBER, sizeof(qa_vec3));
  qa_ss_map_init(&sim->guest_monster_plans, QA_SS_KEY_STRING, sizeof(qa_app_sim_guest_monster_plan *));
  qa_ss_map_init(&sim->monster_sources, QA_SS_KEY_STRING, sizeof(void *));
  qa_ss_map_init(&sim->source.clients, QA_SS_KEY_CLIENT, sizeof(qa_owned_actor));
  qa_ss_map_init(&sim->native_mirrored_combat, QA_SS_KEY_ACTOR, 0);
  sim->recipe.match.provider = match;
  sim->recipe.map.entities.provider = map_provider;
  sim->recipe.map.entities.content = map_provider;
  sim->source.kind = source;
  sim->options.mode = "deathmatch";
  sim->world_entities = text;
  return sim;
}
static void free_fake_sim(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_worlds_destroy(sim);
  qa_ss_map *maps[] = {&sim->player_states, &sim->lmctf_spot_flags, &sim->deathball_goals, &sim->guest_monster_plans,
    &sim->monster_sources, &sim->source.clients, &sim->native_mirrored_combat};
  for (size_t i = 0; i < sizeof maps / sizeof *maps; i++) qa_ss_map_free(maps[i]);
  free(sim->tag_spawns); free(sim->lmctf_spot_health); free(sim->ordered_players);
  for (size_t i = 0; i < sim->allocation_count; i++) free(sim->allocations[i]);
  free(sim->allocations);
  free(sim);
}

/* filterEntityText / planGuestMonsters (guest-monsters.ts). */
static void test_guest_monster_plans(void) {
  double keep[] = {2, 9};
  char *filtered = qa_app_sim_filter_entity_text(entities, keep, 2);
  CHECK(strcmp(filtered, "{ \"classname\" \"worldspawn\" }\n{ \"classname\" \"info_player_deathmatch\" \"origin\" \"512 0 24\" }\n"
    "{ \"classname\" \"monster_soldier\" \"origin\" \"10 20 30\" \"angle\" \"90\" }\n") == 0);
  free(filtered);
  /* A quote ends at the next quote: TS does not honor backslash escapes. */
  double keep_quoted[] = {2};
  char *quoted = qa_app_sim_filter_entity_text("{ \"classname\" \"worldspawn\" \"message\" \"a\\\"}{\" }\n{ \"classname\" \"light\" }\n"
    "{ \"classname\" \"info_null\" }", keep_quoted, 1);
  CHECK(strcmp(quoted, "{ \"classname\" \"worldspawn\" \"message\" \"a\\\"}\n") == 0);
  free(quoted);

  qa_monster_by_classname by_classname[] = {{"monster_berserk", {true, {{NULL, NULL}, NULL}}}};
  qa_enemy_selection selection; memset(&selection, 0, sizeof selection);
  selection.kind = QA_ENEMY_SELECTION_REPLACE;
  selection.default_target.is_map_defined = false;
  selection.default_target.reference = (qa_monster_definition_reference){{"q2:monsters", "q2:classic:baseq2:id"}, "monster_gunner"};
  selection.by_classname = by_classname; selection.by_classname_count = 1;

  qa_app_sim_guest_monster_plan *plan = qa_app_sim_plan_guest_monsters(entities, QA_Q2_EDITION_CLASSIC, "q2", &selection, 1, "singleplayer", true);
  CHECK(plan->replacement_count == 1 && plan->replacements[0].ordinal == 9 && strcmp(plan->replacements[0].classname, "monster_soldier") == 0);
  CHECK(strcmp(plan->replacements[0].definition.classname, "monster_gunner") == 0);
  CHECK(strcmp(qa_q2_spawn_get(plan->replacements[0].fields, "angle"), "90") == 0);
  CHECK(plan->kept_count == 3);
  CHECK(plan->kept_count == 3 && strcmp(plan->kept[0].classname, "monster_tank") == 0 && strcmp(plan->kept[0].reason, "triggered monsters stay guest-native") == 0);
  CHECK(plan->kept_count == 3 && strcmp(plan->kept[1].classname, "monster_gunner") == 0 && strcmp(plan->kept[1].reason, "spawn flags 2 need guest-native handling") == 0);
  CHECK(plan->kept_count == 3 && strcmp(plan->kept[2].classname, "monster_infantry") == 0 && strcmp(plan->kept[2].reason, "item drops stay guest-native") == 0);
  /* The replaced soldier's span is gone; every other entity keeps its bytes plus "\n". */
  CHECK(strstr(plan->entities, "monster_soldier") == NULL && strstr(plan->entities, "\"monster_tank\" \"origin\" \"1 2 3\" \"targetname\" \"t1\" }\n") != NULL);
  CHECK(strncmp(plan->entities, "{ \"classname\" \"worldspawn\" }\n{ \"classname\" \"info_player_deathmatch\" \"origin\" \"0 0 24\" }\n", 85) == 0);
  qa_app_sim_guest_monster_plan_destroy(plan);

  plan = qa_app_sim_plan_guest_monsters(entities, QA_Q2_EDITION_RERELEASE, "q3", &selection, 0, "coop", true);
  CHECK(plan->replacement_count == 1 && plan->replacements[0].ordinal == 9 && plan->kept_count == 3);
  qa_app_sim_guest_monster_plan_destroy(plan);

  plan = qa_app_sim_plan_guest_monsters(entities, QA_Q2_EDITION_CLASSIC, "q2", &selection, 1, "singleplayer", false);
  CHECK(plan->replacement_count == 0 && plan->kept_count == 4 && strcmp(plan->entities, entities) == 0);
  CHECK(plan->kept_count == 4 && strcmp(plan->kept[3].classname, "monster_infantry") == 0
    && strcmp(plan->kept[3].reason, "guest has no declared combat profile to project selected monsters") == 0);
  qa_app_sim_guest_monster_plan_destroy(plan);

  plan = qa_app_sim_plan_guest_monsters(entities, QA_Q2_EDITION_CLASSIC, "q1", &selection, 2, "deathmatch", true);
  CHECK(plan->replacement_count == 0 && plan->kept_count == 4 && strcmp(plan->entities, entities) == 0);
  CHECK(plan->kept_count == 4 && strcmp(plan->kept[0].reason, "authored obligations are not preserved by selected admission") == 0);
  qa_app_sim_guest_monster_plan_destroy(plan);
}

/* tagSpawnOrigins / farthestDeathmatchSpawn / flag modes / lmctfFlagHome / deathballOrigins. */
static void test_match_origins(void) {
  qa_app_shared_simulation *sim = fake_sim("q2:lmctf", "q1:id1", QA_SS_SOURCE_Q1, entities);
  size_t count = 0;
  const qa_vec3 *spawns = qa_app_shared_sim_tag_spawn_origins(sim, &count);
  /* " 1 2 3" splits into four parts, so it is not a spawn origin. */
  CHECK(count == 3);
  if (count == 3) { CHECK_VEC(spawns[0], 0, 0, 24); CHECK_VEC(spawns[1], 512, 0, 24); CHECK_VEC(spawns[2], -256, 768, 40); }
  qa_vec3 spot;
  CHECK(qa_app_shared_sim_farthest_deathmatch_spawn(sim, &spot)); CHECK_VEC(spot, 0, 0, 48);
  CHECK(!qa_app_shared_sim_foreign_tag(sim) && qa_app_shared_sim_foreign_lmctf(sim) && !qa_app_shared_sim_foreign_ctf(sim));
  CHECK(qa_ss_is(qa_app_shared_sim_flag_mode(sim), "lmctf") && !qa_app_shared_sim_foreign_deathball(sim));
  qa_vec3 home;
  CHECK(qa_app_shared_sim_lmctf_flag_home(sim, 1, &home)); CHECK_VEC(home, -256, 768, 40);
  CHECK(qa_app_shared_sim_lmctf_flag_home(sim, 2, &home)); CHECK_VEC(home, 900, 900, 16);
  CHECK(sim->lmctf_spot_health_count == 2);
  if (sim->lmctf_spot_health_count == 2) { CHECK_VEC(sim->lmctf_spot_health[0], 100, 100, 8); CHECK_VEC(sim->lmctf_spot_health[1], -100, 300, 8); }
  free_fake_sim(sim);

  sim = fake_sim("q1:ctf", "q2:base", QA_SS_SOURCE_Q3, entities);
  CHECK(!qa_app_shared_sim_foreign_lmctf(sim) && qa_app_shared_sim_foreign_ctf(sim) && qa_ss_is(qa_app_shared_sim_flag_mode(sim), "ctf"));
  CHECK(qa_app_shared_sim_lmctf_flag_home(sim, 1, &home)); CHECK_VEC(home, -256, 768, 40);
  CHECK(qa_app_shared_sim_lmctf_flag_home(sim, 2, &home)); CHECK_VEC(home, 512, 0, 24);
  free_fake_sim(sim);

  sim = fake_sim("q2:deathball", "q1:id1", QA_SS_SOURCE_Q1, entities);
  qa_vec3 ball; double one = 1, two = 2;
  CHECK(qa_app_shared_sim_foreign_deathball(sim));
  CHECK(qa_app_shared_sim_deathball_origins(sim, &ball)); CHECK_VEC(ball, -256, 768, 40);
  const qa_vec3 *goal1 = qa_ss_map_get(&sim->deathball_goals, &one), *goal2 = qa_ss_map_get(&sim->deathball_goals, &two);
  CHECK(goal1 && goal2 && sim->deathball_goals.count == 2 && qa_ss_map_key_at(&sim->deathball_goals, 0)->number == 1);
  if (goal1 && goal2) { CHECK_VEC(*goal1, -300, 0, 0); CHECK_VEC(*goal2, 300, 0, 0); }
  free_fake_sim(sim);

  /* No authored goals or ball: synthesized from the deathmatch spawns. */
  size_t length = strlen(entities);
  char *no_goals = calloc(length + 1, 1); size_t used = 0;
  for (const char *line = entities; *line;) {
    const char *end = strchr(line, '\n'); size_t size = end ? (size_t)(end - line) + 1 : strlen(line);
    if (!memmem(line, size, "dm_dball", 8)) { memcpy(no_goals + used, line, size); used += size; }
    line += size;
  }
  /* TS joins the kept lines with "\n": drop a trailing separator. */
  if (used && no_goals[used - 1] == '\n') no_goals[--used] = '\0';
  sim = fake_sim("q2:deathball", "q1:id1", QA_SS_SOURCE_Q1, no_goals);
  CHECK(qa_app_shared_sim_deathball_origins(sim, &ball)); CHECK_VEC(ball, 0, 0, 24);
  goal1 = qa_ss_map_get(&sim->deathball_goals, &one); goal2 = qa_ss_map_get(&sim->deathball_goals, &two);
  CHECK(goal1 && goal2);
  if (goal1 && goal2) { CHECK_VEC(*goal1, 512, 0, 24); CHECK_VEC(*goal2, -256, 768, 40); }
  free_fake_sim(sim);
  free(no_goals);

  /* No deathmatch spawn: info_player_start falls back; a two-part origin is skipped. */
  sim = fake_sim("q2:tag", "q1:id1", QA_SS_SOURCE_Q1,
    "{ \"classname\" \"worldspawn\" }\n{ \"classname\" \"info_player_start\" \"origin\" \"8 9 10\" }\n"
    "{ \"classname\" \"info_player_deathmatch\" \"origin\" \"1 2\" }");
  spawns = qa_app_shared_sim_tag_spawn_origins(sim, &count);
  CHECK(count == 1);
  if (count == 1) CHECK_VEC(spawns[0], 8, 9, 10);
  CHECK(qa_app_shared_sim_farthest_deathmatch_spawn(sim, &spot)); CHECK_VEC(spot, 8, 9, 34);
  CHECK(qa_app_shared_sim_foreign_tag(sim));
  free_fake_sim(sim);
}

/* foreignQ2Identity / foreignQ1Identity / foreignAdjunctIdentity / guestMapEdition. */
static qa_archive_mount mount;
static qa_app_shared_simulation *identity_sim(const char *world, const char *map_provider, const char *content) {
  qa_app_shared_simulation *sim = fake_sim("q1:dm", map_provider, QA_SS_SOURCE_LOADING, "");
  sim->world_kind = world;
  memset(&mount, 0, sizeof mount);
  mount.identity.content = content;
  sim->recipe.map.geometry.provenance.kind = QA_CONTENT_MOUNT_ARCHIVE;
  sim->recipe.map.geometry.provenance.archive_mount = &mount;
  return sim;
}
static void check_identity(const char *world, const char *map_provider, const char *content, bool q3_guest,
    const char *q2_provider, const char *q2_edition, const char *q2_program, const char *q1_provider, const char *q1_edition,
    const char *q1_program, const char *adjunct_family) {
  qa_app_shared_simulation *sim = identity_sim(world, map_provider, content);
  qa_app_sim_q3_guest_options guest; memset(&guest, 0, sizeof guest);
  if (q3_guest) sim->options.q3_guest = &guest;
  qa_provider_reference reference; const char *edition, *program, *family;
  bool q2 = qa_app_shared_sim_foreign_q2_identity_full(sim, &reference, &edition, &program);
  CHECK(q2 == (q2_provider != NULL));
  if (q2 && q2_provider) CHECK(strcmp(reference.provider, q2_provider) == 0 && strcmp(edition, q2_edition) == 0
      && strcmp(program, q2_program) == 0 && strcmp(reference.content, content) == 0);
  bool q1 = qa_app_shared_sim_foreign_q1_identity(sim, &reference, &edition, &program);
  CHECK(q1 == (q1_provider != NULL));
  if (q1 && q1_provider) CHECK(strcmp(reference.provider, q1_provider) == 0 && strcmp(edition, q1_edition) == 0 && strcmp(program, q1_program) == 0);
  bool adjunct = qa_app_shared_sim_foreign_adjunct_identity(sim, &reference, &family, &edition);
  CHECK(adjunct == (adjunct_family != NULL));
  if (adjunct && adjunct_family) CHECK(strcmp(family, adjunct_family) == 0
      && strcmp(reference.provider, q2_provider ? q2_provider : q1_provider) == 0);
  free_fake_sim(sim);
}
static void test_identities(void) {
  check_identity("q2-bsp", "q1:base", "q2:rerelease:xatrix:pak0", false, "q2:source/rerelease/xatrix", "rerelease", "xatrix", NULL, NULL, NULL, "q2");
  check_identity("q2-bsp", "q3:base", "q2:classic:n64", false, "q2:source/rerelease/mg2", "rerelease", "mg2", NULL, NULL, NULL, "q2");
  check_identity("q2-bsp", "q2:base", "q2:classic:baseq2", false, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  check_identity("q1-bsp", "q2:base", "q1:rerelease:mg1:x", false, NULL, NULL, NULL, "q1:source/rerelease/mg1", "rerelease", "mg1", "q1");
  check_identity("q1-bsp", "q3:base", "q1", false, NULL, NULL, NULL, "q1:source/classic/id1", "classic", "id1", "q1");
  check_identity("q1-bsp", "q2:base", "q1:classic:hipnotic", true, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  qa_app_shared_simulation *sim = fake_sim("q1:dm", "q2:rerelease:baseq2:x", QA_SS_SOURCE_LOADING, "");
  CHECK(strcmp(qa_app_shared_sim_guest_map_edition(sim), "rerelease") == 0);
  sim->recipe.map.entities.content = "q2:classic:rerelease";
  CHECK(strcmp(qa_app_shared_sim_guest_map_edition(sim), "classic") == 0);
  free_fake_sim(sim);
}

/* nativeWaterContents over movement profiles. */
static double water(const char *profile, qa_movement_state_kind state, double state_water, double water_type) {
  qa_app_sim_movement_player player; memset(&player, 0, sizeof player);
  player.profile.kind = profile; player.state.kind = state; player.state.as.q1.water_type = state_water; player.water_type = water_type;
  return qa_app_shared_sim_native_water_contents(NULL, &player);
}
static void test_native_water_contents(void) {
  CHECK(water("q1-netquake", QA_MOVEMENT_STATE_Q1, -4, 0) == 64);
  CHECK(water("q1-quakeworld", QA_MOVEMENT_STATE_QW, 0, -5) == 128);
  CHECK(water("q3", QA_MOVEMENT_STATE_Q3, 0, 24) == 64);
  CHECK(water("q3", QA_MOVEMENT_STATE_Q3, 0, 16) == 64);
  CHECK(water("q2-classic", QA_MOVEMENT_STATE_Q2, 0, 1 | 64 | 32) == 96);
  CHECK(water("q1-netquake", QA_MOVEMENT_STATE_Q1, -2, 0) == 0);
}

/* rereleaseNavigationServices forward to the bot transport with the transport's own ctx. */
static int transport_sentinel;
static qa_vec3 seen_point; static double seen_tolerance; static qa_actor_id seen_target;
static int32_t sentinel_move(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance) {
  CHECK(ctx == &transport_sentinel); (void)actor;
  seen_point = point; seen_tolerance = tolerance;
  return 2;
}
static int32_t sentinel_follow(void *ctx, qa_actor_id actor, qa_actor_id target) {
  CHECK(ctx == &transport_sentinel); (void)actor;
  seen_target = target;
  return 3;
}
static bool sentinel_is_bot(void *ctx, qa_actor_id actor) { CHECK(ctx == &transport_sentinel); return actor.slot == 7; }
typedef struct install_attempt { qa_app_shared_simulation *sim; qa_app_sim_application_bot_navigation *navigation; } install_attempt;
static void install(void *ctx) { install_attempt *attempt = ctx; qa_app_shared_sim_install_rerelease_navigation(attempt->sim, attempt->navigation); }
static void test_navigation_services(void) {
  qa_app_shared_simulation *sim = fake_sim("q2:dm", "q2:base", QA_SS_SOURCE_Q2_NATIVE, "");
  qa_app_sim_bot_service transport; memset(&transport, 0, sizeof transport);
  transport.ctx = &transport_sentinel; transport.move_to_point = sentinel_move; transport.follow_actor = sentinel_follow; transport.is_bot = sentinel_is_bot;
  sim->bot_services.transport = &transport;
  qa_compat_q2_rerelease_navigation_services services = qa_app_shared_sim_rerelease_navigation_services(sim);
  qa_actor_id actor; memset(&actor, 0, sizeof actor); actor.slot = 7;
  CHECK(services.runtime(services.ctx) == NULL);
  CHECK(services.move_to_point(services.ctx, actor, (qa_vec3){1, 2, 3}, 16) == 2);
  CHECK_VEC(seen_point, 1, 2, 3); CHECK(seen_tolerance == 16);
  qa_actor_id target; memset(&target, 0, sizeof target); target.slot = 9;
  CHECK(services.follow_actor(services.ctx, actor, target) == 3 && seen_target.slot == 9);
  /* installRereleaseNavigation: one navigation per world. */
  qa_app_sim_application_bot_navigation *first = (qa_app_sim_application_bot_navigation *)&transport_sentinel;
  qa_app_sim_application_bot_navigation *second = (qa_app_sim_application_bot_navigation *)&seen_point;
  qa_app_shared_sim_install_rerelease_navigation(sim, first);
  qa_app_shared_sim_install_rerelease_navigation(sim, first);
  CHECK(qa_app_shared_sim_rerelease_navigation(sim) == first);
  install_attempt attempt = {sim, second}; qa_caught_error caught;
  CHECK(!qa_try(install, &attempt, &caught));
  CHECK(strstr(caught.message, "Native navigation already belongs to this world") != NULL);
  /* nativeModContext(): undefined without a weapon behavior clock. */
  qa_app_sim_native_mod_host_context context;
  CHECK(!qa_app_shared_sim_native_mod_context(sim, &context));
  sim->native_navigation = NULL;
  free_fake_sim(sim);
}

/* selectionConsumption on a native guest with no enemy plan. */
static void test_selection_consumption(void) {
  qa_app_shared_simulation *sim = fake_sim("q2:dm", "q2:base", QA_SS_SOURCE_Q2_NATIVE, "");
  sim->source.edition = "classic";
  sim->recipe.map.entities.content = "q2:classic:baseq2:id";
  sim->recipe.enemies.kind = QA_ENEMY_SELECTION_MAP_DEFINED;
  sim->recipe.movement = (qa_provider_reference){"mv:provider", "q1:x"};
  sim->recipe.character.definition.provider = "q3:characters";
  qa_provider_timing timing; memset(&timing, 0, sizeof timing);
  timing.provider = "mv:provider"; timing.clock.kind = QA_CLOCK_Q1_NETQUAKE;
  sim->recipe.timing = &timing; sim->recipe.timing_count = 1;
  qa_app_shared_sim_consumption consumption = qa_app_shared_sim_selection_consumption(sim);
  CHECK(consumption.count == 2);
  if (consumption.count == 2) {
    CHECK(strcmp(consumption.entries[0].axis, "movement") == 0 && strcmp(consumption.entries[0].requested, "mv:provider") == 0
      && strcmp(consumption.entries[0].outcome, "honored") == 0 && strcmp(consumption.entries[0].detail, "q1-netquake interposed at the guest Pmove") == 0);
    CHECK(strcmp(consumption.entries[1].axis, "character") == 0 && strcmp(consumption.entries[1].requested, "q3:characters") == 0
      && strcmp(consumption.entries[1].detail, "selected character drawn over the guest player edict; corpses and bodies stay guest models") == 0);
  }
  qa_app_shared_sim_selection_consumption_free(&consumption);
  timing.clock.kind = QA_CLOCK_Q2_CLASSIC; sim->recipe.movement.content = "q2:other";
  consumption = qa_app_shared_sim_selection_consumption(sim);
  CHECK(consumption.count == 2);
  if (consumption.count == 2) CHECK(strcmp(consumption.entries[0].outcome, "guest-native") == 0 && strcmp(consumption.entries[0].detail,
      "shares the guest q2-classic clock but is a different provider than q2:classic:baseq2:id; the guest Pmove runs") == 0);
  qa_app_shared_sim_selection_consumption_free(&consumption);
  timing.clock.kind = QA_CLOCK_Q3; sim->recipe.movement.content = "q3:x";
  qa_actor_id mirrored[2]; memset(mirrored, 0, sizeof mirrored); mirrored[1].slot = 1;
  qa_ss_map_set(&sim->native_mirrored_combat, &mirrored[0], NULL); qa_ss_map_set(&sim->native_mirrored_combat, &mirrored[1], NULL);
  consumption = qa_app_shared_sim_selection_consumption(sim);
  CHECK(consumption.count == 2);
  if (consumption.count == 2) CHECK(strcmp(consumption.entries[0].detail,
      "q3 interposed at the guest Pmove; combat mirror follows guest health/armor/invulnerability, team null, shared damage writes discarded") == 0);
  qa_app_shared_sim_selection_consumption_free(&consumption);
  free_fake_sim(sim);
}

int main(void) {
  test_guest_monster_plans();
  test_match_origins();
  test_identities();
  test_native_water_contents();
  test_navigation_services();
  test_selection_consumption();
  if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
  puts("test_shared_simulation_b1: ok");
  return 0;
}

/* Rows read against src/settings/server/q2-owner.ts: q2RereleaseItemServices,
 * bindQ2PlayerCvars and bindQ2ServerCvars. Reproduces
 * tests/settings/q2-source-cvars.test.ts with pull/push in place of the TS
 * accessor properties. GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/settings/server/q2_owner.h"

static int failures, checks;
#define CHECK(c)                                                 \
  do {                                                           \
    checks++;                                                    \
    if (!(c)) {                                                  \
      failures++;                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                            \
  } while (0)

static qa_cvar_registry *make(qa_command_dialect dialect) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  qa_register_q2_server_cvars(cvars, "q2:official");
  return cvars;
}
static void set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, false, &snapshot)) qa_cvar_read_free(&snapshot);
}
static double value(qa_cvar_registry *cvars, const char *name) { return qa_cvar_registry_variable_value(cvars, name); }
static void apply_latched(qa_cvar_registry *cvars) {
  size_t count = 0;
  qa_cvar_snapshot *applied = qa_cvar_registry_apply_latched(cvars, NULL, &count);
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&applied[i]);
  free(applied);
}

int main(void) {
  /* Q2 base player cvars drive the rule objects. */
  qa_cvar_registry *classic = make(QA_COMMAND_DIALECT_Q2_CLASSIC);
  qa_q2_base_player_rules rules = qa_q2_create_player_rules(NULL, 0);
  qa_q2_cvar_binding binding;
  qa_bind_q2_player_cvars(&binding, classic, &rules, NULL);
  qa_cvar_registry_set_server_active(classic, true);
  qa_cvar_read found;
  bool present = qa_cvar_registry_find(classic, "g_coop_player_collision", &found);
  if (present) qa_cvar_read_free(&found);
  CHECK(!present);
  CHECK(value(classic, "dmflags") == 0 && value(classic, "needpass") == 0);
  set(classic, "password", "test-game");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(strcmp(rules.password, "test-game") == 0);
  CHECK(value(classic, "needpass") == 1);
  set(classic, "spectator_password", "test-view");
  CHECK(value(classic, "needpass") == 3);
  set(classic, "password", "NoNe");
  CHECK(value(classic, "needpass") == 2);
  set(classic, "flood_msgs", "2");
  set(classic, "gun_x", "3"); set(classic, "gun_y", "-2");
  set(classic, "sv_maplist", "base1,base2 base3");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(rules.flood_messages == 2);
  CHECK(rules.gun_offset.x == 3 && rules.gun_offset.y == -2 && rules.gun_offset.z == 0);
  CHECK(rules.map_list_count == 3 && strcmp(rules.map_list[0], "base1") == 0 && strcmp(rules.map_list[2], "base3") == 0);
  set(classic, "cheats", "1");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(!rules.cheats);
  apply_latched(classic);
  qa_q2_cvar_binding_pull(&binding);
  CHECK(rules.cheats);
  rules.gun_offset = (qa_vec3){0, 1, 2};
  qa_q2_cvar_binding_push(&binding);
  CHECK(value(classic, "gun_z") == 2);
  /* The classic map list setter joins with spaces and reads back split. */
  CHECK(rules.map_list_count == 3);
  char *maps = qa_cvar_registry_variable_string(classic, "sv_maplist");
  CHECK(strcmp(maps, "base1 base2 base3") == 0);
  free(maps);
  qa_q2_rerelease_item_state items;
  CHECK(!qa_q2_rerelease_item_services(classic, &items));
  qa_q2_cvar_binding_destroy(&binding);
  /* The needpass hook is gone with the binding. */
  set(classic, "password", "x");
  CHECK(value(classic, "needpass") == 2);
  qa_cvar_registry_destroy(classic);

  /* Rerelease options keep source defaults and latch cooperative changes. */
  qa_cvar_registry *rerelease = make(QA_COMMAND_DIALECT_Q2_RERELEASE);
  qa_q2_base_player_rules rr_rules = qa_q2_create_player_rules(NULL, 0);
  qa_q2_rerelease_options options = qa_q2_create_rerelease_options(NULL);
  qa_bind_q2_player_cvars(&binding, rerelease, &rr_rules, &options);
  qa_cvar_registry_set_server_active(rerelease, true);
  CHECK(!options.coop_player_collision && options.deathmatch_spawn_farthest);
  set(rerelease, "g_coop_player_collision", "1");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(!options.coop_player_collision);
  set(rerelease, "g_dm_spawn_farthest", "0");
  set(rerelease, "g_coop_instanced_items", "0"); set(rerelease, "g_coop_squad_respawn", "0");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(!options.deathmatch_spawn_farthest && qa_q2_uses_instanced_items(options));
  apply_latched(rerelease);
  qa_q2_cvar_binding_pull(&binding);
  CHECK(options.coop_player_collision && !qa_q2_uses_instanced_items(options));
  set(rerelease, "g_dm_force_respawn_time", "3.5");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(options.deathmatch_force_respawn_time == 3.5);
  options.coop_num_lives = 7;
  qa_q2_cvar_binding_push(&binding);
  CHECK(value(rerelease, "g_coop_num_lives") == 7);
  /* Map shuffle is live in rerelease; the map list splits on spaces only. */
  CHECK(!rr_rules.map_list_shuffle);
  set(rerelease, "g_map_list_shuffle", "1");
  set(rerelease, "g_map_list", "q2dm1,q2dm2 q2dm3");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(rr_rules.map_list_shuffle && rr_rules.map_list_count == 2 && strcmp(rr_rules.map_list[0], "q2dm1,q2dm2") == 0);
  rr_rules.map_list_shuffle = false;
  qa_q2_cvar_binding_push(&binding);
  char *shuffle = qa_cvar_registry_variable_string(rerelease, "g_map_list_shuffle");
  CHECK(strcmp(shuffle, "0") == 0);
  free(shuffle);

  /* q2RereleaseItemServices folds dmflags bits and reads live cvars. */
  CHECK(qa_q2_rerelease_item_services(rerelease, &items));
  qa_q2mp_random_item_settings settings = qa_q2_rerelease_random_items(&items);
  CHECK(!settings.enabled && !settings.no_mines && !settings.no_nukes && !settings.no_spheres);
  set(rerelease, "g_dm_random_items", "1"); set(rerelease, "g_no_nukes", "1");
  set(rerelease, "dmflags", "131072");
  settings = qa_q2_rerelease_random_items(&items);
  CHECK(settings.enabled && settings.no_mines && settings.no_nukes && !settings.no_spheres);
  CHECK(qa_q2_rerelease_drop_quad_fire(&items));
  set(rerelease, "g_dm_no_quadfire_drop", "1");
  CHECK(!qa_q2_rerelease_drop_quad_fire(&items));
  qa_q2_cvar_binding_destroy(&binding);

  /* bindQ2ServerCvars: timelimit/fraglimit and CTF captureLimit, unforced. */
  double capture_limit = 0;
  qa_register_q2_server_cvars(rerelease, "q2:ctf");
  qa_bind_q2_server_cvars(&binding, rerelease, &rr_rules, &options, NULL, &capture_limit);
  set(rerelease, "timelimit", "20"); set(rerelease, "capturelimit", "8");
  qa_q2_cvar_binding_pull(&binding);
  CHECK(rr_rules.time_limit_minutes == 20 && capture_limit == 8);
  rr_rules.frag_limit = 15;
  capture_limit = 3;
  qa_q2_cvar_binding_push(&binding);
  CHECK(value(rerelease, "fraglimit") == 15 && value(rerelease, "capturelimit") == 3);
  qa_q2_cvar_binding_destroy(&binding);
  qa_cvar_registry_destroy(rerelease);

  printf("test_settings_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

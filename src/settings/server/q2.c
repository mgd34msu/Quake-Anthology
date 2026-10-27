/* Port of src/settings/server/q2.ts. GPL-2.0-or-later. */
#include "qa/settings/server/q2.h"

#include <stdbool.h>

#include "qa/settings/server/common.h"

#define VALUE(target_name) \
  ((qa_server_setting_target){.is_bit = false, .name = (target_name)})
#define BIT(target_name, mask_value, inverted_value)                  \
  ((qa_server_setting_target){.is_bit = true,                         \
                              .name = (target_name),                  \
                              .mask = (mask_value),                   \
                              .inverted = (inverted_value)})

static qa_server_setting_definition limit_definitions[2];
static qa_server_setting_definition capture_definitions[1];
static qa_server_setting_definition combat_definitions[1];
static qa_server_setting_definition deathball_definitions[1];
static qa_server_setting_definition rerelease_definitions[20];
static qa_server_setting_definition spawn_definitions[3];
static bool built = false;

static void build(void) {
  if (built) return;
  built = true;
  /* createQ2PlayerRules() defaults: timeLimitMinutes 0, fragLimit 0. */
  qa_server_limit(&limit_definitions[0], 
      "server:time-limit", "timelimit", "Time limit (minutes)", 0,
      "End the level after this many minutes. Zero disables the limit.",
      QA_SERVER_APPLY_LIVE, false);
  qa_server_limit(&limit_definitions[1], 
      "server:frag-limit", "fraglimit", "Frag limit", 0,
      "End the level when a player reaches this score. Zero disables the limit.",
      QA_SERVER_APPLY_LIVE, true);
  /* createQ2CtfRules() default: captureLimit 0. */
  qa_server_limit(&capture_definitions[0], 
      "server:capture-limit", "capturelimit", "Capture limit", 0,
      "End the level when a team reaches this capture count. Zero disables the "
      "limit.",
      QA_SERVER_APPLY_LIVE, true);
  combat_definitions[0] = qa_server_friendly_fire(BIT("dmflags", 256, true), true);
  qa_server_limit(&deathball_definitions[0], 
      "server:goal-limit", "goallimit", "Goal limit", 0,
      "End DeathBall when a team reaches this goal score. Zero disables the "
      "limit.",
      QA_SERVER_APPLY_LIVE, true);
  size_t i = 0;
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.random-items", VALUE("g_dm_random_items"),
      "Random item respawns", false,
      "Replace respawning pickups using the source item categories.",
      QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.no-quadfire-drop", VALUE("g_dm_no_quadfire_drop"),
      "Prevent DualFire drop", false,
      "Do not drop active DualFire Damage on death.", QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.instant-switch", VALUE("g_instant_weapon_switch"),
      "Instant weapon switching", false,
      "Skip weapon lowering and raising animations.", QA_SERVER_APPLY_NEXT_MAP);
  qa_server_limit(&rerelease_definitions[i++], 
      "server:q2.weapon-respawn", "g_weapon_respawn_time",
      "Weapon respawn seconds", 30,
      "Delay before a collected deathmatch weapon returns.",
      QA_SERVER_APPLY_LIVE, false);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.weapons-stay", VALUE("g_dm_weapons_stay"), "Weapons stay",
      false, "Leave map weapons for other players.", QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.instant-items", VALUE("g_dm_instant_items"),
      "Instant powerups", true, "Activate powerups on pickup.",
      QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.same-level", VALUE("g_dm_same_level"), "Repeat current level",
      false, "Restart the same map after the match.", QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.no-quad-drop", VALUE("g_dm_no_quad_drop"),
      "Prevent Quad drop", false, "Do not drop active Quad Damage on death.",
      QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.no-stack-double", VALUE("g_dm_no_stack_double"),
      "Disable stacked Double Damage", false,
      "Do not stack Double Damage with other damage multipliers.",
      QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.strong-mines", VALUE("g_dm_strong_mines"),
      "Strong proximity mines", false, "Use the source strong mine damage policy.",
      QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.squad-respawn", VALUE("g_coop_squad_respawn"),
      "Respawn near teammates", true, "Use the source safe teammate respawn checks.",
      QA_SERVER_APPLY_NEXT_MAP);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.instanced-items", VALUE("g_coop_instanced_items"),
      "Individual cooperative pickups", true,
      "Each cooperative player can collect their own copy of an eligible pickup.",
      QA_SERVER_APPLY_NEXT_MAP);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.coop-lives", VALUE("g_coop_enable_lives"),
      "Limited cooperative lives", false,
      "Use source cooperative lives and all-dead restart rules.",
      QA_SERVER_APPLY_NEXT_MAP);
  qa_server_limit(&rerelease_definitions[i++], 
      "server:q2.coop-num-lives", "g_coop_num_lives", "Extra cooperative lives",
      2, "Additional lives per cooperative player.", QA_SERVER_APPLY_NEXT_MAP,
      true);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.player-collision", VALUE("g_coop_player_collision"),
      "Cooperative player collision", false,
      "Allow cooperative players to block one another.", QA_SERVER_APPLY_NEXT_MAP);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.force-respawn", VALUE("g_dm_force_respawn"),
      "Force deathmatch respawn", false,
      "Respawn dead players using the source respawn delay.",
      QA_SERVER_APPLY_LIVE);
  qa_server_limit(&rerelease_definitions[i++], 
      "server:q2.respawn-time", "g_dm_force_respawn_time",
      "Forced respawn delay", 0,
      "Seconds before forced respawn. Zero uses source timing.",
      QA_SERVER_APPLY_LIVE, false);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.no-fall-damage", VALUE("g_dm_no_fall_damage"),
      "Disable deathmatch fall damage", false,
      "Suppress falling damage in deathmatch.", QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.farthest-spawn", VALUE("g_dm_spawn_farthest"),
      "Farthest deathmatch spawn", true,
      "Prefer the spawn farthest from living players.", QA_SERVER_APPLY_LIVE);
  rerelease_definitions[i++] = qa_server_toggle(
      "server:q2.allow-exit", VALUE("g_dm_allow_exit"),
      "Allow deathmatch exits", false,
      "Allow players to activate authored level exits.", QA_SERVER_APPLY_LIVE);
  spawn_definitions[0] = qa_server_toggle(
      "server:q2.no-health", BIT("dmflags", 1, false), "Exclude health items",
      false, "Remove health pickups when the next map is spawned.",
      QA_SERVER_APPLY_NEXT_MAP);
  spawn_definitions[1] = qa_server_toggle(
      "server:q2.no-powerups", BIT("dmflags", 2, false), "Exclude powerups",
      false, "Remove powerup pickups when the next map is spawned.",
      QA_SERVER_APPLY_NEXT_MAP);
  spawn_definitions[2] = qa_server_toggle(
      "server:q2.no-armor", BIT("dmflags", 2048, false), "Exclude armor items",
      false, "Remove armor pickups when the next map is spawned.",
      QA_SERVER_APPLY_NEXT_MAP);
}

static qa_server_setting_collection collection(
    const char *id, const qa_server_setting_definition *definitions, size_t count) {
  build();
  qa_server_setting_collection result;
  result.id = id;
  result.definitions = definitions;
  result.definition_count = count;
  return result;
}

qa_server_setting_collection qa_q2_limit_settings(void) {
  return collection("q2:limits", limit_definitions, 2);
}
qa_server_setting_collection qa_q2_ctf_capture_settings(void) {
  return collection("q2:ctf-captures", capture_definitions, 1);
}
qa_server_setting_collection qa_q2_combat_settings(void) {
  return collection("q2:combat", combat_definitions, 1);
}
qa_server_setting_collection qa_q2_death_ball_settings(void) {
  return collection("q2:deathball", deathball_definitions, 1);
}
qa_server_setting_collection qa_q2_rerelease_settings(void) {
  return collection("q2:rerelease", rerelease_definitions, 20);
}
qa_server_setting_collection qa_q2_spawn_settings(void) {
  return collection("q2:item-spawn", spawn_definitions, 3);
}

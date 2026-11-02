/*
 * Port of key cases from tests/gameplay/q1-missionpacks/arsenal.test.ts
 * and world/world.test.ts (pure logic reachable without retail PAKs:
 * weapon tables/ranks/selection, travel, obituaries, runes, finale
 * text, velocity/angle math). TS toThrow cases run in a child process
 * and must abort. GPL-2.0-or-later.
 */
#include <math.h>
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

#include "qa/content/q1/missionpacks/items.h"
#include "qa/content/q1/missionpacks/messages.h"
#include "qa/content/q1/missionpacks/obituaries.h"
#include "qa/content/q1/missionpacks/pickup_rules.h"
#include "qa/content/q1/missionpacks/selection.h"
#include "qa/content/q1/missionpacks/travel.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/content/q1/missionpacks/world/rotate_targets.h"
#include "qa/content/q1/missionpacks/world/rogue_runes.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { \
  checks++; pid_t pid = fork(); \
  if (pid == 0) { (void)prctl(PR_SET_DUMPABLE, 0); freopen("/dev/null", "w", stderr); __VA_ARGS__; _exit(0); } \
  int status = 0; waitpid(pid, &status, 0); \
  if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } \
} while (0)
static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

/* --- ext stubs (TS owners: foundation/weapons.ts aim,
 * base/rules.ts q1Obituary, base/travel.ts travel) --- */
qa_vec3 qa_q1mp_ext_aim(qa_q1mp_game *game, qa_owned_actor actor, qa_vec3 forward) { (void)game; (void)actor; return forward; }
/* qa_q1mp_ext_q1_obituary / *_q1_travel are defined for real in
 * src/content/q1/missionpacks/{obituaries,travel}.c (Wave 8b); the
 * admit path is observed through the host stubs below. */
static size_t created_count;
static char created_items[40][96];
static double created_health;
static void stub_set_health(void *ctx, qa_owned_actor actor, double health) { (void)ctx; (void)actor; created_health = health; }
static void stub_set_armor(void *ctx, qa_owned_actor actor, qa_armor_state armor) { (void)ctx; (void)actor; (void)armor; }
static bool stub_inventory_has(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return false; }
static void stub_inventory_create(void *ctx, qa_owned_actor actor, const qa_inventory_entry *entries, size_t count) {
  (void)ctx; (void)actor; created_count = count;
  for (size_t i = 0; i < count && i < 40; i++) (void)snprintf(created_items[i], 96, "%s", entries[i].item);
}
static void stub_inventory_configure(void *ctx, qa_owned_actor actor, const qa_inventory_entry *entry) { (void)ctx; (void)actor; (void)entry; }
static void stub_powerup(void *ctx, qa_owned_actor actor, const char *powerup, double expires) { (void)ctx; (void)actor; (void)powerup; (void)expires; }

/* --- minimal game/host stub --- */
static struct { const char *item; double count; } inventory[64];
static size_t inventory_count_entries;
static double stub_inventory_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  (void)ctx; (void)actor;
  for (size_t i = 0; i < inventory_count_entries; i++) if (streq(inventory[i].item, item)) return inventory[i].count;
  return 0;
}
static void set_item(const char *item, double count) {
  for (size_t i = 0; i < inventory_count_entries; i++) if (streq(inventory[i].item, item)) { inventory[i].count = count; return; }
  inventory[inventory_count_entries].item = item; inventory[inventory_count_entries].count = count; inventory_count_entries++;
}
static bool stub_is_live(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return true; }
static bool stub_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  (void)ctx; (void)actor;
  if (actor.slot == 999) return false;
  out->id = actor; return true;
}
static const char *last_message;
static void stub_message(qa_q1b_game *g, qa_q1b_opt_actor p, const char *text, bool center) { (void)g; (void)p; (void)center; last_message = text; }
static const char *selected_weapon;
static bool stub_select(qa_q1b_game *g, qa_owned_actor a, const char *w) { (void)g; (void)a; selected_weapon = w; return true; }
static qa_item_id stub_weapon_ammo(qa_q1b_game *g, const char *w) {
  (void)g;
  if (streq(w, "axe") || streq(w, "hipnotic:mjolnir") || streq(w, "rogue:grapple")) return NULL;
  if (streq(w, "shotgun") || streq(w, "supershotgun")) return "q1:ammo/shells";
  if (streq(w, "nailgun") || streq(w, "supernailgun")) return "q1:ammo/nails";
  if (streq(w, "grenadelauncher") || streq(w, "rocketlauncher") || streq(w, "hipnotic:proximity")) return "q1:ammo/rockets";
  if (streq(w, "lightning") || streq(w, "hipnotic:laser")) return "q1:ammo/cells";
  if (streq(w, "rogue:lava-nailgun") || streq(w, "rogue:lava-supernailgun")) return "rogue:ammo/lava-nails";
  if (streq(w, "rogue:multi-grenade") || streq(w, "rogue:multi-rocket")) return "rogue:ammo/multi-rockets";
  if (streq(w, "rogue:plasma")) return "rogue:ammo/plasma";
  return NULL;
}
static void stub_register_named(void *ctx, const char *name, const qa_q1b_handlers *h) { (void)ctx; (void)name; (void)h; }
static void stub_sound_owned(qa_q1b_game *g, qa_owned_actor a, const char *path, qa_q1b_channel ch, double att, double vol) { (void)g; (void)a; (void)path; (void)ch; (void)att; (void)vol; }
static double rolls[8]; static size_t roll_index;
static double stub_roll(void *ctx) { (void)ctx; return rolls[roll_index++]; }

static qa_q1b_host host;
static qa_q1b_game base_game;
static qa_q1mp_game game;
static qa_q1b_player_state player;
static qa_q1b_player_state *stub_player(qa_q1b_game *g, qa_actor_id actor) { (void)g; (void)actor; return &player; }
static const qa_q1b_player_extension *stub_no_extension(qa_q1b_game *g, const char *id) { (void)g; (void)id; return NULL; }
static void setup_game(const char *edition, double deathmatch) {
  memset(&host, 0, sizeof host); memset(&base_game, 0, sizeof base_game); memset(&game, 0, sizeof game);
  memset(&player, 0, sizeof player); inventory_count_entries = 0; last_message = NULL; selected_weapon = NULL;
  host.inventory_count = stub_inventory_count;
  host.combat_set_health = stub_set_health;
  host.combat_set_armor = stub_set_armor;
  host.inventory_has = stub_inventory_has;
  host.inventory_create = stub_inventory_create;
  host.inventory_configure = stub_inventory_configure;
  host.powerup = stub_powerup;
  created_count = 0; created_health = -1;
  host.actors_is_live = stub_is_live;
  host.actors_resolve_owned = stub_resolve_owned;
  base_game.host = &host;
  base_game.options.edition = edition; base_game.options.deathmatch = deathmatch;
  base_game.message = stub_message; base_game.select_weapon = stub_select; base_game.weapon_ammo = stub_weapon_ammo;
  base_game.player = stub_player; base_game.player_extension_get = stub_no_extension;
  base_game.named.register_named = stub_register_named;
  base_game.sound_owned = stub_sound_owned;
  base_game.time = 10;
  (void)snprintf(base_game.map_name, sizeof base_game.map_name, "%s", "e1m1");
  game.base = &base_game;
  player.actor.id.slot = 1; player.weapon = "shotgun"; player.auto_switch = "always";
}

static void test_tables_and_math(void) {
  CHECK(QA_Q1MP_MISSION_WEAPONS_COUNT == 8);
  CHECK(streq(qa_q1mp_mission_weapons[0].id, "hipnotic:laser"));
  CHECK(streq(qa_q1mp_mission_weapons[0].model, "progs/v_laserg.mdl"));
  CHECK(streq(qa_q1mp_mission_weapons[0].ammo, "q1:ammo/cells"));
  CHECK(qa_q1mp_mission_weapons[0].pickup_ammo == 30 && qa_q1mp_mission_weapons[0].rank == 3);
  CHECK(qa_q1mp_mission_weapons[1].ammo == NULL && qa_q1mp_mission_weapons[1].rank == 9);
  CHECK(streq(qa_q1mp_mission_weapons[7].id, "rogue:plasma") && qa_q1mp_mission_weapons[7].rank == 0);
  qa_vec3 a = qa_q1mp_velocity_angles((qa_vec3){0, 0, 1}); CHECK(a.x == 90 && a.y == 0);
  a = qa_q1mp_velocity_angles((qa_vec3){0, 0, -1}); CHECK(a.x == 270 && a.y == 0);
  a = qa_q1mp_velocity_angles((qa_vec3){1, 0, 0}); CHECK(a.x == 0 && a.y == 0);
  a = qa_q1mp_velocity_angles((qa_vec3){0, 1, 0}); CHECK(a.x == 0 && a.y == 90);
  a = qa_q1mp_velocity_angles((qa_vec3){-1, 0, 0}); CHECK(a.y == 180);
  a = qa_q1mp_normalize_angles((qa_vec3){370, -10, 765}); CHECK(a.x == 10 && a.y == 350 && a.z == 45);
  CHECK(qa_q1mp_hipnotic_weapon_rank("lightning") == 1);
  CHECK(qa_q1mp_hipnotic_weapon_rank("hipnotic:laser") == 3);
  CHECK(qa_q1mp_hipnotic_weapon_rank("hipnotic:mjolnir") == 9);
  CHECK(qa_q1mp_hipnotic_weapon_rank("axe") == 10);
  CHECK(qa_q1mp_rogue_weapon_rank("rogue:plasma") == 1);
  CHECK(qa_q1mp_rogue_weapon_rank("nailgun") == 11);
  CHECK(qa_q1mp_rogue_weapon_rank("axe") == 12);
  CHECK(streq(qa_q1mp_mission_classic("$qc_mjolnir"), "Mjolnir"));
  CHECK(streq(qa_q1mp_mission_classic("$qc_no_weapon"), "no weapon.\n"));
  CHECK(qa_q1mp_mission_classic("$qc_missing") == NULL);
  const char *finale = qa_q1mp_mission_finale_text("classic", "$qc_finale_coop");
  CHECK(streq(finale, "You have destroyed Quake's\nTemporal Teleporter. His assault\non Time has been defeated."));
  CHECK(streq(qa_q1mp_mission_finale_text("rerelease", "$qc_finale_coop"), "$qc_finale_coop"));
  CHECK(streq(qa_q1mp_mission_finale_text("classic", "$qc_unknown"), "$qc_unknown"));
}

static void give_weapon(const char *weapon, const char *ammo, double ammo_count) {
  char buf[96]; qa_q1b_weapon_item(weapon, buf, sizeof buf);
  /* qa_q1b_weapon_item writes into buf; copy to stable storage */
  static char storage[32][96]; static size_t used;
  (void)snprintf(storage[used], 96, "%s", buf); set_item(storage[used], 1); used++;
  if (ammo) set_item(ammo, ammo_count);
}
static void test_selection(void) {
  setup_game("classic", 0);
  give_weapon("shotgun", "q1:ammo/shells", 10);
  give_weapon("hipnotic:laser", "q1:ammo/cells", 30);
  CHECK(qa_q1mp_mission_weapon_impulse(&game, &player, "hipnotic", 225));
  CHECK(streq(selected_weapon, "hipnotic:laser"));
  /* unowned weapon: no-weapon message */
  selected_weapon = NULL;
  CHECK(qa_q1mp_mission_weapon_impulse(&game, &player, "hipnotic", 226));
  CHECK(selected_weapon == NULL);
  CHECK(streq(last_message, "no weapon.\n"));
  /* unknown impulse unhandled */
  CHECK(!qa_q1mp_mission_weapon_impulse(&game, &player, "hipnotic", 99));
  /* cycle forward from shotgun to laser (only owned with ammo) */
  player.weapon = "shotgun"; selected_weapon = NULL;
  CHECK(qa_q1mp_mission_weapon_impulse(&game, &player, "hipnotic", 10));
  CHECK(streq(selected_weapon, "hipnotic:laser"));
  /* rogue paired selection: impulse 4 with lava owned and nailgun current */
  setup_game("classic", 0);
  give_weapon("nailgun", "q1:ammo/nails", 10);
  give_weapon("rogue:lava-nailgun", "rogue:ammo/lava-nails", 10);
  player.weapon = "nailgun"; selected_weapon = NULL;
  CHECK(qa_q1mp_mission_weapon_impulse(&game, &player, "rogue", 4));
  CHECK(streq(selected_weapon, "rogue:lava-nailgun"));
  /* not enough ammo: supernailgun needs 2 nails */
  setup_game("classic", 0);
  give_weapon("supernailgun", "q1:ammo/nails", 1);
  player.weapon = "shotgun"; selected_weapon = NULL; last_message = NULL;
  CHECK(qa_q1mp_mission_weapon_impulse(&game, &player, "hipnotic", 5));
  CHECK(selected_weapon == NULL);
  CHECK(streq(last_message, "not enough ammo.\n"));
}

static void test_travel(void) {
  setup_game("classic", 0);
  qa_q1mp_travel_state hip = qa_q1mp_new_mission_pack_travel(&game, "hipnotic");
  CHECK(hip.inventory_count == 14 + 3); /* real newQ1Travel: 8 weapons + 4 ammo + 2 keys */
  CHECK(streq(hip.weapon, "shotgun") && hip.health == 100);
  qa_q1mp_travel_state rog = qa_q1mp_new_mission_pack_travel(&game, "rogue");
  CHECK(rog.inventory_count == 14 + 5 + 5);
  /* decode: hipnotic start map resets */
  (void)snprintf(base_game.map_name, sizeof base_game.map_name, "%s", "hip1m1");
  rog.weapon = "rocketlauncher";
  qa_q1mp_travel_state decoded = qa_q1mp_decode_mission_pack_travel(&game, rog, 0, "hipnotic");
  CHECK(streq(decoded.weapon, "shotgun"));
  /* decode: rogue grapple becomes axe below CTF teamplay */
  (void)snprintf(base_game.map_name, sizeof base_game.map_name, "%s", "r1m1");
  rog.weapon = "rogue:grapple";
  decoded = qa_q1mp_decode_mission_pack_travel(&game, rog, 0, "rogue");
  CHECK(streq(decoded.weapon, "axe"));
  /* admit merges missing pack inventory ahead of supplied entries */
  qa_q1mp_travel_state supplied = qa_q1mp_ext_new_q1_travel(&base_game.options);
  qa_owned_actor actor = {0}; actor.id.slot = 1;
  qa_q1mp_admit_mission_pack_travel(&game, actor, supplied, "hipnotic");
  CHECK(created_count == 3 + supplied.inventory_count);
  CHECK(created_health == 100);
  CHECK(streq(created_items[0], "q1:weapon/hipnotic:laser") || strstr(created_items[0], "hipnotic") != NULL);
}

static qa_q1mp_obituary_person person(int slot, const char *name, const char *classname, const char *weapon, bool is_player, bool is_monster) {
  qa_q1mp_obituary_person p = {0};
  p.actor.slot = slot; p.has_actor = true; p.name = name; p.classname = classname; p.weapon = weapon;
  p.is_player = is_player; p.is_monster = is_monster; p.water_type = "empty"; return p;
}
static void test_obituaries(void) {
  qa_q1mp_obituary_input input = {0};
  input.victim = person(1, "Vic", "player", "shotgun", true, false);
  input.has_attacker = true;
  input.attacker = person(2, "Att", "player", "rogue:grapple", true, false);
  input.edition = "classic"; input.death_type = ""; input.random = stub_roll; input.random_ctx = NULL;
  qa_q1mp_obituary_context ctx = {0}; ctx.pack = "rogue"; ctx.inflictor_classname = ""; ctx.attacker_death_type = "";
  rolls[0] = 0.25; roll_index = 0;
  qa_q1mp_obituary out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "Vic was grappled by Att\n"));
  CHECK(out.has_score && out.score_actor.slot == 2 && out.score_delta == 1);
  /* hipnotic suicide, classic, firstRoll > 0.4 -> bored via fallback table? suicide_bored is base classic text */
  input.attacker = person(1, "Vic", "player", "shotgun", true, false);
  ctx.pack = "hipnotic"; rolls[0] = 0.1; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_score && out.score_delta == -1 && out.score_actor.slot == 1);
  CHECK(out.has_message && streq(out.text, "Vic checks if his weapon is loaded\n"));
  /* rogue dragon monster */
  input.attacker = person(3, "", "monster_dragon", "", false, true);
  input.victim.weapon = "shotgun"; ctx.pack = "rogue"; rolls[0] = 0.5; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "Vic was annihilated by the Dragon\n"));
  /* vengeance: message without score */
  input.attacker = person(4, "", "Vengeance", "", false, false);
  rolls[0] = 0.5; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "Vic was purged by the Vengeance Sphere\n"));
  CHECK(!out.has_score);
  /* rerelease keeps key + args */
  input.edition = "rerelease";
  input.attacker = person(2, "Att", "player", "rogue:plasma", true, false);
  rolls[0] = 0.5; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "$qc_death_fused") && out.arg_count == 2);
}

static void test_runes(void) {
  setup_game("classic", 1);
  static qa_q1mp_rogue_runes runes;
  qa_q1mp_rogue_runes_init(&runes, &game, NULL, NULL);
  qa_actor_id actor = {0}; actor.slot = 7;
  CHECK(qa_q1mp_runes_damage(&runes, actor, 10) == 10);
  CHECK(qa_q1mp_runes_resistance(&runes, actor, 10) == 10);
  CHECK(qa_q1mp_runes_attack_delay(&runes, actor, 0.9) == 0.9);
  CHECK(!qa_q1mp_runes_has_regeneration(&runes, actor));
  /* strength+haste+regen runes set directly on the state table */
  runes.players[0].rune = 2 | 4 | 8;
  CHECK(qa_q1mp_runes_damage(&runes, actor, 10) == 20);
  CHECK(fabs(qa_q1mp_runes_attack_delay(&runes, actor, 0.9) - (double)(float)((double)(float)(0.9 * 2) / 3)) < 1e-12);
  CHECK(qa_q1mp_runes_has_regeneration(&runes, actor));
  runes.players[0].rune = 1;
  CHECK(qa_q1mp_runes_resistance(&runes, actor, 10) == 5);
  /* dead actor aborts with the TS Error (qa_range_error) */
  qa_actor_id dead = {0}; dead.slot = 999;
  CHECK_ABORTS((void)qa_q1mp_runes_damage(&runes, dead, 1));
  free(runes.players);
}

int main(void) {
  test_tables_and_math();
  test_selection();
  test_travel();
  test_obituaries();
  test_runes();
  if (failures != 0) { fprintf(stderr, "%d/%d checks failed\n", failures, checks); return 1; }
  printf("test_q1_missionpacks: %d checks passed\n", checks);
  return 0;
}

/*
 * Port of tests/gameplay/q1-missionpacks/arsenal.test.ts and
 * world/world.test.ts cases over the one Q1 game (tests/content/
 * q1_world_fixture.h: real registry, bodies, callbacks, authority and
 * inventory; open-world trace). The TS cases that load retail start.bsp only
 * for spawn points run here over the open world; their expected values were
 * reproduced over the same open world by the TS oracle under bun (see the
 * report of the Q1 unification). TS toThrow cases run in a child process
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

#include "q1_world_fixture.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/travel.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/q1/missionpacks/arsenal.h"
#include "qa/content/q1/missionpacks/backpacks.h"
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"
#include "qa/content/q1/missionpacks/items.h"
#include "qa/content/q1/missionpacks/messages.h"
#include "qa/content/q1/missionpacks/obituaries.h"
#include "qa/content/q1/missionpacks/pickup_rules.h"
#include "qa/content/q1/missionpacks/selection.h"
#include "qa/content/q1/missionpacks/travel.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/content/q1/missionpacks/world/rogue_runes.h"
#include "qa/content/q1/missionpacks/world/rotate_targets.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { \
  checks++; fflush(NULL); pid_t pid = fork(); \
  if (pid == 0) { (void)prctl(PR_SET_DUMPABLE, 0); if (freopen("/dev/null", "w", stderr) == NULL) _exit(2); __VA_ARGS__; _exit(0); } \
  int status = 0; waitpid(pid, &status, 0); \
  if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } \
} while (0)
static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

/* --- session: the TS arsenal.test.ts session() over the open-world fixture --- */
typedef struct session {
  q1_world_fixture fx;
  qa_q1_services *game;
  qa_q1_base *base;
  qa_q1mp_mission_pack_arsenal arsenal;
  bool has_arsenal;
  double gravity_scale[16]; /* host.setGravity record, by fixture player index */
} session;
static session *gravity_session;
static void record_gravity(void *ctx, qa_actor_id actor, double scale) {
  q1_world_fixture *fx = q1_world_fixture_of(ctx);
  for (size_t i = 0; i < fx->player_count; i++)
    if (qa_same_actor(fx->players[i], actor)) gravity_session->gravity_scale[i] = scale;
}
/* TS session registers the policy with sourceEffects: game.damageSourceEffects.
 * The fixture's policy is the first registration on its fresh authority. */
static void use_source_effects(q1_world_fixture *fx) {
  qa_gameplay_authority_unregister(fx->combat, 1);
  free(fx->policy_ctx); free(fx->armor_ctx);
  qa_q1_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = "q1:combat"; policy.ctx = fx; policy.context = q1_wf_combat_context;
  policy.armor = qa_native_victim_armor(q1_wf_armor_context, fx);
  fx->armor_ctx = policy.armor.ctx;
  policy.has_source_effects = true; policy.source_effects = qa_q1_damage_source_effects_of(fx->game);
  const qa_combat_policy registered = qa_create_q1_combat_policy(policy);
  fx->policy_ctx = registered.ctx;
  (void)qa_gameplay_authority_register(fx->combat, registered);
}
static void open_session(session *s, const char *pack, const char *edition, double deathmatch, double teamplay) {
  memset(s, 0, sizeof *s);
  qa_q1_options options = q1_world_fixture_options();
  options.edition = edition; options.deathmatch = deathmatch;
  options.has_teamplay = true; options.teamplay = teamplay;
  CHECK(q1_world_fixture_create(&s->fx, options));
  s->game = s->fx.game;
  gravity_session = s;
  s->game->host.set_gravity = record_gravity;
  use_source_effects(&s->fx);
  s->base = qa_q1_register_base(s->game, NULL);
  if (pack != NULL) { qa_q1mp_mission_pack_arsenal_init(&s->arsenal, s->game, pack); s->has_arsenal = true; }
}
static qa_q1_player_state *add_player(session *s) {
  const double x = 64 * (double)s->fx.player_count;
  const qa_owned_actor owner = q1_world_fixture_admit_player(&s->fx, (qa_vec3){x, 0, 0}, 100);
  return qa_q1_attach_player(s->game, owner, "shotgun", true, NULL);
}
static void close_session(session *s) {
  CHECK(s->fx.ctx_errors == 0);
  if (s->has_arsenal) qa_q1mp_mission_pack_arsenal_destroy(&s->arsenal);
  qa_q1_base_destroy(s->base);
  q1_world_fixture_destroy(&s->fx);
}
static void configure(session *s, qa_q1_player_state *player, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item; entry.count = count; entry.capacity = capacity;
  qa_shared_inventory_table_configure(s->fx.inventory, player->actor, entry, NULL, NULL);
}
static double count_of(session *s, qa_actor_id actor, qa_item_id item) { return qa_shared_inventory_table_count(s->fx.inventory, actor, item); }
static const char *last_message(session *s) {
  for (size_t i = s->fx.event_count; i > 0; i--) if (streq(s->fx.events[i - 1].kind, "message")) return s->fx.events[i - 1].text;
  return NULL;
}
static void touch(session *s, qa_q1_actor *self, qa_actor_id other) {
  qa_touch_contact contact;
  memset(&contact, 0, sizeof contact);
  contact.self = self->actor; contact.other = other;
  (void)qa_actor_callback_table_touch(s->fx.callbacks, contact);
}
static qa_q1_actor *find_entity(session *s, const char *classname, const qa_actor_id *owner) {
  for (size_t i = 0; i < s->game->entity_count; i++) {
    qa_q1_actor *entity = s->game->entities[i];
    if (streq(entity->classname, classname) && (owner == NULL || (entity->has_owner && qa_same_actor(entity->owner, *owner)))) return entity;
  }
  return NULL;
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
  CHECK(streq(qa_q1mp_backpack_base_weapon("rogue:plasma"), "lightning"));
  CHECK(streq(qa_q1mp_backpack_base_weapon("axe"), "axe"));
  CHECK(streq(qa_q1mp_mission_classic("$qc_mjolnir"), "Mjolnir"));
  CHECK(streq(qa_q1mp_mission_classic("$qc_no_weapon"), "no weapon.\n"));
  CHECK(qa_q1mp_mission_classic("$qc_missing") == NULL);
  const char *finale = qa_q1mp_mission_finale_text("classic", "$qc_finale_coop");
  CHECK(streq(finale, "You have destroyed Quake's\nTemporal Teleporter. His assault\non Time has been defeated."));
  CHECK(streq(qa_q1mp_mission_finale_text("rerelease", "$qc_finale_coop"), "$qc_finale_coop"));
  CHECK(streq(qa_q1mp_mission_finale_text("classic", "$qc_unknown"), "$qc_unknown"));
}

static void test_selection(void) {
  session s;
  open_session(&s, NULL, "classic", 0, 0);
  qa_q1mp_register_hipnotic_weapons(s.game);
  qa_q1_player_state *player = add_player(&s);
  configure(&s, player, qa_q1_weapon_item("hipnotic:laser"), 1, 1);
  configure(&s, player, "q1:ammo/cells", 30, 100);
  configure(&s, player, "q1:ammo/shells", 10, 100);
  CHECK(qa_q1mp_mission_weapon_impulse(s.game, player, "hipnotic", 225));
  CHECK(streq(player->weapon, "hipnotic:laser"));
  /* unowned weapon: no-weapon message, selection unchanged */
  qa_q1_select_weapon(s.game, player->actor, "shotgun");
  CHECK(qa_q1mp_mission_weapon_impulse(s.game, player, "hipnotic", 226));
  CHECK(streq(player->weapon, "shotgun"));
  CHECK(streq(last_message(&s), "no weapon.\n"));
  /* unknown impulse unhandled */
  CHECK(!qa_q1mp_mission_weapon_impulse(s.game, player, "hipnotic", 99));
  /* cycle forward from shotgun to laser (only owned with ammo) */
  CHECK(qa_q1mp_mission_weapon_impulse(s.game, player, "hipnotic", 10));
  CHECK(streq(player->weapon, "hipnotic:laser"));
  close_session(&s);
  /* rogue paired selection: impulse 4 with lava owned and nailgun current */
  open_session(&s, "rogue", "classic", 0, 0);
  player = add_player(&s);
  configure(&s, player, qa_q1_weapon_item("nailgun"), 1, 1); configure(&s, player, "q1:ammo/nails", 10, 200);
  configure(&s, player, qa_q1_weapon_item("rogue:lava-nailgun"), 1, 1); configure(&s, player, "rogue:ammo/lava-nails", 10, 200);
  CHECK(qa_q1_select_weapon(s.game, player->actor, "nailgun"));
  CHECK(qa_q1mp_mission_weapon_impulse(s.game, player, "rogue", 4));
  CHECK(streq(player->weapon, "rogue:lava-nailgun"));
  CHECK(streq(last_message(&s), "lava nails"));
  close_session(&s);
  /* not enough ammo: supernailgun needs 2 nails */
  open_session(&s, NULL, "classic", 0, 0);
  qa_q1mp_register_hipnotic_weapons(s.game);
  player = add_player(&s);
  configure(&s, player, qa_q1_weapon_item("supernailgun"), 1, 1); configure(&s, player, "q1:ammo/nails", 1, 200);
  CHECK(qa_q1mp_mission_weapon_impulse(s.game, player, "hipnotic", 5));
  CHECK(streq(player->weapon, "shotgun"));
  CHECK(streq(last_message(&s), "not enough ammo.\n"));
  close_session(&s);
}

static void set_map(qa_q1_services *game, const char *map) { /* TS game.mapName = map */
  char *copy = malloc(strlen(map) + 1);
  if (copy == NULL) abort();
  strcpy(copy, map);
  free(game->map_name);
  game->map_name = copy;
}
static void test_travel(void) {
  session s;
  open_session(&s, NULL, "classic", 0, 0);
  qa_q1_travel_state hip = qa_q1mp_new_mission_pack_travel(s.game, "hipnotic");
  CHECK(hip.inventory_count == 14 + 3); /* real newQ1Travel: 8 weapons + 4 ammo + 2 keys */
  CHECK(streq(hip.weapon, "shotgun") && hip.health == 100);
  qa_q1_travel_state rog = qa_q1mp_new_mission_pack_travel(s.game, "rogue");
  CHECK(rog.inventory_count == 14 + 5 + 5);
  /* decode: hipnotic start map resets */
  set_map(s.game, "hip1m1");
  rog.weapon = "rocketlauncher";
  qa_q1_travel_state decoded = qa_q1mp_decode_mission_pack_travel(s.game, &rog, 0, "hipnotic");
  CHECK(streq(decoded.weapon, "shotgun"));
  qa_q1_travel_state_destroy(&decoded);
  /* decode: rogue grapple becomes axe below CTF teamplay */
  set_map(s.game, "r1m1");
  rog.weapon = "rogue:grapple";
  decoded = qa_q1mp_decode_mission_pack_travel(s.game, &rog, 0, "rogue");
  CHECK(streq(decoded.weapon, "axe"));
  qa_q1_travel_state_destroy(&decoded);
  /* admit merges missing pack inventory ahead of supplied entries */
  qa_q1_travel_state supplied = qa_q1_new_travel(&s.game->options);
  const qa_owned_actor actor = q1_world_fixture_admit_player(&s.fx, (qa_vec3){0, 0, 0}, 1);
  qa_q1mp_admit_mission_pack_travel(s.game, actor, &supplied, "hipnotic");
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(s.fx.inventory, actor.id, &entry_count);
  CHECK(entry_count == 3 + supplied.inventory_count);
  CHECK(qa_gameplay_authority_read(s.fx.combat, actor.id).value.health == 100);
  CHECK(entry_count > 0 && streq(entries[0].item, "q1:weapon/hipnotic:laser"));
  CHECK(s.gravity_scale[0] == 1);
  qa_q1_travel_state_destroy(&supplied); qa_q1_travel_state_destroy(&hip); qa_q1_travel_state_destroy(&rog);
  close_session(&s);
  /* TS "Mission pack travel preserves edition resets and source multiplayer starting equipment" */
  static const char *const EDITIONS[2] = {"classic", "rerelease"};
  for (size_t e = 0; e < 2; e++) {
    open_session(&s, "hipnotic", EDITIONS[e], 1, 0);
    qa_q1_player_state *player = add_player(&s);
    (void)qa_shared_inventory_table_give(s.fx.inventory, player->actor, qa_q1_weapon_item("hipnotic:laser"), 1);
    (void)qa_shared_inventory_table_give(s.fx.inventory, player->actor, "q1:ammo/cells", 30);
    (void)qa_q1_select_weapon(s.game, player->actor, "hipnotic:laser");
    qa_q1_travel_state state = qa_q1mp_capture_mission_pack_travel(s.game, player->actor, "hipnotic");
    CHECK(streq(state.weapon, e == 0 ? "hipnotic:laser" : "shotgun"));
    set_map(s.game, "hip2m1");
    qa_q1_travel_state reset = qa_q1mp_decode_mission_pack_travel(s.game, &state, 0, "hipnotic");
    qa_q1mp_admit_mission_pack_travel(s.game, player->actor, &reset, "hipnotic");
    CHECK(count_of(&s, player->actor.id, qa_q1_weapon_item("hipnotic:laser")) == 0);
    CHECK(streq(player->weapon, "shotgun"));
    qa_q1_travel_state_destroy(&state); qa_q1_travel_state_destroy(&reset);
    close_session(&s);
  }
  open_session(&s, "rogue", "rerelease", 1, 4);
  qa_q1_player_state *player = add_player(&s);
  qa_q1_travel_state ctf = qa_q1mp_new_mission_pack_travel(s.game, "rogue");
  qa_q1mp_admit_mission_pack_travel(s.game, player->actor, &ctf, "rogue");
  CHECK(count_of(&s, player->actor.id, qa_q1_weapon_item("rogue:grapple")) == 1);
  const qa_armor_state armor = qa_gameplay_authority_read(s.fx.combat, player->actor.id).value.armor;
  CHECK(armor.regular.kind == QA_REGULAR_ARMOR_Q1 && armor.regular.points == 50 && armor.regular.absorption == 0.3 &&
        streq(armor.regular.item, "q1:armor/green") && streq(armor.powered.kind, "none"));
  qa_q1_travel_state_destroy(&ctf);
  close_session(&s);
}

static qa_q1_obituary_actor person(int slot, const char *name, const char *classname, const char *weapon, bool is_player, bool is_monster) {
  qa_q1_obituary_actor p;
  memset(&p, 0, sizeof p);
  p.actor.slot = slot; p.name = name; p.classname = classname; p.weapon = weapon;
  p.is_player = is_player; p.is_monster = is_monster; p.water_type = "empty"; p.kill_string = "";
  return p;
}
static double rolls[8]; static size_t roll_index;
static int roll_sentinel;
static double stub_roll(void *ctx) { CHECK(ctx == &roll_sentinel); return rolls[roll_index++]; }
static int tag_calls;
static double tag_five(void *ctx) { CHECK(ctx == &roll_sentinel); tag_calls++; return 5; }
static void test_obituaries(void) {
  qa_q1_obituary_input input;
  memset(&input, 0, sizeof input);
  input.victim = person(1, "Vic", "player", "shotgun", true, false);
  qa_q1_obituary_actor attacker = person(2, "Att", "player", "rogue:grapple", true, false);
  input.attacker = &attacker;
  input.edition = "classic"; input.death_type = ""; input.random = stub_roll; input.random_ctx = &roll_sentinel;
  qa_q1mp_obituary_context ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.pack = "rogue"; ctx.inflictor_classname = ""; ctx.attacker_death_type = "";
  rolls[0] = 0.25; roll_index = 0;
  qa_q1_obituary out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "Vic was grappled by Att\n"));
  CHECK(out.has_score && out.score_actor.slot == 2 && out.score_delta == 1);
  /* hipnotic suicide, classic, firstRoll <= 0.4 -> loaded */
  attacker = person(1, "Vic", "player", "shotgun", true, false);
  ctx.pack = "hipnotic"; rolls[0] = 0.1; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_score && out.score_delta == -1 && out.score_actor.slot == 1);
  CHECK(out.has_message && streq(out.text, "Vic checks if his weapon is loaded\n"));
  /* rogue dragon monster */
  attacker = person(3, "", "monster_dragon", "", false, true);
  input.victim.weapon = "shotgun"; ctx.pack = "rogue"; rolls[0] = 0.5; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "Vic was annihilated by the Dragon\n"));
  /* vengeance: message without score */
  attacker = person(4, "", "Vengeance", "", false, false);
  rolls[0] = 0.5; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "Vic was purged by the Vengeance Sphere\n"));
  CHECK(!out.has_score);
  /* rerelease keeps key + args */
  input.edition = "rerelease";
  attacker = person(2, "Att", "player", "rogue:plasma", true, false);
  rolls[0] = 0.5; roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&input, &ctx);
  CHECK(out.has_message && streq(out.text, "$qc_death_fused") && out.argument_count == 2);
  CHECK(streq(out.arguments[0], "Vic") && streq(out.arguments[1], "Att"));

  /* TS "Pack obituary decisions preserve source random draws and sole score awards" */
  qa_q1_obituary_input pack_input;
  memset(&pack_input, 0, sizeof pack_input);
  pack_input.victim = person(2, "target", "player", "shotgun", true, false);
  pack_input.victim.team = 1; pack_input.victim.health = -1;
  qa_q1_obituary_actor killer = pack_input.victim;
  killer.actor.slot = 1; killer.name = "player"; killer.team = 2; killer.weapon = "rogue:plasma";
  pack_input.attacker = &killer;
  pack_input.edition = "rerelease"; pack_input.teamplay = 0; pack_input.death_type = "hipnotic:empathy";
  pack_input.random = stub_roll; pack_input.random_ctx = &roll_sentinel;
  qa_q1mp_obituary_context pack;
  memset(&pack, 0, sizeof pack);
  pack.pack = "hipnotic"; pack.inflictor_classname = "player"; pack.attacker_death_type = ""; pack.victim_saved_team = 1; pack.gamecfg = 0;
  for (size_t i = 0; i < 8; i++) rolls[i] = 0.4;
  roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&pack_input, &pack);
  CHECK(roll_index == 2);
  CHECK(out.has_message && streq(out.text, "$qc_death_empathy1"));
  CHECK(out.has_score && out.score_actor.slot == 1 && out.score_delta == 1);
  qa_q1_obituary_actor sphere = killer;
  sphere.is_player = false; sphere.classname = "Vengeance";
  qa_q1_obituary_input vengeance = pack_input;
  vengeance.attacker = &sphere;
  qa_q1mp_obituary_context rogue = pack;
  rogue.pack = "rogue";
  roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&vengeance, &rogue);
  CHECK(!out.has_score);
  qa_q1_obituary_input tag = pack_input;
  tag.teamplay = 3;
  rogue.tag_ctx = &roll_sentinel; rogue.tag_score = tag_five;
  roll_index = 0; tag_calls = 0;
  out = qa_q1mp_mission_pack_obituary(&tag, &rogue);
  CHECK(out.has_score && out.score_actor.slot == 1 && out.score_delta == 5);
  CHECK(tag_calls == 1);
  qa_q1_obituary_input suicide = pack_input;
  suicide.edition = "classic"; suicide.attacker = &suicide.victim;
  roll_index = 0;
  out = qa_q1mp_mission_pack_obituary(&suicide, &pack);
  CHECK(out.has_message && streq(out.text, "target checks if his weapon is loaded\n"));
}

static void test_runes(void) {
  session s;
  open_session(&s, NULL, "classic", 1, 0);
  qa_q1_player_state *player = add_player(&s);
  static qa_q1mp_rogue_runes runes;
  qa_q1mp_rogue_runes_init(&runes, s.game, NULL, NULL);
  const qa_actor_id actor = player->actor.id;
  CHECK(qa_q1mp_runes_damage(&runes, actor, 10) == 10);
  CHECK(qa_q1mp_runes_resistance(&runes, actor, 10) == 10);
  CHECK(qa_q1mp_runes_attack_delay(&runes, actor, 0.9) == 0.9);
  CHECK(!qa_q1mp_runes_has_regeneration(&runes, actor));
  /* strength+haste+regen runes set directly on the state table */
  CHECK(runes.player_count == 1);
  runes.players[0].rune = 2 | 4 | 8;
  CHECK(qa_q1mp_runes_damage(&runes, actor, 10) == 20);
  CHECK(qa_q1mp_runes_attack_delay(&runes, actor, 0.9) == (double)(float)((double)(float)(0.9 * 2) / 3));
  CHECK(qa_q1mp_runes_attack_delay(&runes, actor, 0.9) == 0.5999999642372131); /* TS oracle */
  CHECK(qa_q1mp_runes_has_regeneration(&runes, actor));
  runes.players[0].rune = 1;
  CHECK(qa_q1mp_runes_resistance(&runes, actor, 10) == 5);
  /* An actor that no longer resolves: damage returns the amount ("a
   * projectile can outlive its owner", TS oracle: 1); the other rune
   * operations abort with the TS Error. */
  qa_actor_id dead = actor;
  dead.slot = 999;
  CHECK(qa_q1mp_runes_damage(&runes, dead, 1) == 1);
  CHECK_ABORTS((void)qa_q1mp_runes_resistance(&runes, dead, 1));
  qa_q1mp_rogue_runes_destroy(&runes);
  close_session(&s);
}

/* --- aim (TS foundation/weapons.ts aim + q1-aim.ts) over the real game --- */
static bool aim_straight_hit;
static qa_actor_id aim_target;
static qa_q1_trace aim_trace(void *ctx, qa_q1_trace_request request) {
  (void)q1_world_fixture_of(ctx);
  qa_q1_trace out;
  memset(&out, 0, sizeof out);
  out.fraction = 1; out.end = request.end;
  /* Straight ray ends at x=2048; the target-center probe at x=100. */
  if ((request.end.x > 1000 && aim_straight_hit) || request.end.x < 1000) {
    out.has_actor = true; out.actor = aim_target; out.fraction = 0.5;
  }
  return out;
}
static void set_team(session *s, qa_owned_actor actor, const char *team) {
  qa_combat_trait_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_team = true; changes.team = team;
  qa_gameplay_authority_set_traits(s->fx.combat, actor, changes);
}
static void test_aim(void) {
  session s;
  open_session(&s, NULL, "classic", 0, 0);
  s.game->host.trace = aim_trace;
  const qa_owned_actor shooter = q1_world_fixture_admit_player(&s.fx, (qa_vec3){0, 0, 0}, 100);
  set_team(&s, shooter, "red");
  qa_q1_actor *target = qa_q1_create(s.game, "aim_target", NULL, NULL);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = (qa_vec3){100, 0, -10};
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  qa_q1_set_body(s.game, target, &patch);
  target->aimed_damage = true;
  qa_q1_actor_set_damageable(target, true);
  set_team(&s, target->actor, "blue");
  aim_target = target->actor.id; aim_straight_hit = false;
  const qa_vec3 forward = {1, 0, 0};
  /* Visible DAMAGE_AIM target below the ray: z is corrected toward
   * normalize(100,0,-10) while horizontal aim is preserved. */
  qa_vec3 aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(fabs(aimed.x - 0.9950371902099892) < 1e-4 && aimed.y == 0);
  CHECK(fabs(aimed.z - (-0.09950371902099892)) < 1e-4);
  /* No shooter body: forward unchanged. */
  qa_owned_actor bodiless = shooter;
  bodiless.id.slot = 4000;
  aimed = qa_q1_aim(s.game, bodiless, forward);
  CHECK(aimed.x == 1 && aimed.y == 0 && aimed.z == 0);
  /* Straight ray already hits an eligible target: unchanged. */
  aim_straight_hit = true;
  aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(aimed.x == 1 && aimed.z == 0);
  aim_straight_hit = false;
  /* Teamplay excludes a same-team target. */
  s.game->options.has_teamplay = true; s.game->options.teamplay = 1; set_team(&s, target->actor, "red");
  aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(aimed.x == 1 && aimed.z == 0);
  set_team(&s, target->actor, "blue");
  /* Not damageable: excluded. */
  qa_q1_actor_set_damageable(target, false);
  aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(aimed.x == 1 && aimed.z == 0);
  qa_q1_actor_set_damageable(target, true);
  /* Neither aimedDamage nor player: excluded; player re-enables. */
  target->aimed_damage = false;
  aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(aimed.x == 1 && aimed.z == 0);
  s.fx.players[s.fx.player_count++] = target->actor.id;
  aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(fabs(aimed.z - (-0.09950371902099892)) < 1e-4);
  /* Alignment 0.9677 is below an explicit aimThreshold of 0.99. */
  s.game->options.has_aim_threshold = true; s.game->options.aim_threshold = 0.99;
  aimed = qa_q1_aim(s.game, shooter, forward);
  CHECK(aimed.x == 1 && aimed.z == 0);
  close_session(&s);
}

/* TS "Rogue backpacks carry powered ammunition and grant the original base weapon" */
static void test_rogue_backpack(void) {
  session s;
  open_session(&s, "rogue", "rerelease", 0, 0);
  qa_q1_player_state *player = add_player(&s), *target = add_player(&s);
  (void)qa_shared_inventory_table_give(s.fx.inventory, player->actor, qa_q1_weapon_item("nailgun"), 1);
  (void)qa_shared_inventory_table_give(s.fx.inventory, player->actor, "rogue:ammo/lava-nails", 25);
  qa_q1mp_players_enable_combos(&s.arsenal.players, player);
  (void)qa_q1_select_weapon(s.game, player->actor, "rogue:lava-nailgun");
  qa_q1_actor *backpack = qa_q1mp_drop_mission_pack_backpack(s.game, player->actor, "rogue");
  CHECK(backpack != NULL);
  if (backpack != NULL) {
    touch(&s, backpack, target->actor.id);
    CHECK(count_of(&s, target->actor.id, "rogue:ammo/lava-nails") == 25);
    CHECK(count_of(&s, target->actor.id, qa_q1_weapon_item("rogue:lava-nailgun")) == 1);
    CHECK(streq(target->weapon, "nailgun"));
    CHECK(!qa_q1_live(s.game, backpack));
  }
  close_session(&s);
}
/* TS "Rogue team impulses toss source ammo and weapons with owner pickup delay" */
static void test_rogue_toss(void) {
  session s;
  open_session(&s, "rogue", "rerelease", 1, 1);
  qa_q1_player_state *player = add_player(&s), *target = add_player(&s);
  (void)qa_shared_inventory_table_give(s.fx.inventory, player->actor, qa_q1_weapon_item("nailgun"), 1);
  (void)qa_shared_inventory_table_give(s.fx.inventory, player->actor, "rogue:ammo/lava-nails", 50);
  qa_q1mp_players_enable_combos(&s.arsenal.players, player);
  (void)qa_q1_select_weapon(s.game, player->actor, "rogue:lava-nailgun");
  CHECK(qa_q1mp_arsenal_impulse(&s.arsenal, player->actor.id, 20));
  qa_q1_actor *backpack = find_entity(&s, "item_backpack", NULL);
  CHECK(backpack != NULL);
  if (backpack != NULL) {
    CHECK(streq(backpack->movement, "bounce"));
    CHECK(count_of(&s, player->actor.id, "rogue:ammo/lava-nails") == 30);
    touch(&s, backpack, player->actor.id);
    CHECK(qa_q1_live(s.game, backpack));
    touch(&s, backpack, target->actor.id);
    CHECK(count_of(&s, target->actor.id, "rogue:ammo/lava-nails") == 20);
  }
  (void)qa_q1mp_arsenal_impulse(&s.arsenal, player->actor.id, 21);
  CHECK(count_of(&s, player->actor.id, qa_q1_weapon_item("rogue:lava-nailgun")) == 0);
  CHECK(streq(player->weapon, "shotgun"));
  qa_q1_actor *weapon = find_entity(&s, "weapon_nailgun", &player->actor.id);
  CHECK(weapon != NULL);
  if (weapon != NULL) {
    touch(&s, weapon, target->actor.id);
    CHECK(count_of(&s, target->actor.id, qa_q1_weapon_item("rogue:lava-nailgun")) == 1);
  }
  close_session(&s);
}
/* TS "Rogue weapons-stay pickups hand out the plasma, multi-rocket and lava-nail rounds once" */
static void test_rogue_weapons_stay(void) {
  session s;
  open_session(&s, "rogue", "rerelease", 2, 0);
  qa_q1_player_state *player = add_player(&s);
  static const struct { const char *classname; const char *item; double amount; } PICKUPS[3] = {
    {"weapon_lightning", "rogue:ammo/plasma", 5}, {"weapon_rocketlauncher", "rogue:ammo/multi-rockets", 3},
    {"weapon_nailgun", "rogue:ammo/lava-nails", 20}};
  for (size_t i = 0; i < 3; i++) {
    qa_q1_actor *pickup = qa_q1_create(s.game, PICKUPS[i].classname, NULL, NULL);
    qa_q1_spawn_entity(s.game, pickup);
    pickup->solid = "trigger";
    const double before = count_of(&s, player->actor.id, PICKUPS[i].item);
    touch(&s, pickup, player->actor.id);
    CHECK(count_of(&s, player->actor.id, PICKUPS[i].item) - before == PICKUPS[i].amount);
  }
  close_session(&s);
}
/* TS "Hipnotic wetsuit cancels lightning-beam damage but not other weapons" */
static void test_hipnotic_wetsuit(void) {
  session s;
  open_session(&s, "hipnotic", "rerelease", 0, 0);
  qa_q1_player_state *player = add_player(&s), *target = add_player(&s);
  qa_q1_give_powerup(s.game, target, "hipnotic:wetsuit", 30);
  const double before = qa_q1_health(s.game, target->actor.id);
  (void)qa_q1_damage(s.game, target->actor.id, player->actor.id, &player->actor.id, 30, "lightning", NULL, NULL, NULL);
  CHECK(qa_q1_health(s.game, target->actor.id) == before);
  (void)qa_q1_damage(s.game, target->actor.id, player->actor.id, &player->actor.id, 10, "shotgun", NULL, NULL, NULL);
  CHECK(qa_q1_health(s.game, target->actor.id) < before);
  CHECK(qa_q1_health(s.game, target->actor.id) == 90); /* TS oracle over the open world */
  close_session(&s);
}

int main(void) {
  test_tables_and_math();
  test_selection();
  test_travel();
  test_obituaries();
  test_runes();
  test_aim();
  test_rogue_backpack();
  test_rogue_toss();
  test_rogue_weapons_stay();
  test_hipnotic_wetsuit();
  if (failures != 0) { fprintf(stderr, "%d/%d checks failed\n", failures, checks); return 1; }
  printf("test_q1_missionpacks: %d checks passed\n", checks);
  return 0;
}

/*
 * Q1 unification: the mission-pack monsters link and run through the real
 * game. A real qa_q1_services on the shared world tables
 * (tests/content/q1_world_fixture.h) + qa_q1_register_base +
 * qa_q1mp_register_mission_pack_arsenal + qa_q1mpm_register_mission_pack_monsters
 * (+ qa_q1mp_register_hipnotic_misc), as the TS sessions compose them, drives
 * every call that used to be a declared-but-undefined qa_q1mpm_ext_* mirror
 * through real monster behavior and checks its observable effect:
 *   register_monster_callbacks + base species  h0 (ogre spawns as a baseBehavior
 *                                               MissionMonster thinking "hipnotic:monster_start")
 *   fire_bullets            h1 army_fire, h6 gremlin shotgun (blood + player damage)
 *   drop_backpack           h2 grunt:army_die3 (item_backpack)
 *   throw_head_damage       h3 army gib (h_guard head, gib=3)
 *   launch_spike            h4 gremlin nail (spike)
 *   launch_hipnotic_laser   h5 gremlin laser, h8 armagon laser (hiplaser)
 *   launch_ogre_grenade     h6 gremlin grenadelauncher (ogre_grenade)
 *   launch_hipnotic_proximity h6 gremlin proximity (proximity_grenade)
 *   spawn_meat_spray        h7 Gremlin_Melee(200) (meat_spray)
 *   create_missile          h9 armagon satk9 (missile=2)
 *   multi_explosion         h10 armagon die4 (hip_multi_explosion)
 *   launch_laser            r1 morph stab (enforcer_laser=3)
 *   launch_rogue_plasma     r2 dragon fireball at random 0.7 (plasma)
 *   spawn_teleport_fog + spawn_teledeath  r3 overlord teleport (fog x2, teledeath)
 *   throw_head + throw_gib  r4 mummy death (h_zombie head, gib=3)
 * Expected values are from the same scenario in TS under bun over an equal
 * open-world host (scratchpad/mpm/oracle_unified.ts); the printed lines are
 * quoted at each check. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/arsenal.h"
#include "qa/content/q1/missionpacks/monsters.h"
#include "qa/content/q1/missionpacks/world/hipnotic_misc.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/js.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_TEXT(actual, expected) do { const char *a_ = (actual), *e_ = (expected); checks++; \
  if (strcmp(a_, e_) != 0) { failures++; fprintf(stderr, "FAIL %s:%d:\n  got      %s\n  expected %s\n", __FILE__, __LINE__, a_, e_); } } while (0)

typedef struct world {
  q1_world_fixture fx;
  qa_q1_base *base;
  qa_q1mp_mission_pack_arsenal arsenal;
  qa_q1mpm_runtime monsters;
  qa_q1_entity *entities; size_t entity_count;
  qa_q1_player_state *player;
  bool hit_player;
  char keys[256][96]; size_t key_count;
  size_t mark;
  char text[2048];
} world;

/* Trace double: the fixture's open world, or (hit_player) a hit on the player halfway. */
static qa_q1_trace hit_trace(void *ctx, qa_q1_trace_request request) {
  q1_world_fixture *fx = q1_world_fixture_of(ctx);
  world *w = (world *)fx; /* fx is the first member */
  qa_q1_trace trace = q1_wf_trace(ctx, request);
  if (!w->hit_player) return trace;
  trace.fraction = 0.5;
  trace.end = qa_q1_vadd(request.start, qa_q1_vscale(qa_q1_vsub(request.end, request.start), 0.5));
  trace.normal = (qa_vec3){-1, 0, 0};
  trace.has_actor = true;
  trace.actor = fx->players[0];
  return trace;
}
static void keyed_emit(void *ctx, qa_q1_event e);
static int sentinel_charmer;
static bool hooks_charmer(void *ctx, qa_actor_id *out) { (void)out; CHECK(ctx == &sentinel_charmer); return false; }

static void world_open(world *w, const char *pack, const char *map, qa_vec3 player_origin) {
  memset(w, 0, sizeof *w);
  qa_q1_options options = q1_world_fixture_options();
  options.has_teamplay = true;
  options.teamplay = 0;
  options.max_clients = 2;
  options.campaign = strcmp(pack, "hipnotic") == 0 ? "q1:hipnotic" : "q1:rogue";
  options.movement_provider = "q3:movement";
  CHECK(q1_world_fixture_create(&w->fx, options));
  qa_q1_services *game = w->fx.game;
  game->host.trace = hit_trace;
  game->host.emit = keyed_emit;
  w->base = qa_q1_register_base(game, NULL);
  (void)qa_q1mp_register_mission_pack_arsenal(game, pack, &w->arsenal);
  const qa_q1mpm_mission_monster_hooks hooks = {&sentinel_charmer, hooks_charmer};
  (void)qa_q1mpm_register_mission_pack_monsters(game, w->base, pack, &hooks, &w->monsters);
  if (strcmp(pack, "hipnotic") == 0) qa_q1mp_register_hipnotic_misc(game);
  w->entities = qa_q1_parse_entities(map, &w->entity_count);
  for (size_t i = 0; i < w->entity_count; i++) {
    const char *classname = qa_q1_entity_value(&w->entities[i], "classname");
    qa_q1_spawn_entity(game, qa_q1_create(game, classname == NULL ? "" : classname, &w->entities[i], NULL));
  }
  const qa_owned_actor owner = q1_world_fixture_admit_player(&w->fx, player_origin, 1000);
  w->player = qa_q1_attach_player(game, owner, "shotgun", true, NULL);
}
static void world_close(world *w) {
  CHECK(w->fx.ctx_errors == 0);
  qa_q1mpm_runtime_destroy(&w->monsters);
  qa_q1mp_mission_pack_arsenal_destroy(&w->arsenal);
  qa_q1_base_destroy(w->base);
  q1_world_fixture_destroy(&w->fx);
  qa_q1_entities_destroy(w->entities, w->entity_count);
}
static int compare_text(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
/* "classname=count,..." sorted, as the oracle prints it. */
static const char *histogram(world *w) {
  qa_q1_services *game = w->fx.game;
  const char **names = malloc((game->entity_count + 1) * sizeof *names);
  for (size_t i = 0; i < game->entity_count; i++) names[i] = game->entities[i]->classname;
  qa_js_sort(names, game->entity_count, sizeof *names, compare_text);
  size_t at = 0;
  w->text[0] = '\0';
  for (size_t i = 0; i < game->entity_count;) {
    size_t j = i;
    while (j < game->entity_count && strcmp(names[j], names[i]) == 0) j++;
    at += (size_t)snprintf(w->text + at, sizeof w->text - at, "%s%s=%zu", at == 0 ? "" : ",", names[i], j - i);
    i = j;
  }
  free(names);
  return w->text;
}
/* Emit double: keys each event (kind[:path][:effect]) as it is emitted (the
 * fixture keeps shallow copies whose strings may point at a returned caller's
 * buffer), then records through the fixture's own emit. */
static void keyed_emit(void *ctx, qa_q1_event e) {
  world *w = (world *)q1_world_fixture_of(ctx); /* fx is the first member */
  if (w->key_count < sizeof w->keys / sizeof w->keys[0])
    snprintf(w->keys[w->key_count++], sizeof w->keys[0], "%s%s%s%s%s", e.kind, e.path ? ":" : "", e.path ? e.path : "",
             e.effect ? ":" : "", e.effect ? e.effect : "");
  q1_wf_emit(ctx, e);
}
/* Event keys emitted since the last call, sorted and counted. */
static const char *events_since(world *w) {
  const char *sorted[256];
  size_t key_count = 0;
  for (size_t i = w->mark; i < w->key_count; i++) sorted[key_count++] = w->keys[i];
  w->mark = w->key_count;
  qa_js_sort(sorted, key_count, sizeof *sorted, compare_text);
  size_t at = 0;
  w->text[0] = '\0';
  for (size_t i = 0; i < key_count;) {
    size_t j = i;
    while (j < key_count && strcmp(sorted[j], sorted[i]) == 0) j++;
    at += (size_t)snprintf(w->text + at, sizeof w->text - at, "%s%s=%zu", at == 0 ? "" : ",", sorted[i], j - i);
    i = j;
  }
  return w->text;
}
static qa_q1mpm_mission_monster *find(world *w, const char *classname) {
  for (size_t i = 0; i < w->monsters.monster_count; i++)
    if (strcmp(w->monsters.monsters[i].monster->base.entity->classname, classname) == 0) return w->monsters.monsters[i].monster;
  return NULL;
}

static const char *const HIPNOTIC_MAP =
    "{ \"classname\" \"worldspawn\" }\n"
    "{ \"classname\" \"monster_ogre\" \"origin\" \"-512 0 24\" }\n"
    "{ \"classname\" \"monster_army\" \"origin\" \"0 0 24\" }\n"
    "{ \"classname\" \"monster_gremlin\" \"origin\" \"0 256 24\" }\n"
    "{ \"classname\" \"monster_armagon\" \"origin\" \"0 -512 24\" }\n";

static void test_hipnotic(void) {
  world w;
  world_open(&w, "hipnotic", HIPNOTIC_MAP, (qa_vec3){100, 0, 24});
  qa_q1_services *game = w.fx.game;
  const qa_actor_id player = w.player->actor.id;
  /* h0 ogre: mission=true baseBehavior=true think=hipnotic:monster_start baseMonstersHasOgre=false
   * hist=armagon_body=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,worldspawn=1 totalMonsters=4 */
  qa_q1mpm_mission_monster *ogre = find(&w, "monster_ogre");
  CHECK(ogre != NULL && qa_q1mpm_mission_monster_of(&ogre->base) == ogre);
  CHECK(ogre != NULL && ogre->definition->has_base_behavior && ogre->definition->base_behavior);
  CHECK(ogre != NULL && strcmp(qa_q1_action_name(ogre->base.entity->think), "hipnotic:monster_start") == 0);
  CHECK(ogre != NULL && qa_q1_creatures_monsters_get(qa_q1_base_creatures(w.base), ogre->base.entity->actor) == NULL);
  CHECK_TEXT(histogram(&w), "armagon_body=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,worldspawn=1");
  CHECK(game->total_monsters == 4);
  /* h0 ogre start: think=hipnotic:monster_frame time=0.20000000298023224 — the registered "hipnotic:"
   * monster callbacks reach the controller through qa_q1_register_monster_callbacks' lookup. */
  if (ogre != NULL) {
    qa_q1_actor *entity = ogre->base.entity;
    const qa_q1_bound_action callback = entity->think;
    qa_q1_services_set_time(game, entity->next_think);
    entity->think = (qa_q1_bound_action){0};
    entity->next_think = -1;
    qa_q1_call_action(game, entity, callback);
    CHECK(strcmp(qa_q1_action_name(entity->think), "hipnotic:monster_frame") == 0);
    CHECK(game->time == 0.20000000298023224);
  }

  /* h1 army_fire: playerHealth=984 events=effect:blood=4,sound:soldier/sattck1.wav=1 */
  qa_q1mpm_mission_monster *army = find(&w, "monster_army");
  CHECK(army != NULL);
  if (army == NULL) { world_close(&w); return; }
  qa_q1_base_monster_set_enemy(&army->base, &player);
  (void)events_since(&w);
  w.hit_player = true;
  qa_q1_base_monster_play(&army->base, "army_fire");
  w.hit_player = false;
  CHECK(qa_q1_health(game, player) == 984);
  CHECK_TEXT(events_since(&w), "effect:blood=4,sound:soldier/sattck1.wav=1");
  /* h2 army_die3: hist=...,item_backpack=1,... armySolid=none */
  qa_q1_base_monster_play(&army->base, "grunt:army_die3");
  CHECK_TEXT(histogram(&w), "armagon_body=1,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,worldspawn=1");
  CHECK(strcmp(army->base.entity->solid, "none") == 0);
  /* h3 army gib: hist=...,gib=3,... model=progs/h_guard.mdl killed=1 */
  qa_gameplay_authority_set_health(game->host.combat, army->base.entity->actor, -40);
  qa_q1_base_monster_die(&army->base, &player);
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,worldspawn=1");
  CHECK(strcmp(army->base.entity->model, "progs/h_guard.mdl") == 0 && game->killed_monsters == 1);

  /* h4 nail: ...,spike=1,... currentammo=-1 */
  qa_q1mpm_mission_monster *gremlin = find(&w, "monster_gremlin");
  CHECK(gremlin != NULL);
  if (gremlin == NULL) { world_close(&w); return; }
  qa_q1_base_monster_set_enemy(&gremlin->base, &player);
  qa_q1mpm_gremlin_fire_nail(gremlin);
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,spike=1,worldspawn=1");
  CHECK(qa_q1_actor_number(gremlin->base.entity, "currentammo") == -1);
  /* h5 laser: ...,hiplaser=1,... */
  qa_q1mpm_gremlin_fire_laser(gremlin);
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,hiplaser=1,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,spike=1,worldspawn=1");
  /* h6 grenadelauncher: events=effect:muzzleflash=1,sound:weapons/grenade.wav=1 ...,ogre_grenade=1,... currentFrame=gremlin_rocket1
   * h6 hipnotic:proximity: events=sound:weapons/grenade.wav=1 ...,proximity_grenade=1,...
   * h6 shotgun: events=effect:blood=6,sound:weapons/guncock.wav=1 playerHealth=960 currentFrame=gremlin_shot1 */
  static const struct { const char *weapon, *events, *hist, *frame; double health; } attacks[] = {
      {"grenadelauncher", "effect:muzzleflash=1,sound:weapons/grenade.wav=1",
       "armagon_body=1,gib=3,hiplaser=1,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,spike=1,worldspawn=1",
       "gremlin_rocket1", 984},
      {"hipnotic:proximity", "sound:weapons/grenade.wav=1",
       "armagon_body=1,gib=3,hiplaser=1,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,proximity_grenade=1,spike=1,worldspawn=1",
       "gremlin_rocket1", 984},
      {"shotgun", "effect:blood=6,sound:weapons/guncock.wav=1",
       "armagon_body=1,gib=3,hiplaser=1,item_backpack=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,proximity_grenade=1,spike=1,worldspawn=1",
       "gremlin_shot1", 960}};
  for (size_t i = 0; i < 3; i++) {
    (void)qa_q1_actor_field_set(gremlin->base.entity, "gremlin:weapon", attacks[i].weapon);
    (void)qa_q1_actor_field_set(gremlin->base.entity, "currentammo", "5");
    (void)qa_q1_actor_field_set(gremlin->base.entity, "stoleweapon", "1");
    (void)events_since(&w);
    w.hit_player = strcmp(attacks[i].weapon, "shotgun") == 0;
    CHECK(qa_q1mpm_gremlin_weapon_attack(gremlin));
    w.hit_player = false;
    CHECK_TEXT(events_since(&w), attacks[i].events);
    CHECK_TEXT(histogram(&w), attacks[i].hist);
    CHECK(qa_q1_health(game, player) == attacks[i].health);
    CHECK_TEXT(gremlin->base.current_frame, attacks[i].frame);
  }
  /* h7 melee: ...,meat_spray=1,... playerHealth=948 */
  qa_body_state body = qa_shared_body_table_read(game->host.bodies, player).value;
  body.origin = qa_q1_vadd(qa_q1_base_monster_origin(&gremlin->base), (qa_vec3){50, 0, 0});
  qa_shared_body_table_write(game->host.bodies, w.player->actor, body);
  qa_q1_base_monster_play(&gremlin->base, "Gremlin_Melee(200)");
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,hiplaser=1,item_backpack=1,meat_spray=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,proximity_grenade=1,spike=1,worldspawn=1");
  CHECK(qa_q1_health(game, player) == 948);

  /* h8 armagon laser: hiplaser=2; h9 armagon rockets: missile=2; h10 armagon die4: hip_explosion=1,hip_multi_explosion=1 */
  qa_q1mpm_mission_monster *armagon = find(&w, "monster_armagon");
  CHECK(armagon != NULL);
  if (armagon == NULL) { world_close(&w); return; }
  qa_q1_base_monster_set_enemy(&armagon->base, &player);
  qa_q1_base_monster_play(&armagon->base, "armagon_launch_laser(40)");
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,hiplaser=2,item_backpack=1,meat_spray=1,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,proximity_grenade=1,spike=1,worldspawn=1");
  qa_q1_base_monster_play(&armagon->base, "hiparma:armagon_satk9");
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,hiplaser=2,item_backpack=1,meat_spray=1,missile=2,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,proximity_grenade=1,spike=1,worldspawn=1");
  qa_q1_base_monster_play(&armagon->base, "hiparma:armagon_die4");
  CHECK_TEXT(histogram(&w), "armagon_body=1,gib=3,hip_explosion=1,hip_multi_explosion=1,hiplaser=2,item_backpack=1,meat_spray=1,missile=2,missionpack_player_state=1,monster_armagon=1,monster_army=1,monster_gremlin=1,monster_ogre=1,ogre_grenade=1,proximity_grenade=1,spike=1,worldspawn=1");
  world_close(&w);
}

static const char *const ROGUE_MAP =
    "{ \"classname\" \"worldspawn\" }\n"
    "{ \"classname\" \"monster_morph\" \"origin\" \"0 0 24\" \"spawnflags\" \"2\" }\n"
    "{ \"classname\" \"monster_dragon\" \"origin\" \"0 512 200\" }\n"
    "{ \"classname\" \"monster_super_wrath\" \"origin\" \"0 -512 24\" \"spawnflags\" \"2\" }\n"
    "{ \"classname\" \"info_overlord_destination\" \"origin\" \"512 -512 24\" }\n"
    "{ \"classname\" \"monster_mummy\" \"origin\" \"-512 0 24\" }\n";

static void test_rogue(void) {
  world w;
  world_open(&w, "rogue", ROGUE_MAP, (qa_vec3){1000, 0, 24});
  qa_q1_services *game = w.fx.game;
  const qa_actor_id player = w.player->actor.id;
  /* r0: hist=info_overlord_destination=1,missionpack_player_state=1,monster_dragon=1,monster_morph=1,monster_mummy=1,monster_super_wrath=1,worldspawn=1 totalMonsters=4 */
  CHECK_TEXT(histogram(&w), "info_overlord_destination=1,missionpack_player_state=1,monster_dragon=1,monster_morph=1,monster_mummy=1,monster_super_wrath=1,worldspawn=1");
  CHECK(game->total_monsters == 4);
  qa_q1mpm_mission_monster *morph = find(&w, "monster_morph"), *dragon = find(&w, "monster_dragon");
  qa_q1mpm_mission_monster *overlord = find(&w, "monster_super_wrath"), *mummy = find(&w, "monster_mummy");
  CHECK(morph != NULL && dragon != NULL && overlord != NULL && mummy != NULL);
  if (morph == NULL || dragon == NULL || overlord == NULL || mummy == NULL) { world_close(&w); return; }
  /* r1 morph stab: enforcer_laser=3 */
  qa_q1_base_monster_set_enemy(&morph->base, &player);
  qa_q1_base_monster_play(&morph->base, "morph_stab2");
  CHECK_TEXT(histogram(&w), "enforcer_laser=3,info_overlord_destination=1,missionpack_player_state=1,monster_dragon=1,monster_morph=1,monster_mummy=1,monster_super_wrath=1,worldspawn=1");
  /* r2 dragon: plasma=1 */
  qa_q1_base_monster_set_enemy(&dragon->base, &player);
  w.fx.random_value = 0.7;
  qa_q1_base_monster_play(&dragon->base, "dragon_fireball");
  w.fx.random_value = 0.4;
  CHECK_TEXT(histogram(&w), "enforcer_laser=3,info_overlord_destination=1,missionpack_player_state=1,monster_dragon=1,monster_morph=1,monster_mummy=1,monster_super_wrath=1,plasma=1,worldspawn=1");
  /* r3 overlord: hist==2,...,teledeath=1,... origin=512,-512,51 (the two teleport fogs have no classname) */
  qa_q1_base_monster_play(&overlord->base, "overlord_teleport");
  CHECK_TEXT(histogram(&w), "=2,enforcer_laser=3,info_overlord_destination=1,missionpack_player_state=1,monster_dragon=1,monster_morph=1,monster_mummy=1,monster_super_wrath=1,plasma=1,teledeath=1,worldspawn=1");
  const qa_vec3 origin = qa_q1_base_monster_origin(&overlord->base);
  CHECK(origin.x == 512 && origin.y == -512 && origin.z == 51);
  /* r4 mummy: gib=3 model=progs/h_zombie.mdl killed=1 */
  qa_q1_base_monster_die(&mummy->base, &player);
  CHECK_TEXT(histogram(&w), "=2,enforcer_laser=3,gib=3,info_overlord_destination=1,missionpack_player_state=1,monster_dragon=1,monster_morph=1,monster_mummy=1,monster_super_wrath=1,plasma=1,teledeath=1,worldspawn=1");
  CHECK(strcmp(mummy->base.entity->model, "progs/h_zombie.mdl") == 0 && game->killed_monsters == 1);
  world_close(&w);
}

int main(void) {
  test_hipnotic();
  test_rogue();
  printf("test_q1_unified: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

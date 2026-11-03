/*
 * C checks for the 2026-11-02 sync of src/content/q1/missionpacks with the
 * TypeScript: monsters/species-variants.ts (Rogue statues, zombies, boss
 * ogres, tarbaby tiers and mitosis; Hipnotic lying zombies and rotfish
 * gibbing) through MissionMonster's redirect / prePain / sfx / gibDeath
 * hooks, the Rogue lava gun's out-of-ammo message, the Rogue suicide limit
 * (RogueTeams.suicide through Q1MissionPackRuntime.suicide) and the grapple's
 * hook index. Ported from ../quake-anthology-ts/tests/gameplay/
 * q1-missionpacks/monsters/source.test.ts (statues, zombies, ogres/Spawn,
 * Hipnotic lying zombies) over the open-world fixture (tests/content/
 * q1_world_fixture.h) with the retail entity lumps (skipped when the corpus
 * is absent; QFILES_ROOT or QTS_TEST_CORPUS). Every expected value comes from
 * the same scenario run in TS under bun over an equal open-world host:
 * scratchpad/mp/session.ts with variants.ts, direct.ts and misc.ts (the
 * printed lines are quoted above each case); holes.ts for the Hipnotic bullet holes
 * (world/hipnotic-holes.ts aims a hole with velocityAngles); ctf.ts for the Rogue CTF broadcasts
 * and TeamSetSkin (world/rogue-teams.ts as of 04:58). GPL-2.0-or-later.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q1/missionpacks/arsenal.h"
#include "qa/content/q1/missionpacks/monsters.h"
#include "qa/content/q1/missionpacks/monsters/species_variants.h"
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "qa/content/q1/missionpacks/runtime.h"
#include "qa/content/q1/missionpacks/world/hipnotic_holes.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool streq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* --- retail map entity lumps (PACK directory + BSP lump 0) --- */
static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL) root = getenv("QFILES_ROOT");
  return root;
}
static uint8_t *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
  if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size) { free(bytes); bytes = NULL; }
  fclose(file);
  *length = bytes == NULL ? 0 : (size_t)size;
  return bytes;
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
/* Steam layout: <root>/Quake[/rerelease]/<pack>/pak0.pak. NULL = absent. */
static qa_q1_entity *map_entities(const char *pack, const char *map, const char *edition, size_t *count) {
  *count = 0;
  const char *root = corpus_root();
  if (root == NULL) return NULL;
  char path[512], name[64];
  snprintf(path, sizeof path, "%s/Quake/%s%s/pak0.pak", root, strcmp(edition, "rerelease") == 0 ? "rerelease/" : "", pack);
  snprintf(name, sizeof name, "maps/%s.bsp", map);
  size_t length;
  uint8_t *pak = read_file(path, &length);
  if (pak == NULL || length < 12 || memcmp(pak, "PACK", 4) != 0) { free(pak); return NULL; }
  const uint32_t directory = le32(pak + 4), directory_length = le32(pak + 8);
  qa_q1_entity *entities = NULL;
  for (uint32_t at = directory; at + 64 <= directory + directory_length && at + 64 <= length; at += 64) {
    if (strncmp((const char *)pak + at, name, 56) != 0) continue;
    const uint8_t *bsp = pak + le32(pak + at + 56);
    const uint32_t offset = le32(bsp + 4), size = le32(bsp + 8);
    char *text = malloc((size_t)size + 1);
    memcpy(text, bsp + offset, size);
    text[size] = '\0';
    entities = qa_q1_parse_entities(text, count);
    free(text);
    break;
  }
  free(pak);
  return entities;
}

/* --- session: scratchpad/mp/session.ts session(pack, map, edition) --- */
static int sentinel_charmer;
typedef struct session {
  q1_world_fixture fx;
  qa_q1_base *base;
  qa_q1mp_mission_pack_arsenal arsenal;
  qa_q1mpm_runtime monsters;
  qa_q1_entity *entities; size_t entity_count;
  qa_vec3 start;
} session;
static bool hooks_charmer(void *ctx, qa_actor_id *out) { (void)out; CHECK(ctx == &sentinel_charmer); return false; }
static bool spawn_standard(const char *c) {
  return strcmp(c, "worldspawn") == 0 || strncmp(c, "monster_", 8) == 0 || strcmp(c, "path_corner") == 0 ||
         strcmp(c, "info_player_start") == 0;
}
/* map NULL: no map entities (always available). */
static bool session_open(session *s, const char *pack, const char *map) {
  memset(s, 0, sizeof *s);
  if (map != NULL) {
    s->entities = map_entities(pack, map, "rerelease", &s->entity_count);
    if (s->entities == NULL) return false;
  }
  qa_q1_options options = q1_world_fixture_options();
  options.edition = "rerelease";
  options.has_teamplay = true;
  options.teamplay = 0;
  options.max_clients = 2;
  options.campaign = strcmp(pack, "hipnotic") == 0 ? "q1:hipnotic" : "q1:rogue";
  options.movement_provider = "q3:movement";
  CHECK(q1_world_fixture_create(&s->fx, options));
  qa_q1_services *game = s->fx.game;
  s->base = qa_q1_register_base(game, NULL);
  (void)qa_q1mp_register_mission_pack_arsenal(game, pack, &s->arsenal);
  const qa_q1mpm_mission_monster_hooks hooks = {&sentinel_charmer, hooks_charmer};
  CHECK(qa_q1mpm_register_mission_pack_monsters(game, s->base, pack, &hooks, &s->monsters) == &s->monsters);
  for (size_t i = 0; i < s->entity_count; i++) {
    const char *classname = qa_q1_entity_value(&s->entities[i], "classname");
    if (classname == NULL) classname = "";
    if (spawn_standard(classname)) qa_q1_spawn_entity(game, qa_q1_create(game, classname, &s->entities[i], NULL));
  }
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, "info_player_start") == 0) { s->start = qa_q1_get_body(game, game->entities[i]).origin; break; }
  return true;
}
static qa_q1_player_state *session_player(session *s) {
  const double slot = (double)s->fx.player_count + 1;
  const qa_owned_actor owner = q1_world_fixture_admit_player(&s->fx, qa_q1_vadd(s->start, (qa_vec3){64 * (slot - 1), 0, 0}), 100);
  return qa_q1_attach_player(s->fx.game, owner, "shotgun", true, NULL);
}
static void session_close(session *s) {
  CHECK(s->fx.ctx_errors == 0);
  qa_q1mpm_runtime_destroy(&s->monsters);
  qa_q1mp_mission_pack_arsenal_destroy(&s->arsenal);
  qa_q1_base_destroy(s->base);
  q1_world_fixture_destroy(&s->fx);
  if (s->entities != NULL) qa_q1_entities_destroy(s->entities, s->entity_count);
}
/* TS spawnSpecies: a fresh monster 128 units from the start, spawned through the game. */
static qa_q1mpm_mission_monster *spawn_species(session *s, const char *classname, double spawnflags) {
  qa_q1_services *game = s->fx.game;
  qa_q1_actor *entity = qa_q1_create(game, classname, NULL, NULL);
  entity->spawnflags = spawnflags;
  qa_q1_set_origin(game, entity, qa_q1_vadd(s->start, (qa_vec3){0, 128, 0}));
  qa_q1_spawn_entity(game, entity);
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_get(&s->monsters, entity->actor);
  CHECK(monster != NULL);
  return monster;
}
static size_t count_class(qa_q1_services *game, const char *classname) {
  size_t count = 0;
  for (size_t i = 0; i < game->entity_count; i++) count += streq(game->entities[i]->classname, classname);
  return count;
}
static size_t count_live_class(qa_q1_services *game, const char *classname, const qa_actor_id *owner) {
  size_t count = 0;
  for (size_t i = 0; i < game->entity_count; i++) {
    const qa_q1_actor *entity = game->entities[i];
    count += streq(entity->classname, classname) && qa_q1_live(game, entity) &&
             (owner == NULL || (entity->has_owner && qa_same_actor(entity->owner, *owner)));
  }
  return count;
}
static bool capture_ok(qa_q1_services *game) {
  qa_q1_foundation_checkpoint checkpoint;
  if (!qa_q1_services_capture(game, &checkpoint)) return false;
  qa_q1_checkpoint_free(&checkpoint);
  return true;
}
static double health_of(session *s, const qa_q1mpm_mission_monster *monster) {
  return qa_q1_health(s->fx.game, monster->base.entity->actor.id);
}
static void set_health(session *s, qa_q1mpm_mission_monster *monster, double health) {
  qa_gameplay_authority_set_health(s->fx.combat, monster->base.entity->actor, health);
}
static size_t sounds_since(session *s, size_t from, const char **out, size_t capacity) {
  size_t count = 0;
  for (size_t i = from; i < s->fx.event_count && count < capacity; i++)
    if (streq(s->fx.events[i].kind, "sound")) out[count++] = s->fx.events[i].path;
  return count;
}

/* "Rogue statues stay frozen and invulnerable until used, and the three untargeted statues stay inert".
 * bun variants.ts:
 *   statues: count=35 triggered=32 skins=true total=108 inertFrames=44,73,73 inertSolid=slidebox,slidebox,slidebox
 *   statue0: class=monster_hell_knight frame=73 rogue_statue=1 damageable=false current=hknight_stand1 next=hknight_stand1
 *   statue start: damageable=false next=rogue_statue_pause current=hknight_stand1
 *   statue use: damageable=true enemyIsPlayer=true rogue_statue=0 useNull=true total=true sounds=statue/see.wav
 * bun direct.ts:
 *   sfx: statue/pain.wav statue/death.wav statue/idle.wav knight/sword1.wav statue/pain.wav statue/death.wav statue/idle.wav
 *   plain sfx: knight/khurt.wav
 *   statue gib -10: false
 *   statue gib -50: true delta=3 added=gib:progs/statgib1.mdl,gib:progs/statgib2.mdl,gib:progs/statgib3.mdl self=monster_knight:progs/h_knight.mdl sounds=player/udeath.wav
 *   plain knight gib -50: true delta=3 model=progs/h_knight.mdl */
static void test_rogue_statues(void) {
  session s;
  if (!session_open(&s, "rogue", "r2m1")) { printf("skip rogue statues (corpus absent)\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s);
  (void)session_player(&s);
  qa_q1mpm_mission_monster *statues[64];
  size_t statue_count = 0, triggered = 0, skins = 0;
  for (size_t i = 0; i < s.monsters.monster_count; i++) {
    qa_q1mpm_mission_monster *monster = s.monsters.monsters[i].monster;
    const char *species = monster->base.spec->species;
    if (!(streq(species, "knight") || streq(species, "hellknight")) || (qa_js_to_int32(monster->base.entity->spawnflags) & 2) == 0) continue;
    if (statue_count < 64) statues[statue_count] = monster;
    statue_count++;
    skins += monster->base.entity->skin == 1;
  }
  CHECK(statue_count == 35);
  CHECK(skins == 35);
  CHECK(game->total_monsters == 108);
  double inert_frames[3];
  size_t inert = 0;
  qa_q1mpm_mission_monster *first = NULL, *knight = NULL, *hellknight = NULL;
  for (size_t i = 0; i < statue_count && i < 64; i++) {
    qa_q1_actor *entity = statues[i]->base.entity;
    if (entity->targetname != NULL && entity->targetname[0] != '\0') {
      triggered++;
      if (first == NULL) first = statues[i];
      if (knight == NULL && streq(statues[i]->base.spec->species, "knight")) knight = statues[i];
      if (hellknight == NULL && streq(statues[i]->base.spec->species, "hellknight")) hellknight = statues[i];
    } else {
      if (inert < 3) inert_frames[inert] = entity->frame;
      CHECK(streq(entity->solid, "slidebox"));
      inert++;
    }
  }
  CHECK(triggered == 32 && inert == 3);
  CHECK(inert == 3 && inert_frames[0] == 44 && inert_frames[1] == 73 && inert_frames[2] == 73);
  CHECK(first != NULL && knight != NULL && hellknight != NULL);
  if (first == NULL || knight == NULL || hellknight == NULL) { session_close(&s); return; }
  qa_q1_actor *entity = first->base.entity;
  CHECK(streq(entity->classname, "monster_hell_knight") && entity->frame == 73);
  CHECK(qa_q1_actor_number(entity, "rogue_statue") == 1 && !qa_q1_actor_damageable(entity));
  CHECK(streq(first->base.current_frame, "hknight_stand1") && streq(first->base.next_frame, "hknight_stand1"));
  const double total = game->total_monsters;
  qa_q1_base_monster_start(&first->base);
  CHECK(!qa_q1_actor_damageable(entity));
  CHECK(streq(first->base.next_frame, "rogue_statue_pause") && streq(first->base.current_frame, "hknight_stand1"));
  const size_t before = s.fx.event_count;
  qa_q1_base_monster_use(&first->base, &player->actor.id);
  const char *sounds[8];
  CHECK(sounds_since(&s, before, sounds, 8) == 1 && streq(sounds[0], "statue/see.wav"));
  CHECK(qa_q1_actor_damageable(entity));
  CHECK(qa_q1_base_monster_enemy(&first->base) != NULL && qa_same_actor(*qa_q1_base_monster_enemy(&first->base), player->actor.id));
  CHECK(qa_q1_actor_number(entity, "rogue_statue") == 0 && entity->use.fn == NULL && entity->use.name == NULL);
  CHECK(game->total_monsters == total);
  CHECK(capture_ok(game));
  /* MissionMonster sfx and gibDeath overrides (TS protected methods called directly). */
  CHECK(streq(qa_q1mpm_monster_sfx(knight, "knight/khurt.wav"), "statue/pain.wav"));
  CHECK(streq(qa_q1mpm_monster_sfx(knight, "knight/kdeath.wav"), "statue/death.wav"));
  CHECK(streq(qa_q1mpm_monster_sfx(knight, "knight/idle.wav"), "statue/idle.wav"));
  CHECK(streq(qa_q1mpm_monster_sfx(knight, "knight/sword1.wav"), "knight/sword1.wav"));
  CHECK(streq(qa_q1mpm_monster_sfx(hellknight, "hknight/pain1.wav"), "statue/pain.wav"));
  CHECK(streq(qa_q1mpm_monster_sfx(hellknight, "hknight/death1.wav"), "statue/death.wav"));
  CHECK(streq(qa_q1mpm_monster_sfx(hellknight, "hknight/idle.wav"), "statue/idle.wav"));
  qa_q1mpm_mission_monster *plain = spawn_species(&s, "monster_knight", 0);
  CHECK(streq(qa_q1mpm_monster_sfx(plain, "knight/khurt.wav"), "knight/khurt.wav"));
  set_health(&s, knight, -10);
  CHECK(!qa_q1mpm_monster_gib_death(knight));
  set_health(&s, knight, -50);
  size_t count = game->entity_count;
  size_t events = s.fx.event_count;
  CHECK(qa_q1mpm_monster_gib_death(knight));
  CHECK(game->entity_count == count + 3);
  if (game->entity_count == count + 3) {
    static const char *const MODELS[3] = {"progs/statgib1.mdl", "progs/statgib2.mdl", "progs/statgib3.mdl"};
    for (size_t i = 0; i < 3; i++)
      CHECK(streq(game->entities[count + i]->classname, "gib") && streq(game->entities[count + i]->model, MODELS[i]));
  }
  CHECK(streq(knight->base.entity->classname, "monster_knight") && streq(knight->base.entity->model, "progs/h_knight.mdl"));
  CHECK(sounds_since(&s, events, sounds, 8) == 1 && streq(sounds[0], "player/udeath.wav"));
  set_health(&s, plain, -50);
  count = game->entity_count;
  CHECK(qa_q1mpm_monster_gib_death(plain));
  CHECK(game->entity_count == count + 3 && streq(plain->base.entity->model, "progs/h_knight.mdl"));
  session_close(&s);
}

/* Statue sounds and gibs through the shared pain/die paths (BaseMonster sfx and gibDeath hooks).
 * bun variants.ts (after statue0 start/use as above):
 *   knight statue pain sounds=statue/pain.wav current=knight_pain1
 *   knight statue die sounds=statue/death.wav current=knight_die1
 *   hellknight statue pain sounds=statue/pain.wav,statue/pain.wav current=hknight_pain1
 *   hellknight statue die sounds=statue/death.wav current=hknight_dieb1
 *   knight statue gib: entitiesDelta=4 sounds=player/udeath.wav classnames=gib:progs/statgib1.mdl,gib:progs/statgib2.mdl,gib:progs/statgib3.mdl self=monster_knight:progs/h_knight.mdl */
static void test_rogue_statue_sounds(void) {
  session s;
  if (!session_open(&s, "rogue", "r2m1")) { printf("skip rogue statue sounds (corpus absent)\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s);
  (void)session_player(&s);
  qa_q1mpm_mission_monster *triggered[64];
  size_t count = 0;
  for (size_t i = 0; i < s.monsters.monster_count && count < 64; i++) {
    qa_q1mpm_mission_monster *monster = s.monsters.monsters[i].monster;
    const char *species = monster->base.spec->species;
    const qa_q1_actor *entity = monster->base.entity;
    if ((streq(species, "knight") || streq(species, "hellknight")) && (qa_js_to_int32(entity->spawnflags) & 2) != 0 &&
        entity->targetname != NULL && entity->targetname[0] != '\0') triggered[count++] = monster;
  }
  CHECK(count == 32);
  if (count != 32) { session_close(&s); return; }
  qa_q1_base_monster_start(&triggered[0]->base);
  qa_q1_base_monster_use(&triggered[0]->base, &player->actor.id);
  qa_q1mpm_mission_monster *pair[2] = {NULL, NULL}, *second_knight = NULL;
  for (size_t i = 0; i < count; i++) {
    if (streq(triggered[i]->base.spec->species, "knight")) {
      if (pair[0] == NULL) pair[0] = triggered[i];
      else if (second_knight == NULL) second_knight = triggered[i];
    } else if (pair[1] == NULL) pair[1] = triggered[i];
  }
  CHECK(pair[0] != NULL && pair[1] != NULL && second_knight != NULL);
  if (pair[0] == NULL || pair[1] == NULL || second_knight == NULL) { session_close(&s); return; }
  static const struct { const char *pain[2]; size_t pains; const char *pain_frame, *death, *death_frame; } EXPECT[2] = {
      {{"statue/pain.wav", NULL}, 1, "knight_pain1", "statue/death.wav", "knight_die1"},
      {{"statue/pain.wav", "statue/pain.wav"}, 2, "hknight_pain1", "statue/death.wav", "hknight_dieb1"}};
  const char *sounds[8];
  for (size_t m = 0; m < 2; m++) {
    qa_q1mpm_mission_monster *monster = pair[m];
    qa_q1_base_monster_use(&monster->base, &player->actor.id);
    qa_q1_actor_set_damageable(monster->base.entity, true);
    monster->base.state->pain_finished = 0;
    s.fx.random_value = 0.1;
    size_t from = s.fx.event_count;
    qa_q1_base_monster_pain(&monster->base, &player->actor.id, 50);
    const size_t pains = sounds_since(&s, from, sounds, 8);
    CHECK(pains == EXPECT[m].pains);
    for (size_t i = 0; i < pains && i < EXPECT[m].pains; i++) CHECK(streq(sounds[i], EXPECT[m].pain[i]));
    CHECK(streq(monster->base.current_frame, EXPECT[m].pain_frame));
    set_health(&s, monster, -10);
    from = s.fx.event_count;
    qa_q1_base_monster_die(&monster->base, &player->actor.id);
    CHECK(sounds_since(&s, from, sounds, 8) == 1 && streq(sounds[0], EXPECT[m].death));
    CHECK(streq(monster->base.current_frame, EXPECT[m].death_frame));
  }
  qa_q1_base_monster_use(&second_knight->base, &player->actor.id);
  const size_t before = game->entity_count, from = s.fx.event_count;
  set_health(&s, second_knight, -50);
  qa_q1_base_monster_die(&second_knight->base, &player->actor.id);
  CHECK(game->entity_count == before + 4);
  CHECK(sounds_since(&s, from, sounds, 8) == 1 && streq(sounds[0], "player/udeath.wav"));
  static const char *const MODELS[3] = {"progs/statgib1.mdl", "progs/statgib2.mdl", "progs/statgib3.mdl"};
  for (size_t i = 0; i < 3 && game->entity_count >= 3; i++) {
    const qa_q1_actor *gib = game->entities[game->entity_count - 3 + i];
    CHECK(streq(gib->classname, "gib") && streq(gib->model, MODELS[i]));
  }
  CHECK(streq(second_knight->base.entity->classname, "monster_knight") && streq(second_knight->base.entity->model, "progs/h_knight.mdl"));
  session_close(&s);
}

/* "Rogue zombies read the lying, ambush and sneaky flags, and sneaky ones escape after their delay".
 * bun variants.ts:
 *   lying: spawnflags=2 solid=none maxz=-16 minz=-24 zstate=1 current=zombie_stand1
 *   lying start: current=zombie_sleep next=zombie_sleep
 *   lying pain: zstate=0 current=zombie_paine12 maxz=40 next=zombie_paine11
 *   ambush: spawnflags=2 solid=slidebox
 *   sneaky: totalDelta=1 solid=none damageable=false current=rogue_cruc1 delay=3 health=60 model=progs/zombie.mdl yawSpeed=20 flags=32 frame=192
 *   sneaky run: next=rogue_cruc1 painFinished=3 time=0
 *   sneaky cruc2: next=zombie_escape1
 *   sneaky escaped: solid=slidebox damageable=true zstate=0 next=zombie_run1 health=60 aimed=true dx=16 dy=0 frame=207 */
static void test_rogue_zombies(void) {
  session s;
  if (!session_open(&s, "rogue", "r2m1")) { printf("skip rogue zombies (corpus absent)\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s);
  (void)session_player(&s);
  qa_q1mpm_mission_monster *lying = spawn_species(&s, "monster_zombie", 2 | 4);
  qa_q1_actor *entity = lying->base.entity;
  CHECK(entity->spawnflags == 2 && streq(entity->solid, "none"));
  CHECK(qa_q1_get_body(game, entity).bounds.max.z == -16 && qa_q1_get_body(game, entity).bounds.min.z == -24);
  CHECK(qa_q1_actor_number(entity, "rogue_zstate") == 1 && streq(lying->base.current_frame, "zombie_stand1"));
  qa_q1_base_monster_start(&lying->base);
  CHECK(streq(lying->base.current_frame, "zombie_sleep") && streq(lying->base.next_frame, "zombie_sleep"));
  qa_q1_base_monster_pain(&lying->base, &player->actor.id, 1);
  CHECK(qa_q1_actor_number(entity, "rogue_zstate") == 0 && streq(lying->base.current_frame, "zombie_paine12"));
  CHECK(qa_q1_get_body(game, entity).bounds.max.z == 40 && streq(lying->base.next_frame, "zombie_paine11"));
  qa_q1mpm_mission_monster *ambush = spawn_species(&s, "monster_zombie", 4);
  CHECK(ambush->base.entity->spawnflags == 2 && streq(ambush->base.entity->solid, "slidebox"));
  const double total = game->total_monsters;
  qa_q1mpm_mission_monster *sneaky = spawn_species(&s, "monster_zombie", 8);
  entity = sneaky->base.entity;
  CHECK(game->total_monsters == total + 1 && streq(entity->solid, "none") && !qa_q1_actor_damageable(entity));
  CHECK(streq(sneaky->base.current_frame, "rogue_cruc1") && entity->delay == 3 && health_of(&s, sneaky) == 60);
  CHECK(streq(entity->model, "progs/zombie.mdl") && entity->yaw_speed == 20 && entity->movement_flags == 32 && entity->frame == 192);
  qa_q1_base_monster_play(&sneaky->base, "zombie_run1");
  CHECK(streq(sneaky->base.next_frame, "rogue_cruc1") && sneaky->base.state->pain_finished == 3 && game->time == 0);
  qa_q1_services_set_time(game, game->time + 3.5);
  qa_q1_base_monster_play(&sneaky->base, "rogue_cruc2");
  CHECK(streq(sneaky->base.next_frame, "zombie_escape1"));
  const qa_vec3 before = qa_q1_get_body(game, entity).origin;
  for (int i = 1; i <= 16; i++) {
    char frame[32];
    snprintf(frame, sizeof frame, "zombie_escape%d", i);
    qa_q1_base_monster_play(&sneaky->base, frame);
  }
  const qa_vec3 after = qa_q1_get_body(game, entity).origin;
  CHECK(streq(entity->solid, "slidebox") && qa_q1_actor_damageable(entity) && qa_q1_actor_number(entity, "rogue_zstate") == 0);
  CHECK(streq(sneaky->base.next_frame, "zombie_run1") && health_of(&s, sneaky) == 60 && entity->aimed_damage);
  CHECK(after.x - before.x == 16 && after.y - before.y == 0 && entity->frame == 207);
  session_close(&s);
}

/* "Rogue boss ogres fire multi-grenades and drop multi-rockets; Spawn pick tiers and split".
 * bun variants.ts:
 *   ogre: boss=2 skin=1
 *   ogre fire: grenade=true vel={"x":0,"y":-600,"z":200} angles={"x":18.434947967529297,"y":270,"z":0} next=2.5
 *   ogre drop: packs=1 / plain: skin=1 flags=3 / normal: boss=0 skin=0 / normal fire: ogre_grenades=1 / normal drop: packs=1
 *   blue: skin=2 painFinished=0 health=100 max=100 mitosis=1 / green: health=60 skin=1 / plainBaby: skin=0 mitosis=0 health=80
 *   mitosis: babies=1 ownerBlue=true health=100 skin=2 blueMitosis=0 childNext=rogue_tbaby_checknew childThink=0.5
 *   second pain: babies=1 / checknew: childLive=false blueMitosis=0 totalDelta=0
 *   tier r=0.2: skin=0 health=80 mitosis=0 / tier r=0.35: skin=1 health=60 mitosis=1 / tier r=0.9: skin=1 health=60 mitosis=1 */
static void test_rogue_ogres_and_spawn(void) {
  session s;
  if (!session_open(&s, "rogue", "r2m1")) { printf("skip rogue ogres (corpus absent)\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s);
  (void)session_player(&s);
  s.fx.random_value = 0.1;
  qa_q1mpm_mission_monster *ogre = spawn_species(&s, "monster_ogre", 0);
  CHECK((qa_js_to_int32(ogre->base.entity->spawnflags) & 2) == 2 && ogre->base.entity->skin == 1);
  qa_q1_base_monster_set_enemy(&ogre->base, &player->actor.id);
  qa_q1_base_monster_play(&ogre->base, "ogre_nail4");
  qa_q1_actor *grenade = NULL;
  for (size_t i = 0; i < game->entity_count; i++)
    if (streq(game->entities[i]->classname, "MultiGrenade") && game->entities[i]->has_owner &&
        qa_same_actor(game->entities[i]->owner, ogre->base.entity->actor.id)) grenade = game->entities[i];
  CHECK(grenade != NULL);
  if (grenade != NULL) {
    const qa_vec3 velocity = qa_q1_get_body(game, grenade).velocity;
    CHECK(velocity.x == 0 && velocity.y == -600 && velocity.z == 200);
    CHECK(grenade->mangle.x == 18.434947967529297 && grenade->mangle.y == 270 && grenade->mangle.z == 0);
    CHECK(grenade->next_think == 2.5);
  }
  size_t packs = count_class(game, "item_backpack");
  qa_q1_base_monster_play(&ogre->base, "ogre_die3");
  CHECK(count_class(game, "item_backpack") == packs + 1);
  qa_q1mpm_mission_monster *plain = spawn_species(&s, "monster_ogre", 1);
  s.fx.random_value = 0.9;
  CHECK(plain->base.entity->skin == 1 && plain->base.entity->spawnflags == 3);
  s.fx.random_value = 0.9;
  qa_q1mpm_mission_monster *normal = spawn_species(&s, "monster_ogre", 0);
  CHECK((qa_js_to_int32(normal->base.entity->spawnflags) & 2) == 0 && normal->base.entity->skin == 0);
  qa_q1_base_monster_set_enemy(&normal->base, &player->actor.id);
  qa_q1_base_monster_play(&normal->base, "ogre_nail4");
  CHECK(count_class(game, "ogre_grenade") == 1);
  packs = count_class(game, "item_backpack");
  qa_q1_base_monster_play(&normal->base, "ogre_die3");
  CHECK(count_class(game, "item_backpack") == packs + 1);
  qa_q1mpm_mission_monster *blue = spawn_species(&s, "monster_tarbaby", 8);
  CHECK(blue->base.entity->skin == 2 && blue->base.state->pain_finished == 0 && health_of(&s, blue) == 100);
  CHECK(blue->base.entity->max_health == 100 && qa_q1_actor_number(blue->base.entity, "rogue_mitosis") == 1);
  qa_q1mpm_mission_monster *green = spawn_species(&s, "monster_tarbaby", 4);
  CHECK(health_of(&s, green) == 60 && green->base.entity->skin == 1);
  qa_q1mpm_mission_monster *plain_baby = spawn_species(&s, "monster_tarbaby", 2);
  CHECK(plain_baby->base.entity->skin == 0 && qa_q1_actor_number(plain_baby->base.entity, "rogue_mitosis") == 0 && health_of(&s, plain_baby) == 80);
  s.fx.random_value = 0.1;
  const size_t babies = count_class(game, "monster_tarbaby");
  qa_q1_base_monster_pain(&blue->base, &player->actor.id, 1);
  CHECK(count_class(game, "monster_tarbaby") == babies + 1);
  qa_q1_actor *child = game->entities[game->entity_count - 1];
  CHECK(streq(child->classname, "monster_tarbaby") && child->has_owner && qa_same_actor(child->owner, blue->base.entity->actor.id));
  CHECK(qa_q1_health(game, child->actor.id) == 100 && child->skin == 2 && qa_q1_actor_number(blue->base.entity, "rogue_mitosis") == 0);
  qa_q1mpm_mission_monster *controller = qa_q1mpm_runtime_get(&s.monsters, child->actor);
  CHECK(controller != NULL && streq(controller->base.next_frame, "rogue_tbaby_checknew"));
  CHECK(child->next_think == 0.5);
  qa_q1_base_monster_pain(&blue->base, &player->actor.id, 1);
  CHECK(count_class(game, "monster_tarbaby") == babies + 1);
  const double total = game->total_monsters;
  if (controller != NULL) qa_q1_base_monster_play(&controller->base, controller->base.next_frame);
  CHECK(!qa_q1_live(game, child) && qa_q1_actor_number(blue->base.entity, "rogue_mitosis") == 0 && game->total_monsters == total);
  static const struct { double random, skin, health, mitosis; } TIERS[3] = {{0.2, 0, 80, 0}, {0.35, 1, 60, 1}, {0.9, 1, 60, 1}};
  for (size_t i = 0; i < 3; i++) {
    s.fx.random_value = TIERS[i].random;
    qa_q1mpm_mission_monster *tarbaby = spawn_species(&s, "monster_tarbaby", 0);
    CHECK(tarbaby->base.entity->skin == TIERS[i].skin && health_of(&s, tarbaby) == TIERS[i].health);
    CHECK(qa_q1_actor_number(tarbaby->base.entity, "rogue_mitosis") == TIERS[i].mitosis);
  }
  session_close(&s);
}

/* "Hipnotic lying zombies wait in zombie_lay and rotfish gib below -20 health".
 * bun variants.ts:
 *   hip lying: count=8 hip_lay=1 / hip lying start: current=zombie_lay next=zombie_lay
 *   hip lying pain: current=zombie_lay12 next=zombie_paine13 / hip lay replay: current=zombie_lay health=60 hip_lay=0
 *   fish -10: delta=0 current=f_death1
 * bun direct.ts:
 *   fish gib -20: false
 *   fish gib -21: true delta=3 added=gib:progs/gib1.mdl,gib:progs/gib2.mdl,gib:progs/gib3.mdl think=0.10000000149011612
 *   hip zombie default gib -5: true delta=3 */
static void test_hipnotic_variants(void) {
  session s;
  if (!session_open(&s, "hipnotic", "hip2m4")) { printf("skip hipnotic variants (corpus absent)\n"); return; }
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s);
  (void)session_player(&s);
  qa_q1mpm_mission_monster *zombie = NULL;
  size_t lying = 0;
  for (size_t i = 0; i < s.monsters.monster_count; i++) {
    qa_q1mpm_mission_monster *monster = s.monsters.monsters[i].monster;
    if (!streq(monster->base.spec->species, "zombie") || (qa_js_to_int32(monster->base.entity->spawnflags) & 5) != 4) continue;
    if (zombie == NULL) zombie = monster;
    lying++;
  }
  CHECK(lying == 8 && zombie != NULL);
  if (zombie == NULL) { session_close(&s); return; }
  CHECK(qa_q1_actor_number(zombie->base.entity, "hip_lay") == 1);
  qa_q1_base_monster_start(&zombie->base);
  CHECK(streq(zombie->base.current_frame, "zombie_lay") && streq(zombie->base.next_frame, "zombie_lay"));
  qa_q1_base_monster_pain(&zombie->base, &player->actor.id, 5);
  CHECK(streq(zombie->base.current_frame, "zombie_lay12") && streq(zombie->base.next_frame, "zombie_paine13"));
  qa_q1_base_monster_play(&zombie->base, "zombie_lay");
  CHECK(streq(zombie->base.current_frame, "zombie_lay") && health_of(&s, zombie) == 60);
  CHECK(qa_q1_actor_number(zombie->base.entity, "hip_lay") == 0);
  set_health(&s, zombie, -5);
  size_t count = game->entity_count;
  CHECK(qa_q1mpm_monster_gib_death(zombie) && game->entity_count == count + 3);
  qa_q1mpm_mission_monster *fish = spawn_species(&s, "monster_fish", 0);
  set_health(&s, fish, -20);
  CHECK(!qa_q1mpm_monster_gib_death(fish));
  set_health(&s, fish, -21);
  count = game->entity_count;
  CHECK(qa_q1mpm_monster_gib_death(fish));
  CHECK(game->entity_count == count + 3);
  if (game->entity_count == count + 3) {
    static const char *const MODELS[3] = {"progs/gib1.mdl", "progs/gib2.mdl", "progs/gib3.mdl"};
    for (size_t i = 0; i < 3; i++)
      CHECK(streq(game->entities[count + i]->classname, "gib") && streq(game->entities[count + i]->model, MODELS[i]));
  }
  CHECK(fish->base.entity->next_think == 0.10000000149011612);
  /* bun variants.ts: fish gib: delta=3 current=f_stand1 sounds=player/udeath.wav think=0.10000000149011612 */
  qa_q1mpm_mission_monster *fish1 = spawn_species(&s, "monster_fish", 0);
  set_health(&s, fish1, -30);
  count = game->entity_count;
  const size_t from = s.fx.event_count;
  qa_q1_base_monster_die(&fish1->base, &player->actor.id);
  const char *sounds[8];
  CHECK(game->entity_count == count + 3 && streq(fish1->base.current_frame, "f_stand1"));
  CHECK(sounds_since(&s, from, sounds, 8) == 1 && streq(sounds[0], "player/udeath.wav"));
  CHECK(fish1->base.entity->next_think == 0.10000000149011612);
  qa_q1mpm_mission_monster *fish2 = spawn_species(&s, "monster_fish", 0);
  set_health(&s, fish2, -10);
  count = game->entity_count;
  qa_q1_base_monster_die(&fish2->base, &player->actor.id);
  CHECK(game->entity_count == count && streq(fish2->base.current_frame, "f_death1"));
  session_close(&s);
}

/* Updated "retail Hipnotic and Rogue monsters admit exact frames" rows with the species variants
 * (definitions may continue into the shared monsterFrames, as the TS check now allows).
 * bun variants.ts:
 *   counts {classic,rerelease} hip2m1: gremlin=6 missionMonsters=68 entities=101 totalMonsters=68 consistent=true
 *   counts {classic,rerelease} r2m1: eel=13 missionMonsters=102 entities=124 totalMonsters=108 consistent=true */
static void test_variant_frames_consistent(void) {
  session s;
  if (!session_open(&s, "rogue", "r2m1")) { printf("skip variant frames (corpus absent)\n"); return; }
  bool consistent = true;
  for (size_t i = 0; i < s.monsters.monster_count; i++) {
    const qa_q1mpm_pack_monster_definition *definition = s.monsters.monsters[i].monster->definition;
    for (size_t f = 0; f < definition->frames.count; f++) {
      const qa_q1_monster_frame *frame = &definition->frames.entries[f].frame;
      bool next = qa_q1_monster_frame_table_has(&definition->frames, frame->next) || qa_q1_monster_frames_has(frame->next);
      for (size_t a = 0; a < definition->action_count && !next; a++) next = streq(definition->actions[a].name, frame->next);
      if (!next) consistent = false;
      for (size_t o = 0; o < frame->operation_count; o++) {
        if (frame->operations[o].kind != QA_Q1_OP_ACTION) continue;
        bool found = false;
        for (size_t a = 0; a < definition->action_count && !found; a++) found = streq(definition->actions[a].name, frame->operations[o].name);
        if (!found) consistent = false;
      }
    }
  }
  CHECK(consistent);
  CHECK(s.monsters.monster_count == 102 && s.fx.game->entity_count == 124 && s.fx.game->total_monsters == 108);
  CHECK(capture_ok(s.fx.game));
  session_close(&s);
}

/* fireRogueLava without lava nails, and the grapple's hook index.
 * bun misc.ts:
 *   lava empty: fired=false messages=1 text=$qc_out_lava_nails center=false args=undefined spikes=0
 *   lava one: fired=true nails=0 spikes=1
 *   grapple fire1: ok=true hooks=1 frame=1 attackFinished=0.10000000149011612
 *   grapple fire2: ok=true hooks=1 frame=2
 *   grapple removed: hooks=0
 *   grapple fire3: ok=true hooks=1 frame=1
 *   grapple foreign: ok=true otherHooks=2 frame=1 */
static void test_lava_and_grapple(void) {
  session s;
  CHECK(session_open(&s, "rogue", NULL));
  qa_q1_services *game = s.fx.game;
  qa_q1_player_state *player = session_player(&s);
  player->weapon = "rogue:lava-nailgun";
  size_t events = s.fx.event_count;
  CHECK(!qa_q1mp_fire_rogue_lava(game, player));
  size_t messages = 0;
  const qa_q1_event *message = NULL;
  for (size_t i = events; i < s.fx.event_count; i++)
    if (streq(s.fx.events[i].kind, "message")) { messages++; message = &s.fx.events[i]; }
  CHECK(messages == 1 && message != NULL && streq(message->text, "$qc_out_lava_nails") && !message->center && message->arg_count == 0);
  CHECK(count_class(game, "lava_spike") == 0);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "rogue:ammo/lava-nails"; entry.count = 1; entry.capacity = 200;
  qa_shared_inventory_table_configure(s.fx.inventory, player->actor, entry, NULL, NULL);
  CHECK(qa_q1mp_fire_rogue_lava(game, player));
  CHECK(qa_shared_inventory_table_count(s.fx.inventory, player->actor.id, "rogue:ammo/lava-nails") == 0);
  CHECK(count_class(game, "lava_spike") == 1);
  qa_q1mp_rogue_grapple *grapple = s.arsenal.grapple;
  CHECK(grapple != NULL);
  if (grapple == NULL) { session_close(&s); return; }
  CHECK(grapple->weapon.fire(grapple->weapon.ctx, game, player));
  CHECK(count_live_class(game, "hook", &player->actor.id) == 1 && player->weapon_frame == 1);
  CHECK(player->attack_finished == 0.10000000149011612);
  CHECK(grapple->weapon.fire(grapple->weapon.ctx, game, player));
  CHECK(count_live_class(game, "hook", &player->actor.id) == 1 && player->weapon_frame == 2);
  qa_q1_actor *hook = NULL;
  for (size_t i = 0; i < game->entity_count; i++)
    if (streq(game->entities[i]->classname, "hook") && qa_q1_live(game, game->entities[i])) hook = game->entities[i];
  CHECK(hook != NULL);
  if (hook != NULL) qa_q1_remove(game, hook);
  CHECK(count_live_class(game, "hook", &player->actor.id) == 0);
  player->weapon_frame = 0;
  CHECK(grapple->weapon.fire(grapple->weapon.ctx, game, player));
  CHECK(count_live_class(game, "hook", &player->actor.id) == 1 && player->weapon_frame == 1);
  /* A hook made outside fire after the index was built is not in the index. */
  qa_q1_player_state *other = session_player(&s);
  qa_q1_actor *foreign = qa_q1_create(game, "hook", NULL, NULL);
  foreign->has_owner = true; foreign->owner = other->actor.id;
  CHECK(grapple->weapon.fire(grapple->weapon.ctx, game, other));
  CHECK(count_live_class(game, "hook", &other->actor.id) == 2 && other->weapon_frame == 1);
  session_close(&s);
}

/* Q1MissionPackRuntime.suicide (Rogue ClientKill limit through RogueTeams; Hipnotic always allows).
 * bun misc.ts:
 *   suicide 4: msg=$qc_suicided_too_much center=false / suicide 5: msg=$qc_suicided_too_much center=false
 *   suicide results=true,true,true,true,false,false
 *   hipnotic suicide=true,true,true,true,true */
static void test_suicide(void) {
  static const char *const PACKS[2] = {"rogue", "hipnotic"};
  for (size_t p = 0; p < 2; p++) {
    q1_world_fixture fx;
    qa_q1_options options = q1_world_fixture_options();
    options.has_teamplay = true;
    options.teamplay = 0;
    options.max_clients = 2;
    options.campaign = p == 0 ? "q1:rogue" : "q1:hipnotic";
    options.movement_provider = "q3:movement";
    CHECK(q1_world_fixture_create(&fx, options));
    qa_q1_base *base = qa_q1_register_base(fx.game, NULL);
    qa_q1mp_mission_pack_runtime runtime;
    CHECK(qa_q1mp_register_q1_mission_pack(fx.game, base, PACKS[p], NULL, &runtime) == &runtime);
    const qa_owned_actor owner = q1_world_fixture_admit_player(&fx, (qa_vec3){0, 0, 0}, 100);
    qa_q1_player_state *player = qa_q1_attach_player(fx.game, owner, "shotgun", true, NULL);
    for (int i = 0; i < (p == 0 ? 6 : 5); i++) {
      const size_t events = fx.event_count;
      const bool allowed = qa_q1mp_runtime_suicide(&runtime, player->actor.id);
      size_t messages = 0;
      for (size_t e = events; e < fx.event_count; e++)
        if (streq(fx.events[e].kind, "message")) {
          messages++;
          CHECK(streq(fx.events[e].text, "$qc_suicided_too_much") && !fx.events[e].center);
        }
      if (p == 0) CHECK(allowed == (i < 4) && messages == (i < 4 ? 0u : 1u));
      else CHECK(allowed && messages == 0);
    }
    CHECK(fx.ctx_errors == 0);
    qa_q1mp_mission_pack_runtime_destroy(&runtime);
    qa_q1_base_destroy(base);
    q1_world_fixture_destroy(&fx);
  }
}

/* world/hipnotic-holes.ts: ten holes at most, aimed with velocityAngles of the mirrored normal.
 * bun holes.ts:
 *   holes: live=10 firstX=100.80000305175781 angles={"x":53.130104064941406,"y":180,"z":0} origin={"x":109.80000305175781,"y":0,"z":0} think=300
 *   down: angles={"x":270,"y":0,"z":0} */
static void test_hipnotic_bullet_holes(void) {
  session s;
  CHECK(session_open(&s, "hipnotic", NULL));
  qa_q1_services *game = s.fx.game;
  qa_q1mp_register_hipnotic_bullet_holes(game);
  qa_q1_player_state *player = session_player(&s);
  CHECK(game->source_bullet_hole.fn != NULL);
  if (game->source_bullet_hole.fn == NULL) { session_close(&s); return; }
  for (int i = 0; i < 11; i++)
    game->source_bullet_hole.fn(game->source_bullet_hole.ctx, player->actor, (qa_vec3){100 + i, 0, 0}, (qa_vec3){0.6, 0, 0.8});
  qa_q1_actor *first = NULL, *last = NULL;
  size_t live = 0;
  for (size_t i = 0; i < game->entity_count; i++)
    if (streq(game->entities[i]->classname, "bullethole") && qa_q1_live(game, game->entities[i])) {
      if (first == NULL) first = game->entities[i];
      last = game->entities[i];
      live++;
    }
  CHECK(live == 10 && first != NULL && last != NULL);
  if (first != NULL && last != NULL) {
    CHECK(qa_q1_get_body(game, first).origin.x == 100.80000305175781);
    const qa_q1_body body = qa_q1_get_body(game, last);
    CHECK(body.angles.x == 53.130104064941406 && body.angles.y == 180 && body.angles.z == 0);
    CHECK(body.origin.x == 109.80000305175781 && body.origin.y == 0 && body.origin.z == 0);
    CHECK(last->next_think == 300);
  }
  game->source_bullet_hole.fn(game->source_bullet_hole.ctx, player->actor, (qa_vec3){0, 0, 0}, (qa_vec3){0, 0, -1});
  last = game->entities[game->entity_count - 1];
  CHECK(streq(last->classname, "bullethole"));
  const qa_vec3 angles = qa_q1_get_body(game, last).angles;
  CHECK(angles.x == 270 && angles.y == 0 && angles.z == 0);
  session_close(&s);
}

/* --- Rogue CTF: world hooks with a sentinel ctx, recording names, colours and the hook log --- */
typedef struct ctf_hooks {
  int magic;
  qa_actor_id actors[3]; const char *names[3]; double colors[3]; size_t count;
  char log[16][48]; size_t log_count;
  /* Message texts copied at emit time (the source may format them in a stack buffer). */
  qa_actor_id message_players[64]; char messages[64][128]; size_t message_count;
  void *emit_ctx; void (*emit)(void *ctx, qa_q1_event event);
} ctf_hooks;
#define CTF_MAGIC 0x43544631
static ctf_hooks *ctf_of(void *ctx) {
  ctf_hooks *hooks = ctx;
  CHECK(hooks != NULL && hooks->magic == CTF_MAGIC);
  return hooks;
}
static int ctf_index(ctf_hooks *hooks, qa_actor_id actor) {
  for (size_t i = 0; i < hooks->count; i++) if (qa_same_actor(hooks->actors[i], actor)) return (int)i;
  return -1;
}
static const char *ctf_name(ctf_hooks *hooks, qa_actor_id actor) {
  const int at = ctf_index(hooks, actor);
  return at < 0 ? "?" : hooks->names[at];
}
static void ctf_log(ctf_hooks *hooks, const char *text) {
  if (hooks->log_count < 16) snprintf(hooks->log[hooks->log_count++], sizeof hooks->log[0], "%s", text);
}
static double ctf_team_color(void *ctx, qa_actor_id actor) {
  ctf_hooks *hooks = ctf_of(ctx);
  const int at = ctf_index(hooks, actor);
  return at < 0 ? 0 : hooks->colors[at];
}
static void ctf_set_team_color(void *ctx, qa_actor_id actor, double team) {
  ctf_hooks *hooks = ctf_of(ctx);
  char text[48];
  snprintf(text, sizeof text, "setColor %s %s", ctf_name(hooks, actor), QA_JS_NUMBER(team));
  ctf_log(hooks, text);
  const int at = ctf_index(hooks, actor);
  if (at >= 0) hooks->colors[at] = team;
}
static void ctf_add_frags(void *ctx, qa_actor_id actor, double delta) {
  ctf_hooks *hooks = ctf_of(ctx);
  char text[48];
  snprintf(text, sizeof text, "frags %s %s", ctf_name(hooks, actor), QA_JS_NUMBER(delta));
  ctf_log(hooks, text);
}
static double ctf_frags(void *ctx, qa_actor_id actor) { (void)actor; (void)ctf_of(ctx); return 0; }
static void ctf_disconnect(void *ctx, qa_actor_id actor) {
  ctf_hooks *hooks = ctf_of(ctx);
  char text[48];
  snprintf(text, sizeof text, "disconnect %s", ctf_name(hooks, actor));
  ctf_log(hooks, text);
}
static double ctf_player_frame(void *ctx, qa_actor_id actor) { (void)actor; (void)ctf_of(ctx); return 0; }
static const char *ctf_player_name(void *ctx, qa_actor_id actor) { return ctf_name(ctf_of(ctx), actor); }
/* The host emit, wrapped: records message texts, then forwards to the fixture's emit with its own ctx. */
static ctf_hooks *ctf_emit_hooks;
static void ctf_emit(void *ctx, qa_q1_event event) {
  ctf_hooks *hooks = ctf_emit_hooks;
  if (streq(event.kind, "message") && hooks->message_count < 64) {
    hooks->message_players[hooks->message_count] = event.player;
    snprintf(hooks->messages[hooks->message_count++], sizeof hooks->messages[0], "%s", event.text);
  }
  CHECK(ctx == hooks->emit_ctx);
  hooks->emit(ctx, event);
}
/* "P1:text | P2:text" for the messages recorded since message index from. */
static void ctf_messages(ctf_hooks *hooks, size_t from, char *out, size_t size) {
  size_t used = 0;
  out[0] = '\0';
  for (size_t i = from; i < hooks->message_count && used < size; i++) {
    const int written = snprintf(out + used, size - used, "%s%s:%s", used == 0 ? "" : " | ", ctf_name(hooks, hooks->message_players[i]), hooks->messages[i]);
    if (written < 0) return;
    used += (size_t)written;
  }
}
static void ctf_log_text(ctf_hooks *hooks, char *out, size_t size) {
  size_t used = 0;
  out[0] = '\0';
  for (size_t i = 0; i < hooks->log_count && used < size; i++) {
    const int written = snprintf(out + used, size - used, "%s%s", i == 0 ? "" : ",", hooks->log[i]);
    if (written < 0) return;
    used += (size_t)written;
  }
  hooks->log_count = 0;
}
static qa_q1_actor *team_state(qa_q1_services *game, qa_actor_id actor) {
  for (size_t i = 0; i < game->entity_count; i++)
    if (streq(game->entities[i]->classname, "rogue_team_state") && game->entities[i]->has_owner &&
        qa_same_actor(game->entities[i]->owner, actor)) return game->entities[i];
  return NULL;
}
static void set_team_field(qa_q1_services *game, qa_actor_id actor, const char *key, const char *value) {
  qa_q1_actor *state = team_state(game, actor);
  CHECK(state != NULL);
  if (state != NULL) (void)qa_q1_actor_field_set(state, key, value);
}

/* world/rogue-teams.ts (04:58): TeamSetSkin, returned-flag, defence and assist broadcasts, and the
 * bad-colour-sense broadcast, through Q1MissionPackRuntime (deathmatch 1, teamplay 4).
 * bun ctf.ts:
 *   spawned: msgs=P1:"You have been assigned to the Blue team.\n" | P2:"You have been assigned to the Red team.\n" log=setColor P1 14,setColor P2 5 p1team=14 p2team=5
 *   skin: other=1 msgs=X:"You have been assigned to the Blue team.\n" log=setColor X 14
 *   returned: msgs=P1:"P2 returned the Red flag!\n" | P2:"P2 returned the Red flag!\n" | P1:"$qc_enemy_flag_returned_base" | P2:"$qc_your_flag_returned_base" log=frags P2 1 cnt=0 p2returned=0
 *   assists: msgs=P1:"P2 defends Red's flag carrier against an agressive enemy\n" | P2:(same) | P1:"P2 defends the Red flag\n" | P2:(same) | P1:(same) | P2:(same) log=frags P2 2,frags P2 1,frags P2 1
 *   capture: msgs=P1:"P2 captured the flag!\n" | P2:(same) | P1:"$qc_your_flag_captured" | P1:"P2 gets an assist for returning the Red flag!\n" | P2:(same) | P1:"P2 gets an assist for fragging the flag carrier!\n" | P2:(same) | P2:"$qc_your_team_captured" log=frags P2 15,frags P2 1,frags P2 2
 *   colors: msgs=P1:"$qc_color_games" | P1:"P1 has bad color sense\n" | P2:"P1 has bad color sense\n" log=disconnect P1 */
static void test_rogue_ctf_broadcasts(void) {
  q1_world_fixture fx;
  qa_q1_options options = q1_world_fixture_options();
  options.has_teamplay = true;
  options.teamplay = 0;
  options.max_clients = 2;
  options.campaign = "q1:rogue";
  options.movement_provider = "q3:movement";
  CHECK(q1_world_fixture_create(&fx, options));
  qa_q1_services *game = fx.game;
  qa_q1_base *base = qa_q1_register_base(game, NULL);
  game->options.deathmatch = 1;
  game->options.teamplay = 4;
  static ctf_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.magic = CTF_MAGIC;
  hooks.emit = game->host.emit; hooks.emit_ctx = game->host.ctx;
  ctf_emit_hooks = &hooks;
  game->host.emit = ctf_emit;
  qa_q1mp_mission_pack_options pack_options;
  memset(&pack_options, 0, sizeof pack_options);
  pack_options.world.ctx = &hooks;
  pack_options.world.team_color = ctf_team_color;
  pack_options.world.set_team_color = ctf_set_team_color;
  pack_options.world.add_frags = ctf_add_frags;
  pack_options.world.frags = ctf_frags;
  pack_options.world.disconnect = ctf_disconnect;
  pack_options.world.player_frame = ctf_player_frame;
  pack_options.world.player_name = ctf_player_name;
  static qa_q1mp_mission_pack_runtime runtime;
  CHECK(qa_q1mp_register_q1_mission_pack(game, base, "rogue", &pack_options, &runtime) == &runtime);
  const qa_owned_actor o1 = q1_world_fixture_admit_player(&fx, (qa_vec3){0, 0, 0}, 100);
  qa_q1_player_state *p1 = qa_q1_attach_player(game, o1, "shotgun", true, NULL);
  const qa_owned_actor o2 = q1_world_fixture_admit_player(&fx, (qa_vec3){64, 0, 0}, 100);
  qa_q1_player_state *p2 = qa_q1_attach_player(game, o2, "shotgun", true, NULL);
  hooks.actors[0] = p1->actor.id; hooks.names[0] = "P1"; hooks.colors[0] = 5;
  hooks.actors[1] = p2->actor.id; hooks.names[1] = "P2"; hooks.colors[1] = 14;
  hooks.count = 2;
  char messages[1024], log[256];
  size_t from = hooks.message_count;
  qa_q1mp_runtime_player_spawned(&runtime, p1->actor.id);
  qa_q1mp_runtime_player_spawned(&runtime, p2->actor.id);
  ctf_messages(&hooks, from, messages, sizeof messages);
  ctf_log_text(&hooks, log, sizeof log);
  CHECK(streq(messages, "P1:You have been assigned to the Blue team.\n | P2:You have been assigned to the Red team.\n"));
  CHECK(streq(log, "setColor P1 14,setColor P2 5"));
  CHECK(qa_q1mp_teams_team(runtime.world.teams, &p1->actor.id) == 14 && qa_q1mp_teams_team(runtime.world.teams, &p2->actor.id) == 5);
  qa_q1_actor *other = qa_q1_create(game, "info_null", NULL, NULL);
  hooks.actors[2] = other->actor.id; hooks.names[2] = "X"; hooks.colors[2] = 5; hooks.count = 3;
  from = hooks.message_count;
  qa_q1mp_runtime_player_spawned(&runtime, other->actor.id);
  ctf_messages(&hooks, from, messages, sizeof messages);
  ctf_log_text(&hooks, log, sizeof log);
  CHECK(other->skin == 1 && streq(messages, "X:You have been assigned to the Blue team.\n") && streq(log, "setColor X 14"));
  /* Returned flag: a lying red flag touched by its own (red) player P2. */
  qa_q1_actor *flag = qa_q1_create(game, "item_flag_team1", NULL, NULL);
  (void)qa_q1_actor_field_set(flag, "team", "5");
  (void)qa_q1_actor_field_set(flag, "cnt", "2");
  (void)qa_q1_actor_field_set(flag, "noise1", "misc/flagret.wav");
  flag->solid = "trigger";
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = (qa_vec3){32, 0, 0};
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 74}};
  qa_q1_set_body(game, flag, &patch);
  flag->touch = qa_q1_callback_registry_touch(game->named, flag, "rogue:flag_touch");
  from = hooks.message_count;
  qa_q1_call_touch(game, flag, flag->touch, p2->actor.id, NULL, NULL);
  ctf_messages(&hooks, from, messages, sizeof messages);
  ctf_log_text(&hooks, log, sizeof log);
  CHECK(streq(messages, "P1:P2 returned the Red flag!\n | P2:P2 returned the Red flag!\n | P1:$qc_enemy_flag_returned_base | P2:$qc_your_flag_returned_base"));
  CHECK(streq(log, "frags P2 1") && qa_q1_actor_number(flag, "cnt") == 0);
  CHECK(team_state(game, p2->actor.id) != NULL && qa_q1_actor_number(team_state(game, p2->actor.id), "ctf_lastreturnedflag") == 0);
  flag->solid = "trigger";
  /* Assists: P2 (red) kills P1 (blue) right after P1 hurt the red carrier; the red flag lies nearby. */
  set_team_field(game, p1->actor.id, "ctf_lasthurtcarrier", "0");
  from = hooks.message_count;
  qa_q1mp_runtime_player_died(&runtime, p1->actor.id, &p2->actor.id);
  ctf_messages(&hooks, from, messages, sizeof messages);
  ctf_log_text(&hooks, log, sizeof log);
  CHECK(streq(messages, "P1:P2 defends Red's flag carrier against an agressive enemy\n | P2:P2 defends Red's flag carrier against an agressive enemy\n"
                        " | P1:P2 defends the Red flag\n | P2:P2 defends the Red flag\n | P1:P2 defends the Red flag\n | P2:P2 defends the Red flag\n"));
  CHECK(streq(log, "frags P2 2,frags P2 1,frags P2 1"));
  /* Capture: P2 carries the blue flag (bit 2) and touches its own flag at base. */
  set_team_field(game, p2->actor.id, "ctf_flags", "2");
  set_team_field(game, p2->actor.id, "ctf_lastfraggedcarrier", "0");
  from = hooks.message_count;
  qa_q1_call_touch(game, flag, flag->touch, p2->actor.id, NULL, NULL);
  ctf_messages(&hooks, from, messages, sizeof messages);
  ctf_log_text(&hooks, log, sizeof log);
  CHECK(streq(messages, "P1:P2 captured the flag!\n | P2:P2 captured the flag!\n | P1:$qc_your_flag_captured"
                        " | P1:P2 gets an assist for returning the Red flag!\n | P2:P2 gets an assist for returning the Red flag!\n"
                        " | P1:P2 gets an assist for fragging the flag carrier!\n | P2:P2 gets an assist for fragging the flag carrier!\n"
                        " | P2:$qc_your_team_captured"));
  CHECK(streq(log, "frags P2 15,frags P2 1,frags P2 2"));
  /* Bad colour sense: P1 (blue) turns red with four suicides counted. */
  hooks.colors[0] = 5;
  set_team_field(game, p1->actor.id, "ctf_flags", "0");
  set_team_field(game, p1->actor.id, "suicide_count", "4");
  from = hooks.message_count;
  qa_q1mp_teams_frame(runtime.world.teams, p1->actor.id);
  ctf_messages(&hooks, from, messages, sizeof messages);
  ctf_log_text(&hooks, log, sizeof log);
  CHECK(streq(messages, "P1:$qc_color_games | P1:P1 has bad color sense\n | P2:P1 has bad color sense\n"));
  CHECK(streq(log, "disconnect P1"));
  CHECK(fx.ctx_errors == 0);
  qa_q1mp_mission_pack_runtime_destroy(&runtime);
  qa_q1_base_destroy(base);
  q1_world_fixture_destroy(&fx);
}

int main(void) {
  test_rogue_statues();
  test_rogue_statue_sounds();
  test_rogue_zombies();
  test_rogue_ogres_and_spawn();
  test_hipnotic_variants();
  test_variant_frames_consistent();
  test_lava_and_grapple();
  test_suicide();
  test_hipnotic_bullet_holes();
  test_rogue_ctf_broadcasts();
  printf("test_q1_sync_mp: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

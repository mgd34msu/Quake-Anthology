/*
 * Byte checks of the Q2 player / rerelease checkpoint codecs over the content
 * types (src/persistence/q2-players.ts, q2-rerelease-state.ts). Expected
 * bytes come from TS under bun (Q2PlayerState/createQ2PlayerRules plus the
 * Q2Players.capture literal; Q2RereleasePlayers.captureRerelease(); the
 * Q2RereleaseModule.capture literal), and the decode paths are checked against
 * the TS restore-then-capture / decode-then-encode bytes.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/player/checkpoint.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/persistence/q2_players.h"
#include "qa/persistence/q2_rerelease_state.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

static const char TS_PLAYERS[] =
  "{\"version\":1,\"corpseIndex\":2,\"deathAnimation\":1,\"painAnimation\":0,\"rules\":{\"password\":\"pw\",\"spe"
  "ctatorPassword\":\"\",\"maxSpectators\":4,\"cheats\":false,\"timeLimitMinutes\":0,\"fragLimit\":20,\"mapList\""
  ":[\"base1\",\"base2\"],\"mapListShuffle\":false,\"nextMap\":\"\",\"spawnPoint\":\"\",\"floodMessages\":4,\"flo"
  "odSeconds\":4,\"floodWaitSeconds\":10,\"rollSpeed\":200,\"rollAngle\":2,\"runPitch\":0.002,\"runRoll\":0.005,\""
  "bobUp\":0.005,\"bobPitch\":0.002,\"bobRoll\":0.002,\"gunOffset\":{\"x\":0,\"y\":0,\"z\":0}},\"intermission\":{"
  "\"kind\":\"intermission\",\"map\":\"base2\",\"landmark\":{\"player\":{\"slot\":3,\"generation\":1},\"name\":\""
  "lm\",\"relativeOrigin\":{\"x\":1,\"y\":2,\"z\":3},\"relativeVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"relativeVie"
  "wAngles\":{\"x\":0,\"y\":90,\"z\":0}},\"started\":4,\"exit\":false},\"players\":[{\"actor\":{\"slot\":3,\"gene"
  "ration\":1},\"state\":{\"slot\":3,\"enteredAt\":1.5,\"useQ2Weapons\":true,\"useQ2Inventory\":true,\"spawnInven"
  "tory\":[{\"item\":\"q2:weapon_blaster\",\"count\":1,\"capacity\":1}],\"userinfo\":\"\\\\name\\\\Hero\\\\skin\\"
  "\\female/athena\",\"name\":\"Hero\",\"skin\":\"female/athena\",\"gender\":\"female\",\"fov\":90,\"hand\":\"lef"
  "t\",\"spectator\":false,\"requestedSpectator\":false,\"connected\":true,\"dead\":false,\"gibbed\":false,\"nocl"
  "ip\":false,\"god\":false,\"notarget\":false,\"score\":7,\"ping\":0,\"respawnTime\":0,\"airFinished\":0,\"nextD"
  "rownTime\":0,\"drownDamage\":2,\"oldWaterLevel\":0,\"breatherSound\":0,\"painDebounce\":0,\"damageBlood\":0,\""
  "damageArmor\":0,\"damagePowerArmor\":0,\"damageKnockback\":0,\"damageFrom\":{\"x\":1,\"y\":-2,\"z\":0.5},\"dam"
  "ageBlend\":{\"x\":0,\"y\":0,\"z\":0},\"damageAlpha\":0,\"bonusAlpha\":0,\"damagePitch\":0,\"damageRoll\":0,\"d"
  "amageTime\":0,\"powerArmorTime\":0,\"fallTime\":0,\"fallValue\":0,\"helpChanged\":2,\"gameHelpChanged\":0,\"la"
  "ndmarkFreeFall\":false,\"landmarkNoiseTime\":0,\"oldVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"oldViewAngles\":{\""
  "x\":0,\"y\":0,\"z\":0},\"killerYaw\":0,\"buttons\":0,\"latchedButtons\":0,\"weaponThunk\":false,\"bobTime\":0,"
  "\"bobMove\":0,\"event\":\"q2:footstep\",\"animationPriority\":0,\"animationEnd\":39,\"animationDuck\":false,\""
  "animationRun\":false,\"loopSound\":\"weapons/hyperbl.wav\",\"selectedItem\":\"q2:weapon_shotgun\",\"jumpHeld\""
  ":true,\"showScores\":false,\"showInventory\":false,\"showHelp\":false,\"chaseTarget\":{\"slot\":5,\"generation"
  "\":2},\"coopRespawn\":{\"health\":80,\"maximumHealth\":100,\"armor\":{\"regular\":{\"kind\":\"none\"},\"powere"
  "d\":{\"kind\":\"none\"}},\"inventory\":[{\"item\":\"q2:ammo_shells\",\"count\":10,\"capacity\":100}],\"weapon\""
  ":\"shotgun\",\"selectedItem\":\"q2:weapon_shotgun\",\"score\":7,\"flags\":16,\"powerCubes\":0},\"floodTimes\":"
  "[2,3.25],\"floodLockUntil\":0}}]}"
  ;
static const char TS_RERELEASE[] =
  "{\"version\":1,\"options\":{\"coopSquadRespawn\":true,\"coopInstancedItems\":true,\"coopLives\":true,\"coopNum"
  "Lives\":3,\"deathmatchForceRespawn\":false,\"deathmatchNoFallDamage\":false,\"deathmatchSpawnFarthest\":true,\""
  "deathmatchForceRespawnTime\":0,\"deathmatchAllowExit\":false,\"coopPlayerCollision\":false,\"autoSaveMinimumTi"
  "me\":60},\"coopRestartTime\":12.5,\"deadlyKillBox\":false,\"intermissionFlags\":0,\"intermissionFadeUntil\":9,"
  "\"intermissionCamera\":{\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"angles\":{\"x\":0,\"y\":0,\"z\":0}},\"intermiss"
  "ionCameraSet\":false,\"players\":[{\"actor\":{\"slot\":3,\"generation\":1},\"state\":{\"seat\":1,\"socialId\":"
  "\"steam:42\",\"spawned\":false,\"gameHelp1Changed\":0,\"gameHelp2Changed\":0,\"helpChanged\":0,\"helpTime\":0,"
  "\"invisibilityUntil\":0,\"invisibilityFadeUntil\":0,\"slimeDebounce\":0,\"animationTime\":0,\"flashTime\":0,\""
  "flashes\":0,\"lastDamageUntil\":0,\"lastFiringUntil\":0,\"lives\":4,\"coopRespawnState\":\"waiting\",\"flashli"
  "ght\":false,\"fog\":{\"fog\":{\"density\":0,\"color\":{\"x\":0,\"y\":0,\"z\":0},\"skyFactor\":0},\"heightFog\""
  ":{\"startColor\":{\"x\":0,\"y\":0,\"z\":0},\"startDistance\":0,\"endColor\":{\"x\":0,\"y\":0,\"z\":0},\"endDis"
  "tance\":0,\"falloff\":0,\"density\":0}},\"wantedFog\":{\"fog\":{\"density\":0.5,\"color\":{\"x\":0,\"y\":0,\"z"
  "\":0},\"skyFactor\":0},\"heightFog\":{\"startColor\":{\"x\":0,\"y\":0,\"z\":0},\"startDistance\":0,\"endColor\""
  ":{\"x\":0,\"y\":0,\"z\":0},\"endDistance\":0,\"density\":0.25,\"falloff\":2}},\"fogTransition\":0,\"bobSkip\":"
  "false,\"autoSwitch\":2,\"autoShield\":-1,\"dogtag\":\"tag\",\"impactDelta\":0,\"onLadder\":false,\"grappleRele"
  "asedUntil\":0,\"grappleAttached\":false,\"slowViewAngles\":{\"x\":0,\"y\":0,\"z\":0},\"quakeTime\":0,\"windSou"
  "ndTime\":0,\"awaitingRespawn\":false,\"respawnTimeout\":0,\"pendingLandmark\":{\"name\":\"lm\",\"relativeOrigi"
  "n\":{\"x\":1,\"y\":0,\"z\":0},\"relativeVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"relativeViewAngles\":{\"x\":0,\""
  "y\":45,\"z\":0}},\"helpLocation\":{\"x\":0,\"y\":0,\"z\":0},\"helpImage\":\"friend\",\"helpPoints\":[{\"x\":1,"
  "\"y\":2,\"z\":3}],\"helpIndex\":0,\"helpDrawTime\":0,\"helpMarkerUntil\":0}}],\"squadSpawns\":[{\"actor\":{\"s"
  "lot\":3,\"generation\":1},\"origin\":{\"x\":4,\"y\":5,\"z\":6},\"angles\":{\"x\":0,\"y\":90,\"z\":0}}]}"
  ;
static const char TS_MODULE[] =
  "{\"version\":1,\"worldFog\":{\"fog\":{\"density\":0.5,\"color\":{\"x\":0,\"y\":0,\"z\":0},\"skyFactor\":0},\"h"
  "eightFog\":{\"startColor\":{\"x\":0,\"y\":0,\"z\":0},\"startDistance\":0,\"endColor\":{\"x\":0,\"y\":0,\"z\":0"
  "},\"endDistance\":0,\"density\":0,\"falloff\":0}},\"story\":\"story\",\"sky\":{\"name\":\"space1\",\"rotation\""
  ":2,\"autoRotate\":true,\"axis\":{\"x\":0,\"y\":0,\"z\":1}},\"poi\":{\"actor\":{\"slot\":7,\"generation\":0},\""
  "origin\":{\"x\":1,\"y\":1,\"z\":1},\"image\":\"friend\",\"dynamic\":null},\"poiStage\":1,\"lastAutoSave\":30,\""
  "campaign\":{\"crossUnitFlags\":4,\"visitedMaps\":[\"base1\"],\"levels\":[{\"map\":\"base1\",\"name\":\"Outer\""
  ",\"visitOrder\":1,\"totalSecrets\":2,\"foundSecrets\":1,\"totalMonsters\":9,\"killedMonsters\":3,\"time\":60}]"
  ",\"mission\":{\"primary\":\"go\",\"secondary\":\"\",\"primaryChanges\":1,\"secondaryChanges\":0}},\"healthBars"
  "\":[null,{\"controller\":{\"slot\":1,\"generation\":0},\"target\":{\"slot\":2,\"generation\":0},\"deadUntil\":"
  "null}],\"healthTargets\":[{\"controller\":{\"slot\":1,\"generation\":0},\"target\":{\"slot\":2,\"generation\":"
  "0}}],\"pickedUpBy\":[{\"actor\":{\"slot\":8,\"generation\":0},\"slots\":[0,2]}],\"triggerSoundTimes\":[{\"acto"
  "r\":{\"slot\":9,\"generation\":0},\"time\":3}],\"q64\":{\"eyes\":[{\"actor\":{\"slot\":10,\"generation\":0},\""
  "state\":{\"neutralAngles\":{\"x\":0,\"y\":0,\"z\":0},\"eyePosition\":{\"x\":0,\"y\":0,\"z\":8},\"visionCone\":"
  "0.5}}],\"cameras\":[{\"actor\":{\"slot\":11,\"generation\":0},\"state\":{\"speed\":4.5,\"distance\":100,\"rema"
  "ining\":100,\"angles\":{\"x\":0,\"y\":0,\"z\":0}}}],\"dummies\":[{\"actor\":{\"slot\":12,\"generation\":0},\"s"
  "tate\":{\"fading\":false,\"fadeDuration\":0,\"fadeRemaining\":0}}]},\"lights\":[{\"actor\":{\"slot\":13,\"gene"
  "ration\":0},\"active\":true}],\"goals\":{\"goals\":null,\"goalNumber\":0}}"
  ;
static const char TS_PLAYERS_RESTORED[] =
  "{\"version\":1,\"corpseIndex\":2,\"deathAnimation\":1,\"painAnimation\":0,\"rules\":{\"password\":\"pw\",\"spe"
  "ctatorPassword\":\"\",\"maxSpectators\":4,\"cheats\":false,\"timeLimitMinutes\":0,\"fragLimit\":20,\"mapList\""
  ":[\"base1\",\"base2\"],\"mapListShuffle\":false,\"nextMap\":\"\",\"spawnPoint\":\"\",\"floodMessages\":4,\"flo"
  "odSeconds\":4,\"floodWaitSeconds\":10,\"rollSpeed\":200,\"rollAngle\":2,\"runPitch\":0.002,\"runRoll\":0.005,\""
  "bobUp\":0.005,\"bobPitch\":0.002,\"bobRoll\":0.002,\"gunOffset\":{\"x\":0,\"y\":0,\"z\":0}},\"intermission\":{"
  "\"kind\":\"intermission\",\"map\":\"base2\",\"started\":4,\"exit\":false,\"landmark\":{\"player\":{\"slot\":3,"
  "\"generation\":1},\"name\":\"lm\",\"relativeOrigin\":{\"x\":1,\"y\":2,\"z\":3},\"relativeVelocity\":{\"x\":0,\""
  "y\":0,\"z\":0},\"relativeViewAngles\":{\"x\":0,\"y\":90,\"z\":0}}},\"players\":[{\"actor\":{\"slot\":3,\"gener"
  "ation\":1},\"state\":{\"slot\":3,\"enteredAt\":1.5,\"useQ2Weapons\":true,\"useQ2Inventory\":true,\"spawnInvent"
  "ory\":[{\"item\":\"q2:weapon_blaster\",\"count\":1,\"capacity\":1}],\"userinfo\":\"\\\\name\\\\Hero\\\\skin\\\\"
  "female/athena\",\"name\":\"Hero\",\"skin\":\"female/athena\",\"gender\":\"female\",\"fov\":90,\"hand\":\"left\""
  ",\"spectator\":false,\"requestedSpectator\":false,\"connected\":true,\"dead\":false,\"gibbed\":false,\"noclip\""
  ":false,\"god\":false,\"notarget\":false,\"score\":7,\"ping\":0,\"respawnTime\":0,\"airFinished\":0,\"nextDrown"
  "Time\":0,\"drownDamage\":2,\"oldWaterLevel\":0,\"breatherSound\":0,\"painDebounce\":0,\"damageBlood\":0,\"dama"
  "geArmor\":0,\"damagePowerArmor\":0,\"damageKnockback\":0,\"damageFrom\":{\"x\":1,\"y\":-2,\"z\":0.5},\"damageB"
  "lend\":{\"x\":0,\"y\":0,\"z\":0},\"damageAlpha\":0,\"bonusAlpha\":0,\"damagePitch\":0,\"damageRoll\":0,\"damag"
  "eTime\":0,\"powerArmorTime\":0,\"fallTime\":0,\"fallValue\":0,\"helpChanged\":2,\"gameHelpChanged\":0,\"landma"
  "rkFreeFall\":false,\"landmarkNoiseTime\":0,\"oldVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"oldViewAngles\":{\"x\":"
  "0,\"y\":0,\"z\":0},\"killerYaw\":0,\"buttons\":0,\"latchedButtons\":0,\"weaponThunk\":false,\"bobTime\":0,\"bo"
  "bMove\":0,\"event\":\"q2:footstep\",\"animationPriority\":0,\"animationEnd\":39,\"animationDuck\":false,\"anim"
  "ationRun\":false,\"loopSound\":\"weapons/hyperbl.wav\",\"selectedItem\":\"q2:weapon_shotgun\",\"jumpHeld\":fal"
  "se,\"showScores\":false,\"showInventory\":false,\"showHelp\":false,\"chaseTarget\":{\"slot\":5,\"generation\":"
  "2},\"coopRespawn\":{\"health\":80,\"maximumHealth\":100,\"armor\":{\"regular\":{\"kind\":\"none\"},\"powered\""
  ":{\"kind\":\"none\"}},\"inventory\":[{\"item\":\"q2:ammo_shells\",\"count\":10,\"capacity\":100}],\"weapon\":\""
  "shotgun\",\"selectedItem\":\"q2:weapon_shotgun\",\"score\":7,\"flags\":16,\"powerCubes\":0},\"floodTimes\":[2,"
  "3.25],\"floodLockUntil\":0}}]}"
  ;
static const char TS_MODULE_RESTORED[] =
  "{\"version\":1,\"worldFog\":{\"fog\":{\"density\":0.5,\"color\":{\"x\":0,\"y\":0,\"z\":0},\"skyFactor\":0},\"h"
  "eightFog\":{\"startColor\":{\"x\":0,\"y\":0,\"z\":0},\"startDistance\":0,\"endColor\":{\"x\":0,\"y\":0,\"z\":0"
  "},\"endDistance\":0,\"falloff\":0,\"density\":0}},\"story\":\"story\",\"sky\":{\"name\":\"space1\",\"rotation\""
  ":2,\"autoRotate\":true,\"axis\":{\"x\":0,\"y\":0,\"z\":1}},\"poi\":{\"actor\":{\"slot\":7,\"generation\":0},\""
  "origin\":{\"x\":1,\"y\":1,\"z\":1},\"image\":\"friend\",\"dynamic\":null},\"poiStage\":1,\"lastAutoSave\":30,\""
  "campaign\":{\"crossUnitFlags\":4,\"visitedMaps\":[\"base1\"],\"levels\":[{\"map\":\"base1\",\"name\":\"Outer\""
  ",\"visitOrder\":1,\"totalSecrets\":2,\"foundSecrets\":1,\"totalMonsters\":9,\"killedMonsters\":3,\"time\":60}]"
  ",\"mission\":{\"primary\":\"go\",\"secondary\":\"\",\"primaryChanges\":1,\"secondaryChanges\":0}},\"healthBars"
  "\":[null,{\"controller\":{\"slot\":1,\"generation\":0},\"target\":{\"slot\":2,\"generation\":0},\"deadUntil\":"
  "null}],\"healthTargets\":[{\"controller\":{\"slot\":1,\"generation\":0},\"target\":{\"slot\":2,\"generation\":"
  "0}}],\"pickedUpBy\":[{\"actor\":{\"slot\":8,\"generation\":0},\"slots\":[0,2]}],\"triggerSoundTimes\":[{\"acto"
  "r\":{\"slot\":9,\"generation\":0},\"time\":3}],\"q64\":{\"eyes\":[{\"actor\":{\"slot\":10,\"generation\":0},\""
  "state\":{\"neutralAngles\":{\"x\":0,\"y\":0,\"z\":0},\"eyePosition\":{\"x\":0,\"y\":0,\"z\":8},\"visionCone\":"
  "0.5}}],\"cameras\":[{\"actor\":{\"slot\":11,\"generation\":0},\"state\":{\"remaining\":100,\"distance\":100,\""
  "speed\":4.5,\"angles\":{\"x\":0,\"y\":0,\"z\":0}}}],\"dummies\":[{\"actor\":{\"slot\":12,\"generation\":0},\"s"
  "tate\":{\"fadeRemaining\":0,\"fadeDuration\":0,\"fading\":false}}]},\"lights\":[{\"actor\":{\"slot\":13,\"gene"
  "ration\":0},\"active\":true}],\"goals\":{\"goals\":null,\"goalNumber\":0}}"
  ;
static const char TS_RERELEASE_RESTORED[] =
  "{\"version\":1,\"options\":{\"coopSquadRespawn\":true,\"coopInstancedItems\":true,\"coopLives\":true,\"coopNum"
  "Lives\":3,\"deathmatchForceRespawn\":false,\"deathmatchNoFallDamage\":false,\"deathmatchSpawnFarthest\":true,\""
  "deathmatchForceRespawnTime\":0,\"deathmatchAllowExit\":false,\"coopPlayerCollision\":false,\"autoSaveMinimumTi"
  "me\":60},\"coopRestartTime\":12.5,\"deadlyKillBox\":false,\"intermissionFlags\":0,\"intermissionFadeUntil\":9,"
  "\"intermissionCamera\":{\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"angles\":{\"x\":0,\"y\":0,\"z\":0}},\"intermiss"
  "ionCameraSet\":false,\"players\":[{\"actor\":{\"slot\":3,\"generation\":1},\"state\":{\"seat\":1,\"socialId\":"
  "\"steam:42\",\"spawned\":false,\"gameHelp1Changed\":0,\"gameHelp2Changed\":0,\"helpChanged\":0,\"helpTime\":0,"
  "\"invisibilityUntil\":0,\"invisibilityFadeUntil\":0,\"slimeDebounce\":0,\"animationTime\":0,\"flashTime\":0,\""
  "flashes\":0,\"lastDamageUntil\":0,\"lastFiringUntil\":0,\"lives\":4,\"coopRespawnState\":\"waiting\",\"flashli"
  "ght\":false,\"fog\":{\"fog\":{\"density\":0,\"color\":{\"x\":0,\"y\":0,\"z\":0},\"skyFactor\":0},\"heightFog\""
  ":{\"startColor\":{\"x\":0,\"y\":0,\"z\":0},\"startDistance\":0,\"endColor\":{\"x\":0,\"y\":0,\"z\":0},\"endDis"
  "tance\":0,\"falloff\":0,\"density\":0}},\"wantedFog\":{\"fog\":{\"density\":0.5,\"color\":{\"x\":0,\"y\":0,\"z"
  "\":0},\"skyFactor\":0},\"heightFog\":{\"startColor\":{\"x\":0,\"y\":0,\"z\":0},\"startDistance\":0,\"endColor\""
  ":{\"x\":0,\"y\":0,\"z\":0},\"endDistance\":0,\"falloff\":2,\"density\":0.25}},\"fogTransition\":0,\"bobSkip\":"
  "false,\"autoSwitch\":2,\"autoShield\":-1,\"dogtag\":\"tag\",\"impactDelta\":0,\"onLadder\":false,\"grappleRele"
  "asedUntil\":0,\"grappleAttached\":false,\"slowViewAngles\":{\"x\":0,\"y\":0,\"z\":0},\"quakeTime\":0,\"windSou"
  "ndTime\":0,\"awaitingRespawn\":false,\"respawnTimeout\":0,\"pendingLandmark\":{\"name\":\"lm\",\"relativeOrigi"
  "n\":{\"x\":1,\"y\":0,\"z\":0},\"relativeVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"relativeViewAngles\":{\"x\":0,\""
  "y\":45,\"z\":0}},\"helpLocation\":{\"x\":0,\"y\":0,\"z\":0},\"helpImage\":\"friend\",\"helpPoints\":[{\"x\":1,"
  "\"y\":2,\"z\":3}],\"helpIndex\":0,\"helpDrawTime\":0,\"helpMarkerUntil\":0}}],\"squadSpawns\":[{\"actor\":{\"s"
  "lot\":3,\"generation\":1},\"origin\":{\"x\":4,\"y\":5,\"z\":6},\"angles\":{\"x\":0,\"y\":90,\"z\":0}}]}"
  ;

static bool bytes_are(uint8_t *bytes, size_t length, const char *expected) {
  bool same = bytes != NULL && length == strlen(expected) && memcmp(bytes, expected, length) == 0;
  if (!same && bytes != NULL && getenv("Q2P_DEBUG")) fprintf(stderr, "got      %.*s\nexpected %s\n", (int)length, (const char *)bytes, expected);
  free(bytes);
  return same;
}
static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static qa_saved_actor_id saved(double slot, double generation) { qa_saved_actor_id a = {slot, generation}; return a; }

static void test_players(void) {
  qa_q2_base_player_state state;
  qa_q2_base_player_state_init(&state, 3, 1.5);
  snprintf(state.userinfo, sizeof state.userinfo, "\\name\\Hero\\skin\\female/athena");
  snprintf(state.name, sizeof state.name, "Hero");
  snprintf(state.skin, sizeof state.skin, "female/athena");
  state.gender = "female"; state.hand = "left"; state.score = 7;
  state.event = "q2:footstep"; state.loop_sound = "weapons/hyperbl.wav"; state.selected_item = "q2:weapon_shotgun";
  qa_inventory_entry spawn = {0};
  spawn.item = "q2:weapon_blaster"; spawn.count = 1; spawn.capacity = 1;
  state.spawn_inventory = malloc(sizeof spawn); *state.spawn_inventory = spawn; state.spawn_inventory_count = 1;
  state.flood_times = malloc(2 * sizeof *state.flood_times);
  state.flood_times[0] = 2; state.flood_times[1] = 3.25; state.flood_time_count = 2; state.flood_time_cap = 2;
  state.damage_from = v3(1, -2, 0.5); state.help_changed = 2; state.jump_held = true;
  qa_inventory_entry shells = {0};
  shells.item = "q2:ammo_shells"; shells.count = 10; shells.capacity = 100;
  qa_q2_base_player_carry carry = {0};
  carry.health = 80; carry.maximum_health = 100;
  carry.armor.regular.kind = QA_REGULAR_ARMOR_NONE; carry.armor.powered.kind = "none";
  carry.inventory = &shells; carry.inventory_count = 1;
  carry.weapon = "shotgun"; carry.selected_item = "q2:weapon_shotgun"; carry.score = 7; carry.flags = 16;
  qa_q2_base_player_carry_set(&state.coop_respawn, carry);
  state.has_coop_respawn = true;

  qa_q2_base_player_checkpoint_entry entry = {0};
  entry.actor = saved(3, 1);
  qa_q2_base_player_state_checkpoint_capture(&state, &entry.state);
  entry.state.has_chase_target = true; entry.state.chase_target = saved(5, 2);
  static const char *const maps[] = {"base1", "base2"};
  qa_q2_base_player_rules changes = {0};
  changes.password = "pw"; changes.map_list = maps; changes.map_list_count = 2; changes.frag_limit = 20;
  qa_q2_base_players_checkpoint players = {0};
  players.version = 1; players.corpse_index = 2; players.death_animation = 1; players.pain_animation = 0;
  players.rules = qa_q2_create_player_rules(&changes, QA_Q2_RULE_PASSWORD | QA_Q2_RULE_MAP_LIST | QA_Q2_RULE_FRAG_LIMIT);
  players.intermission.kind = "intermission"; players.intermission.map = "base2";
  players.intermission.started = 4; players.intermission.exit = false;
  players.intermission.has_landmark = true; players.intermission.landmark_first = true;
  players.intermission.landmark.player = saved(3, 1); players.intermission.landmark.name = "lm";
  players.intermission.landmark.relative_origin = v3(1, 2, 3);
  players.intermission.landmark.relative_view_angles = v3(0, 90, 0);
  players.players = &entry; players.player_count = 1;

  size_t length = 0;
  { uint8_t *bytes = qa_encode_q2_players_checkpoint(&players, &length); CHECK(bytes_are(bytes, length, TS_PLAYERS)); }
  /* decode, then encode what a restore would capture again */
  length = strlen(TS_PLAYERS);
  qa_persist_decoded owner = {0};
  qa_q2_base_players_checkpoint decoded;
  CHECK(qa_decode_q2_players_checkpoint((const uint8_t *)TS_PLAYERS, length, &decoded, &owner));
  CHECK(decoded.player_count == 1 && strcmp(decoded.players[0].state.name, "Hero") == 0);
  CHECK(decoded.players[0].state.help_changed == 2 && !decoded.players[0].state.jump_held);
  CHECK(!decoded.intermission.landmark_first && decoded.rules.map_list_count == 2);
  { uint8_t *bytes = qa_encode_q2_players_checkpoint(&decoded, &length); CHECK(bytes_are(bytes, length, TS_PLAYERS_RESTORED)); }
  qa_persist_decoded_free(&owner);
  CHECK(owner.root == NULL && owner.block_count == 0);

  qa_q2_base_player_state_checkpoint_clear(&entry.state);
  qa_q2_base_player_state_destroy(&state);
}

static qa_q2_player_movement movement(void *c, qa_actor_id a) { (void)c; (void)a; qa_q2_player_movement m = {0}; return m; }
static void emit(void *c, qa_q2_player_event e) { (void)c; (void)e; }
static void rr_emit(void *c, qa_q2_rerelease_event e) { (void)c; (void)e; }

static void test_rerelease_players(void) {
  qa_q2_player_hooks hooks = {0};
  hooks.movement = movement; hooks.emit = emit;
  qa_q2_rerelease_hooks rr = {0};
  rr.emit = rr_emit;
  qa_q2_rerelease_options_changes options = {0};
  options.has_coop_lives = true; options.v.coop_lives = true;
  options.has_coop_num_lives = true; options.v.coop_num_lives = 3;
  qa_q2_rerelease_players players;
  CHECK(qa_q2_rerelease_players_init(&players, NULL, NULL, hooks, rr, &options, NULL, 0));
  qa_q2rr_player_extra_state *extra = malloc(sizeof *extra);
  qa_q2rr_player_extra_state_init(extra, 1, "steam:42");
  extra->lives = 4; snprintf(extra->dogtag, sizeof extra->dogtag, "tag");
  extra->coop_respawn_state = QA_Q2_COOP_WAITING; extra->auto_switch = 2;
  extra->help_points = malloc(sizeof *extra->help_points);
  extra->help_points[0] = v3(1, 2, 3); extra->help_point_count = 1; extra->help_point_cap = 1;
  extra->pending_landmark.has = true; extra->pending_landmark.name = "lm";
  extra->pending_landmark.relative_origin = v3(1, 0, 0); extra->pending_landmark.relative_view_angles = v3(0, 45, 0);
  /* q2RereleaseFogFields({fog_density 0.5, heightfog_falloff 2, heightfog_density 0.25}) */
  extra->wanted_fog.fog.density = 0.5;
  extra->wanted_fog.height_fog.falloff = 2; extra->wanted_fog.height_fog.density = 0.25;
  extra->wanted_fog.height_fog.density_first = true;
  qa_actor_id actor = {0}; actor.slot = 3; actor.generation = 1;
  CHECK(qa_q2rr_players_set_extra(&players, actor, extra));
  players.squad_spawns = malloc(sizeof *players.squad_spawns); players.squad_cap = 1; players.squad_count = 1;
  memset(players.squad_spawns, 0, sizeof *players.squad_spawns);
  players.squad_spawns[0].actor = actor;
  players.squad_spawns[0].origin = v3(4, 5, 6); players.squad_spawns[0].angles = v3(0, 90, 0);
  players.coop_restart_time = 12.5;
  players.has_intermission_fade_until = true; players.intermission_fade_until = 9;
  players.has_intermission_camera = true; players.intermission_camera_origin = v3(1, 2, 3);
  qa_q2_rerelease_players_checkpoint checkpoint = qa_q2_rerelease_players_capture(&players);
  size_t length = 0;
  { uint8_t *bytes = qa_encode_q2_rerelease_players_checkpoint(&checkpoint, &length); CHECK(bytes_are(bytes, length, TS_RERELEASE)); }
  qa_persist_decoded owner = {0};
  qa_q2_rerelease_players_checkpoint decoded;
  CHECK(qa_decode_q2_rerelease_players_checkpoint((const uint8_t *)TS_RERELEASE, strlen(TS_RERELEASE), &decoded, &owner));
  CHECK(decoded.player_count == 1 && decoded.players[0].state.lives == 4 && strcmp(decoded.players[0].state.dogtag, "tag") == 0);
  CHECK(decoded.players[0].state.coop_respawn_state == QA_Q2_COOP_WAITING && decoded.players[0].state.help_point_count == 1);
  CHECK(decoded.squad_count == 1 && decoded.squad_spawns[0].origin.y == 5 && decoded.options.coop_num_lives == 3);
  /* TS restoreRerelease assigns onto the class instance, so the next capture
   * keeps class key order; heightFog comes back in reader order. */
  { uint8_t *bytes = qa_encode_q2_rerelease_players_checkpoint(&decoded, &length); CHECK(bytes_are(bytes, length, TS_RERELEASE_RESTORED)); }
  qa_persist_decoded_free(&owner);
  qa_q2_rerelease_players_checkpoint_clear(&checkpoint);
  qa_q2_rerelease_players_destroy(&players);
}

static void test_module(void) {
  qa_q2_rerelease_module_checkpoint module;
  memset(&module, 0, sizeof module);
  module.version = 1;
  module.world_fog.fog.density = 0.5; module.world_fog.height_fog.density_first = true;
  module.story = "story";
  module.sky.name = "space1"; module.sky.rotation = 2; module.sky.auto_rotate = true; module.sky.axis = v3(0, 0, 1);
  module.has_poi = true; module.poi.actor = saved(7, 0); module.poi.origin = v3(1, 1, 1); module.poi.image = "friend";
  module.poi_stage = 1; module.last_auto_save = 30;
  const char *visited[] = {"base1"};
  qa_q2_rerelease_level_entry level = {0};
  level.map = "base1"; level.name = "Outer"; level.visit_order = 1; level.total_secrets = 2; level.found_secrets = 1;
  level.total_monsters = 9; level.killed_monsters = 3; level.time = 60;
  module.campaign.cross_unit_flags = 4; module.campaign.visited_maps = visited; module.campaign.visited_count = 1;
  module.campaign.levels = &level; module.campaign.level_count = 1;
  module.campaign.mission.primary = "go"; module.campaign.mission.secondary = ""; module.campaign.mission.primary_changes = 1;
  qa_q2rr_saved_healthbar bars[2];
  memset(bars, 0, sizeof bars);
  bars[1].has = true; bars[1].controller = saved(1, 0); bars[1].target = saved(2, 0);
  module.health_bars = bars; module.health_bar_count = 2;
  qa_q2rr_saved_pair pair = {saved(1, 0), saved(2, 0)};
  module.health_targets = &pair; module.health_target_count = 1;
  const double slots[] = {0, 2};
  qa_q2rr_saved_slots picked = {saved(8, 0), slots, 2};
  module.picked_up_by = &picked; module.picked_count = 1;
  qa_q2rr_saved_time sound = {saved(9, 0), 3};
  module.trigger_sound_times = &sound; module.trigger_sound_count = 1;
  qa_q2rr_eye_entry eye = {saved(10, 0), {v3(0, 0, 0), v3(0, 0, 8), 0.5}};
  qa_q2rr_camera_entry camera = {saved(11, 0), {100, 100, 4.5, v3(0, 0, 0), false}};
  qa_q2rr_dummy_entry dummy = {saved(12, 0), {0, 0, false, false}};
  module.q64.eyes = &eye; module.q64.eye_count = 1;
  module.q64.cameras = &camera; module.q64.camera_count = 1;
  module.q64.dummies = &dummy; module.q64.dummy_count = 1;
  qa_q2rr_saved_light light = {saved(13, 0), true};
  module.lights = &light; module.light_count = 1;
  module.goals.goals = NULL; module.goals.goal_number = 0;
  size_t length = 0;
  { uint8_t *bytes = qa_encode_q2_rerelease_module_checkpoint(&module, &length); CHECK(bytes_are(bytes, length, TS_MODULE)); }
  /* decode keeps the reader key orders that TS restore keeps (structuredClone of
   * fog and q64 states); the outer literals are rebuilt by capture */
  qa_persist_decoded owner = {0};
  qa_q2_rerelease_module_checkpoint decoded;
  CHECK(qa_decode_q2_rerelease_module_checkpoint((const uint8_t *)TS_MODULE, strlen(TS_MODULE), &decoded, &owner));
  CHECK(decoded.campaign.level_count == 1 && strcmp(decoded.campaign.levels[0].name, "Outer") == 0);
  CHECK(decoded.q64.camera_count == 1 && decoded.q64.cameras[0].state.reader_order && decoded.q64.cameras[0].state.speed == 4.5);
  CHECK(decoded.picked_count == 1 && decoded.picked_up_by[0].slot_count == 2 && decoded.picked_up_by[0].slots[1] == 2);
  CHECK(decoded.goals.goals == NULL && !decoded.health_bars[0].has && decoded.health_bars[1].target.slot == 2);
  { uint8_t *bytes = qa_encode_q2_rerelease_module_checkpoint(&decoded, &length); CHECK(bytes_are(bytes, length, TS_MODULE_RESTORED)); }
  qa_persist_decoded_free(&owner);
}

int main(void) {
  test_players();
  test_rerelease_players();
  test_module();
  printf("test_persistence_q2_players: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}

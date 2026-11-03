/*
 * Byte identity of the Q2 checkpoint codecs with the TypeScript encoders, for
 * checkpoints built as the content capture functions build them, plus a
 * decode / re-encode round trip. Expected bytes: bun, running
 * src/persistence/q2-weapons.ts encodeQ2WeaponsCheckpoint and
 * q2-foundation.ts encodeQ2FoundationCheckpoint on objects shaped as
 * Q2Weapons.capture() and Q2EntityServices.capture() produce them (a
 * Q2WeaponState("blaster"); a Q2Entity with saveQ2Attack of a
 * Q2EntityServices.attack()-shaped provenance).
 * GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_weapons.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); } } while (0)

static bool bytes_are(const uint8_t *bytes, size_t length, const char *expected) {
  bool same = bytes != NULL && length == strlen(expected) && memcmp(bytes, expected, length) == 0;
  if (!same && getenv("BYTES_DEBUG") != NULL && bytes != NULL) fprintf(stderr, "got %.*s\n", (int)length, (const char *)bytes);
  return same;
}

static const char WEAPONS[] =
    "{\"formatVersion\":3,\"silencerCharges\":[{\"actor\":{\"slot\":1,\"generation\":2},\"charges\":3}],\"sourceRules\""
    ":\"base\",\"registered\":[\"blaster\"],\"fallbackOrder\":null,\"states\":[{\"actor\":{\"slot\":1,\"generation\":2},\""
    "state\":{\"primaryHandoff\":\"active\",\"weapon\":\"blaster\",\"lastWeapon\":null,\"pending\":null,\"phase\":\"ready"
    "\",\"frame\":3,\"thinkTime\":0,\"fireFinished\":0,\"fireBuffered\":false,\"latchedAttack\":false,\"machinegunSho"
    "ts\":0,\"emptySoundTime\":0,\"handReservation\":{\"kind\":\"none\"},\"grenadeTime\":0,\"grenadeFinished\":0,\"gren"
    "adeBlewUp\":false,\"kickOrigin\":{\"x\":0,\"y\":0,\"z\":0},\"kickAngles\":{\"x\":0,\"y\":0,\"z\":0},\"kickTime\":0,\"kic"
    "kUntil\":0,\"kickDuration\":0.2,\"loopSound\":\"\",\"viewModel\":null,\"viewSkin\":0,\"lastFiringTime\":0,\"source"
    "Firing\":false,\"gunRate\":10}}],\"inputs\":[{\"actor\":{\"slot\":1,\"generation\":2},\"input\":{\"attack\":false,\""
    "latchedAttack\":false,\"holster\":false,\"angles\":{\"x\":0,\"y\":0,\"z\":0},\"ducked\":false,\"spectator\":false,\""
    "notarget\":false,\"hand\":\"right\",\"animatePlayer\":false,\"quadUntil\":0,\"doubleUntil\":0,\"quadFireUntil\":0"
    ",\"haste\":false,\"noStackDouble\":false,\"instantSwitch\":false,\"quickSwitch\":false,\"infiniteAmmo\":false,"
    "\"playersCollide\":true,\"gravity\":800,\"weaponThunk\":false}}],\"blasterCauses\":[{\"actor\":{\"slot\":1,\"gen"
    "eration\":2},\"meansOfDeath\":1}]}";

static void test_weapons(void) {
  qa_saved_actor_id actor = {1, 2};
  qa_q2w_charge_entry charge = {actor, 3};
  const char *registered[] = {"blaster"};
  qa_q2w_state_entry state;
  memset(&state, 0, sizeof state);
  state.actor = actor;
  qa_q2w_state_init(&state.state, "blaster");
  state.state.phase = QA_Q2W_READY;
  state.state.frame = 3;
  qa_q2w_input_entry input;
  memset(&input, 0, sizeof input);
  input.actor = actor;
  input.input.hand = QA_Q2W_HAND_RIGHT;
  input.input.players_collide = true;
  input.input.gravity = 800;
  qa_q2w_cause_entry cause = {actor, 1};
  qa_q2w_weapons_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.format_version = 3;
  checkpoint.silencer_charges = &charge; checkpoint.silencer_charge_count = 1;
  checkpoint.source_rules = "base";
  checkpoint.registered = registered; checkpoint.registered_count = 1;
  checkpoint.states = &state; checkpoint.state_count = 1;
  checkpoint.inputs = &input; checkpoint.input_count = 1;
  checkpoint.blaster_causes = &cause; checkpoint.blaster_cause_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_weapons_checkpoint(&checkpoint, &length);
  CHECK(bytes_are(bytes, length, WEAPONS));
  qa_persist_decoded owner = {0};
  qa_q2w_weapons_checkpoint decoded;
  CHECK(qa_decode_q2_weapons_checkpoint(bytes, length, &decoded, &owner));
  size_t again_length = 0;
  uint8_t *again = qa_encode_q2_weapons_checkpoint(&decoded, &again_length);
  /* TS re-encodes a decoded state in readQ2WeaponState's key order (primaryHandoff, handReservation,
     sourceFiring, weapon, ...); Q2Weapons.restore rebuilds a Q2WeaponState (class field order) before any
     capture, so only the capture order reaches a save. The C codec keeps one order: the capture order. */
  CHECK(bytes_are(again, again_length, WEAPONS));
  CHECK(decoded.states[0].state.phase == QA_Q2W_READY && decoded.states[0].state.kick_duration == 0.2);
  free(again);
  qa_persist_decoded_free(&owner);
  free(bytes);
}

static const char FOUNDATION[] =
    "{\"version\":1,\"nextSourceSlot\":4,\"sequence\":6,\"counters\":{\"totalSecrets\":0,\"foundSecrets\":0,\"totalGoa"
    "ls\":0,\"foundGoals\":0,\"totalMonsters\":1,\"killedMonsters\":0,\"serverFlags\":0},\"freedSlots\":[{\"slot\":2,\""
    "time\":0.5}],\"entities\":[{\"actor\":{\"slot\":3,\"generation\":0},\"sourceSlot\":3,\"spawn\":{\"classname\":\"mons"
    "ter_soldier\",\"ordinal\":4,\"values\":[{\"key\":\"classname\",\"value\":\"monster_soldier\"},{\"key\":\"target\",\"va"
    "lue\":\"t1\"}]},\"values\":{\"classname\":\"monster_soldier\",\"target\":\"t1\",\"targetname\":\"\",\"killtarget\":\"\",\""
    "combatTarget\":\"\",\"deathTarget\":\"\",\"healthTarget\":\"\",\"itemTarget\":\"\",\"message\":\"\",\"model\":\"\",\"model2\""
    ":\"\",\"model3\":\"\",\"model4\":\"\",\"spawnflags\":2,\"delay\":0,\"wait\":0,\"speed\":0,\"accel\":0,\"decel\":0,\"damage\""
    ":0,\"damageRadius\":0,\"radiusDamage\":0,\"count\":0,\"maxHealth\":20,\"viewHeight\":0,\"frame\":0,\"oldFrame\":-1"
    ",\"scale\":1,\"alpha\":1,\"skin\":0,\"effects\":0,\"renderFlags\":0,\"flags\":0,\"serverFlags\":0,\"lightLevel\":128"
    ",\"powerCubes\":0,\"timestamp\":0,\"noise\":\"\",\"sound\":\"\",\"volume\":0,\"attenuation\":0,\"random\":0,\"map\":\"\",\""
    "style\":0,\"transitionStarted\":false,\"itemDropped\":true,\"clipMask\":100663299,\"projectile\":false,\"dodge"
    "able\":false,\"laserImmune\":false,\"damageableTarget\":false,\"visible\":true,\"solid\":\"box\",\"motion\":\"step"
    "\",\"gravity\":1,\"gravityVector\":{\"x\":0,\"y\":0,\"z\":-1},\"angularVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"movedir\":{"
    "\"x\":0,\"y\":0,\"z\":0},\"pos1\":{\"x\":0,\"y\":0,\"z\":0},\"pos2\":{\"x\":0,\"y\":0,\"z\":0},\"nextThink\":1.5},\"links\":{\""
    "activator\":null,\"enemy\":{\"slot\":1,\"generation\":2},\"owner\":null,\"goal\":null,\"teamMaster\":null,\"teamCh"
    "ain\":null,\"chain\":null,\"beam\":null,\"beam2\":null,\"proboscus\":null},\"lastAttack\":{\"sequence\":5,\"time\":"
    "{\"kind\":\"seconds\",\"value\":2},\"weapon\":null,\"weaponProvider\":\"q2:game\",\"combatProvider\":\"q2:combat\",\""
    "inventoryProvider\":\"q2:inventory\",\"movementProvider\":\"q2:movement\",\"damagePowerupOwner\":\"q2:game\",\"c"
    "ause\":{\"kind\":\"q2\",\"meansOfDeath\":4,\"damageFlags\":0},\"attacker\":{\"slot\":1,\"generation\":2},\"inflictor"
    "\":{\"slot\":3,\"generation\":0}},\"callbacks\":{\"think\":\"monster_think\",\"prethink\":null,\"postthink\":null,\""
    "use\":null,\"touch\":null,\"pain\":null,\"die\":null,\"blocked\":null}}]}";

static void test_foundation(void) {
  qa_q2_spawn_fields spawn;
  memset(&spawn, 0, sizeof spawn);
  spawn.ordinal = 4;
  CHECK(qa_q2_spawn_set(&spawn, "classname", "monster_soldier"));
  CHECK(qa_q2_spawn_set(&spawn, "target", "t1"));
  spawn.classname = "monster_soldier";
  qa_owned_actor owned;
  memset(&owned, 0, sizeof owned);
  owned.id.slot = 3;
  qa_q2_entity entity;
  qa_q2_entity_init(&entity, owned, spawn);
  entity.target = "t1";
  entity.spawnflags = 2; entity.max_health = 20; entity.solid = "box"; entity.motion = "step"; entity.item_dropped = true;
  entity.has_next_think = true; entity.next_think = 1.5;
  qa_actor_id attacker, inflictor;
  memset(&attacker, 0, sizeof attacker); memset(&inflictor, 0, sizeof inflictor);
  attacker.slot = 1; attacker.generation = 2; inflictor.slot = 3;
  qa_attack_provenance attack;
  memset(&attack, 0, sizeof attack);
  attack.sequence = 5; attack.time.kind = QA_SOURCE_TIME_SECONDS; attack.time.value = 2;
  attack.attacker = &attacker; attack.inflictor = &inflictor;
  attack.weapon_provider = "q2:game"; attack.combat_provider = "q2:combat"; attack.inventory_provider = "q2:inventory";
  attack.movement_provider = "q2:movement"; attack.has_damage_powerup_owner = true; attack.damage_powerup_owner = "q2:game";
  attack.cause.kind = QA_ATTACK_CAUSE_Q2; attack.cause.means_of_death = 4;
  qa_q2_entity_checkpoint record;
  memset(&record, 0, sizeof record);
  record.actor = (qa_saved_actor_id){3, 0};
  record.has_source_slot = true; record.source_slot = 3;
  record.spawn = entity.spawn;
  record.values = entity;
  record.enemy = (qa_q2_saved_link){true, {1, 2}};
  record.has_last_attack = true; record.last_attack = qa_q2_save_attack(attack);
  record.think = "monster_think";
  qa_q2_freed_slot freed = {2, 0.5};
  qa_q2_foundation_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.version = 1; checkpoint.next_source_slot = 4; checkpoint.sequence = 6;
  checkpoint.freed_slots = &freed; checkpoint.freed_slot_count = 1;
  checkpoint.counters.total_monsters = 1;
  checkpoint.entities = &record; checkpoint.entity_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_foundation_checkpoint(&checkpoint, &length);
  CHECK(bytes_are(bytes, length, FOUNDATION));
  qa_persist_decoded owner = {0};
  qa_q2_foundation_checkpoint decoded;
  CHECK(qa_decode_q2_foundation_checkpoint(bytes, length, &decoded, &owner));
  size_t again_length = 0;
  uint8_t *again = qa_encode_q2_foundation_checkpoint(&decoded, &again_length);
  CHECK(bytes_are(again, again_length, FOUNDATION));
  CHECK(decoded.entities[0].values.item_dropped && decoded.entities[0].last_attack.has_attacker &&
        decoded.entities[0].last_attack.values.cause.means_of_death == 4);
  free(again);
  qa_persist_decoded_free(&owner);
  free(bytes);
  qa_q2_entity_destroy(&entity);
}

int main(void) {
  test_weapons();
  test_foundation();
  printf("test_q2_checkpoint_bytes: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

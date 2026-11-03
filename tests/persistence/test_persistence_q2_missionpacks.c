/*
 * src/persistence/q2-missionpacks.ts codecs over the content checkpoint
 * types. Expected bytes are the TS output under bun of
 * encodeQ2MissionPack{Items,Monsters}Checkpoint / encodeQ2RogueEntities /
 * encodeQ2Tag / encodeQ2DeathBallCheckpoint applied to captured
 * checkpoints: Q2MissionPackMonsterState.capture (actor 5 with blocked,
 * turretOrientation 2, healer 7, medicTries 1, chosenReinforcements [0,3],
 * reactToDamageTime 1.5, summonStrength 2, badArea 9, widowInvulnerableUntil
 * 0.25; actor 2 default; flyerNextMove run, widowShotsFired 4, multiplier 2)
 * spread with hints as Q2MissionPackMonsters.capture does, Q2Tag.capture
 * after restore, and the Q2MissionPackItems / Q2DeathBall capture shapes.
 * TS re-encodes a decoded monsters checkpoint in reader order (actors last);
 * the composition only encodes captured checkpoints, so C writes capture
 * order. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/q2_missionpacks.h"

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static bool bytes_are(const uint8_t *bytes, size_t length, const char *expected) {
  if (bytes == NULL) return false;
  bool same = length == strlen(expected) && memcmp(bytes, expected, length) == 0;
  if (!same) fprintf(stderr, "got: %.*s\n", (int)length, (const char *)bytes);
  return same;
}
static qa_saved_actor_id saved(int64_t slot) { qa_saved_actor_id a; memset(&a, 0, sizeof a); a.slot = slot; a.generation = 0; return a; }

static const char *const MONSTERS =
    "{\"version\":1,\"actors\":[{\"actor\":{\"slot\":5,\"generation\":0},\"state\":{\"blocked\":true,\"turretOrientation\":2,"
    "\"healer\":{\"slot\":7,\"generation\":0},\"badMedic1\":null,\"badMedic2\":null,\"medicTries\":1,\"chosenReinforcements\":[0,3],"
    "\"reactToDamageTime\":1.5,\"summonStrength\":2,\"lastPlayerEnemy\":null,\"badArea\":{\"slot\":9,\"generation\":0},\"goodGuy\":false,"
    "\"widowQuadUntil\":0,\"widowDoubleUntil\":0,\"widowInvulnerableUntil\":0.25}},{\"actor\":{\"slot\":2,\"generation\":0},\"state\":"
    "{\"blocked\":false,\"turretOrientation\":0,\"healer\":null,\"badMedic1\":null,\"badMedic2\":null,\"medicTries\":0,"
    "\"chosenReinforcements\":[],\"reactToDamageTime\":0,\"summonStrength\":0,\"lastPlayerEnemy\":null,\"badArea\":null,\"goodGuy\":false,"
    "\"widowQuadUntil\":0,\"widowDoubleUntil\":0,\"widowInvulnerableUntil\":0}}],\"flyerNextMove\":\"run\",\"hints\":{\"version\":1,"
    "\"present\":true,\"starts\":[{\"slot\":11,\"generation\":0}],\"nodes\":[{\"actor\":{\"slot\":11,\"generation\":0},\"chain\":0,"
    "\"next\":null},{\"actor\":{\"slot\":10,\"generation\":0},\"chain\":0,\"next\":{\"slot\":11,\"generation\":0}}],\"monsters\":"
    "[{\"actor\":{\"slot\":5,\"generation\":0},\"goal\":{\"slot\":11,\"generation\":0},\"lastTime\":3}]},\"widowShotsFired\":4,"
    "\"widowDamageMultiplier\":2}";

static void build_monsters(qa_q2mpm_monsters_checkpoint *c, double *reinforcements, qa_q2mpm_checkpoint_actor *actors,
                           qa_saved_actor_id *starts, qa_q2mpm_hint_node_checkpoint *nodes,
                           qa_q2mpm_hint_monster_checkpoint *monsters) {
  memset(c, 0, sizeof *c);
  memset(actors, 0, 2 * sizeof *actors);
  c->version = 1; c->flyer_next_move = QA_Q2MPM_FLYER_RUN; c->widow_shots_fired = 4; c->widow_damage_multiplier = 2;
  reinforcements[0] = 0; reinforcements[1] = 3;
  actors[0].actor = saved(5);
  qa_q2mpm_saved_state *s = &actors[0].state;
  s->blocked = true; s->turret_orientation = 2; s->has_healer = true; s->healer = saved(7); s->medic_tries = 1;
  s->chosen_reinforcements = reinforcements; s->reinforcement_count = 2; s->react_to_damage_time = 1.5;
  s->summon_strength = 2; s->has_bad_area = true; s->bad_area = saved(9); s->widow_invulnerable_until = 0.25;
  actors[1].actor = saved(2);
  c->actors = actors; c->actor_count = 2;
  c->has_hints = true; c->hints.version = 1; c->hints.present = true;
  starts[0] = saved(11);
  c->hints.starts = starts; c->hints.start_count = 1;
  memset(nodes, 0, 2 * sizeof *nodes);
  nodes[0].actor = saved(11);
  nodes[1].actor = saved(10); nodes[1].has_next = true; nodes[1].next = saved(11);
  c->hints.nodes = nodes; c->hints.node_count = 2;
  memset(monsters, 0, sizeof *monsters);
  monsters[0].actor = saved(5); monsters[0].has_goal = true; monsters[0].goal = saved(11); monsters[0].last_time = 3;
  c->hints.monsters = monsters; c->hints.monster_count = 1;
}

static void test_monsters(void) {
  double reinforcements[2];
  qa_q2mpm_checkpoint_actor actors[2];
  qa_saved_actor_id starts[1];
  qa_q2mpm_hint_node_checkpoint nodes[2];
  qa_q2mpm_hint_monster_checkpoint monsters[1];
  qa_q2mpm_monsters_checkpoint c;
  build_monsters(&c, reinforcements, actors, starts, nodes, monsters);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_mission_pack_monsters_checkpoint(&c, &length);
  CHECK(bytes_are(bytes, length, MONSTERS));
  qa_q2mpm_monsters_checkpoint decoded;
  CHECK(qa_decode_q2_mission_pack_monsters_checkpoint(bytes, length, &decoded));
  CHECK(decoded.flyer_next_move == QA_Q2MPM_FLYER_RUN && decoded.widow_shots_fired == 4 && decoded.widow_damage_multiplier == 2);
  CHECK(decoded.actor_count == 2 && decoded.actors[0].actor.slot == 5 && decoded.actors[0].state.blocked);
  CHECK(decoded.actors[0].state.reinforcement_count == 2 && decoded.actors[0].state.chosen_reinforcements[1] == 3);
  CHECK(decoded.actors[0].state.has_healer && decoded.actors[0].state.healer.slot == 7 && !decoded.actors[0].state.has_bad_medic1);
  CHECK(decoded.actors[0].state.react_to_damage_time == 1.5 && decoded.actors[0].state.widow_invulnerable_until == 0.25);
  CHECK(decoded.has_hints && decoded.hints.present && decoded.hints.start_count == 1 && decoded.hints.node_count == 2);
  CHECK(decoded.hints.nodes[1].has_next && decoded.hints.nodes[1].next.slot == 11 && decoded.hints.monsters[0].last_time == 3);
  size_t again_length = 0;
  uint8_t *again = qa_encode_q2_mission_pack_monsters_checkpoint(&decoded, &again_length);
  CHECK(bytes_are(again, again_length, MONSTERS));
  free(again);
  qa_q2mpm_monsters_checkpoint_free(&decoded);
  free(bytes);

  c.has_hints = false;
  bytes = qa_encode_q2_mission_pack_monsters_checkpoint(&c, &length);
  const char *hints = strstr(MONSTERS, "\"hints\":{");
  const char *after = strstr(MONSTERS, ",\"widowShotsFired\"");
  char expected[2048];
  snprintf(expected, sizeof expected, "%.*s\"hints\":null%s", (int)(hints - MONSTERS), MONSTERS, after);
  CHECK(bytes_are(bytes, length, expected));
  free(bytes);
}

static void test_items(void) {
  qa_q2mp_item_power_checkpoint power;
  memset(&power, 0, sizeof power);
  power.actor = saved(3); power.state.quad_fire_until = 12.5; power.state.ir_until = 40;
  qa_q2mp_items_checkpoint c = {&power, 1};
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_mission_pack_items_checkpoint(&c, &length);
  const char *expected = "{\"powers\":[{\"actor\":{\"slot\":3,\"generation\":0},\"state\":{\"quadFireUntil\":12.5,\"doubleUntil\":0,\"irUntil\":40}}]}";
  CHECK(bytes_are(bytes, length, expected));
  qa_q2mp_items_checkpoint decoded;
  CHECK(qa_decode_q2_mission_pack_items_checkpoint(bytes, length, &decoded));
  CHECK(decoded.power_count == 1 && decoded.powers[0].actor.slot == 3 && decoded.powers[0].state.quad_fire_until == 12.5 &&
        decoded.powers[0].state.double_until == 0 && decoded.powers[0].state.ir_until == 40);
  qa_q2mp_items_checkpoint_free(&decoded);
  free(bytes);
  qa_q2mp_items_checkpoint empty = {NULL, 0};
  bytes = qa_encode_q2_mission_pack_items_checkpoint(&empty, &length);
  CHECK(bytes_are(bytes, length, "{\"powers\":[]}"));
  free(bytes);
}

static void test_small(void) {
  size_t length = 0;
  qa_q2mp_rogue_entities_checkpoint rogue = {7};
  uint8_t *bytes = qa_encode_q2_rogue_entities_checkpoint(&rogue, &length);
  CHECK(bytes_are(bytes, length, "{\"steamId\":7}"));
  qa_q2mp_rogue_entities_checkpoint rogue_decoded;
  CHECK(qa_decode_q2_rogue_entities_checkpoint(bytes, length, &rogue_decoded) && rogue_decoded.steam_id == 7);
  free(bytes);

  qa_q2mp_tag_checkpoint tag;
  memset(&tag, 0, sizeof tag);
  tag.has_token = true; tag.token = saved(4); tag.count = 2;
  bytes = qa_encode_q2_tag_checkpoint(&tag, &length);
  CHECK(bytes_are(bytes, length, "{\"token\":{\"slot\":4,\"generation\":0},\"owner\":null,\"count\":2}"));
  qa_q2mp_tag_checkpoint tag_decoded;
  CHECK(qa_decode_q2_tag_checkpoint(bytes, length, &tag_decoded));
  CHECK(tag_decoded.has_token && tag_decoded.token.slot == 4 && !tag_decoded.has_owner && tag_decoded.count == 2);
  free(bytes);

  qa_q2mp_deathball_checkpoint ball;
  memset(&ball, 0, sizeof ball);
  ball.has_ball = true; ball.ball = saved(6); ball.starts = 3; ball.team1_score = 1; ball.team2_score = 5;
  bytes = qa_encode_q2_death_ball_checkpoint(&ball, &length);
  CHECK(bytes_are(bytes, length, "{\"ball\":{\"slot\":6,\"generation\":0},\"starts\":3,\"team1Score\":1,\"team2Score\":5}"));
  qa_q2mp_deathball_checkpoint ball_decoded;
  CHECK(qa_decode_q2_death_ball_checkpoint(bytes, length, &ball_decoded));
  CHECK(ball_decoded.has_ball && ball_decoded.ball.slot == 6 && ball_decoded.starts == 3 && ball_decoded.team2_score == 5);
  free(bytes);
  memset(&ball, 0, sizeof ball);
  bytes = qa_encode_q2_death_ball_checkpoint(&ball, &length);
  CHECK(bytes_are(bytes, length, "{\"ball\":null,\"starts\":0,\"team1Score\":0,\"team2Score\":0}"));
  free(bytes);
}

int main(void) {
  test_monsters();
  test_items();
  test_small();
  printf("test_persistence_q2_missionpacks: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

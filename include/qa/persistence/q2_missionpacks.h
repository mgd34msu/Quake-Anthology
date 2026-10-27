/*
 * Port of src/persistence/q2-missionpacks.ts. Mission-pack item/monster/entity/mode checkpoints. The rogue
 * hints reader mirrors content/q2/missionpacks/monsters/hints.ts
 * decodeQ2RogueHintsCheckpoint (content not yet ported).
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_MISSIONPACKS_H
#define QA_PERSISTENCE_Q2_MISSIONPACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_mission_pack_powerups {
  double quad_fire_until;
  double double_until;
  double ir_until;
} qa_q2_mission_pack_powerups;

typedef struct qa_q2_mission_pack_power_entry {
  qa_saved_actor_id actor;
  qa_q2_mission_pack_powerups state;
} qa_q2_mission_pack_power_entry;

typedef struct qa_q2_mission_pack_items_checkpoint {
  qa_q2_mission_pack_power_entry *powers; size_t powers_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_mission_pack_items_checkpoint;

typedef struct qa_q2_rogue_hint_node {
  qa_saved_actor_id actor;
  double chain;
  bool has_next;
  qa_saved_actor_id next;
} qa_q2_rogue_hint_node;

typedef struct qa_q2_rogue_hint_monster {
  qa_saved_actor_id actor;
  bool has_goal;
  qa_saved_actor_id goal;
  double last_time;
} qa_q2_rogue_hint_monster;

typedef struct qa_q2_rogue_hints_checkpoint {
  double version;
  bool present;
  qa_saved_actor_id *starts; size_t starts_count;
  qa_q2_rogue_hint_node *nodes; size_t nodes_count;
  qa_q2_rogue_hint_monster *monsters; size_t monsters_count;
} qa_q2_rogue_hints_checkpoint;

typedef struct qa_q2_mission_pack_monster_state {
  bool blocked;
  double turret_orientation;
  bool has_healer;
  qa_saved_actor_id healer;
  bool has_bad_medic1;
  qa_saved_actor_id bad_medic1;
  bool has_bad_medic2;
  qa_saved_actor_id bad_medic2;
  double medic_tries;
  double *chosen_reinforcements; size_t chosen_reinforcements_count;
  double react_to_damage_time;
  double summon_strength;
  bool has_last_player_enemy;
  qa_saved_actor_id last_player_enemy;
  bool has_bad_area;
  qa_saved_actor_id bad_area;
  bool good_guy;
  double widow_quad_until;
  double widow_double_until;
  double widow_invulnerable_until;
} qa_q2_mission_pack_monster_state;

typedef struct qa_q2_mission_pack_monster_entry {
  qa_saved_actor_id actor;
  qa_q2_mission_pack_monster_state state;
} qa_q2_mission_pack_monster_entry;

typedef struct qa_q2_mission_pack_monsters_checkpoint {
  double version;
  const char *flyer_next_move;
  bool has_hints;
  qa_q2_rogue_hints_checkpoint hints;
  double widow_shots_fired;
  double widow_damage_multiplier;
  qa_q2_mission_pack_monster_entry *actors; size_t actors_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_mission_pack_monsters_checkpoint;

typedef struct qa_q2_rogue_entities_checkpoint {
  double steam_id;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_rogue_entities_checkpoint;

typedef struct qa_q2_tag_checkpoint {
  bool has_token;
  qa_saved_actor_id token;
  bool has_owner;
  qa_saved_actor_id owner;
  double count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_tag_checkpoint;

typedef struct qa_q2_death_ball_checkpoint {
  bool has_ball;
  qa_saved_actor_id ball;
  double starts;
  double team1_score;
  double team2_score;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_death_ball_checkpoint;

qa_q2_mission_pack_items_checkpoint qa_read_q2_mission_pack_items_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_mission_pack_items_checkpoint(const qa_q2_mission_pack_items_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_mission_pack_items_checkpoint(const uint8_t *bytes, size_t length, qa_q2_mission_pack_items_checkpoint *out);
qa_q2_rogue_hints_checkpoint qa_read_q2_rogue_hints_checkpoint(const qa_save_reader *reader);
/* Builds the checkpoint tree (C encoder helper); NULL on OOM. */
qa_save_value *qa_q2_rogue_hints_checkpoint_value(const qa_q2_rogue_hints_checkpoint *checkpoint);
qa_q2_mission_pack_monsters_checkpoint qa_read_q2_mission_pack_monsters_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_mission_pack_monsters_checkpoint(const qa_q2_mission_pack_monsters_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_mission_pack_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2_mission_pack_monsters_checkpoint *out);
qa_q2_rogue_entities_checkpoint qa_read_q2_rogue_entities_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_rogue_entities_checkpoint(const qa_q2_rogue_entities_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_rogue_entities_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rogue_entities_checkpoint *out);
qa_q2_tag_checkpoint qa_read_q2_tag_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_tag_checkpoint(const qa_q2_tag_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_tag_checkpoint(const uint8_t *bytes, size_t length, qa_q2_tag_checkpoint *out);
qa_q2_death_ball_checkpoint qa_read_q2_death_ball_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_death_ball_checkpoint(const qa_q2_death_ball_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_death_ball_checkpoint(const uint8_t *bytes, size_t length, qa_q2_death_ball_checkpoint *out);

#endif

/* Port of src/content/q2/multiplayer/ctf/checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_CHECKPOINT_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_CHECKPOINT_H
#include <stdint.h>
#include "qa/content/q2/equipment/ctf_grapple.h"
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
typedef struct qa_q2_ctf_player_checkpoint {
  qa_q2_ctf_player_state state;
  qa_ctf_grapple_checkpoint grapple;
} qa_q2_ctf_player_checkpoint;
typedef struct qa_q2_ctf_ghost_checkpoint {
  double code; qa_q2_ctf_playing_team team; char *name; /* owned */
  bool has_actor; qa_saved_actor_id actor;
  double score, deaths, kills, captures, base_defense, carrier_defense;
} qa_q2_ctf_ghost_checkpoint;
typedef struct qa_q2_ctf_election_checkpoint {
  const char *kind; qa_saved_actor_id target;
  char *map, *message; /* owned */
  double votes, needed, expires;
} qa_q2_ctf_election_checkpoint;
typedef struct qa_q2_ctf_rules_checkpoint {
  const char *force_join; double competition; bool match_lock;
  double election_percentage, match_minutes, setup_minutes, start_seconds, capture_limit;
  bool instant_weapons;
} qa_q2_ctf_rules_checkpoint;
typedef struct qa_q2_ctf_match_checkpoint {
  double team1, team2, total1, total2;
  bool has_last_flag_capture; double last_flag_capture;
  bool has_last_capture_team; qa_q2_ctf_playing_team last_capture_team;
  const char *phase; double match_time, last_time;
  bool has_election; qa_q2_ctf_election_checkpoint election;
  qa_q2_ctf_ghost_checkpoint *ghosts; size_t ghost_count;
} qa_q2_ctf_match_checkpoint;
typedef struct qa_q2_ctf_player_entry {
  qa_saved_actor_id actor;
  qa_q2_ctf_player_checkpoint state;
} qa_q2_ctf_player_entry;
typedef struct qa_q2_ctf_checkpoint {
  double version; /* 1 */
  qa_q2_ctf_rules_checkpoint rules;
  qa_q2_ctf_match_checkpoint match;
  qa_q2_ctf_player_entry *players; size_t player_count;
} qa_q2_ctf_checkpoint;
void qa_q2_ctf_checkpoint_destroy(qa_q2_ctf_checkpoint *checkpoint);
/* equipment NULL = TS null. Frees via qa_q2_ctf_checkpoint_destroy. */
void qa_capture_q2_ctf(qa_q2_ctf_context *context, qa_q2_ctf_grapple_equipment *equipment, qa_q2_ctf_checkpoint *out);
void qa_restore_q2_ctf(qa_q2_ctf_context *context, qa_q2_game_services *game, const qa_q2_ctf_checkpoint *checkpoint, qa_q2_ctf_grapple_equipment *equipment);
/* Malloc'd bytes; NULL only on OOM. */
uint8_t *qa_encode_q2_ctf_checkpoint(const qa_q2_ctf_checkpoint *checkpoint, size_t *out_length);
void qa_decode_q2_ctf_checkpoint(const uint8_t *bytes, size_t length, qa_q2_ctf_checkpoint *out);
#endif

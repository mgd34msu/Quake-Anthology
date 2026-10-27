/*
 * Port of src/persistence/q2-weapons.ts. Weapon state/input/noise checkpoints (format version 2).
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_WEAPONS_H
#define QA_PERSISTENCE_Q2_WEAPONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_hand_reservation { const char *kind; /* "none"|"finite"|"infinite" */ } qa_q2_hand_reservation;

typedef struct qa_q2_noise_checkpoint {
  qa_saved_actor_id actor;
  qa_vec3 origin;
  double time;
  bool secondary;
} qa_q2_noise_checkpoint;

typedef struct qa_q2_weapon_input {
  bool attack;
  bool latched_attack;
  bool holster;
  qa_vec3 angles;
  bool ducked;
  bool spectator;
  bool notarget;
  const char *hand;
  bool animate_player;
  double quad_until;
  double double_until;
  double quad_fire_until;
  bool haste;
  bool no_stack_double;
  bool instant_switch;
  bool quick_switch;
  bool infinite_ammo;
  bool players_collide;
  double gravity;
  bool weapon_thunk;
} qa_q2_weapon_input;

typedef struct qa_q2_weapon_state {
  const char *primary_handoff;
  qa_q2_hand_reservation hand_reservation;
  bool source_firing;
  const char *weapon;
  const char *last_weapon;
  const char *pending;
  const char *phase;
  double frame;
  double think_time;
  double fire_finished;
  bool fire_buffered;
  bool latched_attack;
  double machinegun_shots;
  double empty_sound_time;
  double grenade_time;
  double grenade_finished;
  bool grenade_blew_up;
  qa_vec3 kick_origin;
  qa_vec3 kick_angles;
  double kick_time;
  double kick_until;
  double kick_duration;
  const char *loop_sound;
  const char *view_model;
  double view_skin;
  double last_firing_time;
  double gun_rate;
} qa_q2_weapon_state;

typedef struct qa_q2_silencer_charge {
  qa_saved_actor_id actor;
  double charges;
} qa_q2_silencer_charge;

typedef struct qa_q2_weapon_state_entry {
  qa_saved_actor_id actor;
  qa_q2_weapon_state state;
} qa_q2_weapon_state_entry;

typedef struct qa_q2_weapon_input_entry {
  qa_saved_actor_id actor;
  qa_q2_weapon_input input;
} qa_q2_weapon_input_entry;

typedef struct qa_q2_weapon_noise_entry {
  qa_saved_actor_id actor;
  bool has_primary;
  qa_q2_noise_checkpoint primary;
  bool has_secondary;
  qa_q2_noise_checkpoint secondary;
} qa_q2_weapon_noise_entry;

typedef struct qa_q2_blaster_cause {
  qa_saved_actor_id actor;
  double means_of_death;
} qa_q2_blaster_cause;

typedef struct qa_q2_weapons_checkpoint {
  double format_version;
  qa_q2_silencer_charge *silencer_charges; size_t silencer_charges_count;
  const char *source_rules;
  const char * *registered; size_t registered_count;
  bool has_fallback_order;
  const char * *fallback_order; size_t fallback_order_count;
  qa_q2_weapon_state_entry *states; size_t states_count;
  qa_q2_weapon_input_entry *inputs; size_t inputs_count;
  qa_q2_weapon_noise_entry *noises; size_t noises_count;
  bool has_sound_entity;
  qa_q2_noise_checkpoint sound_entity;
  bool has_sound2_entity;
  qa_q2_noise_checkpoint sound2_entity;
  qa_q2_blaster_cause *blaster_causes; size_t blaster_causes_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_weapons_checkpoint;

qa_q2_weapon_input qa_read_q2_weapon_input(const qa_save_reader *reader);
qa_q2_weapon_state qa_read_q2_weapon_state(const qa_save_reader *reader);
qa_q2_weapons_checkpoint qa_read_q2_weapons_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_weapons_checkpoint(const qa_q2_weapons_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_weapons_checkpoint(const uint8_t *bytes, size_t length, qa_q2_weapons_checkpoint *out);

#endif

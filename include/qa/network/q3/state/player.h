/* Port of src/network/q3/state/player.ts (playerState_t wire storage).
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_STATE_PLAYER_H
#define QA_NETWORK_Q3_STATE_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/network/q3/state/product.h"
#define QA_Q3_ENTITYNUM_NONE 1023
/* TS PlayerStateSlots: fixed Int32Array with RangeError bounds checks. */
typedef struct qa_q3_player_state_slots {
  int32_t *values; /* owned */
  int64_t length;
} qa_q3_player_state_slots;
bool qa_q3_player_state_slots_init(qa_q3_player_state_slots *s, int64_t length,
                                   const int32_t *source_values, int64_t source_length, bool has_source);
void qa_q3_player_state_slots_destroy(qa_q3_player_state_slots *s);
int32_t qa_q3_player_state_slots_get(const qa_q3_player_state_slots *s, int64_t index);
void qa_q3_player_state_slots_set(qa_q3_player_state_slots *s, int64_t index, int32_t value);
bool qa_q3_player_state_slots_copy(const qa_q3_player_state_slots *s, int32_t **out); /* caller frees */

/* TS PlayerStateRecord<M,W,S> / SourcePlayerState. Slots are fixed inline
 * arrays (events/event_parms 2, stats/persistant/powerups/ammo 16) to keep
 * value copy semantics; slot accessors below retain RangeError checks. */
typedef struct qa_q3_player_state_record {
  qa_q3_product product;
  int32_t command_time, pm_type, bob_cycle, pm_flags, pm_time;
  qa_vec3 origin, velocity;
  int32_t weapon_time, gravity, speed;
  qa_vec3 delta_angles;
  int32_t ground_entity_num, legs_timer, legs_anim, torso_timer, torso_anim, movement_dir;
  qa_vec3 grapple_point;
  int32_t e_flags, event_sequence;
  int32_t events[2], event_parms[2];
  int32_t external_event, external_event_parm, external_event_time, client_num;
  int32_t weapon, weapon_state;
  qa_vec3 viewangles;
  int32_t viewheight, damage_event, damage_yaw, damage_pitch, damage_count;
  int32_t stats[16], persistant[16], powerups[16], ammo[16];
  int32_t generic1, loop_sound, jumppad_ent, ping, pmove_framecount, jumppad_frame, entity_event_sequence;
} qa_q3_player_state_record;
typedef qa_q3_player_state_record qa_q3_source_player_state;
void qa_q3_player_state_record_init(qa_q3_player_state_record *s, qa_q3_product product,
                                    int32_t pm_type, int32_t weapon, int32_t weapon_state);
qa_q3_product qa_q3_player_state_record_get_product(const qa_q3_player_state_record *s);
void qa_q3_player_state_record_copy_from(qa_q3_player_state_record *dst, const qa_q3_player_state_record *src);
void qa_q3_player_state_record_copy(qa_q3_player_state_record *out, const qa_q3_player_state_record *s);
/* Slot helpers over inline arrays (kind: 0 events 1 event_parms 2 stats 3 persistant 4 powerups 5 ammo). */
int32_t qa_q3_player_state_slot_get(const qa_q3_player_state_record *s, int kind, int64_t index);
void qa_q3_player_state_slot_set(qa_q3_player_state_record *s, int kind, int64_t index, int32_t value);
#endif

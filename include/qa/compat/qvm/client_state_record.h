/*
 * Port of src/compat/qvm/client-state-record.ts.
 * qa_qvmx_snapshot / qa_qvmx_player_state_fields mirror
 * network/q3/server-message.ts Snapshot/PlayerStateFields (network
 * unported) with plain arrays for the TS slot objects.
 * Player-state record writes mirror player-record.ts (qvm-b slice)
 * as qa_qvmx_write_player_state / qa_qvmx_write_source_player_state.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_STATE_RECORD_H
#define QA_COMPAT_QVM_CLIENT_STATE_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/client_state.h"
#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/protocol.h"

#define QA_QVM_GAME_STATE_BYTES 20100
#define QA_QVM_SNAPSHOT_BYTES 53772
#define QA_QVM_USER_COMMAND_BYTES 24

double qa_qvm_snapshot_bytes_for(qa_qvm_abi_profile profile);

typedef struct qa_qvm_user_command_layout {
  double buttons_offset;
  double buttons_bytes; /* 1 | 4 */
  double angles, weapon, forward, right, up;
} qa_qvm_user_command_layout;

qa_qvm_user_command_layout qa_qvm_get_user_command_layout(qa_qvm_abi_profile profile);

/* Mirror of network/q3 PlayerStateFields (fields read by writeQvmSnapshot). */
typedef struct qa_qvmx_player_state_fields {
  double command_time, pm_type, bob_cycle, pm_flags, pm_time;
  qa_vec3 origin, velocity;
  double weapon_time, gravity, speed;
  qa_vec3 delta_angles;
  double ground_entity_num, legs_timer, legs_anim, torso_timer, torso_anim, movement_dir;
  qa_vec3 grapple_point;
  double e_flags, event_sequence;
  double events[2], event_parms[2];
  double external_event, external_event_parm, external_event_time;
  double client_num, weapon, weapon_state;
  qa_vec3 viewangles;
  double viewheight, damage_event, damage_yaw, damage_pitch, damage_count;
  double stats[16], persistant[16], powerups[16], ammo[16];
  double generic1, loop_sound, jumppad_ent, ping, pmove_framecount, jumppad_frame;
  double entity_event_sequence;
} qa_qvmx_player_state_fields;

/* Mirror of network/q3 Snapshot. */
typedef struct qa_qvmx_snapshot {
  double message_number, server_time, delta_number, flags, server_command_number;
  double parse_entities_number;
  const uint8_t *area_mask;
  size_t area_mask_length;
  qa_qvmx_player_state_fields player_state;
  const qa_qvm_entity_state_fields *entities;
  size_t entity_count;
} qa_qvmx_snapshot;

typedef struct qa_qvm_source_snapshot {
  double number, server_time, flags;
  const uint8_t *area_mask;
  size_t area_mask_length;
  qa_q3_player_state player_state;
  const qa_qvm_entity_state_fields *entities;
  size_t entity_count;
  double server_command_sequence;
} qa_qvm_source_snapshot;

/* Mirrors of player-record.ts (qvm-b slice); profile NULL = "q3-modern"
 * where the TS default applies. Slot arrays must have 16 entries. */
void qa_qvmx_write_source_player_state(qa_qvmx_view view, const qa_q3_player_state *state,
                                       qa_qvm_abi_profile profile);
/* persistent_write: "initialize" | "preserve-private" (NULL = initialize). */
void qa_qvmx_write_player_state(qa_qvmx_view view, const qa_q3_player_state *state,
                                qa_qvm_abi_profile profile, const char *persistent_write);

void qa_qvm_write_source_snapshot(qa_qvmx_memory *memory, qa_qvmx_view view,
                                  const qa_qvm_source_snapshot *snapshot,
                                  qa_qvm_abi_profile profile);
void qa_qvm_write_game_state(qa_qvmx_memory *memory, qa_qvmx_view view,
                             const qa_qvmx_source_game_state_record *state,
                             qa_qvm_abi_profile profile);
void qa_qvm_write_source_game_state(qa_qvmx_memory *memory, qa_qvmx_view view,
                                    const qa_qvmx_source_game_state_record *state);
void qa_qvm_write_snapshot(qa_qvmx_memory *memory, qa_qvmx_view view,
                           const qa_qvmx_snapshot *snapshot, double ping,
                           qa_qvm_abi_profile profile);
/* mode: "encode" | "update" (NULL = "encode"). */
void qa_qvm_write_user_command(qa_qvmx_view view, const qa_qvmx_wire_user_command *command,
                               qa_qvm_abi_profile profile, const char *mode);
qa_qvmx_wire_user_command qa_qvm_read_user_command(qa_qvmx_view view, qa_qvm_abi_profile profile);

#endif

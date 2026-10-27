/* Port of src/network/q3/adapters.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_ADAPTERS_H
#define QA_NETWORK_Q3_ADAPTERS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/protocol.h"
#include "qa/network/q3/message.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/state/entity.h"
#include "qa/network/q3/state/player.h"
extern const qa_q3_protocol_identity qa_q3_protocol;
/* TS requireQ3Protocol: aborts unless q3/68, returns identity copy. */
qa_q3_protocol_identity qa_q3_require_protocol(qa_protocol_identity protocol);
qa_q3_user_command qa_q3_to_user_command(const qa_q3_wire_user_command *value);
qa_q3_wire_user_command qa_q3_from_user_command(const qa_q3_user_command *value);
qa_q3_entity_state qa_q3_to_entity_state(const qa_q3_entity_state_record *value);
void qa_q3_from_entity_state(const qa_q3_entity_state *value, qa_q3_entity_state_record *out);
/* toQ3PlayerState: slot views point into scratch arrays inside out_storage. */
typedef struct qa_q3_adapted_player_state {
  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
} qa_q3_adapted_player_state;
void qa_q3_to_player_state(const qa_q3_player_state_record *value, qa_q3_adapted_player_state *out);
/* fromQ3PlayerState: value slot views must have TS lengths (16/2), else RangeError. */
void qa_q3_from_player_state(const qa_q3_player_state *value, qa_q3_product product, qa_q3_player_state_record *out);
typedef struct qa_q3_adapted_snapshot {
  qa_q3_snapshot snapshot;
  qa_q3_entity_state *entities;
  qa_q3_adapted_player_state player;
  uint8_t area_mask[32];
} qa_q3_adapted_snapshot;
bool qa_q3_to_snapshot(const qa_q3_wire_snapshot *value, double ping_milliseconds, double server_command_count, qa_q3_adapted_snapshot *out);
void qa_q3_adapted_snapshot_destroy(qa_q3_adapted_snapshot *s);
#endif

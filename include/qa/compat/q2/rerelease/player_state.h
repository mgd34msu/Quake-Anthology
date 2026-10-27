/* Port of src/compat/q2/rerelease/player-state.ts. GPL-2.0-or-later.
 * DataView parameters are borrowed little-endian byte views. */
#ifndef QA_COMPAT_Q2_RERELEASE_PLAYER_STATE_H
#define QA_COMPAT_Q2_RERELEASE_PLAYER_STATE_H
#include <stdint.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/contracts/protocol.h"

void qa_compat_q2_write_rerelease_movement_state(uint8_t *view, qa_q2_rerelease_movement_state value);
qa_q2_rerelease_movement_state qa_compat_q2_read_rerelease_movement_state(const uint8_t *view);
void qa_compat_q2_write_rerelease_player_state(const qa_compat_q2_memory *memory, qa_guest_address address, const qa_q2_rerelease_player_state *value);
qa_q2_rerelease_player_state qa_compat_q2_read_rerelease_player_state(const qa_compat_q2_memory *memory, qa_guest_address address);
void qa_compat_q2_write_rerelease_user_command(uint8_t *view, qa_q2_rerelease_user_command value);
qa_q2_rerelease_user_command qa_compat_q2_read_rerelease_user_command(const uint8_t *view);
#endif

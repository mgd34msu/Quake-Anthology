/*
 * Port of src/compat/qvm/player-record.ts. The TS DataView becomes a
 * qa_qvm_writable_view. qa_q3_player_state slot arrays are views: on
 * read the caller points stats/persistent/powerups/ammo at 16-double
 * buffers (counts are set to 16); on write each view must hold 16
 * slots, as the TS length check requires. persistent_write keeps the
 * exact TS strings "initialize" | "preserve-private".
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PLAYER_RECORD_H
#define QA_COMPAT_QVM_PLAYER_RECORD_H

#include <stddef.h>

#include "qa/compat/qvm/memory_writes.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/protocol.h"

#define QA_QVM_PLAYER_STATE_BYTES 468

int qa_qvm_player_state_bytes(qa_qvm_abi_profile profile);
void qa_qvm_read_player_state(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile, qa_q3_player_state *out);
void qa_qvm_read_source_player_state(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile, qa_q3_player_state *out);
void qa_qvm_write_player_state(qa_qvm_writable_view *view, const qa_q3_player_state *state,
                               qa_qvm_abi_profile profile, const char *persistent_write);
/* TS writeQvmPlayerState(..., unmapped): ignore_unmapped is "ignore" (client paths, remote values). */
void qa_qvm_write_player_state_unmapped(qa_qvm_writable_view *view, const qa_q3_player_state *state,
                                        qa_qvm_abi_profile profile, const char *persistent_write, bool ignore_unmapped);
void qa_qvm_write_source_player_state(qa_qvm_writable_view *view, const qa_q3_player_state *state,
                                      qa_qvm_abi_profile profile);

#endif

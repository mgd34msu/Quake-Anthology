/*
 * Port of src/network/q2/adapters.ts: contracts <-> wire conversion.
 * TS allocates fresh states; the C form writes into caller storage.
 * Player stats views point into a caller-retained double buffer
 * (stats_storage) that the adapter fills. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_ADAPTERS_H
#define QA_NETWORK_Q2_ADAPTERS_H

#include <stddef.h>

#include "qa/contracts/protocol.h"
#include "qa/network/q2/state.h"

/* Doubled stats scratch retained beside a contracts player view. */
#define QA_Q2_ADAPTER_STATS_MAX 64

/* TS fromQ2Entity: is_rerelease selects the rerelease input fields
 * (alpha/scale/... and bigint effects split). */
void qa_q2_from_entity(const qa_q2_entity_state *state, bool is_rerelease,
                       uint64_t rerelease_effects, double alpha, double scale,
                       double instance_bits, double loop_volume,
                       double loop_attenuation, double owner, double old_frame,
                       qa_q2_wire_entity *out);
/* TS toQ2Entity / toQ2RereleaseEntity. */
void qa_q2_to_entity(const qa_q2_wire_entity *wire, qa_q2_entity_state *out);
void qa_q2_to_rerelease_entity(const qa_q2_wire_entity *wire,
                               qa_q2_rerelease_entity_state *out);
/* TS fromQ2Player: is_rerelease selects the rerelease movement input.
 * Aborts with the TS RangeError over 64 stats. */
void qa_q2_from_player_classic(const qa_q2_player_state *state, qa_q2_wire_player *out);
void qa_q2_from_player_rerelease(const qa_q2_rerelease_player_state *state,
                                 qa_q2_wire_player *out);
/* TS toQ2Player / toQ2RereleasePlayer (stats copied to storage). */
void qa_q2_to_player(const qa_q2_wire_player *wire, double stats_storage[64],
                     qa_q2_player_state *out);
void qa_q2_to_rerelease_player(const qa_q2_wire_player *wire,
                               double stats_storage[64],
                               qa_q2_rerelease_player_state *out);
/* TS fromQ2Command / toQ2Command / toQ2RereleaseCommand. */
void qa_q2_from_command_classic(const qa_q2_user_command *command,
                                qa_q2_wire_usercmd *out);
void qa_q2_from_command_rerelease(const qa_q2_rerelease_user_command *command,
                                  qa_q2_wire_usercmd *out);
void qa_q2_to_command(const qa_q2_wire_usercmd *wire, qa_q2_user_command *out);
void qa_q2_to_rerelease_command(const qa_q2_wire_usercmd *wire, double server_frame,
                                qa_q2_rerelease_user_command *out);

#endif

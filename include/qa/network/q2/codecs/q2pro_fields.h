/*
 * Port of src/network/q2/codecs/q2pro-fields.ts. TS bigint bit math
 * becomes uint64_t; readQ2ProEntityBits returns both the TS number
 * form and the exact uint64. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_Q2PRO_FIELDS_H
#define QA_NETWORK_Q2_CODECS_Q2PRO_FIELDS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/state.h"

/* TS Q2ProFeatures. */
typedef struct qa_q2pro_features {
  double revision;
  double flags;
} qa_q2pro_features;

bool qa_q2pro_extensions(qa_q2pro_features features);
bool qa_q2pro_extensions_v2(qa_q2pro_features features);

/* TS readQ2ProInt23(message, previous=0) / writeQ2ProInt23 over TS numbers.
 * The write raises RangeError for a non-integer or out-of-range current. */
double qa_q2pro_read_int23_number(qa_q2_sizebuf *message, double previous);
void qa_q2pro_write_int23_number(qa_q2_sizebuf *message, double current, double previous);
/* Int32Array-element forms: the read result is the TS number stored into an
 * Int32Array element (ToInt32). */
int32_t qa_q2pro_read_int23(qa_q2_sizebuf *message, int32_t previous);
void qa_q2pro_write_int23(qa_q2_sizebuf *message, int32_t current, int32_t previous);
/* TS readQ2ProVar64 / writeQ2ProVar64 (Error raises, catchable with qa_try). */
uint64_t qa_q2pro_read_var64(qa_q2_sizebuf *message);
void qa_q2pro_write_var64(qa_q2_sizebuf *message, uint64_t value);
/* TS readQ2ProFog / q2proFogBits / writeQ2ProFog. */
void qa_q2pro_read_fog(qa_q2_sizebuf *message, const qa_q2_pro_player_fog *from,
                       qa_q2_pro_player_fog *out);
int32_t qa_q2pro_fog_bits(const qa_q2_pro_player_fog *from,
                          const qa_q2_pro_player_fog *to);
void qa_q2pro_write_fog(qa_q2_sizebuf *message, int32_t bits,
                        const qa_q2_pro_player_fog *fog);
/* TS readQ2ProEntityBits / writeQ2ProEntity / readQ2ProEntity. */
void qa_q2pro_read_entity_bits(qa_q2_sizebuf *message, qa_q2_entity_bits *out);
void qa_q2pro_write_entity(qa_q2_sizebuf *message, qa_q2pro_features features,
                           const qa_q2_wire_entity *from,
                           const qa_q2_wire_entity *to, bool force,
                           bool new_entity);
void qa_q2pro_read_entity(qa_q2_sizebuf *message, qa_q2pro_features features,
                          const qa_q2_wire_entity *from,
                          qa_q2_wire_entity *to, double number, uint64_t bits);

#endif

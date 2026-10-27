/* Port of src/network/q2/solid.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q2_SOLID_H
#define QA_NETWORK_Q2_SOLID_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"

/* TS Q2SolidEncoding: "short" | "r1q2" | "q2pro-v2". */
typedef enum qa_q2_solid_encoding {
  QA_Q2_SOLID_SHORT, QA_Q2_SOLID_R1Q2, QA_Q2_SOLID_Q2PRO_V2
} qa_q2_solid_encoding;

/* TS q2SolidEncoding(protocol, extendedGame=false). Unknown kinds fall
 * through every TS case and return undefined; the C form returns false
 * and leaves *out untouched in that case. */
bool qa_q2_get_solid_encoding(qa_q2_protocol_identity protocol,
                              bool extended_game,
                              qa_q2_solid_encoding *out);
/* TS packQ2Solid / unpackQ2Solid. */
uint32_t qa_q2_pack_solid(qa_bounds bounds, qa_q2_solid_encoding encoding);
qa_bounds qa_q2_unpack_solid(double solid, qa_q2_solid_encoding encoding);

#endif

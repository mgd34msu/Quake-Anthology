/* Port of src/network/q2/solid.ts. GPL-2.0-or-later. */
#include "qa/network/q2/solid.h"

#include "qa/foundation/js.h"

#include <math.h>
#include <string.h>

bool qa_q2_get_solid_encoding(qa_q2_protocol_identity protocol,
                              bool extended_game,
                              qa_q2_solid_encoding *out) {
  const char *kind = protocol.kind != NULL ? protocol.kind : "";
  if (strcmp(kind, "q2-classic") == 0) {
    *out = QA_Q2_SOLID_SHORT;
    return true;
  }
  if (strcmp(kind, "q2-r1q2") == 0) {
    *out = protocol.revision >= 1905 ? QA_Q2_SOLID_R1Q2 : QA_Q2_SOLID_SHORT;
    return true;
  }
  if (strcmp(kind, "q2-q2pro") == 0) {
    *out = extended_game ? QA_Q2_SOLID_Q2PRO_V2 : QA_Q2_SOLID_R1Q2;
    return true;
  }
  if (strcmp(kind, "q2-rerelease") == 0 ||
      strcmp(kind, "q2-private-classic") == 0 ||
      strcmp(kind, "q2-kex") == 0 || strcmp(kind, "q2-kex-demo") == 0) {
    *out = QA_Q2_SOLID_Q2PRO_V2;
    return true;
  }
  return false;
}

/* TS clamp: Math.max(min, Math.min(max, Math.trunc(value))). */
static int32_t clamp_trunc(double value, int32_t minimum, int32_t maximum) {
  double t = trunc(value);
  if (t < (double)minimum) t = (double)minimum;
  if (t > (double)maximum) t = (double)maximum;
  return qa_js_to_int32(t); /* NaN bounds stay NaN in TS; the | and << make them 0 */
}

uint32_t qa_q2_pack_solid(qa_bounds bounds, qa_q2_solid_encoding encoding) {
  if (encoding == QA_Q2_SOLID_SHORT) {
    int32_t v = clamp_trunc(bounds.max.x / 8.0, 1, 31) |
                (clamp_trunc(-bounds.min.z / 8.0, 1, 31) << 5) |
                (clamp_trunc((bounds.max.z + 32.0) / 8.0, 1, 63) << 10);
    return (uint32_t)v;
  }
  if (encoding == QA_Q2_SOLID_R1Q2) {
    /* Math.fround(bounds.max.z + 32768) staged rounding. */
    double up = (double)(float)(bounds.max.z + 32768.0);
    uint32_t v = (uint32_t)clamp_trunc(bounds.max.x, 1, 255) |
                 ((uint32_t)clamp_trunc(-bounds.min.z, 0, 255) << 8) |
                 ((uint32_t)clamp_trunc(up, 0, 65535) << 16);
    return v;
  }
  double up = (double)(float)(bounds.max.z + 32.0);
  uint32_t v = (uint32_t)clamp_trunc(bounds.max.x, 1, 255) |
               ((uint32_t)clamp_trunc(bounds.max.y, 1, 255) << 8) |
               ((uint32_t)clamp_trunc(-bounds.min.z, 0, 255) << 16) |
               ((uint32_t)clamp_trunc(up, 0, 255) << 24);
  return v;
}

qa_bounds qa_q2_unpack_solid(double solid_value, qa_q2_solid_encoding encoding) {
  /* TS bitwise ops apply ToInt32/ToUint32 to the number first. */
  uint32_t solid = qa_js_to_uint32(solid_value);
  double x = encoding == QA_Q2_SOLID_SHORT ? (double)(solid & 31u) * 8.0
                                            : (double)(solid & 255u);
  double y = encoding == QA_Q2_SOLID_Q2PRO_V2 ? (double)((solid >> 8) & 255u) : x;
  double down = encoding == QA_Q2_SOLID_SHORT
                    ? (double)((solid >> 5) & 31u) * 8.0
                    : encoding == QA_Q2_SOLID_R1Q2
                          ? (double)((solid >> 8) & 255u)
                          : (double)((solid >> 16) & 255u);
  double up = encoding == QA_Q2_SOLID_SHORT
                  ? (double)((solid >> 10) & 63u) * 8.0 - 32.0
                  : encoding == QA_Q2_SOLID_R1Q2
                        ? (double)((solid >> 16) & 65535u) - 32768.0
                        : (double)((solid >> 24) & 255u) - 32.0;
  qa_bounds out;
  out.min.x = -x;
  out.min.y = -y;
  out.min.z = -down;
  out.max.x = x;
  out.max.y = y;
  out.max.z = up;
  return out;
}

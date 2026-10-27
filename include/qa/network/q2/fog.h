/* Port of src/network/q2/fog.ts (types only). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q2_FOG_H
#define QA_NETWORK_Q2_FOG_H

#include <stdint.h>

/* TS SvcFogDataBitsT bit values. */
#define QA_Q2_SVC_FOG_BIT_DENSITY (1 << 0)
#define QA_Q2_SVC_FOG_BIT_R (1 << 1)
#define QA_Q2_SVC_FOG_BIT_G (1 << 2)
#define QA_Q2_SVC_FOG_BIT_B (1 << 3)
#define QA_Q2_SVC_FOG_BIT_TIME (1 << 4)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_FALLOFF (1 << 5)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_DENSITY (1 << 6)
#define QA_Q2_SVC_FOG_BIT_MORE_BITS (1 << 7)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_R (1 << 8)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_G (1 << 9)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_B (1 << 10)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_DIST (1 << 11)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_R (1 << 12)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_G (1 << 13)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_B (1 << 14)
#define QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_DIST (1 << 15)

/* TS SvcFogDataT (bits is TS SvcFogDataBitsT = number). */
typedef struct qa_q2_svc_fog_data {
  double bits;
  double density, skyfactor, red, green, blue, time;
  double hf_falloff, hf_density;
  double hf_start_r, hf_start_g, hf_start_b, hf_start_dist;
  double hf_end_r, hf_end_g, hf_end_b, hf_end_dist;
} qa_q2_svc_fog_data;

void qa_q2_svc_fog_data_init(qa_q2_svc_fog_data *data);

#endif

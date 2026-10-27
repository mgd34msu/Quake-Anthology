/*
 * Port of src/network/q2/codecs/q2repro.ts (rerelease + classic).
 * The context closure helpers become qa_q2repro_* functions; both
 * codecs share the context's pending frame extraflags as in TS.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_Q2REPRO_H
#define QA_NETWORK_Q2_CODECS_Q2REPRO_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/vanilla.h"
#include "qa/network/q2/fog.h"
#include "qa/network/q2/message.h"

#define QA_Q2REPRO_HI_SCALE 1
#define QA_Q2REPRO_HI_MOREFX16 2
#define QA_Q2REPRO_SOUND_FLAG_VOLUME (1 << 14)
#define QA_Q2REPRO_SOUND_FLAG_ATTENUATION (1 << 15)
#define QA_Q2REPRO_GUNINDEX_BITS 13
#define QA_Q2REPRO_GUNINDEX_MASK ((1 << QA_Q2REPRO_GUNINDEX_BITS) - 1)
#define QA_Q2REPRO_VIEWOFFSET_SCALE 16
#define QA_Q2REPRO_KICK_ANGLE_SCALE 1024

typedef struct qa_q2repro_context qa_q2repro_context;
typedef struct qa_q2repro_binding {
  qa_q2repro_context *context;
  bool classic;
} qa_q2repro_binding;

/* TS createRereleaseContext(net_message) result. */
struct qa_q2repro_context {
  qa_q2_sizebuf *net_message;
  qa_q2_vanilla_context vanilla;
  qa_q2_protocol_codec codec;         /* TS Q2REPRO_CODEC */
  qa_q2_protocol_codec classic_codec; /* TS Q2REPRO_CLASSIC_CODEC */
  qa_q2repro_binding binding;
  qa_q2repro_binding classic_binding;
  int32_t pending_frame_extraflags;
};

void qa_q2repro_context_init(qa_q2repro_context *ctx, qa_q2_sizebuf *net_message);

/* TS context helpers. */
double qa_q2repro_combine_bits(double lo, double hi);
bool qa_q2repro_bits_has_hi(double bits, double hi_flag);
int32_t qa_q2repro_encode_alpha(double x);
double qa_q2repro_decode_alpha(double b);
int32_t qa_q2repro_encode_scale(double x);
double qa_q2repro_decode_scale(double b);
int32_t qa_q2repro_encode_loop_volume(double x);
double qa_q2repro_decode_loop_volume(double b);
int32_t qa_q2repro_encode_loop_attenuation(double x);
double qa_q2repro_decode_loop_attenuation(double b);
int32_t qa_q2repro_pm_float_to_short(double f);
void qa_q2repro_write_entity_bits_wide(qa_q2_sizebuf *msg, uint32_t lo0, uint32_t hi,
                                       double entnum);
void qa_q2repro_read_entity_bits_wide(qa_q2repro_context *ctx,
                                      qa_q2_entity_bits *out);
void qa_q2repro_read_fog(qa_q2repro_context *ctx, qa_q2_svc_fog_data *out);

#endif

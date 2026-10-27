/*
 * Port of src/network/q2/codec.ts: per-connection Q2WireCodec.
 * C accommodations: the selected codec is a pointer into the owned
 * sub-contexts; nested()/decodeWithQ2Codec generics become fn + ctx
 * with a void* result slot owned by the callback. Rerelease/kex
 * sub-contexts are public fields as in TS. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODEC_H
#define QA_NETWORK_Q2_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/protocol.h"
#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/kexdemo.h"
#include "qa/network/q2/codecs/q2pro.h"
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/network/q2/codecs/r1q2.h"
#include "qa/network/q2/codecs/vanilla.h"
#include "qa/network/q2/message.h"

/* TS Q2CodecSupport. */
typedef struct qa_q2_codec_support {
  bool is_supported; /* false is TS kind "unbound" */
  bool encode;
  bool decode;
  char reason[256];
} qa_q2_codec_support;

/* TS q2CodecSupport / negotiatedR1Q2Protocol. negotiated aborts on an
 * unsupported reported revision like TS (has_reported false is
 * TS undefined). */
void qa_q2_get_codec_support(qa_q2_protocol_identity protocol,
                             qa_q2_codec_support *out);
void qa_q2_negotiated_r1q2_protocol(qa_q2_protocol_identity offered,
                                    bool has_reported, double reported,
                                    qa_q2_protocol_identity *out);

/* TS Q2WireCodec. */
typedef struct qa_q2_wire_codec {
  qa_q2_sizebuf message;
  qa_q2_protocol_identity offered_protocol;
  qa_q2_protocol_identity selected_protocol;
  qa_q2_protocol_codec *selected_codec;
  qa_q2_vanilla_context vanilla;
  qa_q2repro_context rerelease;
  qa_q2_kex_context kex;
  qa_q2_r1_context r1;
  qa_q2pro_context q2pro;
} qa_q2_wire_codec;

/* False only on OOM; unbound support aborts like the TS constructor. */
bool qa_q2_wire_codec_init(qa_q2_wire_codec *wire,
                           qa_q2_protocol_identity offered_protocol);
void qa_q2_wire_codec_destroy(qa_q2_wire_codec *wire);
/* TS get protocol / get codec (get_ infix: names must differ from types). */
qa_q2_protocol_identity qa_q2_wire_codec_get_protocol(const qa_q2_wire_codec *wire);
qa_q2_protocol_codec *qa_q2_wire_codec_get_codec(qa_q2_wire_codec *wire);
/* TS acceptServerRevision / acceptQ2ProFeatures. */
void qa_q2_wire_codec_accept_server_revision(qa_q2_wire_codec *wire,
                                             bool has_reported, double reported);
void qa_q2_wire_codec_accept_q2pro_features(qa_q2_wire_codec *wire,
                                            double revision, double flags);
double qa_q2_wire_codec_q2pro_revision(const qa_q2_wire_codec *wire);
bool qa_q2_wire_codec_q2pro_extended(const qa_q2_wire_codec *wire);
bool qa_q2_wire_codec_q2pro_extended_v2(const qa_q2_wire_codec *wire);
bool qa_q2_wire_codec_floating_coordinates(const qa_q2_wire_codec *wire);
bool qa_q2_wire_codec_wide_indexes(const qa_q2_wire_codec *wire);
/* TS begin / finish / opcode. begin copies bytes into owned storage. */
bool qa_q2_wire_codec_begin(qa_q2_wire_codec *wire, const uint8_t *bytes,
                            size_t length);
void qa_q2_wire_codec_finish(qa_q2_wire_codec *wire);
int32_t qa_q2_wire_codec_opcode(qa_q2_wire_codec *wire, int32_t raw);

typedef void (*qa_q2_wire_read_fn)(void *ctx, qa_q2_protocol_codec *codec,
                                   qa_q2_sizebuf *message);
/* TS nested(bytes, read) / decodeWithQ2Codec(wire, bytes, read). */
bool qa_q2_wire_codec_nested(qa_q2_wire_codec *wire, const uint8_t *bytes,
                             size_t length, qa_q2_wire_read_fn read, void *ctx);
bool qa_q2_decode_with_codec(qa_q2_wire_codec *wire, const uint8_t *bytes,
                             size_t length, qa_q2_wire_read_fn read, void *ctx);

#endif

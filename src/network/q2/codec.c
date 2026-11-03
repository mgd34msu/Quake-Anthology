/* Port of src/network/q2/codec.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codec.h"
#include "qa/network/common/js_number.h"

#include <stdlib.h>

#include "qa/foundation/support.h"
#include <string.h>
#include <math.h>

#include "qa/network/q2/constants.h"

static bool kind_is(const qa_q2_protocol_identity *p, const char *kind) {
  return p->kind != NULL && strcmp(p->kind, kind) == 0;
}

void qa_q2_get_codec_support(qa_q2_protocol_identity protocol,
                             qa_q2_codec_support *out) {
  memset(out, 0, sizeof *out);
  if (kind_is(&protocol, "q2-q2pro") && protocol.revision == 1016) {
    out->is_supported = false;
    const char *reason = "Q2PRO revision 1016 is reserved by the native protocol";
    size_t n = strlen(reason);
    memcpy(out->reason, reason, n + 1);
    return;
  }
  out->is_supported = true;
  out->encode = true;
  out->decode = true;
}

void qa_q2_negotiated_r1q2_protocol(qa_q2_protocol_identity offered,
                                    bool has_reported, double reported,
                                    qa_q2_protocol_identity *out) {
  if (!has_reported || (reported != 1903 && reported != 1904 && reported != 1905)) {
    if (has_reported) qa_q2_throw_error("Unsupported R1Q2 server revision %s", QA_JS_NUMBER(reported));
    qa_q2_throw_error("Unsupported R1Q2 server revision undefined");
  }
  double revision = reported < (double)offered.revision ? reported
                                                         : (double)offered.revision;
  out->kind = "q2-r1q2";
  out->version = 35;
  out->flags = 0;
  out->revision = (int64_t)revision;
}

bool qa_q2_wire_codec_init(qa_q2_wire_codec *wire,
                           qa_q2_protocol_identity offered_protocol) {
  memset(wire, 0, sizeof *wire);
  wire->offered_protocol = offered_protocol;
  wire->selected_protocol = offered_protocol;
  qa_q2_codec_support support;
  qa_q2_get_codec_support(offered_protocol, &support);
  if (!support.is_supported) qa_q2_throw_error("%s", support.reason);
  if (!qa_q2_message_create(&wire->message, 0)) return false;
  qa_q2_vanilla_context_init(&wire->vanilla, &wire->message);
  qa_q2repro_context_init(&wire->rerelease, &wire->message);
  qa_q2_kex_context_init(&wire->kex, &wire->message);
  qa_q2_r1_context_init(&wire->r1, &wire->message);
  qa_q2pro_context_init(&wire->q2pro, &wire->message);
  const qa_q2_protocol_identity *p = &offered_protocol;
  if (kind_is(p, "q2-classic")) {
    wire->selected_codec = &wire->vanilla.codec;
  } else if (kind_is(p, "q2-r1q2")) {
    wire->selected_codec = qa_q2_r1_create_codec(&wire->r1, (double)p->revision);
  } else if (kind_is(p, "q2-q2pro")) {
    wire->selected_codec = qa_q2pro_create_codec(&wire->q2pro, (double)p->revision);
  } else if (kind_is(p, "q2-rerelease")) {
    wire->selected_codec = &wire->rerelease.codec;
  } else if (kind_is(p, "q2-private-classic")) {
    wire->selected_codec = &wire->rerelease.classic_codec;
  } else if (kind_is(p, "q2-kex") || kind_is(p, "q2-kex-demo")) {
    qa_q2_kex_set_protocol(&wire->kex, (double)p->version);
    wire->selected_codec = &wire->kex.codec;
  } else {
    /* TS leaves selectedCodec unset for unknown kinds; keep vanilla
     * storage addressable but never selected by protocol checks. */
    wire->selected_codec = &wire->vanilla.codec;
  }
  return true;
}

void qa_q2_wire_codec_destroy(qa_q2_wire_codec *wire) {
  qa_q2_message_destroy(&wire->message);
  qa_q2_kex_context_destroy(&wire->kex);
}

qa_q2_protocol_identity qa_q2_wire_codec_get_protocol(const qa_q2_wire_codec *wire) {
  return wire->selected_protocol;
}
qa_q2_protocol_codec *qa_q2_wire_codec_get_codec(qa_q2_wire_codec *wire) {
  return wire->selected_codec;
}

void qa_q2_wire_codec_accept_server_revision(qa_q2_wire_codec *wire,
                                             bool has_reported, double reported) {
  if (!kind_is(&wire->offered_protocol, "q2-r1q2")) return;
  qa_q2_protocol_identity protocol;
  qa_q2_negotiated_r1q2_protocol(wire->offered_protocol, has_reported, reported,
                                 &protocol);
  wire->selected_codec = qa_q2_r1_create_codec(&wire->r1, (double)protocol.revision);
  wire->selected_protocol = protocol;
}

void qa_q2_wire_codec_accept_q2pro_features(qa_q2_wire_codec *wire,
                                            double revision, double flags) {
  bool integer_flags = isfinite(flags) && flags == trunc(flags); /* Number.isInteger */
  if (!kind_is(&wire->selected_protocol, "q2-q2pro") ||
      (double)wire->selected_protocol.revision != revision || !integer_flags ||
      flags < 0 || flags > 65535) {
    qa_q2_throw_error("Recorded Q2PRO features disagree with protocol identity");
  }
  wire->q2pro.features.revision = revision;
  wire->q2pro.features.flags = flags;
}

double qa_q2_wire_codec_q2pro_revision(const qa_q2_wire_codec *wire) {
  return wire->q2pro.features.revision;
}
bool qa_q2_wire_codec_q2pro_extended(const qa_q2_wire_codec *wire) {
  return kind_is(&wire->selected_protocol, "q2-q2pro") &&
         qa_q2pro_extensions(wire->q2pro.features);
}
bool qa_q2_wire_codec_q2pro_extended_v2(const qa_q2_wire_codec *wire) {
  return kind_is(&wire->selected_protocol, "q2-q2pro") &&
         qa_q2pro_extensions_v2(wire->q2pro.features);
}
bool qa_q2_wire_codec_floating_coordinates(const qa_q2_wire_codec *wire) {
  return kind_is(&wire->selected_protocol, "q2-rerelease") ||
         kind_is(&wire->selected_protocol, "q2-private-classic") ||
         kind_is(&wire->selected_protocol, "q2-kex");
}
bool qa_q2_wire_codec_wide_indexes(const qa_q2_wire_codec *wire) {
  return wire->selected_protocol.version >= 1038;
}

bool qa_q2_wire_codec_begin(qa_q2_wire_codec *wire, const uint8_t *bytes,
                            size_t length) {
  uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
  if (copy == NULL) return false;
  if (length > 0) memcpy(copy, bytes, length);
  qa_q2_message_load(&wire->message, copy, length);
  wire->message.owns_data = true;
  return true;
}

void qa_q2_wire_codec_finish(qa_q2_wire_codec *wire) {
  qa_q2_message_check_read(&wire->message);
}

int32_t qa_q2_wire_codec_opcode(qa_q2_wire_codec *wire, int32_t raw) {
  if (kind_is(&wire->selected_protocol, "q2-r1q2")) {
    qa_q2_r1_set_frame_extrabits(&wire->r1, raw & 0xe0);
    return raw & 31;
  }
  if (kind_is(&wire->selected_protocol, "q2-q2pro")) {
    qa_q2pro_note_frame_opcode_extrabits(&wire->q2pro, raw & 0xe0);
    return raw & 31;
  }
  return raw;
}

typedef struct nested_scope {
  qa_q2_wire_codec *wire;
  qa_q2_wire_read_fn read;
  void *ctx;
} nested_scope;
static void nested_body(void *arg) {
  nested_scope *scope = (nested_scope *)arg;
  scope->read(scope->ctx, scope->wire->selected_codec, &scope->wire->message);
  qa_q2_wire_codec_finish(scope->wire);
}

bool qa_q2_wire_codec_nested(qa_q2_wire_codec *wire, const uint8_t *bytes,
                             size_t length, qa_q2_wire_read_fn read, void *ctx) {
  uint8_t *prev_data = wire->message.data;
  size_t prev_len = wire->message.data_length;
  int64_t prev_cursize = wire->message.cursize;
  int64_t prev_readcount = wire->message.readcount;
  int64_t prev_maxsize = wire->message.maxsize;
  bool prev_owns = wire->message.owns_data;
  uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
  if (copy == NULL) return false;
  if (length > 0) memcpy(copy, bytes, length);
  /* Install without freeing the previous buffer (TS keeps it alive). */
  wire->message.owns_data = false;
  qa_q2_sz_init(&wire->message, copy, length, (int64_t)length);
  wire->message.cursize = (int64_t)length;
  /* TS try/finally: the containing packet is restored whether or not the nested read throws. */
  nested_scope scope = {wire, read, ctx};
  qa_caught_error caught;
  bool ok = qa_try(nested_body, &scope, &caught);
  free(copy);
  wire->message.data = prev_data;
  wire->message.data_length = prev_len;
  wire->message.cursize = prev_cursize;
  wire->message.readcount = prev_readcount;
  wire->message.maxsize = prev_maxsize;
  wire->message.owns_data = prev_owns;
  if (!ok) qa_rethrow(&caught);
  return true;
}

bool qa_q2_decode_with_codec(qa_q2_wire_codec *wire, const uint8_t *bytes,
                             size_t length, qa_q2_wire_read_fn read, void *ctx) {
  if (!qa_q2_wire_codec_begin(wire, bytes, length)) return false;
  read(ctx, wire->selected_codec, &wire->message);
  qa_q2_wire_codec_finish(wire);
  return true;
}

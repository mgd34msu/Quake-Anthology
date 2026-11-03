/*
 * Port of src/network/q2/mvd-encoding.ts. Capture Maps become entry
 * lists (borrowed on input; the encoder deep-copies its previous
 * capture). Byte results are qa_q2_mvd_byte_block lists.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_MVD_ENCODING_H
#define QA_NETWORK_Q2_MVD_ENCODING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/mvd_recording.h"
#include "qa/network/q2/state.h"

/* TS MvdRecipient. */
typedef enum qa_q2_mvd_recipient_kind {
  QA_Q2_MVD_RECIPIENT_ALL, QA_Q2_MVD_RECIPIENT_PLAYER,
  QA_Q2_MVD_RECIPIENT_PVS, QA_Q2_MVD_RECIPIENT_PHS
} qa_q2_mvd_recipient_kind;
typedef struct qa_q2_mvd_recipient {
  qa_q2_mvd_recipient_kind kind;
  double number; /* player */
  double leaf;   /* pvs/phs */
} qa_q2_mvd_recipient;

/* TS MvdEmission (bytes borrowed). */
typedef struct qa_q2_mvd_emission {
  qa_q2_mvd_recipient recipient;
  bool reliable;
  const uint8_t *bytes;
  size_t length;
} qa_q2_mvd_emission;

/* configStrings Map entry (value borrowed on input). */
typedef struct qa_q2_mvd_capture_config {
  double index;
  const char *value;
} qa_q2_mvd_capture_config;
/* players Map entry. */
typedef struct qa_q2_mvd_capture_player {
  double number;
  qa_q2_wire_player player;
} qa_q2_mvd_capture_player;

/* TS MvdCapture (all spans borrowed). */
typedef struct qa_q2_mvd_capture {
  double revision, flags, servercount;
  const char *gamedir;
  double dummy;
  const qa_q2_mvd_capture_config *config_strings;
  size_t config_string_count;
  const uint8_t *portal_bits;
  size_t portal_bits_length;
  const qa_q2_mvd_capture_player *players;
  size_t player_count;
  const qa_q2_wire_entity *entities;
  size_t entity_count;
  const qa_q2_mvd_emission *messages;
  size_t message_count;
} qa_q2_mvd_capture;

/* TS encodeMvdEmission; invalid events raise the TS RangeError. Malloc'd bytes or
 * NULL only on OOM. */
uint8_t *qa_q2_encode_mvd_emission(const qa_q2_mvd_emission *event,
                                   size_t *out_length);

/* TS MvdEncoder (previous capture owned). */
typedef struct qa_q2_mvd_encoder {
  bool has_previous;
  double revision, flags, servercount;
  qa_q2_mvd_capture_config *configs; /* values owned */
  size_t config_count;
  qa_q2_mvd_capture_player *players; /* owned */
  size_t player_count;
  qa_q2_wire_entity *entities; /* owned */
  size_t entity_count;
} qa_q2_mvd_encoder;

void qa_q2_mvd_encoder_init(qa_q2_mvd_encoder *encoder);
void qa_q2_mvd_encoder_destroy(qa_q2_mvd_encoder *encoder);
void qa_q2_mvd_encoder_reset(qa_q2_mvd_encoder *encoder);
/* TS capture(capture); invalid captures raise the TS RangeError/Error with nothing
 * held. False only on OOM. */
bool qa_q2_mvd_encoder_capture(qa_q2_mvd_encoder *encoder,
                               const qa_q2_mvd_capture *capture,
                               qa_q2_mvd_byte_block **out, size_t *out_count);

#endif

/*
 * Port of src/network/q2/server-demo.ts. Signon configStrings are
 * entry lists sorted by index on encode; the generator becomes a
 * collected malloc'd list. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_SERVER_DEMO_H
#define QA_NETWORK_Q2_SERVER_DEMO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/server_messages.h"
#include "qa/network/q2/state.h"

/* One configStrings Map entry (borrowed on encode, owned on read). */
typedef struct qa_q2_server_demo_config {
  double index;
  const char *value;
} qa_q2_server_demo_config;

/* TS Q2ServerDemoState (gamedir owned on read). */
typedef struct qa_q2_server_demo_state {
  double servercount;
  char *gamedir;
  qa_q2_server_demo_config *config_strings;
  char **config_values; /* owned value strings on read */
  size_t config_count;
} qa_q2_server_demo_state;

/* TS Q2ServerDemoRecord. */
typedef enum qa_q2_server_demo_kind {
  QA_Q2_SERVER_DEMO_SIGNON, QA_Q2_SERVER_DEMO_FRAME
} qa_q2_server_demo_kind;
typedef struct qa_q2_server_demo_record {
  qa_q2_server_demo_kind kind;
  qa_q2_server_demo_state state; /* signon */
  double server_frame;           /* frame */
  qa_q2_wire_entity *entities;   /* frame (owned) */
  size_t entity_count;
  qa_q2_server_record *multicasts; /* frame (owned) */
  size_t multicast_count;
} qa_q2_server_demo_record;

void qa_q2_server_demo_records_free(qa_q2_server_demo_record *records, size_t count);

/* TS encodeQ2ServerDemoSignon / encodeQ2ServerDemoFrame /
 * readQ2ServerDemo. Multicast inputs are byte spans. */
typedef struct qa_q2_byte_span {
  const uint8_t *bytes;
  size_t length;
} qa_q2_byte_span;

uint8_t *qa_q2_encode_server_demo_signon(const qa_q2_server_demo_state *state,
                                         size_t *out_length);
uint8_t *qa_q2_encode_server_demo_frame(double server_frame,
                                        const qa_q2_wire_entity *entities,
                                        size_t entity_count,
                                        const qa_q2_byte_span *multicasts,
                                        size_t multicast_count, size_t *out_length);
bool qa_q2_read_server_demo(const uint8_t *bytes, size_t length,
                            qa_q2_server_demo_record **out, size_t *out_count);

#endif

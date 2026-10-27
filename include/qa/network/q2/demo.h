/*
 * Port of src/network/q2/demo.ts: DM2 record framing. TS generators
 * become collected malloc'd lists; playQ2Demo yields one record-list
 * per demo record (nested lists with a combined free helper).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_DEMO_H
#define QA_NETWORK_Q2_DEMO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/protocol.h"
#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/server_messages.h"

/* TS Q2DemoRecord. */
typedef struct qa_q2_demo_record {
  double offset;
  uint8_t *bytes; /* owned */
  size_t bytes_length;
} qa_q2_demo_record;

void qa_q2_demo_records_free(qa_q2_demo_record *records, size_t count);

/* TS readQ2Demo generator, collected. False only on OOM. */
bool qa_q2_read_demo(const uint8_t *bytes, size_t length, qa_q2_demo_record **out,
                     size_t *out_count);
/* TS writeQ2DemoRecord / finishQ2Demo: malloc'd (NULL on OOM). */
uint8_t *qa_q2_write_demo_record(const uint8_t *bytes, size_t length,
                                 size_t *out_length);
uint8_t *qa_q2_finish_demo(size_t *out_length);

/* One playQ2Demo step (records owned). */
typedef struct qa_q2_demo_step {
  qa_q2_server_record *records;
  size_t record_count;
} qa_q2_demo_step;

void qa_q2_demo_steps_free(qa_q2_demo_step *steps, size_t count);
/* TS playQ2Demo generator, collected. False only on OOM. */
bool qa_q2_play_demo(const uint8_t *bytes, size_t length,
                     qa_q2_server_message_reader *reader, qa_q2_demo_step **out,
                     size_t *out_count);

/* TS Q2DemoHeader / readQ2DemoHeader. False only on OOM. */
typedef struct qa_q2_demo_header {
  double recorded_version;
  qa_q2_protocol_identity protocol;
  qa_q2_server_data_result data;
} qa_q2_demo_header;

bool qa_q2_read_demo_header(const uint8_t *bytes, size_t length,
                            qa_q2_demo_header *out);

#endif

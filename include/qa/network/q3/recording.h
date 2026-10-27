/* Port of src/network/q3/recording.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_RECORDING_H
#define QA_NETWORK_Q3_RECORDING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/client.h"
/* TS Q3DemoSink as fn+ctx. */
typedef void (*qa_q3_demo_sink_write_fn)(void *ctx, const uint8_t *bytes, size_t len);
typedef void (*qa_q3_demo_sink_close_fn)(void *ctx);
typedef struct qa_q3_demo_sink {
  qa_q3_demo_sink_write_fn write_bytes; qa_q3_demo_sink_close_fn close; void *ctx;
} qa_q3_demo_sink;
bool qa_q3_demo_gamestate(qa_q3_client_connection *client, int64_t *sequence_out, uint8_t **message_out, size_t *message_len_out);
typedef struct qa_q3_demo_recording qa_q3_demo_recording;
bool qa_q3_demo_recording_init(qa_q3_demo_recording **out, qa_q3_client_connection *client, const qa_q3_demo_sink *file);
void qa_q3_demo_recording_destroy(qa_q3_demo_recording *r);
qa_q3_client_connection *qa_q3_demo_recording_client(const qa_q3_demo_recording *r);
void qa_q3_demo_recording_append(qa_q3_demo_recording *r, const uint8_t *bytes, size_t len);
void qa_q3_demo_recording_stop(qa_q3_demo_recording *r);
void qa_q3_demo_recording_close(qa_q3_demo_recording *r);
#endif

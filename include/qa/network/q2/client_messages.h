/*
 * Port of src/network/q2/client-messages.ts. Records and their raw
 * spans are malloc'd (free helper); text is owned bytes. The replay
 * think callback is fn + ctx. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CLIENT_MESSAGES_H
#define QA_NETWORK_Q2_CLIENT_MESSAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/state.h"

/* TS Q2ClientEvent kinds. */
typedef enum qa_q2_client_event_kind {
  QA_Q2_CLIENT_NOP, QA_Q2_CLIENT_USERINFO, QA_Q2_CLIENT_COMMAND,
  QA_Q2_CLIENT_USERINFO_DELTA, QA_Q2_CLIENT_SETTING, QA_Q2_CLIENT_MOVE,
  QA_Q2_CLIENT_BATCH_MOVE
} qa_q2_client_event_kind;

/* TS Q2ClientEvent (text owned; batch owned). */
typedef struct qa_q2_client_event {
  qa_q2_client_event_kind kind;
  char *text; /* userinfo/command */
  qa_q2_clc_userinfo_delta delta;
  qa_q2_clc_client_setting setting;
  double last_frame; /* move */
  qa_q2_wire_usercmd commands[3]; /* move */
  qa_q2_clc_batch_move batch; /* batch-move */
} qa_q2_client_event;

/* TS Q2ClientRecord. */
typedef struct qa_q2_client_record {
  qa_q2_client_event event;
  uint8_t *raw; /* owned */
  size_t raw_length;
  double seat;
} qa_q2_client_record;

void qa_q2_client_records_free(qa_q2_client_record *records, size_t count);

/* TS readQ2ClientMessages(wire, bytes, sequence, seats=1).
 * False only on OOM; protocol failures abort like TS. */
bool qa_q2_read_client_messages(qa_q2_wire_codec *wire, const uint8_t *bytes,
                                size_t length, double sequence, double seats,
                                qa_q2_client_record **out, size_t *out_count);
/* TS encodeQ2Move / encodeQ2BatchMove / encodeQ2ClientControl.
 * Malloc'd bytes (NULL only on OOM). */
uint8_t *qa_q2_encode_move(qa_q2_wire_codec *wire, double sequence, double last_frame,
                           const qa_q2_wire_usercmd commands[3], size_t *out_length);
uint8_t *qa_q2_encode_batch_move(qa_q2_wire_codec *wire, bool has_last_frame,
                                 double last_frame,
                                 const qa_q2_clc_batch_move_frame *frames,
                                 size_t frame_count, size_t *out_length);
uint8_t *qa_q2_encode_client_control(const qa_q2_client_event *event, bool kex,
                                     size_t *out_length);

typedef void (*qa_q2_command_think_fn)(void *ctx, const qa_q2_wire_usercmd *command);

/* TS Q2CommandReplay. */
typedef struct qa_q2_command_replay {
  qa_q2_wire_usercmd previous;
  double last_frame;
} qa_q2_command_replay;

void qa_q2_command_replay_init(qa_q2_command_replay *replay);
/* Event must be move or batch-move (TS Extract). */
void qa_q2_command_replay_execute(qa_q2_command_replay *replay,
                                  const qa_q2_client_event *event, double dropped,
                                  qa_q2_command_think_fn think, void *ctx);

/* TS Q2RateWindow. */
typedef struct qa_q2_rate_window {
  uint32_t sizes[10];
  double suppressed;
} qa_q2_rate_window;

void qa_q2_rate_window_init(qa_q2_rate_window *window);
bool qa_q2_rate_window_drop(qa_q2_rate_window *window, double server_frame,
                            double bytes_per_second, bool loopback);
void qa_q2_rate_window_sent(qa_q2_rate_window *window, double server_frame,
                            double bytes);
double qa_q2_rate_window_take_suppressed(qa_q2_rate_window *window);

#endif

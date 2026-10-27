/*
 * Port of src/network/q2/server-write.ts. The download source uses
 * the landed services downloads vtable. next() returns an owned
 * event pointer or NULL when ended. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_SERVER_WRITE_H
#define QA_NETWORK_Q2_SERVER_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/codec.h"
#include "qa/network/q2/fog.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/server_messages.h"
#include "qa/network/services/downloads.h"

/* TS writeQ2Fog(m, fog). */
void qa_q2_write_fog(qa_q2_sizebuf *m, const qa_q2_svc_fog_data *fog);
/* TS encodeQ2ServerEvent: malloc'd bytes (NULL only on OOM). */
uint8_t *qa_q2_encode_server_event(qa_q2_wire_codec *wire, const qa_q2_server_event *event,
                                   size_t *out_length);

/* TS Q2DownloadSender. */
typedef struct qa_q2_download_sender {
  qa_download_source source;
  double offset;
  bool ended;
  double block_bytes;
} qa_q2_download_sender;

/* offset/block defaults: has_* false gives 0/1024. */
void qa_q2_download_sender_init(qa_q2_download_sender *sender,
                                qa_download_source source, bool has_offset,
                                double offset, bool has_block_bytes, double block_bytes);
/* TS next(): NULL when ended. Caller clears and frees the event. */
qa_q2_server_event *qa_q2_download_sender_next(qa_q2_download_sender *sender);
void qa_q2_download_sender_close(qa_q2_download_sender *sender);

#endif

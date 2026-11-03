/*
 * Port of src/network/q2/codecs/zpacket.ts. node:zlib raw deflate
 * becomes zlib (-lz); inflate failures throw the same ComError prefix
 * with zlib's message. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_ZPACKET_H
#define QA_NETWORK_Q2_CODECS_ZPACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/message.h"

#define QA_Q2_ZPACKET_MIN_COMPRESS_SIZE (5 + 16)

/* TS tryWrapZPacket(data, len, maxOut): malloc'd packet or NULL. */
uint8_t *qa_q2_try_wrap_zpacket(const uint8_t *data, size_t data_length, int32_t len,
                                int32_t max_out, size_t *out_length);
/* TS readZPacketPayload(msg): malloc'd payload (caller frees); NULL
 * only on OOM. Inflate failure raises ComError(ERR_DROP) like TS. */
uint8_t *qa_q2_read_zpacket_payload(qa_q2_sizebuf *msg, size_t *out_length);

/* Node zlib inflateSync (raw=false) / inflateRawSync (raw=true) with { maxOutputLength:
 * max_output, finishFlush: flush (Z_FINISH or Z_SYNC_FLUSH) }. Returns malloc'd bytes, NULL
 * only on OOM. Raises RangeError "Cannot create a Buffer larger than N bytes" past the bound,
 * Error "unexpected end of file" for a truncated Z_FINISH stream, or Error with zlib's text. */
uint8_t *qa_q2_inflate_sync(const uint8_t *bytes, size_t length, bool raw, size_t max_output,
                            int flush, size_t *out_length);

#endif

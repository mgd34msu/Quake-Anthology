/* Port of src/network/q3/transport.ts. GPL-2.0-or-later.
 * TS-owner mirror: DatagramTransport lives in network/common/transport.ts
 * (another slice); only its send surface is used here. */
#ifndef QA_NETWORK_Q3_TRANSPORT_H
#define QA_NETWORK_Q3_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/admission.h"
#include "qa/network/q3/message.h"
#include "qa/network/q3/netchan.h"
typedef void (*qa_q3_mirror_datagram_send_fn)(void *ctx, const qa_q3_address *to, const uint8_t *packet, size_t len);
typedef void (*qa_q3_remote_address_fn)(void *ctx, qa_q3_address *out);
/* Storage for the produced ChannelDelivery; fill via qa_q3_channel_delivery_create. */
typedef struct qa_q3_channel_delivery_storage {
  qa_q3_mirror_datagram_send_fn transport_send; void *transport_ctx;
  qa_q3_remote_address_fn remote; void *remote_ctx;
  qa_q3_message_print_fn trace; void *trace_ctx;
  qa_q3_channel_delivery delivery;
} qa_q3_channel_delivery_storage;
const qa_q3_channel_delivery *qa_q3_channel_delivery_create(qa_q3_channel_delivery_storage *storage,
                                                            qa_q3_mirror_datagram_send_fn transport_send, void *transport_ctx,
                                                            qa_q3_remote_address_fn remote, void *remote_ctx,
                                                            qa_q3_source_message_state *source_state,
                                                            qa_q3_message_print_fn trace, void *trace_ctx);
#endif

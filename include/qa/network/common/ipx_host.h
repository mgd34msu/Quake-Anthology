/* Port of src/network/common/ipx-host.ts. Native capability bind is
 * synchronous fn+ctx (TS Promise). GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_IPX_HOST_H
#define QA_NETWORK_COMMON_IPX_HOST_H
#include <stdbool.h>
#include "qa/network/common/ipx_dosbox.h"
#include "qa/network/common/transport.h"
/* IpxGame: "quake1"|"quake2"|"quake3" */
typedef struct qa_native_ipx_capability {
  void *ctx; bool available; const char *reason;
  /* On success fills *out with an owned transport (ctx owned by caller
   * of bind through the returned vtable) and returns 1. A refused bind raises the
   * host's error (TS rejected promise); 0 means allocation failure. */
  int (*bind)(void *ctx, double port, double packet_type, qa_datagram_transport *out);
} qa_native_ipx_capability;
typedef struct qa_ipx_host {
  bool is_dosbox;
  qa_native_ipx_capability capability;
  qa_dosbox_ipx_network *network;
} qa_ipx_host;
typedef struct qa_ipx_game_transport {
  qa_datagram_transport socket; bool is_quake1; uint32_t sequence;
} qa_ipx_game_transport;
void qa_ipx_game_transport_init(qa_ipx_game_transport *t, qa_datagram_transport socket, const char *game);
void qa_ipx_game_transport_destroy(qa_ipx_game_transport *t);
qa_datagram_transport qa_ipx_game_transport_as_transport(qa_ipx_game_transport *t);
int64_t qa_ipx_game_transport_max_datagram_bytes(qa_ipx_game_transport *t);
bool qa_ipx_game_transport_send(qa_ipx_game_transport *t, const qa_net_address *to, const uint8_t *payload, size_t length);
int qa_ipx_game_transport_poll(qa_ipx_game_transport *t, qa_receive_event *out);
void qa_bind_ipx_transport(const qa_ipx_host *host, const char *game, double port, qa_ipx_game_transport *out);
#endif

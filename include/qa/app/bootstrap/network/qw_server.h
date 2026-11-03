/* Port of src/app/bootstrap/network/qw-server.ts (QwServerNetwork).
 * Host clientInfo rides as a landed qa_qw_info*; frame entities
 * are qa_quakeworld_entity*, messages qa_quakeworld_message*;
 * host signon returns a qa_qw_signon_host*. Administration and
 * authentication are the local vtables below (TS picks from
 * ServerAdministration / authentication settings).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_SERVER_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_SERVER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/qw_server_types.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/network/common/transport.h"
typedef struct qa_app_qw_server_administration {
  void *ctx;
  /* TS `rconPassword` getter, read whenever the connection host reads it (borrowed until the next call). */
  const char *(*rcon_password)(void *ctx);
  bool (*blocked)(void *ctx, const qa_net_address *from);
  char *(*status)(void *ctx);           /* malloc'd */
  char *(*log)(void *ctx, double sequence); /* malloc'd or NULL */
  void (*execute_admin)(void *ctx, const char *command,
      void (*write)(void *, const char *, size_t), void *write_ctx);
} qa_app_qw_server_administration;
typedef struct qa_app_qw_server_authentication {
  const char *password, *spectator_password; bool high_characters;
} qa_app_qw_server_authentication;
typedef struct qa_app_qw_server_network qa_app_qw_server_network;
qa_app_qw_server_network *qa_app_qw_server_network_create(qa_app_qw_server_network_options options);
void qa_app_qw_server_network_destroy(qa_app_qw_server_network *network);
/* Parallel arrays (clients/values), counts equal. */
size_t qa_app_qw_server_network_client_pings(qa_app_qw_server_network *network,
    qa_client_id *clients, double *values, size_t capacity);
void qa_app_qw_server_network_heartbeat(qa_app_qw_server_network *network, double now);
bool qa_app_qw_server_network_address(qa_app_qw_server_network *network, qa_net_address *out);
qa_app_network_phase qa_app_qw_server_network_phase(const qa_app_qw_server_network *network);
size_t qa_app_qw_server_network_clients(qa_app_qw_server_network *network,
    qa_app_qw_player *out, size_t capacity);
bool qa_app_qw_server_network_disconnect_client(qa_app_qw_server_network *network,
    qa_client_id client, const char *reason);
void qa_app_qw_server_network_change_world(qa_app_qw_server_network *network,
    qa_app_qw_server_host host);
const void *const *qa_app_qw_server_network_poll(qa_app_qw_server_network *network,
    double now, size_t *out_count);
void qa_app_qw_server_network_submit(qa_app_qw_server_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_qw_server_network_publish(qa_app_qw_server_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_qw_server_network_close(qa_app_qw_server_network *network);
qa_app_application_network qa_app_qw_server_network_as_network(qa_app_qw_server_network *network);
#endif

/* Port of src/network/services/discovery.ts. DiscoveryWire/PacketSender
 * -> fn+ctx (wire byte results malloc'd by the callback, freed by the
 * browser after send). Request handles are opaque ids (>0).
 * saveFavorites/restoreFavorites use the same JSON address records.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_DISCOVERY_H
#define QA_NETWORK_SERVICES_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/common/session.h"
#include "qa/network/q1/handshake.h"
typedef struct qa_player_detail { char *name; double score, ping; } qa_player_detail;
typedef struct qa_server_status {
  char *name, *map; double players, max_players;
  qa_qw_info rules;
  qa_player_detail *player_details; size_t player_count;
  qa_wire_selection wire;
} qa_server_status;
void qa_server_status_destroy(qa_server_status *s);
void qa_server_status_copy(const qa_server_status *src, qa_server_status *dst);
/* DiscoverySource strings: "lan"|"master"|"secondary-master"|"favorite"|"direct". */
typedef struct qa_browser_entry {
  qa_net_address address;
  const char **sources; size_t source_count;
  bool has_status; qa_server_status status;
  bool has_ping; double ping_ms, updated_at;
} qa_browser_entry;
typedef struct qa_discovery_wire {
  void *ctx;
  uint8_t *(*query)(void *ctx, const char *kind, const char *challenge, size_t *out_len);
  uint8_t *(*master_query)(void *ctx, size_t *out_len);
  uint8_t *(*heartbeat)(void *ctx, bool active, size_t *out_len);
} qa_discovery_wire;
typedef struct qa_packet_sender {
  void *ctx;
  bool (*send)(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length);
} qa_packet_sender;
typedef struct qa_discovery_result {
  const char *kind; /* "pending"|"completed"|"expired"|"cancelled" */
  qa_net_address address; const char *request_kind; double sent_at;
  bool has_status; qa_server_status status; double ping_ms, completed_at;
} qa_discovery_result;
typedef struct qa_server_browser qa_server_browser;
void qa_server_browser_init(qa_server_browser *b, qa_discovery_wire wire, qa_packet_sender transport);
void qa_server_browser_destroy(qa_server_browser *b);
/* Heap form (the struct is private to discovery.c): NULL only on OOM; free destroys. */
qa_server_browser *qa_server_browser_create(qa_discovery_wire wire, qa_packet_sender transport);
void qa_server_browser_free(qa_server_browser *b);
const qa_browser_entry *qa_server_browser_add(qa_server_browser *b, const qa_net_address *address, const char *source, double now);
void qa_server_browser_remove_favorite(qa_server_browser *b, const qa_net_address *address);
void qa_server_browser_remove_source(qa_server_browser *b, const qa_net_address *address, const char *source);
void qa_server_browser_restore_entry(qa_server_browser *b, const qa_browser_entry *entry);
const qa_browser_entry *qa_server_browser_get_entry(qa_server_browser *b, const qa_net_address *address);
size_t qa_server_browser_size(qa_server_browser *b);
const qa_browser_entry *qa_server_browser_entry_at(qa_server_browser *b, size_t index);
size_t qa_server_browser_pending_requests(qa_server_browser *b);
bool qa_server_browser_query(qa_server_browser *b, const qa_net_address *address, double now, const char *kind);
long long qa_server_browser_request(qa_server_browser *b, const qa_net_address *address, double now,
                                    const char *kind, bool has_timeout, bool timeout_is_null, double timeout_ms);
int qa_server_browser_request_result(qa_server_browser *b, long long handle, qa_discovery_result *out);
bool qa_server_browser_cancel_request(qa_server_browser *b, long long handle);
void qa_server_browser_release_request(qa_server_browser *b, long long handle);
bool qa_server_browser_receive(qa_server_browser *b, const qa_net_address *address, const qa_server_status *status,
                               const char *challenge, double now, const char *kind);
bool qa_server_browser_query_master(qa_server_browser *b, const qa_net_address *address);
void qa_server_browser_receive_master(qa_server_browser *b, const qa_net_address *addresses, size_t count, double now);
double qa_server_browser_broadcast(qa_server_browser *b, const qa_net_address *addresses, size_t count, double now);
qa_net_address *qa_server_browser_expire_queries(qa_server_browser *b, double now, double timeout_ms, size_t *out_count);
qa_net_address *qa_server_browser_favorite_addresses(qa_server_browser *b, size_t *out_count);
char *qa_server_browser_save_favorites(qa_server_browser *b);
void qa_server_browser_restore_favorites(qa_server_browser *b, const char *text);

typedef struct qa_master_heartbeat_service {
  qa_discovery_wire wire; qa_packet_sender transport; double interval_ms, next_time;
} qa_master_heartbeat_service;
void qa_master_heartbeat_service_init(qa_master_heartbeat_service *s, qa_discovery_wire wire, qa_packet_sender transport, double interval_ms);
double qa_master_heartbeat_service_send(qa_master_heartbeat_service *s, const qa_net_address *masters, size_t count, double now, bool active, bool force);
#endif

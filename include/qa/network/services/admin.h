/* Port of src/network/services/admin.ts. RconHost -> fn+ctx; the async
 * serialized handle() runs synchronously in call order (PORTING async
 * rule). GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_ADMIN_H
#define QA_NETWORK_SERVICES_ADMIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
typedef struct qa_ipv4_filter { uint32_t mask, compare; } qa_ipv4_filter;
int qa_source_ipv4_filter(const char *text, qa_ipv4_filter *out);
qa_ipv4_filter qa_cidr_ipv4_filter(const qa_net_address *address, double prefix_bits);
typedef struct qa_ip_filter_list {
  bool deny_matches; qa_ipv4_filter *entries; size_t count, capacity;
} qa_ip_filter_list;
void qa_ip_filter_list_init(qa_ip_filter_list *l, bool deny_matches);
void qa_ip_filter_list_destroy(qa_ip_filter_list *l);
void qa_ip_filter_list_add(qa_ip_filter_list *l, qa_ipv4_filter filter);
bool qa_ip_filter_list_remove(qa_ip_filter_list *l, qa_ipv4_filter filter);
bool qa_ip_filter_list_rejects(const qa_ip_filter_list *l, const qa_net_address *address);
qa_ipv4_filter *qa_ip_filter_list_snapshot(const qa_ip_filter_list *l, size_t *out_count);

typedef struct qa_flood_entry { char *key; double tokens, time; } qa_flood_entry;
typedef struct qa_flood_limiter {
  double burst, interval_ms; qa_flood_entry *entries; size_t count, capacity;
} qa_flood_limiter;
void qa_flood_limiter_init(qa_flood_limiter *l, double burst, double interval_ms);
void qa_flood_limiter_destroy(qa_flood_limiter *l);
bool qa_flood_limiter_allow(qa_flood_limiter *l, const qa_net_address *address, double now);
void qa_flood_limiter_expire(qa_flood_limiter *l, double now);

typedef struct qa_source_chat_flood {
  double messages, seconds, lock_seconds; double times[10]; int head; double locked_until;
} qa_source_chat_flood;
void qa_source_chat_flood_init(qa_source_chat_flood *f, double messages, double seconds, double lock_seconds);
/* kind: "allowed"|"locked"|"flood"; *seconds set when not allowed. */
const char *qa_source_chat_flood_check(qa_source_chat_flood *f, double now, bool paused, double *seconds);

typedef void (*qa_rcon_output_fn)(void *ctx, const char *text, size_t length);
typedef struct qa_rcon_host {
  void *ctx;
  const char *(*password)(void *ctx);
  void (*execute)(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx);
  void (*reply)(void *ctx, const qa_net_address *to, const char *text);
} qa_rcon_host;
typedef struct qa_server_administration {
  void *ctx;
  const char *(*rcon_password)(void *ctx);
  void (*execute)(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx);
  bool (*rejects)(void *ctx, const qa_net_address *address);
  const qa_net_address *(*masters)(void *ctx, size_t *out_count);
  void (*record)(void *ctx, const qa_net_address *address, const char *result);
} qa_server_administration;
char *qa_q3_rcon_command(const char *line);
typedef struct qa_rcon_service {
  qa_rcon_host host; bool is_q3; double output_bytes, last_time;
} qa_rcon_service;
void qa_rcon_service_init(qa_rcon_service *s, qa_rcon_host host, bool is_q3, double output_bytes);
/* Returns "throttled"|"disabled"|"denied"|"executed". */
const char *qa_rcon_service_handle(qa_rcon_service *s, const qa_net_address *from,
                                   const char *supplied_password, const char *command, double milliseconds);
#endif

/* Port of src/network/q1/handshake.ts. Host interfaces -> fn+ctx
 * structs; async receive -> synchronous. Strings in decoded results
 * are malloc'd (destroy frees). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_HANDSHAKE_H
#define QA_NETWORK_Q1_HANDSHAKE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/common/rcon_guard.h"
typedef enum qa_nq_control_kind {
  QA_NQ_CONNECT_REQUEST, QA_NQ_SERVER_INFO_REQUEST, QA_NQ_PLAYER_INFO_REQUEST,
  QA_NQ_RULE_INFO_REQUEST, QA_NQ_ACCEPT, QA_NQ_REJECT, QA_NQ_SERVER_INFO,
  QA_NQ_PLAYER_INFO, QA_NQ_RULE_INFO
} qa_nq_control_kind;
typedef struct qa_netquake_control {
  qa_nq_control_kind kind;
  char *game; double version;          /* connect/server-info-request */
  double player;                        /* player-info-request/-info */
  char *previous;                       /* rule-info-request */
  double port;                          /* accept */
  char *reason;                         /* reject */
  char *address, *name, *map;           /* server-info (name also player-info) */
  double players, max_players;          /* server-info */
  double colors, frags, seconds;        /* player-info */
  bool has_rule; char *rule_name, *rule_value; /* rule-info */
} qa_netquake_control;
void qa_netquake_control_destroy(qa_netquake_control *m);
uint8_t *qa_encode_netquake_control(const qa_netquake_control *m, size_t *out_len);
void qa_decode_netquake_control(const uint8_t *bytes, size_t length, qa_netquake_control *out);

typedef struct qa_nq_connect_result { int kind; double port; const char *reason; } qa_nq_connect_result;
/* kind: 0 accepted, 1 rejected, 2 retry */
typedef struct qa_netquake_host {
  void *ctx;
  void (*server_info)(void *ctx, qa_netquake_control *out);
  int (*player_info)(void *ctx, double index, qa_netquake_control *out);
  int (*next_rule)(void *ctx, const char *previous, char **name, char **value);
  qa_nq_connect_result (*connect)(void *ctx, const qa_net_address *from, double now_ms);
} qa_netquake_host;
uint8_t *qa_answer_netquake_control(const uint8_t *bytes, size_t length, const qa_net_address *from,
                                    double now_ms, const qa_netquake_host *host, size_t *out_len);

uint8_t *qa_quakeworld_out_of_band(const char *text, bool nul, size_t *out_len);
char *qa_read_quakeworld_out_of_band(const uint8_t *bytes, size_t length);
/* Malloc'd array of malloc'd args; free each + array. */
char **qa_quakeworld_command_arguments(const char *text, size_t *out_count);
typedef struct qa_qw_info_entry { char *key, *value; } qa_qw_info_entry;
typedef struct qa_qw_info { qa_qw_info_entry *entries; size_t count, capacity; } qa_qw_info;
void qa_qw_info_init(qa_qw_info *info);
void qa_qw_info_destroy(qa_qw_info *info);
const char *qa_qw_info_get(const qa_qw_info *info, const char *key);
void qa_qw_info_set(qa_qw_info *info, const char *key, const char *value);
void qa_qw_info_delete(qa_qw_info *info, const char *key);
char *qa_qw_info_string(const qa_qw_info *info);
void qa_quakeworld_info_parse(const char *text, qa_qw_info *out);

typedef double (*qa_random_fn)(void *ctx);
typedef struct qa_qw_challenges qa_qw_challenges;
struct qa_qw_challenge_record { char *key; double challenge, issued; };
struct qa_qw_challenges {
  qa_random_fn random; void *random_ctx; double capacity;
  struct qa_qw_challenge_record *records; size_t count, cap;
};
void qa_qw_challenges_init(qa_qw_challenges *c, qa_random_fn random, void *ctx, double capacity);
void qa_qw_challenges_destroy(qa_qw_challenges *c);
double qa_qw_challenges_issue(qa_qw_challenges *c, const qa_net_address *address, double now_ms);
bool qa_qw_challenges_validate(qa_qw_challenges *c, const qa_net_address *address, double challenge);
/* TS validate(address, challenge, nowMilliseconds): with a time, a challenge older than 30 s fails. */
bool qa_qw_challenges_validate_at(qa_qw_challenges *c, const qa_net_address *address, double challenge,
                                  bool has_now, double now_ms);

typedef struct qa_qw_connect_request {
  qa_net_address from; double qport, challenge; char *userinfo; bool spectator, donor_wide;
} qa_qw_connect_request;
typedef struct qa_qw_host_result { int kind; const char *reason; } qa_qw_host_result;
/* kind: 0 accepted, 1 rejected, 2 duplicate */
typedef void (*qa_qw_write_fn)(void *ctx, const char *text, size_t length);
typedef struct qa_quakeworld_host {
  void *ctx;
  const char *password, *spectator_password, *rcon_password; bool high_characters;
  bool (*blocked)(void *ctx, const qa_net_address *from);
  qa_qw_host_result (*connect)(void *ctx, const qa_qw_connect_request *request, double now_ms);
  char *(*status)(void *ctx);
  char *(*log)(void *ctx, double sequence);
  void (*execute_admin)(void *ctx, const char *command, qa_qw_write_fn write, void *write_ctx);
} qa_quakeworld_host;
typedef struct qa_qw_connectionless_server {
  qa_quakeworld_host host; qa_qw_challenges *challenges;
  /* TS rconGuard; a zeroed guard takes the TS defaults (3 attempts, 1024 sources) on first use. */
  qa_rcon_guard rcon_guard;
} qa_qw_connectionless_server;
/* Frees the rcon guard table. */
void qa_qw_connectionless_server_destroy(qa_qw_connectionless_server *s);
/* Replies: malloc'd array of malloc'd datagrams; lengths in reply_lens. */
uint8_t **qa_qw_connectionless_receive(qa_qw_connectionless_server *s, const uint8_t *bytes, size_t length,
                                       const qa_net_address *from, double now_ms,
                                       size_t **reply_lens, size_t *reply_count);

typedef struct qa_qw_connect_client {
  int kind; /* 0 challenge, 1 connect, 2 connected, 3 rejected */
  double sent_at, challenge; char *reason;
  double qport; char *userinfo;
} qa_qw_connect_client;
int qa_qw_connect_client_init(qa_qw_connect_client *c, double qport, const char *userinfo);
void qa_qw_connect_client_destroy(qa_qw_connect_client *c);
uint8_t *qa_qw_connect_client_next(qa_qw_connect_client *c, double now_ms, size_t *out_len);
void qa_qw_connect_client_receive(qa_qw_connect_client *c, const uint8_t *bytes, size_t length);

uint8_t *qa_quakeworld_heartbeat(double sequence, double active_clients, size_t *out_len);
uint8_t *qa_quakeworld_shutdown(size_t *out_len);

typedef struct qa_nq_connect_client {
  int kind; /* 0 waiting, 1 connected, 2 rejected */
  double attempts, sent_at, port; char *reason;
} qa_nq_connect_client;
void qa_nq_connect_client_init(qa_nq_connect_client *c);
void qa_nq_connect_client_destroy(qa_nq_connect_client *c);
uint8_t *qa_nq_connect_client_next(qa_nq_connect_client *c, double now_ms, size_t *out_len);
void qa_nq_connect_client_receive(qa_nq_connect_client *c, const uint8_t *bytes, size_t length);

typedef struct qa_qw_master_heartbeat { double previous_ms, sequence; } qa_qw_master_heartbeat;
void qa_qw_master_heartbeat_init(qa_qw_master_heartbeat *h);
uint8_t *qa_qw_master_heartbeat_next(qa_qw_master_heartbeat *h, double now_ms, double active_clients, bool force, size_t *out_len);
#endif

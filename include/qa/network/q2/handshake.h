/*
 * Port of src/network/q2/handshake.ts. Q2ConnectionlessMessage
 * strings are owned (free helper); the handshake/challenge classes
 * are caller-allocated structs with fn + ctx callbacks for
 * userinfo/socialId/random. readQ2OutOfBand utf8=true needs valid
 * UTF-8 (validated like TextDecoder fatal mode; invalid input yields
 * the decoder throw -> abort helper qa_q2_throw_utf8_error).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_HANDSHAKE_H
#define QA_NETWORK_Q2_HANDSHAKE_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <stddef.h>

#include "qa/contracts/protocol.h"
#include "qa/network/common/endpoint.h"

/* TS TextDecoder('utf-8', {fatal:true}) throw. */
noreturn void qa_q2_throw_utf8_error(void);

/* TS Q2ConnectionlessMessage (all strings owned). */
typedef struct qa_q2_connectionless_message {
  char *command;
  char **arguments;
  size_t argument_count;
  char *text;
  char *body;
} qa_q2_connectionless_message;

void qa_q2_connectionless_message_free(qa_q2_connectionless_message *message);

/* TS q2OutOfBand(text, utf8=false): malloc'd packet (NULL on OOM).
 * The C text is bytes; utf8 does not change the encoding of bytes. */
uint8_t *qa_q2_out_of_band(const char *text, bool utf8, size_t *out_length);
/* TS readQ2OutOfBand: false is TS null. */
bool qa_q2_read_out_of_band(const uint8_t *bytes, size_t length, bool utf8,
                            qa_q2_connectionless_message *out);
/* TS q2Protocol(version, minor=0); unsupported versions abort. */
void qa_q2_get_protocol(double version, double minor, qa_q2_protocol_identity *out);

/* TS Q2ConnectRequest (strings owned when produced on read). */
typedef struct qa_q2_connect_request {
  qa_q2_protocol_identity protocol;
  double qport;
  double challenge;
  char *userinfo; /* owned */
  double payload_bytes;
  bool channel_new; /* TS channel "new" */
  bool compression;
  bool has_social_ids;
  char **social_ids; /* owned */
  size_t social_id_count;
  bool has_split_seat;
  double split_seat;
} qa_q2_connect_request;

void qa_q2_connect_request_free(qa_q2_connect_request *request);
/* Request-string copy helper (NULL only on OOM). */
bool qa_q2_connect_request_copy_into(const qa_q2_connect_request *in,
                                     qa_q2_connect_request *out);

/* TS writeQ2Connect / readQ2Connect. */
uint8_t *qa_q2_write_connect(const qa_q2_connect_request *request,
                             size_t *out_length);
bool qa_q2_read_connect(const qa_q2_connectionless_message *message,
                        qa_q2_connect_request *out);

/* TS Q2Challenge / readQ2Challenge (versions owned). */
typedef struct qa_q2_challenge {
  double challenge;
  double *versions;
  size_t version_count;
} qa_q2_challenge;
void qa_q2_challenge_free(qa_q2_challenge *challenge);
bool qa_q2_read_challenge(const qa_q2_connectionless_message *message,
                          qa_q2_challenge *out);
/* TS readQ2DownloadServer(arguments, server?): malloc'd URL href (new URL normalization:
 * lowercased host, IPv4/IPv6 canonical, default port dropped, dot segments removed, path
 * percent-encoded, trailing '/') or NULL. `server` NULL is TS `undefined`. */
char *qa_q2_read_download_server(char **arguments, size_t argument_count,
                                 const qa_net_address *server);

/* TS Q2ClientHandshakeState kinds. */
typedef enum qa_q2_handshake_state_kind {
  QA_Q2_HANDSHAKE_CHALLENGING, QA_Q2_HANDSHAKE_CONNECTING,
  QA_Q2_HANDSHAKE_CONNECTED, QA_Q2_HANDSHAKE_REJECTED
} qa_q2_handshake_state_kind;

typedef const char *(*qa_q2_string_fn)(void *ctx);

/* TS Q2ClientHandshake (remote/userinfo/social hold caller refs). */
typedef struct qa_q2_client_handshake {
  qa_net_address remote;
  qa_q2_protocol_identity *preferences; /* owned copy */
  size_t preference_count;
  double qport;
  qa_q2_string_fn userinfo;
  void *userinfo_ctx;
  double payload_bytes;
  double retry_milliseconds;
  qa_q2_string_fn social_id; /* NULL is TS default () => '' */
  void *social_ctx;
  qa_q2_handshake_state_kind kind;
  bool has_last_sent;
  double last_sent;
  qa_q2_connect_request request; /* connecting/connected */
  char *download_server;         /* connected; owned or NULL */
  char *reason;                  /* rejected; owned */
} qa_q2_client_handshake;

/* False only on OOM; empty preferences abort like TS. Defaults:
 * payload_bytes 1390, retry 3000 when *_set false. */
bool qa_q2_client_handshake_init(qa_q2_client_handshake *handshake,
                                 const qa_net_address *remote,
                                 const qa_q2_protocol_identity *preferences,
                                 size_t preference_count, double qport,
                                 qa_q2_string_fn userinfo, void *userinfo_ctx,
                                 bool has_payload_bytes, double payload_bytes,
                                 bool has_retry, double retry_milliseconds,
                                 qa_q2_string_fn social_id, void *social_ctx);
void qa_q2_client_handshake_destroy(qa_q2_client_handshake *handshake);
/* TS poll: malloc'd packet or NULL. receive returns consumed. */
uint8_t *qa_q2_client_handshake_poll(qa_q2_client_handshake *handshake, double now,
                                     size_t *out_length);
bool qa_q2_client_handshake_receive(qa_q2_client_handshake *handshake,
                                    const qa_net_address *from,
                                    const qa_q2_connectionless_message *message);

typedef double (*qa_q2_random_fn)(void *ctx);

/* One Q2ChallengeTable entry. */
typedef struct qa_q2_challenge_entry {
  char *key; /* owned */
  double value;
  double time;
} qa_q2_challenge_entry;

/* TS Q2ChallengeTable (entries malloc'd). */
typedef struct qa_q2_challenge_table {
  qa_q2_random_fn random;
  void *random_ctx;
  double capacity;
  qa_q2_challenge_entry *entries;
  size_t entry_count;
  size_t entry_capacity;
} qa_q2_challenge_table;

/* False only on OOM. capacity 0 is TS default 1024. */
bool qa_q2_challenge_table_init(qa_q2_challenge_table *table, qa_q2_random_fn random,
                                void *ctx, double capacity);
void qa_q2_challenge_table_destroy(qa_q2_challenge_table *table);
double qa_q2_challenge_table_issue(qa_q2_challenge_table *table,
                                   const qa_net_address *from, double now);
/* TS validate(from, challenge, now = 0): a nonzero now also requires the challenge to be at
 * most 30 s old. */
bool qa_q2_challenge_table_validate(const qa_q2_challenge_table *table,
                                    const qa_net_address *from, double challenge, double now);
uint8_t *qa_q2_challenge_table_reply(qa_q2_challenge_table *table,
                                     const qa_net_address *from, double now,
                                     const qa_q2_protocol_identity *protocols,
                                     size_t protocol_count, size_t *out_length);

/* TS q2KexSeatUserinfo / q2KexClientUserinfo: malloc'd or NULL on OOM. */
char *qa_q2_kex_seat_userinfo(const char *text, double seat);
char *qa_q2_kex_client_userinfo(const char *text);

#endif

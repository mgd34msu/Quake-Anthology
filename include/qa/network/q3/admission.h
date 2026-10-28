/* Port of src/network/q3/admission.ts. GPL-2.0-or-later.
 * The endpoint owner has landed (qa/network/common/endpoint.h,
 * qa_net_address). At this slice's boundaries use
 * qa_q3_address_to_net / qa_q3_address_from_net (lossless field
 * copies); qa_q3_mirror_address_key / qa_q3_mirror_same_address
 * delegate to qa_net_address_key / qa_net_same_address. The
 * internal qa_q3_address value type remains until its external
 * consumers migrate. Async bindings become synchronous (PORTING). */
#ifndef QA_NETWORK_Q3_ADMISSION_H
#define QA_NETWORK_Q3_ADMISSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/q3/connectionless.h"
#include "qa/network/q3/message.h"
typedef enum qa_q3_mirror_address_kind { QA_Q3_ADDR_IPV4, QA_Q3_ADDR_IPV6, QA_Q3_ADDR_IPX, QA_Q3_ADDR_LOOPBACK } qa_q3_mirror_address_kind;
/* Mirror of endpoint.ts Ipv4Address|IpxAddress|LoopbackAddress (Q3Address). */
typedef struct qa_q3_mirror_address {
  qa_q3_mirror_address_kind kind;
  uint8_t host[4];       /* ipv4 */
  int64_t network;       /* ipx */
  uint8_t node[6];       /* ipx */
  int64_t port;          /* ipv4/ipx */
  char *loopback_id;     /* owned, loopback */
  char *ipv6_host;       /* owned, ipv6 (NetworkAddress mirror for browser-view) */
} qa_q3_mirror_address;
typedef qa_q3_mirror_address qa_q3_address;
typedef qa_q3_mirror_address qa_q3_mirror_ipv4_address;
typedef qa_q3_mirror_address qa_q3_mirror_network_address;
/* Mirror of endpoint.ts addressKey (malloc'd). */
char *qa_q3_mirror_address_key(const qa_q3_mirror_address *a, bool include_port);
bool qa_q3_mirror_address_copy(qa_q3_mirror_address *dst, const qa_q3_mirror_address *src);
void qa_q3_mirror_address_destroy(qa_q3_mirror_address *a);
/* Mirror of endpoint.ts sameAddress. */
bool qa_q3_mirror_same_address(const qa_q3_mirror_address *a, const qa_q3_mirror_address *b, bool include_port);
/* Boundary conversions to the landed endpoint owner type. Field
 * copies (ipx network narrows to uint32 exactly as endpoint stores
 * it); string fields are copied, caller destroys out. False on OOM. */
bool qa_q3_address_to_net(const qa_q3_address *in, qa_net_address *out);
bool qa_q3_address_from_net(const qa_net_address *in, qa_q3_address *out);
bool qa_q3_is_lan_address(const qa_q3_address *address);
/* q3InfoValue: returns malloc'd value or "" (NULL only on OOM). */
char *qa_q3_info_value(const char *info, const char *key);
/* TS Q3OutgoingDatagram. */
typedef struct qa_q3_outgoing_datagram { qa_q3_address to; uint8_t *payload; size_t payload_len; } qa_q3_outgoing_datagram;
void qa_q3_outgoing_datagram_destroy(qa_q3_outgoing_datagram *d);
typedef enum qa_q3_client_admission_phase { QA_Q3_PHASE_DISCONNECTED, QA_Q3_PHASE_CONNECTING, QA_Q3_PHASE_CHALLENGING, QA_Q3_PHASE_CONNECTED } qa_q3_client_admission_phase;
typedef enum qa_q3_client_admission_result_kind {
  QA_Q3_ADMIT_HANDLED, QA_Q3_ADMIT_ADMITTED, QA_Q3_ADMIT_CONNECTIONLESS, QA_Q3_ADMIT_SEQUENCED, QA_Q3_ADMIT_IGNORED
} qa_q3_client_admission_result_kind;
typedef struct qa_q3_client_admission_result {
  qa_q3_client_admission_result_kind kind;
  qa_q3_address address; int64_t challenge, qport; /* admitted */
  qa_q3_connectionless_packet packet; /* connectionless */
  const uint8_t *bytes; size_t byte_len; /* sequenced: borrowed input */
} qa_q3_client_admission_result;
void qa_q3_client_admission_result_destroy(qa_q3_client_admission_result *r);
typedef struct qa_q3_client_admission qa_q3_client_admission;
bool qa_q3_client_admission_init(qa_q3_client_admission **out, int64_t qport, qa_q3_message_print_fn print, void *print_ctx);
void qa_q3_client_admission_destroy(qa_q3_client_admission *a);
qa_q3_client_admission_phase qa_q3_client_admission_get_phase(const qa_q3_client_admission *a);
void qa_q3_client_admission_set_phase(qa_q3_client_admission *a, qa_q3_client_admission_phase phase);
bool qa_q3_client_admission_get_address(const qa_q3_client_admission *a, qa_q3_address *out); /* false = null */
int64_t qa_q3_client_admission_challenge(const qa_q3_client_admission *a);
void qa_q3_client_admission_set_challenge(qa_q3_client_admission *a, int64_t challenge);
int64_t qa_q3_client_admission_connect_time(const qa_q3_client_admission *a);
void qa_q3_client_admission_set_connect_time(qa_q3_client_admission *a, int64_t t);
int64_t qa_q3_client_admission_connect_packet_count(const qa_q3_client_admission *a);
void qa_q3_client_admission_set_connect_packet_count(qa_q3_client_admission *a, int64_t n);
int64_t qa_q3_client_admission_last_packet_time(const qa_q3_client_admission *a);
void qa_q3_client_admission_set_last_packet_time(qa_q3_client_admission *a, int64_t t);
int64_t qa_q3_client_admission_get_qport(const qa_q3_client_admission *a);
bool qa_q3_client_admission_begin(qa_q3_client_admission *a, const qa_q3_address *address);
/* resend: false = TS null. now is a TS number (double arithmetic kept). */
bool qa_q3_client_admission_resend(qa_q3_client_admission *a, double now, const char *userinfo, qa_q3_outgoing_datagram *out);
bool qa_q3_client_admission_receive(qa_q3_client_admission *a, const qa_q3_address *from, const uint8_t *bytes, size_t len, double now, qa_q3_client_admission_result *out);
void qa_q3_client_admission_disconnect(qa_q3_client_admission *a);
/* TS Q3Challenge (mutable). */
typedef struct qa_q3_challenge {
  bool has_address; qa_q3_address address;
  int64_t challenge; double time, first_time, ping_time; bool connected;
} qa_q3_challenge;
typedef enum qa_q3_slot_phase { QA_Q3_SLOT_FREE, QA_Q3_SLOT_ZOMBIE, QA_Q3_SLOT_CONNECTED, QA_Q3_SLOT_PRIMED, QA_Q3_SLOT_ACTIVE } qa_q3_slot_phase;
/* TS Q3AdmissionSlot. */
typedef struct qa_q3_admission_slot {
  int64_t slot; qa_q3_slot_phase phase;
  bool has_address; qa_q3_address address;
  bool bot; int64_t qport; double last_connect_time;
} qa_q3_admission_slot;
/* TS Q3AcceptedConnect. userinfo owned. */
typedef struct qa_q3_accepted_connect {
  int64_t slot; qa_q3_address address; int64_t challenge, qport; char *userinfo;
} qa_q3_accepted_connect;
typedef bool (*qa_q3_adm_bool_fn)(void *ctx);
typedef int64_t (*qa_q3_adm_int_fn)(void *ctx);
typedef double (*qa_q3_adm_double_fn)(void *ctx);
typedef const char *(*qa_q3_adm_string_fn)(void *ctx);
typedef const qa_q3_admission_slot *(*qa_q3_adm_slots_fn)(void *ctx, size_t *count);
typedef bool (*qa_q3_adm_address_fn)(void *ctx, qa_q3_address *out); /* false = null */
typedef bool (*qa_q3_adm_is_lan_fn)(void *ctx, const qa_q3_address *address);
typedef void (*qa_q3_adm_authorize_fn)(void *ctx, const qa_q3_challenge *challenge);
typedef void (*qa_q3_adm_send_fn)(void *ctx, const qa_q3_address *address, const uint8_t *packet, size_t len);
/* admit returns malloc'd rejection string or NULL (accepted). */
typedef char *(*qa_q3_adm_admit_fn)(void *ctx, const qa_q3_accepted_connect *connection);
typedef void (*qa_q3_adm_drop_bot_fn)(void *ctx, int64_t slot);
typedef void (*qa_q3_adm_query_fn)(void *ctx, const qa_q3_address *from, qa_q3_connectionless_packet *packet);
/* TS Q3ServerAdmissionBindings. */
typedef struct qa_q3_server_admission_bindings {
  qa_q3_adm_bool_fn enabled;
  qa_q3_adm_slots_fn slots;
  qa_q3_adm_int_fn private_clients;
  qa_q3_adm_string_fn private_password;
  qa_q3_adm_double_fn reconnect_limit_seconds, minimum_ping, maximum_ping;
  qa_q3_adm_address_fn authorize_address;
  qa_q3_adm_bool_fn demo_restricted;
  qa_q3_adm_is_lan_fn is_lan;
  qa_q3_adm_double_fn random;
  qa_q3_adm_authorize_fn authorize;
  qa_q3_adm_send_fn send;
  qa_q3_adm_admit_fn admit;
  qa_q3_adm_drop_bot_fn drop_bot;
  qa_q3_message_print_fn print;
  qa_q3_adm_query_fn query;
  void *ctx; void *print_ctx;
} qa_q3_server_admission_bindings;
typedef struct qa_q3_server_admission qa_q3_server_admission;
bool qa_q3_server_admission_init(qa_q3_server_admission **out, const qa_q3_server_admission_bindings *bindings);
void qa_q3_server_admission_destroy(qa_q3_server_admission *a);
/* challenges: mutable borrowed array of 1024. */
qa_q3_challenge *qa_q3_server_admission_challenges(qa_q3_server_admission *a, size_t *count);
const qa_q3_server_admission_bindings *qa_q3_server_admission_get_bindings(const qa_q3_server_admission *a);
bool qa_q3_server_admission_receive(qa_q3_server_admission *a, const qa_q3_address *from, const uint8_t *bytes, size_t len, double now);
void qa_q3_server_admission_disconnect(qa_q3_server_admission *a, const qa_q3_address *address);
/* routeQ3SequencedPacket: returns slot index or -1. */
int64_t qa_q3_route_sequenced_packet(const qa_q3_address *from, const uint8_t *bytes, size_t len,
                                     const qa_q3_admission_slot *slots, size_t slot_count);
#endif

/* Port of src/network/common/session.ts. Accommodations:
 *  - SessionComposition's canonical form: TS canonical() serializes the
 *    full recipe object graph; the C composition carries caller-built
 *    canonical text (same key-sorted JSON rules for the parts the TS
 *    covers) and the digest is SHA-256 of those bytes, exactly as TS.
 *  - Errors -> qa_range_error with the TS message.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_SESSION_H
#define QA_NETWORK_COMMON_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/foundation/event_ring.h"
#include "qa/network/common/endpoint.h"
typedef struct qa_session_composition {
  int64_t schema_version; /* 1 */
  char *canonical_text;   /* owned; canonical(recipe+schema+actors) */
  char *snapshot_schema;  /* owned ProviderId string */
} qa_session_composition;
typedef struct qa_composition_identity {
  char *digest; /* owned qa_content_digest string */
  qa_session_composition composition;
} qa_composition_identity;
void qa_composition_identity_destroy(qa_composition_identity *id);
/* SHA-256 hex of data into out[65]; also used by downloads. */
void qa_net_sha256_hex(const uint8_t *data, size_t length, char out[65]);
int qa_make_composition_identity(const char *canonical_text, const char *snapshot_schema, qa_composition_identity *out);
uint8_t *qa_encode_composition_offer(const qa_composition_identity *identity, size_t *out_len);

typedef struct qa_wire_selection {
  bool is_unified; /* true: unified v1; false: source protocol */
  const char *composition; const char *snapshot_schema;
  qa_protocol_identity protocol;
} qa_wire_selection;
typedef struct qa_wire_admission {
  bool supported; char **reasons; size_t reason_count;
} qa_wire_admission;
void qa_wire_admission_destroy(qa_wire_admission *a);
typedef qa_wire_admission (*qa_source_wire_supports_fn)(void *ctx, const qa_session_composition *composition, qa_protocol_identity protocol);
qa_wire_admission qa_admit_composition_offer(const qa_composition_identity *local, const uint8_t *bytes, size_t length);
qa_wire_admission qa_admit_wire(const qa_composition_identity *identity, const qa_wire_selection *selection,
                                qa_source_wire_supports_fn supports, void *ctx);

typedef struct qa_remote_seat { qa_seat_id seat; double index; } qa_remote_seat;
typedef struct qa_client_attachment {
  const char *kind; /* "local"|"remote"|"headless" */
  qa_seat_id seat; qa_net_address endpoint;
  qa_remote_seat *remote_seats; size_t remote_seat_count;
} qa_client_attachment;
void qa_client_attachment_destroy(qa_client_attachment *a);
typedef struct qa_session_client {
  qa_client_id id; qa_client_attachment attachment; qa_wire_selection wire;
  const char *phase; double connected_at, last_received_at;
} qa_session_client;
typedef struct qa_network_session qa_network_session;
struct qa_client_record { qa_session_client value; qa_event_ring events; };
struct qa_network_session {
  const qa_identity_owner *identities;
  const qa_composition_identity *identity;
  qa_source_wire_supports_fn supports; void *supports_ctx;
  struct qa_client_record *clients; size_t count, capacity;
  bool ended;
};
void qa_network_session_init(qa_network_session *s, const qa_identity_owner *identities,
                             const qa_composition_identity *identity,
                             qa_source_wire_supports_fn supports, void *supports_ctx);
void qa_network_session_destroy(qa_network_session *s);
qa_session_id qa_network_session_get_session(const qa_network_session *s);
/* Attachment/wire are copied (endpoint cloned; remote seats copied). */
qa_wire_admission qa_network_session_connect(qa_network_session *s, qa_client_id id,
                                             const qa_client_attachment *attachment,
                                             const qa_wire_selection *wire, double now);
const qa_session_client *qa_network_session_get_client(qa_network_session *s, qa_client_id id);
size_t qa_network_session_client_count(qa_network_session *s);
const qa_session_client *qa_network_session_client_at(qa_network_session *s, size_t index);
void qa_network_session_prime(qa_network_session *s, qa_client_id id);
void qa_network_session_activate(qa_network_session *s, qa_client_id id);
void qa_network_session_received(qa_network_session *s, qa_client_id id, double now);
void qa_network_session_rebind(qa_network_session *s, qa_client_id id, const qa_net_address *endpoint);
void qa_network_session_route(qa_network_session *s, const qa_simulation_event *event);
/* Malloc'd event array (ring drainAll storage); caller frees. */
qa_simulation_event *qa_network_session_drain(qa_network_session *s, qa_client_id id, size_t *out_count);
qa_client_id *qa_network_session_expired(qa_network_session *s, double now, double timeout_ms, size_t *out_count);
void qa_network_session_disconnect(qa_network_session *s, qa_client_id id);
void qa_network_session_close(qa_network_session *s);
#endif

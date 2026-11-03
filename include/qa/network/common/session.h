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
/* Incremental SHA-256 (node:crypto Hash update/digest). */
typedef struct qa_net_sha256 { uint32_t state[8]; uint64_t length; uint8_t buffer[64]; size_t buffered; } qa_net_sha256;
void qa_net_sha256_init(qa_net_sha256 *hash);
void qa_net_sha256_update(qa_net_sha256 *hash, const uint8_t *data, size_t length);
void qa_net_sha256_digest(qa_net_sha256 *hash, uint8_t out[32]);
void qa_net_sha256_hex_digest(qa_net_sha256 *hash, char out[65]);
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
#endif

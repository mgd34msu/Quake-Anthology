/* Port of src/network/common/session.ts. GPL-2.0-or-later. */
#include "qa/network/common/session.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
/* --- SHA-256 (FIPS 180-4), written for this port. --- */
static const uint32_t SHA_K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
void qa_net_sha256_hex(const uint8_t *data, size_t length, char out[65]) {
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  size_t total = length + 9;
  size_t padded = (total + 63) / 64 * 64;
  uint8_t *msg = calloc(padded, 1);
  memcpy(msg, data, length); msg[length] = 0x80;
  uint64_t bits = (uint64_t)length * 8;
  for (int i = 0; i < 8; i++) msg[padded - 8 + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
  for (size_t off = 0; off < padded; off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)msg[off+4*(size_t)i]<<24)|((uint32_t)msg[off+4*(size_t)i+1]<<16)|((uint32_t)msg[off+4*(size_t)i+2]<<8)|msg[off+4*(size_t)i+3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3);
      uint32_t s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],dd=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t s1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + s1 + ch + SHA_K[i] + w[i];
      uint32_t s0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22);
      uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = s0 + mj;
      hh=g; g=f; f=e; e=dd+t1; dd=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=dd; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  }
  free(msg);
  static const char hexd[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) out[i*8+j] = hexd[(h[i] >> (28 - 4*j)) & 15];
  out[64] = '\0';
}
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
void qa_composition_identity_destroy(qa_composition_identity *id) {
  free(id->digest); free(id->composition.canonical_text); free(id->composition.snapshot_schema);
  memset(id, 0, sizeof *id);
}
int qa_make_composition_identity(const char *canonical_text, const char *snapshot_schema, qa_composition_identity *out) {
  memset(out, 0, sizeof *out);
  out->composition.schema_version = 1;
  out->composition.canonical_text = dupstr(canonical_text);
  out->composition.snapshot_schema = dupstr(snapshot_schema);
  if (!out->composition.canonical_text || !out->composition.snapshot_schema) return 0;
  char hex[65]; qa_net_sha256_hex((const uint8_t *)canonical_text, strlen(canonical_text), hex);
  out->digest = qa_create_content_digest(hex);
  return out->digest != NULL;
}
uint8_t *qa_encode_composition_offer(const qa_composition_identity *identity, size_t *out_len) {
  size_t plen = strlen(identity->composition.canonical_text);
  uint8_t *b = malloc(plen + 10); if (!b) return NULL;
  b[0]=81; b[1]=84; b[2]=83; b[3]=85;
  b[4]=1; b[5]=0;
  uint32_t n = (uint32_t)plen;
  b[6]=(uint8_t)n; b[7]=(uint8_t)(n>>8); b[8]=(uint8_t)(n>>16); b[9]=(uint8_t)(n>>24);
  memcpy(b + 10, identity->composition.canonical_text, plen);
  *out_len = plen + 10; return b;
}
static qa_wire_admission unsupported1(const char *reason) {
  qa_wire_admission a; memset(&a, 0, sizeof a);
  a.supported = false; a.reason_count = 1;
  a.reasons = malloc(sizeof *a.reasons); a.reasons[0] = dupstr(reason);
  return a;
}
void qa_wire_admission_destroy(qa_wire_admission *a) {
  for (size_t i = 0; i < a->reason_count; i++) free(a->reasons[i]);
  free(a->reasons); memset(a, 0, sizeof *a);
}
qa_wire_admission qa_admit_composition_offer(const qa_composition_identity *local, const uint8_t *bytes, size_t length) {
  if (length < 10 || bytes[0] != 81 || bytes[1] != 84 || bytes[2] != 83 || bytes[3] != 85)
    return unsupported1("Not a unified composition offer");
  uint16_t version = (uint16_t)((uint16_t)bytes[4] | ((uint16_t)bytes[5] << 8));
  uint32_t plen = (uint32_t)bytes[6]|((uint32_t)bytes[7]<<8)|((uint32_t)bytes[8]<<16)|((uint32_t)bytes[9]<<24);
  if (version != 1 || (size_t)plen != length - 10)
    return unsupported1("Invalid unified composition version or length");
  char hex[65]; qa_net_sha256_hex(bytes + 10, length - 10, hex);
  char *digest = qa_create_content_digest(hex);
  bool same = digest && local->digest && strcmp(digest, local->digest) == 0;
  free(digest);
  if (same) { qa_wire_admission a; memset(&a, 0, sizeof a); a.supported = true; return a; }
  return unsupported1("Content, executable, numeric, actor or snapshot composition differs");
}
qa_wire_admission qa_admit_wire(const qa_composition_identity *identity, const qa_wire_selection *selection,
                                qa_source_wire_supports_fn supports, void *ctx) {
  if (!selection->is_unified) return supports(ctx, &identity->composition, selection->protocol);
  qa_wire_admission a; memset(&a, 0, sizeof a);
  const char *reasons[2]; size_t n = 0;
  if (!selection->composition || !identity->digest || strcmp(selection->composition, identity->digest) != 0)
    reasons[n++] = "Session composition differs";
  if (!selection->snapshot_schema || strcmp(selection->snapshot_schema, identity->composition.snapshot_schema) != 0)
    reasons[n++] = "Snapshot schema differs";
  if (n == 0) { a.supported = true; return a; }
  a.supported = false; a.reason_count = n;
  a.reasons = malloc(n * sizeof *a.reasons);
  for (size_t i = 0; i < n; i++) a.reasons[i] = dupstr(reasons[i]);
  return a;
}
void qa_client_attachment_destroy(qa_client_attachment *a) {
  qa_net_address_destroy(&a->endpoint); free(a->remote_seats); a->remote_seats = NULL;
}
void qa_network_session_init(qa_network_session *s, const qa_identity_owner *identities,
                             const qa_composition_identity *identity,
                             qa_source_wire_supports_fn supports, void *supports_ctx) {
  memset(s, 0, sizeof *s);
  s->identities = identities; s->identity = identity; s->supports = supports; s->supports_ctx = supports_ctx;
}
void qa_network_session_destroy(qa_network_session *s) {
  for (size_t i = 0; i < s->count; i++) {
    qa_client_attachment_destroy(&s->clients[i].value.attachment);
    qa_event_ring_destroy(&s->clients[i].events);
  }
  free(s->clients); memset(s, 0, sizeof *s);
}
qa_session_id qa_network_session_get_session(const qa_network_session *s) { return s->identities->session; }
static void opened(qa_network_session *s) { if (s->ended) qa_range_error("Network session is closed"); }
static struct qa_client_record *find_record(qa_network_session *s, qa_client_id id) {
  opened(s);
  struct qa_client_record *rec = NULL;
  for (size_t i = 0; i < s->count; i++) if (s->clients[i].value.id.slot == id.slot) rec = &s->clients[i];
  if (!qa_identity_owner_owns_client(s->identities, id) || !rec || !qa_same_client(rec->value.id, id))
    qa_range_error("Client is stale or belongs to another network session");
  return rec;
}
static bool seat_in(const qa_seat_id *seats, size_t n, qa_seat_id seat) {
  for (size_t i = 0; i < n; i++) if (qa_same_seat(seats[i], seat)) return true;
  return false;
}
qa_wire_admission qa_network_session_connect(qa_network_session *s, qa_client_id id,
                                             const qa_client_attachment *attachment,
                                             const qa_wire_selection *wire, double now) {
  opened(s);
  bool slot_taken = false;
  for (size_t i = 0; i < s->count; i++) if (s->clients[i].value.id.slot == id.slot) slot_taken = true;
  if (!qa_identity_owner_owns_client(s->identities, id) || slot_taken)
    qa_range_error("Client slot is foreign or already connected");
  if (strcmp(attachment->kind, "local") == 0) {
    if (!qa_identity_owner_owns_seat(s->identities, attachment->seat))
      qa_range_error("Seat belongs to another session");
    for (size_t i = 0; i < s->count; i++) {
      const qa_client_attachment *a = &s->clients[i].value.attachment;
      if (strcmp(a->kind, "local") == 0 && qa_same_seat(a->seat, attachment->seat))
        qa_range_error("Seat is already connected");
    }
  }
  if (strcmp(attachment->kind, "remote") == 0) {
    bool bad = false;
    for (size_t i = 0; i < attachment->remote_seat_count && !bad; i++) {
      double idx = attachment->remote_seats[i].index;
      if (idx != (double)(int64_t)idx || idx < 0 || !qa_identity_owner_owns_seat(s->identities, attachment->remote_seats[i].seat)) bad = true;
      for (size_t j = i + 1; j < attachment->remote_seat_count; j++) {
        if (attachment->remote_seats[j].index == idx) bad = true;
        if (attachment->remote_seats[j].seat.index == attachment->remote_seats[i].seat.index) bad = true;
      }
    }
    if (bad) qa_range_error("Invalid remote seat bindings");
  }
  /* Assigned seats vs existing seats overlap check. */
  size_t assigned_count = strcmp(attachment->kind, "local") == 0 ? 1
    : strcmp(attachment->kind, "remote") == 0 ? attachment->remote_seat_count : 0;
  qa_seat_id *assigned = malloc((assigned_count ? assigned_count : 1) * sizeof *assigned);
  if (assigned_count == 1 && strcmp(attachment->kind, "local") == 0) assigned[0] = attachment->seat;
  else for (size_t i = 0; i < assigned_count; i++) assigned[i] = attachment->remote_seats[i].seat;
  for (size_t i = 0; i < s->count; i++) {
    const qa_client_attachment *a = &s->clients[i].value.attachment;
    if (strcmp(a->kind, "local") == 0) {
      if (seat_in(assigned, assigned_count, a->seat)) { free(assigned); qa_range_error("Seat already belongs to a connected client"); }
    } else if (strcmp(a->kind, "remote") == 0) {
      for (size_t j = 0; j < a->remote_seat_count; j++)
        if (seat_in(assigned, assigned_count, a->remote_seats[j].seat)) { free(assigned); qa_range_error("Seat already belongs to a connected client"); }
    }
  }
  free(assigned);
  qa_wire_admission result = qa_admit_wire(s->identity, wire, s->supports, s->supports_ctx);
  if (!result.supported) return result;
  if (s->count == s->capacity) { size_t nc = s->capacity ? s->capacity * 2 : 8; s->clients = realloc(s->clients, nc * sizeof *s->clients); s->capacity = nc; }
  struct qa_client_record *rec = &s->clients[s->count++];
  memset(rec, 0, sizeof *rec);
  rec->value.id = id;
  rec->value.attachment.kind = attachment->kind;
  rec->value.attachment.seat = attachment->seat;
  (void)qa_net_address_clone(&attachment->endpoint, &rec->value.attachment.endpoint);
  rec->value.attachment.remote_seat_count = attachment->remote_seat_count;
  if (attachment->remote_seat_count) {
    rec->value.attachment.remote_seats = malloc(attachment->remote_seat_count * sizeof *rec->value.attachment.remote_seats);
    memcpy(rec->value.attachment.remote_seats, attachment->remote_seats,
           attachment->remote_seat_count * sizeof *rec->value.attachment.remote_seats);
  }
  rec->value.wire = *wire;
  rec->value.phase = "connected"; rec->value.connected_at = now; rec->value.last_received_at = now;
  (void)qa_event_ring_init(&rec->events, 8192, sizeof(qa_simulation_event));
  return result;
}
const qa_session_client *qa_network_session_get_client(qa_network_session *s, qa_client_id id) { return &find_record(s, id)->value; }
size_t qa_network_session_client_count(qa_network_session *s) { opened(s); return s->count; }
const qa_session_client *qa_network_session_client_at(qa_network_session *s, size_t index) {
  opened(s); return &s->clients[index].value;
}
void qa_network_session_prime(qa_network_session *s, qa_client_id id) {
  struct qa_client_record *rec = find_record(s, id);
  if (strcmp(rec->value.phase, "connected") != 0) qa_range_error("Client is not awaiting gamestate");
  rec->value.phase = "primed";
}
void qa_network_session_activate(qa_network_session *s, qa_client_id id) {
  struct qa_client_record *rec = find_record(s, id);
  if (strcmp(rec->value.phase, "primed") != 0) qa_range_error("Client has not received gamestate");
  rec->value.phase = "active";
}
void qa_network_session_received(qa_network_session *s, qa_client_id id, double now) {
  find_record(s, id)->value.last_received_at = now;
}
void qa_network_session_rebind(qa_network_session *s, qa_client_id id, const qa_net_address *endpoint) {
  struct qa_client_record *rec = find_record(s, id);
  if (!qa_net_same_address(&rec->value.attachment.endpoint, endpoint, false))
    qa_range_error("Port rebinding cannot change base address");
  qa_net_address_destroy(&rec->value.attachment.endpoint);
  (void)qa_net_address_clone(endpoint, &rec->value.attachment.endpoint);
}
void qa_network_session_route(qa_network_session *s, const qa_simulation_event *event) {
  opened(s);
  for (size_t i = 0; i < s->count; i++) {
    struct qa_client_record *rec = &s->clients[i];
    const qa_event_audience *aud = &event->audience;
    bool admit = strcmp(aud->kind, "world") == 0
      || (strcmp(aud->kind, "client") == 0 && qa_same_client(rec->value.id, aud->client));
    if (!admit && strcmp(aud->kind, "seat") == 0) {
      const qa_client_attachment *a = &rec->value.attachment;
      if (strcmp(a->kind, "local") == 0 && qa_same_seat(a->seat, aud->seat)) admit = true;
      if (strcmp(a->kind, "remote") == 0)
        for (size_t j = 0; j < a->remote_seat_count; j++) if (qa_same_seat(a->remote_seats[j].seat, aud->seat)) admit = true;
    }
    if (admit) (void)qa_event_ring_admit(&rec->events, event);
  }
}
qa_simulation_event *qa_network_session_drain(qa_network_session *s, qa_client_id id, size_t *out_count) {
  struct qa_client_record *rec = find_record(s, id);
  return qa_event_ring_drain_all(&rec->events, out_count);
}
qa_client_id *qa_network_session_expired(qa_network_session *s, double now, double timeout_ms, size_t *out_count) {
  opened(s);
  qa_client_id *out = malloc((s->count ? s->count : 1) * sizeof *out);
  size_t n = 0;
  for (size_t i = 0; i < s->count; i++) {
    const qa_session_client *c = &s->clients[i].value;
    if (strcmp(c->attachment.kind, "local") != 0 && now - c->last_received_at > timeout_ms) out[n++] = c->id;
  }
  *out_count = n; return out;
}
void qa_network_session_disconnect(qa_network_session *s, qa_client_id id) {
  struct qa_client_record *rec = find_record(s, id);
  size_t idx = (size_t)(rec - s->clients);
  qa_client_attachment_destroy(&rec->value.attachment);
  qa_event_ring_destroy(&rec->events);
  memmove(&s->clients[idx], &s->clients[idx + 1], (s->count - idx - 1) * sizeof *s->clients);
  s->count--;
}
void qa_network_session_close(qa_network_session *s) {
  if (s->ended) return;
  s->ended = true;
  for (size_t i = 0; i < s->count; i++) {
    qa_client_attachment_destroy(&s->clients[i].value.attachment);
    qa_event_ring_destroy(&s->clients[i].events);
  }
  s->count = 0;
}

/* Port of src/network/common/session.ts. GPL-2.0-or-later. */
#include "qa/network/common/session.h"
#include "qa/foundation/support.h"
#include <math.h>
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
static void sha256_block(uint32_t h[8], const uint8_t *msg) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)msg[4*i]<<24)|((uint32_t)msg[4*i+1]<<16)|((uint32_t)msg[4*i+2]<<8)|msg[4*i+3];
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
void qa_net_sha256_init(qa_net_sha256 *hash) {
  static const uint32_t initial[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(hash->state, initial, sizeof initial); hash->length = 0; hash->buffered = 0;
}
void qa_net_sha256_update(qa_net_sha256 *hash, const uint8_t *data, size_t length) {
  hash->length += length;
  while (length > 0) {
    size_t take = 64 - hash->buffered; if (take > length) take = length;
    memcpy(hash->buffer + hash->buffered, data, take);
    hash->buffered += take; data += take; length -= take;
    if (hash->buffered == 64) { sha256_block(hash->state, hash->buffer); hash->buffered = 0; }
  }
}
void qa_net_sha256_digest(qa_net_sha256 *hash, uint8_t out[32]) {
  uint64_t bits = hash->length * 8;
  uint8_t pad = 0x80, zero = 0;
  uint64_t length = hash->length;
  qa_net_sha256_update(hash, &pad, 1);
  while (hash->buffered != 56) qa_net_sha256_update(hash, &zero, 1);
  uint8_t tail[8]; for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - 8 * i));
  qa_net_sha256_update(hash, tail, 8);
  hash->length = length;
  for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) out[i*4+j] = (uint8_t)(hash->state[i] >> (24 - 8*j));
}
void qa_net_sha256_hex_digest(qa_net_sha256 *hash, char out[65]) {
  uint8_t bytes[32]; qa_net_sha256_digest(hash, bytes);
  static const char hexd[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) { out[i*2] = hexd[bytes[i] >> 4]; out[i*2+1] = hexd[bytes[i] & 15]; }
  out[64] = '\0';
}
void qa_net_sha256_hex(const uint8_t *data, size_t length, char out[65]) {
  qa_net_sha256 hash; qa_net_sha256_init(&hash); qa_net_sha256_update(&hash, data, length); qa_net_sha256_hex_digest(&hash, out);
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

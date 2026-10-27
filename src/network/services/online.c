/* Port of src/network/services/online.ts. GPL-2.0-or-later. */
#include "qa/network/services/online.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char *dupstr(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }
/* --- SHA-256 with context (for HMAC/scrypt). --- */
struct sha256 { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t buflen; };
static const uint32_t K256[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define RR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(struct sha256 *s, const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4*(size_t)i]<<24)|((uint32_t)p[4*(size_t)i+1]<<16)|((uint32_t)p[4*(size_t)i+2]<<8)|p[4*(size_t)i+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = RR(w[i-15],7)^RR(w[i-15],18)^(w[i-15]>>3);
    uint32_t s1 = RR(w[i-2],17)^RR(w[i-2],19)^(w[i-2]>>10);
    w[i] = w[i-16]+s0+w[i-7]+s1;
  }
  uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = RR(e,6)^RR(e,11)^RR(e,25), ch = (e&f)^(~e&g);
    uint32_t t1 = h+s1+ch+K256[i]+w[i];
    uint32_t s0 = RR(a,2)^RR(a,13)^RR(a,22), mj = (a&b)^(a&c)^(b&c);
    uint32_t t2 = s0+mj;
    h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
  }
  s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}
static void sha_init(struct sha256 *s) {
  static const uint32_t H0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(s->h, H0, sizeof H0); s->len = 0; s->buflen = 0;
}
static void sha_update(struct sha256 *s, const uint8_t *data, size_t n) {
  s->len += n;
  while (n) {
    size_t take = 64 - s->buflen; if (take > n) take = n;
    memcpy(s->buf + s->buflen, data, take);
    s->buflen += take; data += take; n -= take;
    if (s->buflen == 64) { sha_block(s, s->buf); s->buflen = 0; }
  }
}
static void sha_final(struct sha256 *s, uint8_t out[32]) {
  uint64_t bits = s->len * 8;
  uint8_t pad = 0x80; sha_update(s, &pad, 1);
  uint8_t zero = 0;
  while (s->buflen != 56) sha_update(s, &zero, 1);
  uint8_t lenbuf[8];
  for (int i = 0; i < 8; i++) lenbuf[i] = (uint8_t)(bits >> (56 - 8*i));
  /* Feed length without counting it in s->len accounting of the pad loop:
   * buflen is 56, appending 8 completes the block. */
  memcpy(s->buf + 56, lenbuf, 8);
  sha_block(s, s->buf);
  for (int i = 0; i < 8; i++) {
    out[4*(size_t)i] = (uint8_t)(s->h[i] >> 24); out[4*(size_t)i+1] = (uint8_t)(s->h[i] >> 16);
    out[4*(size_t)i+2] = (uint8_t)(s->h[i] >> 8); out[4*(size_t)i+3] = (uint8_t)s->h[i];
  }
}
static void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *data, size_t dlen, uint8_t out[32]) {
  uint8_t kblock[64]; memset(kblock, 0, sizeof kblock);
  if (klen > 64) { struct sha256 s; sha_init(&s); sha_update(&s, key, klen); sha_final(&s, kblock); }
  else memcpy(kblock, key, klen);
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) { ipad[i] = (uint8_t)(kblock[i] ^ 0x36); opad[i] = (uint8_t)(kblock[i] ^ 0x5c); }
  struct sha256 s; sha_init(&s); sha_update(&s, ipad, 64); sha_update(&s, data, dlen);
  uint8_t inner[32]; sha_final(&s, inner);
  sha_init(&s); sha_update(&s, opad, 64); sha_update(&s, inner, 32); sha_final(&s, out);
}
static void pbkdf2(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t iter, uint8_t *out, size_t dklen) {
  uint8_t *msg = malloc(slen + 4);
  memcpy(msg, salt, slen);
  for (size_t block = 1, done = 0; done < dklen; block++) {
    msg[slen] = (uint8_t)(block >> 24); msg[slen+1] = (uint8_t)(block >> 16);
    msg[slen+2] = (uint8_t)(block >> 8); msg[slen+3] = (uint8_t)block;
    uint8_t u[32], t[32];
    hmac_sha256(pw, pwlen, msg, slen + 4, u);
    memcpy(t, u, 32);
    for (uint32_t i = 1; i < iter; i++) {
      hmac_sha256(pw, pwlen, u, 32, u);
      for (int j = 0; j < 32; j++) t[j] ^= u[j];
    }
    size_t n = dklen - done < 32 ? dklen - done : 32;
    memcpy(out + done, t, n); done += n;
  }
  free(msg);
}
#define ROTL(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
static void salsa208(uint32_t x[16]) {
  uint32_t o[16]; memcpy(o, x, sizeof o);
  for (int i = 0; i < 8; i += 2) {
    x[ 4] ^= ROTL(x[ 0]+x[12], 7);  x[ 8] ^= ROTL(x[ 4]+x[ 0], 9);
    x[12] ^= ROTL(x[ 8]+x[ 4],13);  x[ 0] ^= ROTL(x[12]+x[ 8],18);
    x[ 9] ^= ROTL(x[ 5]+x[ 1], 7);  x[13] ^= ROTL(x[ 9]+x[ 5], 9);
    x[ 1] ^= ROTL(x[13]+x[ 9],13);  x[ 5] ^= ROTL(x[ 1]+x[13],18);
    x[14] ^= ROTL(x[10]+x[ 6], 7);  x[ 2] ^= ROTL(x[14]+x[10], 9);
    x[ 6] ^= ROTL(x[ 2]+x[14],13);  x[10] ^= ROTL(x[ 6]+x[ 2],18);
    x[ 3] ^= ROTL(x[15]+x[11], 7);  x[ 7] ^= ROTL(x[ 3]+x[15], 9);
    x[11] ^= ROTL(x[ 7]+x[ 3],13);  x[15] ^= ROTL(x[11]+x[ 7],18);
    x[ 1] ^= ROTL(x[ 0]+x[ 3], 7);  x[ 2] ^= ROTL(x[ 1]+x[ 0], 9);
    x[ 3] ^= ROTL(x[ 2]+x[ 1],13);  x[ 0] ^= ROTL(x[ 3]+x[ 2],18);
    x[ 6] ^= ROTL(x[ 5]+x[ 4], 7);  x[ 7] ^= ROTL(x[ 6]+x[ 5], 9);
    x[ 4] ^= ROTL(x[ 7]+x[ 6],13);  x[ 5] ^= ROTL(x[ 4]+x[ 7],18);
    x[11] ^= ROTL(x[10]+x[ 9], 7);  x[ 8] ^= ROTL(x[11]+x[10], 9);
    x[ 9] ^= ROTL(x[ 8]+x[11],13);  x[10] ^= ROTL(x[ 9]+x[ 8],18);
    x[12] ^= ROTL(x[15]+x[14], 7);  x[13] ^= ROTL(x[12]+x[15], 9);
    x[14] ^= ROTL(x[13]+x[12],13);  x[15] ^= ROTL(x[14]+x[13],18);
  }
  for (int i = 0; i < 16; i++) x[i] += o[i];
}
static uint32_t load32le(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void store32le(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void blockmix(const uint8_t *in, uint8_t *out, int r) {
  uint32_t x[16];
  for (int j = 0; j < 16; j++) x[j] = load32le(in + (size_t)(2*r - 1) * 64 + 4*(size_t)j);
  uint8_t *y = malloc((size_t)128 * (size_t)r);
  for (int i = 0; i < 2 * r; i++) {
    for (int j = 0; j < 16; j++) x[j] ^= load32le(in + (size_t)i * 64 + 4*(size_t)j);
    salsa208(x);
    for (int j = 0; j < 16; j++) store32le(y + (size_t)i * 64 + 4*(size_t)j, x[j]);
  }
  for (int i = 0; i < r; i++) memcpy(out + (size_t)i * 64, y + (size_t)(2*i) * 64, 64);
  for (int i = 0; i < r; i++) memcpy(out + (size_t)(r + i) * 64, y + (size_t)(2*i + 1) * 64, 64);
  free(y);
}
static uint64_t integerify(const uint8_t *b, int r) {
  const uint8_t *last = b + (size_t)(2*r - 1) * 64;
  return (uint64_t)load32le(last) | ((uint64_t)load32le(last + 4) << 32);
}
static void romix(uint8_t *block, int r, uint32_t n) {
  size_t bsz = (size_t)128 * (size_t)r;
  uint8_t *v = malloc(bsz * n);
  uint8_t *x = malloc(bsz), *t = malloc(bsz);
  memcpy(x, block, bsz);
  for (uint32_t i = 0; i < n; i++) { memcpy(v + bsz * i, x, bsz); blockmix(x, t, r); memcpy(x, t, bsz); }
  for (uint32_t i = 0; i < n; i++) {
    uint64_t j = integerify(x, r) % n;
    for (size_t k = 0; k < bsz; k++) x[k] ^= v[bsz * (size_t)j + k];
    blockmix(x, t, r); memcpy(x, t, bsz);
  }
  memcpy(block, x, bsz);
  free(v); free(x); free(t);
}
/* Node scryptSync defaults N=16384 r=8 p=1; salt is the hex TEXT bytes. */
static void scrypt_hex(const char *password, const char *salt_text, char out_hex[65]) {
  const uint32_t N = 16384, rr = 8, pp = 1;
  size_t dklen = 32;
  size_t bsz = (size_t)128 * rr * pp;
  uint8_t *b = malloc(bsz);
  pbkdf2((const uint8_t *)password, strlen(password), (const uint8_t *)salt_text, strlen(salt_text), 1, b, bsz);
  for (uint32_t i = 0; i < pp; i++) romix(b + (size_t)i * 128 * rr, (int)rr, N);
  uint8_t *dk = malloc(dklen);
  pbkdf2((const uint8_t *)password, strlen(password), b, bsz, 1, dk, dklen);
  free(b);
  static const char hexd[] = "0123456789abcdef";
  for (size_t i = 0; i < dklen; i++) { out_hex[2*i] = hexd[dk[i] >> 4]; out_hex[2*i+1] = hexd[dk[i] & 15]; }
  out_hex[64] = '\0';
  free(dk);
}
static void random_hex(char *out, size_t bytes) {
  uint8_t *buf = malloc(bytes);
  int fd = open("/dev/urandom", O_RDONLY);
  size_t done = 0;
  if (fd >= 0) {
    while (done < bytes) { ssize_t n = read(fd, buf + done, bytes - done); if (n <= 0) break; done += (size_t)n; }
    close(fd);
  }
  static const char hexd[] = "0123456789abcdef";
  for (size_t i = 0; i < bytes; i++) { out[2*i] = hexd[buf[i] >> 4]; out[2*i+1] = hexd[buf[i] & 15]; }
  out[2*bytes] = '\0';
  free(buf);
}
static bool timing_equal(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}
static void hex_to_bytes(const char *hex, uint8_t *out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int hi = hex[2*i] >= 'a' ? hex[2*i] - 'a' + 10 : hex[2*i] - '0';
    int lo = hex[2*i+1] >= 'a' ? hex[2*i+1] - 'a' + 10 : hex[2*i+1] - '0';
    out[i] = (uint8_t)(hi * 16 + lo);
  }
}
void qa_online_account_destroy(qa_online_account *a) { free(a->id); free(a->name); memset(a, 0, sizeof *a); }
void qa_authorization_destroy(qa_authorization *a) {
  qa_online_account_destroy(&a->account);
  free(a->token); free(a->reason); free(a->provider); memset(a, 0, sizeof *a);
}
struct account_record { qa_online_account account; char *salt, *password_hash; };
struct token_entry { char *token; qa_online_account account; };
struct qa_local_authorization {
  struct account_record *accounts; size_t account_count, account_cap;
  char **names;
  struct token_entry *tokens; size_t token_count, token_cap;
};
void qa_local_authorization_init(qa_local_authorization *a) { memset(a, 0, sizeof *a); }
void qa_local_authorization_destroy(qa_local_authorization *a) {
  for (size_t i = 0; i < a->account_count; i++) {
    qa_online_account_destroy(&a->accounts[i].account);
    free(a->accounts[i].salt); free(a->accounts[i].password_hash); free(a->names[i]);
  }
  for (size_t i = 0; i < a->token_count; i++) { free(a->tokens[i].token); qa_online_account_destroy(&a->tokens[i].account); }
  free(a->accounts); free(a->names); free(a->tokens); memset(a, 0, sizeof *a);
}
static struct account_record *find_account(qa_local_authorization *a, const char *name) {
  for (size_t i = 0; i < a->account_count; i++) if (strcmp(a->names[i], name) == 0) return &a->accounts[i];
  return NULL;
}
static bool trimmed_empty(const char *s) {
  for (; *s; s++) if (!isspace((unsigned char)*s)) return false;
  return true;
}
void qa_local_authorization_register(qa_local_authorization *a, const char *name, const char *password, qa_online_account *out) {
  if (trimmed_empty(name) || password[0] == '\0' || find_account(a, name))
    qa_range_error("Account requires an unused name and a password");
  char idbuf[48], hexbuf[33];
  random_hex(hexbuf, 16);
  snprintf(idbuf, sizeof idbuf, "account:%s", hexbuf);
  char salt[33]; random_hex(salt, 16);
  char hash[65]; scrypt_hex(password, salt, hash);
  if (a->account_count == a->account_cap) {
    size_t nc = a->account_cap ? a->account_cap * 2 : 8;
    a->accounts = realloc(a->accounts, nc * sizeof *a->accounts);
    a->names = realloc(a->names, nc * sizeof *a->names);
    a->account_cap = nc;
  }
  struct account_record *rec = &a->accounts[a->account_count];
  rec->account.id = dupstr(idbuf); rec->account.name = dupstr(name);
  rec->salt = dupstr(salt); rec->password_hash = dupstr(hash);
  a->names[a->account_count++] = dupstr(name);
  out->id = dupstr(rec->account.id); out->name = dupstr(rec->account.name);
}
void qa_local_authorization_authorize(qa_local_authorization *a, const char *name, const char *credential, qa_authorization *out) {
  memset(out, 0, sizeof *out);
  struct account_record *rec = find_account(a, name);
  if (!rec) { out->kind = "denied"; out->reason = dupstr("Invalid account credentials"); return; }
  char supplied_hex[65]; scrypt_hex(credential, rec->salt, supplied_hex);
  uint8_t supplied[32], stored[32];
  hex_to_bytes(supplied_hex, supplied, 32);
  hex_to_bytes(rec->password_hash, stored, 32);
  if (!timing_equal(supplied, stored, 32)) { out->kind = "denied"; out->reason = dupstr("Invalid account credentials"); return; }
  char hexbuf[65]; random_hex(hexbuf, 32);
  char token[80]; snprintf(token, sizeof token, "access:%s", hexbuf);
  if (a->token_count == a->token_cap) { size_t nc = a->token_cap ? a->token_cap * 2 : 8; a->tokens = realloc(a->tokens, nc * sizeof *a->tokens); a->token_cap = nc; }
  a->tokens[a->token_count].token = dupstr(token);
  a->tokens[a->token_count].account.id = dupstr(rec->account.id);
  a->tokens[a->token_count].account.name = dupstr(rec->account.name);
  a->token_count++;
  out->kind = "authorized";
  out->account.id = dupstr(rec->account.id); out->account.name = dupstr(rec->account.name);
  out->token = dupstr(token);
}
int qa_local_authorization_get_account(qa_local_authorization *a, const char *token, qa_online_account *out) {
  for (size_t i = 0; i < a->token_count; i++) if (strcmp(a->tokens[i].token, token) == 0) {
    out->id = dupstr(a->tokens[i].account.id); out->name = dupstr(a->tokens[i].account.name);
    return 1;
  }
  return 0;
}
void qa_local_authorization_revoke(qa_local_authorization *a, const char *token) {
  for (size_t i = 0; i < a->token_count; i++) if (strcmp(a->tokens[i].token, token) == 0) {
    free(a->tokens[i].token); qa_online_account_destroy(&a->tokens[i].account);
    memmove(&a->tokens[i], &a->tokens[i+1], (a->token_count - i - 1) * sizeof *a->tokens);
    a->token_count--; return;
  }
}
static void json_escape_cat(char **buf, size_t *len, size_t *cap, const char *text) {
  /* Append raw text (ids/names here are hex/simple; escape quotes/backslash). */
  for (const char *p = text; *p; p++) {
    char tmp[2] = { *p, '\0' };
    if (*p == '"' || *p == '\\') {
      if (*len + 3 > *cap) { size_t nc = *cap ? *cap * 2 : 256; *buf = realloc(*buf, nc); *cap = nc; }
      (*buf)[(*len)++] = '\\';
    }
    if (*len + 2 > *cap) { size_t nc = *cap ? *cap * 2 : 256; *buf = realloc(*buf, nc); *cap = nc; }
    (*buf)[(*len)++] = tmp[0];
  }
  (*buf)[*len] = '\0';
}
static void cat_raw(char **buf, size_t *len, size_t *cap, const char *text) {
  size_t n = strlen(text);
  if (*len + n + 1 > *cap) { size_t nc = *cap ? *cap : 256; while (nc < *len + n + 1) nc *= 2; *buf = realloc(*buf, nc); *cap = nc; }
  memcpy(*buf + *len, text, n + 1); *len += n;
}
char *qa_local_authorization_save(qa_local_authorization *a) {
  char *buf = NULL; size_t len = 0, cap = 0;
  cat_raw(&buf, &len, &cap, "{\"version\":1,\"accounts\":[");
  for (size_t i = 0; i < a->account_count; i++) {
    if (i) cat_raw(&buf, &len, &cap, ",");
    cat_raw(&buf, &len, &cap, "{\"account\":{\"id\":\"");
    json_escape_cat(&buf, &len, &cap, a->accounts[i].account.id);
    cat_raw(&buf, &len, &cap, "\",\"name\":\"");
    json_escape_cat(&buf, &len, &cap, a->accounts[i].account.name);
    cat_raw(&buf, &len, &cap, "\"},\"salt\":\"");
    cat_raw(&buf, &len, &cap, a->accounts[i].salt);
    cat_raw(&buf, &len, &cap, "\",\"passwordHash\":\"");
    cat_raw(&buf, &len, &cap, a->accounts[i].password_hash);
    cat_raw(&buf, &len, &cap, "\"}");
  }
  cat_raw(&buf, &len, &cap, "]}");
  return buf;
}
static const char *jfind(const char *obj, const char *key) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
  const char *p = strstr(obj, pat);
  if (!p) return NULL;
  p = strchr(p, ':');
  if (!p) return NULL;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n') p++;
  return p;
}
static int jstring(const char *obj, const char *key, char *out, size_t outsz) {
  const char *v = jfind(obj, key);
  if (!v || *v != '"') return 0;
  v++;
  size_t n = 0;
  while (*v && *v != '"' && n + 1 < outsz) {
    if (*v == '\\' && v[1]) v++;
    out[n++] = *v++;
  }
  out[n] = '\0';
  return *v == '"';
}
static bool is_account_id(const char *v) {
  if (strncmp(v, "account:", 8) != 0 || strlen(v) != 40) return false;
  for (int i = 8; i < 40; i++) if (!isxdigit((unsigned char)v[i]) || (v[i] >= 'A' && v[i] <= 'F')) return false;
  return true;
}
static bool is_hex_lower(const char *v, size_t n) {
  if (strlen(v) != n) return false;
  for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)v[i]) || (v[i] >= 'A' && v[i] <= 'F')) return false;
  return true;
}
void qa_local_authorization_restore(qa_local_authorization *a, const char *text) {
  const char *ver = jfind(text, "version");
  const char *acc = jfind(text, "accounts");
  if (!ver || strtod(ver, NULL) != 1 || !acc || *acc != '[') qa_range_error("Invalid local account save");
  struct account_record *restored = NULL; char **names = NULL; size_t count = 0, cap = 0;
  const char *p = acc + 1;
  for (;;) {
    while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    if (*p == ']' || *p == '\0') break;
    if (*p != '{') qa_range_error("Invalid account record");
    const char *start = p;
    int depth = 0; bool in_str = false, esc = false;
    for (; *p; p++) {
      if (esc) { esc = false; continue; }
      if (*p == '\\' && in_str) { esc = true; continue; }
      if (*p == '"') in_str = !in_str;
      if (in_str) continue;
      if (*p == '{') depth++;
      if (*p == '}' && --depth == 0) { p++; break; }
    }
    if (depth != 0) qa_range_error("Invalid account record");
    char *obj = malloc((size_t)(p - start) + 1);
    memcpy(obj, start, (size_t)(p - start)); obj[p - start] = '\0';
    const char *acct = jfind(obj, "account");
    char id[64] = {0}, name[512] = {0}, salt[64] = {0}, hash[80] = {0};
    bool ok = acct && jstring(acct, "id", id, sizeof id) && jstring(acct, "name", name, sizeof name)
      && jstring(obj, "salt", salt, sizeof salt) && jstring(obj, "passwordHash", hash, sizeof hash);
    free(obj);
    if (!ok || !is_account_id(id) || name[0] == '\0' || !is_hex_lower(salt, 32) || !is_hex_lower(hash, 64))
      qa_range_error("Invalid account fields");
    for (size_t i = 0; i < count; i++)
      if (strcmp(names[i], name) == 0 || strcmp(restored[i].account.id, id) == 0)
        qa_range_error("Invalid account fields");
    if (count == cap) { size_t nc = cap ? cap * 2 : 8; restored = realloc(restored, nc * sizeof *restored); names = realloc(names, nc * sizeof *names); cap = nc; }
    restored[count].account.id = dupstr(id); restored[count].account.name = dupstr(name);
    restored[count].salt = dupstr(salt); restored[count].password_hash = dupstr(hash);
    names[count] = dupstr(name);
    count++;
  }
  for (size_t i = 0; i < a->account_count; i++) {
    qa_online_account_destroy(&a->accounts[i].account);
    free(a->accounts[i].salt); free(a->accounts[i].password_hash); free(a->names[i]);
  }
  for (size_t i = 0; i < a->token_count; i++) { free(a->tokens[i].token); qa_online_account_destroy(&a->tokens[i].account); }
  free(a->accounts); free(a->names);
  a->accounts = restored; a->names = names; a->account_count = count; a->account_cap = cap;
  a->token_count = 0;
}
static void provider_authorize(void *ctx, const char *name, const char *credential, qa_authorization *out) {
  qa_local_authorization_authorize(ctx, name, credential, out);
}
qa_authorization_provider qa_local_authorization_as_provider(qa_local_authorization *a) {
  qa_authorization_provider p; p.ctx = a; p.authorize = provider_authorize; return p;
}

/* --- Lobbies --- */
struct qa_local_lobby_service { qa_lobby *lobbies; size_t count, capacity; };
void qa_local_lobby_service_init(qa_local_lobby_service *s) { memset(s, 0, sizeof *s); }
static void lobby_free_members(qa_lobby *l) {
  for (size_t i = 0; i < l->member_count; i++) qa_online_account_destroy(&l->members[i].account);
  free(l->members);
}
void qa_local_lobby_service_destroy(qa_local_lobby_service *s) {
  for (size_t i = 0; i < s->count; i++) {
    free(s->lobbies[i].id); free(s->lobbies[i].owner); free(s->lobbies[i].name);
    lobby_free_members(&s->lobbies[i]);
    if (s->lobbies[i].has_endpoint) qa_net_address_destroy(&s->lobbies[i].endpoint);
  }
  free(s->lobbies); memset(s, 0, sizeof *s);
}
static qa_lobby *require_lobby(qa_local_lobby_service *s, const char *id) {
  for (size_t i = 0; i < s->count; i++) if (strcmp(s->lobbies[i].id, id) == 0) return &s->lobbies[i];
  qa_range_error("Lobby no longer exists");
}
static bool is_safe_d(double v) { return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0; }
const qa_lobby *qa_local_lobby_service_create(qa_local_lobby_service *s, const qa_online_account *owner,
                                              const char *name, double capacity,
                                              const qa_composition_identity *composition, double seats) {
  if (!is_safe_d(capacity) || capacity < 1 || !is_safe_d(seats) || seats < 1 || seats > capacity || trimmed_empty(name))
    qa_range_error("Invalid lobby settings");
  if (s->count == s->capacity) { size_t nc = s->capacity ? s->capacity * 2 : 8; s->lobbies = realloc(s->lobbies, nc * sizeof *s->lobbies); s->capacity = nc; }
  qa_lobby *l = &s->lobbies[s->count++];
  memset(l, 0, sizeof *l);
  char hexbuf[33]; random_hex(hexbuf, 16);
  char idbuf[48]; snprintf(idbuf, sizeof idbuf, "lobby:%s", hexbuf);
  l->id = dupstr(idbuf); l->owner = dupstr(owner->id); l->name = dupstr(name);
  l->capacity = capacity; l->composition = composition;
  l->member_count = 1;
  l->members = calloc(1, sizeof *l->members);
  l->members[0].account.id = dupstr(owner->id); l->members[0].account.name = dupstr(owner->name);
  l->members[0].seats = seats; l->members[0].ready = false;
  l->phase = "open"; l->match_generation = 0;
  return l;
}
size_t qa_local_lobby_service_count(qa_local_lobby_service *s) { return s->count; }
const qa_lobby *qa_local_lobby_service_at(qa_local_lobby_service *s, size_t index) { return &s->lobbies[index]; }
const qa_lobby *qa_local_lobby_service_join(qa_local_lobby_service *s, const char *id, const qa_online_account *account, double seats) {
  qa_lobby *l = require_lobby(s, id);
  bool member = false; double total = seats;
  for (size_t i = 0; i < l->member_count; i++) {
    if (strcmp(l->members[i].account.id, account->id) == 0) member = true;
    total += l->members[i].seats;
  }
  if (strcmp(l->phase, "open") != 0 || member) qa_range_error("Cannot join this lobby");
  if (!is_safe_d(seats) || seats < 1 || total > l->capacity) qa_range_error("Lobby has insufficient seat capacity");
  l->members = realloc(l->members, (l->member_count + 1) * sizeof *l->members);
  qa_lobby_member *m = &l->members[l->member_count++];
  m->account.id = dupstr(account->id); m->account.name = dupstr(account->name);
  m->seats = seats; m->ready = false;
  return l;
}
const qa_lobby *qa_local_lobby_service_ready(qa_local_lobby_service *s, const char *id, const char *account, bool ready) {
  qa_lobby *l = require_lobby(s, id);
  bool member = false;
  for (size_t i = 0; i < l->member_count; i++) if (strcmp(l->members[i].account.id, account) == 0) member = true;
  if (strcmp(l->phase, "open") != 0 || !member) qa_range_error("Account cannot change readiness");
  for (size_t i = 0; i < l->member_count; i++) if (strcmp(l->members[i].account.id, account) == 0) l->members[i].ready = ready;
  return l;
}
const qa_lobby *qa_local_lobby_service_start(qa_local_lobby_service *s, const char *id, const char *owner) {
  qa_lobby *l = require_lobby(s, id);
  bool any_unready = false;
  for (size_t i = 0; i < l->member_count; i++) if (!l->members[i].ready) any_unready = true;
  if (strcmp(l->owner, owner) != 0 || strcmp(l->phase, "open") != 0 || any_unready)
    qa_range_error("Lobby is not ready to start");
  l->phase = "starting";
  l->match_generation += 1;
  return l;
}
const qa_lobby *qa_local_lobby_service_publish(qa_local_lobby_service *s, const char *id, const char *owner,
                                               double match_generation, const qa_net_address *endpoint, const qa_wire_selection *wire) {
  qa_lobby *l = require_lobby(s, id);
  if (strcmp(l->owner, owner) != 0 || strcmp(l->phase, "starting") != 0 || l->match_generation != match_generation)
    qa_range_error("Lobby launch is no longer current");
  if (wire->is_unified) {
    bool bad = !wire->composition || !l->composition->digest || strcmp(wire->composition, l->composition->digest) != 0
      || !wire->snapshot_schema || strcmp(wire->snapshot_schema, l->composition->composition.snapshot_schema) != 0;
    if (bad) qa_range_error("Bound lobby wire does not match its prepared composition");
  }
  if (l->has_endpoint) qa_net_address_destroy(&l->endpoint);
  (void)qa_net_address_clone(endpoint, &l->endpoint);
  l->has_endpoint = true; l->wire = *wire; l->phase = "playing";
  return l;
}
const qa_lobby *qa_local_lobby_service_complete(qa_local_lobby_service *s, const char *id, const char *owner, double match_generation) {
  qa_lobby *l = require_lobby(s, id);
  if (strcmp(l->owner, owner) != 0) qa_range_error("Only the lobby owner can complete its match");
  if (strcmp(l->phase, "open") == 0 || l->match_generation != match_generation) return l;
  l->phase = "open";
  if (l->has_endpoint) { qa_net_address_destroy(&l->endpoint); l->has_endpoint = false; }
  memset(&l->wire, 0, sizeof l->wire);
  for (size_t i = 0; i < l->member_count; i++) l->members[i].ready = false;
  return l;
}
void qa_local_lobby_service_leave(qa_local_lobby_service *s, const char *id, const char *account) {
  qa_lobby *l = require_lobby(s, id);
  if (strcmp(l->owner, account) == 0) {
    size_t idx = (size_t)(l - s->lobbies);
    free(l->id); free(l->owner); free(l->name); lobby_free_members(l);
    if (l->has_endpoint) qa_net_address_destroy(&l->endpoint);
    memmove(&s->lobbies[idx], &s->lobbies[idx+1], (s->count - idx - 1) * sizeof *s->lobbies);
    s->count--; return;
  }
  size_t w = 0;
  for (size_t i = 0; i < l->member_count; i++) {
    if (strcmp(l->members[i].account.id, account) == 0) qa_online_account_destroy(&l->members[i].account);
    else l->members[w++] = l->members[i];
  }
  l->member_count = w;
}

/* --- Local rankings --- */
struct stored_player { char *account; double score; bool won; struct { char *key; double value; } *stats; size_t stat_count; };
struct stored_report { char *key, *match, *rules; struct stored_player *players; size_t player_count; };
struct qa_local_ranking_service { struct stored_report *reports; size_t count, capacity; };
void qa_local_ranking_service_init(qa_local_ranking_service *s) { memset(s, 0, sizeof *s); }
static void free_report(struct stored_report *r) {
  free(r->key); free(r->match); free(r->rules);
  for (size_t i = 0; i < r->player_count; i++) {
    free(r->players[i].account);
    for (size_t j = 0; j < r->players[i].stat_count; j++) free(r->players[i].stats[j].key);
    free(r->players[i].stats);
  }
  free(r->players);
}
void qa_local_ranking_service_destroy(qa_local_ranking_service *s) {
  for (size_t i = 0; i < s->count; i++) free_report(&s->reports[i]);
  free(s->reports); memset(s, 0, sizeof *s);
}
bool qa_local_ranking_service_submit(qa_local_ranking_service *s, const qa_online_ranking_report *report) {
  size_t klen = strlen(report->rules) + strlen(report->match) + 8;
  char *key = malloc(klen);
  snprintf(key, klen, "%s\001%s", report->rules, report->match);
  for (size_t i = 0; i < s->count; i++) if (strcmp(s->reports[i].key, key) == 0) { free(key); return false; }
  bool bad = report->match[0] == '\0';
  for (size_t i = 0; i < report->player_count && !bad; i++) {
    if (!isfinite(report->players[i].score)) bad = true;
    for (size_t j = 0; j < report->players[i].statistic_count; j++)
      if (!isfinite(report->players[i].statistics[j].value)) bad = true;
    for (size_t j = i + 1; j < report->player_count; j++)
      if (strcmp(report->players[i].account, report->players[j].account) == 0) bad = true;
  }
  if (bad) { free(key); qa_range_error("Invalid ranking report"); }
  if (s->count == s->capacity) { size_t nc = s->capacity ? s->capacity * 2 : 8; s->reports = realloc(s->reports, nc * sizeof *s->reports); s->capacity = nc; }
  struct stored_report *r = &s->reports[s->count++];
  memset(r, 0, sizeof *r);
  r->key = key; r->match = dupstr(report->match); r->rules = dupstr(report->rules);
  r->player_count = report->player_count;
  r->players = calloc(r->player_count ? r->player_count : 1, sizeof *r->players);
  for (size_t i = 0; i < r->player_count; i++) {
    r->players[i].account = dupstr(report->players[i].account);
    r->players[i].score = report->players[i].score;
    r->players[i].won = report->players[i].won;
    r->players[i].stat_count = report->players[i].statistic_count;
    r->players[i].stats = calloc(r->players[i].stat_count ? r->players[i].stat_count : 1, sizeof *r->players[i].stats);
    for (size_t j = 0; j < r->players[i].stat_count; j++) {
      r->players[i].stats[j].key = dupstr(report->players[i].statistics[j].key);
      r->players[i].stats[j].value = report->players[i].statistics[j].value;
    }
  }
  return true;
}
void qa_ranking_entries_destroy(qa_ranking_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(entries[i].account);
    for (size_t j = 0; j < entries[i].statistic_count; j++) free(entries[i].stat_keys[j]);
    free(entries[i].stat_keys); free(entries[i].statistics);
  }
  free(entries);
}
static int cmp_ranking(const void *a, const void *b) {
  const qa_ranking_entry *x = a, *y = b;
  double ds = y->score - x->score;
  if (ds != 0) return ds > 0 ? 1 : -1;
  double dw = y->wins - x->wins;
  if (dw != 0) return dw > 0 ? 1 : -1;
  return strcmp(x->account, y->account);
}
qa_ranking_entry *qa_local_ranking_service_standings(qa_local_ranking_service *s, qa_provider_id rules, size_t *out_count) {
  qa_ranking_entry *entries = NULL; size_t count = 0, cap = 0;
  for (size_t ri = 0; ri < s->count; ri++) {
    struct stored_report *report = &s->reports[ri];
    if (strcmp(report->rules, rules) != 0) continue;
    for (size_t pi = 0; pi < report->player_count; pi++) {
      struct stored_player *player = &report->players[pi];
      qa_ranking_entry *e = NULL;
      for (size_t i = 0; i < count; i++) if (strcmp(entries[i].account, player->account) == 0) e = &entries[i];
      if (!e) {
        if (count == cap) { size_t nc = cap ? cap * 2 : 8; entries = realloc(entries, nc * sizeof *entries); cap = nc; }
        e = &entries[count++];
        memset(e, 0, sizeof *e); e->account = dupstr(player->account);
      }
      e->matches += 1; e->wins += player->won ? 1 : 0; e->score += player->score;
      for (size_t j = 0; j < player->stat_count; j++) {
        bool found = false;
        for (size_t k = 0; k < e->statistic_count; k++) if (strcmp(e->stat_keys[k], player->stats[j].key) == 0) {
          e->statistics[k].value += player->stats[j].value; found = true; break;
        }
        if (!found) {
          e->statistics = realloc(e->statistics, (e->statistic_count + 1) * sizeof *e->statistics);
          e->stat_keys = realloc(e->stat_keys, (e->statistic_count + 1) * sizeof *e->stat_keys);
          e->stat_keys[e->statistic_count] = dupstr(player->stats[j].key);
          e->statistics[e->statistic_count].key = e->stat_keys[e->statistic_count];
          e->statistics[e->statistic_count].value = player->stats[j].value;
          e->statistic_count++;
        }
      }
    }
  }
  qsort(entries, count, sizeof *entries, cmp_ranking);
  *out_count = count;
  return entries ? entries : calloc(1, sizeof *entries);
}
char *qa_local_ranking_service_save(qa_local_ranking_service *s) {
  char *buf = NULL; size_t len = 0, cap = 0;
  cat_raw(&buf, &len, &cap, "[");
  char num[64];
  for (size_t ri = 0; ri < s->count; ri++) {
    struct stored_report *r = &s->reports[ri];
    if (ri) cat_raw(&buf, &len, &cap, ",");
    cat_raw(&buf, &len, &cap, "{\"match\":\"");
    json_escape_cat(&buf, &len, &cap, r->match);
    cat_raw(&buf, &len, &cap, "\",\"rules\":\"");
    json_escape_cat(&buf, &len, &cap, r->rules);
    cat_raw(&buf, &len, &cap, "\",\"players\":[");
    for (size_t pi = 0; pi < r->player_count; pi++) {
      struct stored_player *p = &r->players[pi];
      if (pi) cat_raw(&buf, &len, &cap, ",");
      cat_raw(&buf, &len, &cap, "{\"account\":\"");
      json_escape_cat(&buf, &len, &cap, p->account);
      snprintf(num, sizeof num, "\",\"score\":%g,\"won\":%s,\"statistics\":[", p->score, p->won ? "true" : "false");
      cat_raw(&buf, &len, &cap, num);
      for (size_t j = 0; j < p->stat_count; j++) {
        if (j) cat_raw(&buf, &len, &cap, ",");
        cat_raw(&buf, &len, &cap, "[\"");
        json_escape_cat(&buf, &len, &cap, p->stats[j].key);
        snprintf(num, sizeof num, "\",%g]", p->stats[j].value);
        cat_raw(&buf, &len, &cap, num);
      }
      cat_raw(&buf, &len, &cap, "]}");
    }
    cat_raw(&buf, &len, &cap, "]}");
  }
  cat_raw(&buf, &len, &cap, "]");
  return buf;
}
void qa_local_ranking_service_restore(qa_local_ranking_service *s, const char *text) {
  const char *p = text;
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
  if (*p != '[') qa_range_error("Invalid ranking save");
  qa_local_ranking_service restored; qa_local_ranking_service_init(&restored);
  p++;
  for (;;) {
    while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    if (*p == ']' || *p == '\0') break;
    if (*p != '{') qa_range_error("Invalid ranking report");
    const char *start = p;
    int depth = 0; bool in_str = false, esc = false;
    for (; *p; p++) {
      if (esc) { esc = false; continue; }
      if (*p == '\\' && in_str) { esc = true; continue; }
      if (*p == '"') in_str = !in_str;
      if (in_str) continue;
      if (*p == '{') depth++;
      if (*p == '}' && --depth == 0) { p++; break; }
    }
    if (depth != 0) qa_range_error("Invalid ranking report");
    char *obj = malloc((size_t)(p - start) + 1);
    memcpy(obj, start, (size_t)(p - start)); obj[p - start] = '\0';
    char match[512] = {0}, rules[512] = {0};
    if (!jstring(obj, "match", match, sizeof match) || !jstring(obj, "rules", rules, sizeof rules)
        || !strchr(rules, ':'))
      qa_range_error("Invalid ranking report");
    const char *players_at = jfind(obj, "players");
    if (!players_at || *players_at != '[') qa_range_error("Invalid ranking report");
    /* Parse players array objects. */
    qa_ranking_report_player *players = NULL; qa_stat_entry *all_stats = NULL;
    char **owned = NULL; size_t owned_count = 0, owned_cap = 0;
    size_t player_count = 0, player_cap = 0;
    const char *q = players_at + 1;
    for (;;) {
      while (*q == ' ' || *q == ',' || *q == '\n' || *q == '\t' || *q == '\r') q++;
      if (*q == ']' || *q == '\0') break;
      if (*q != '{') qa_range_error("Invalid ranked player");
      const char *ps = q;
      int d2 = 0; bool ins = false, esc2 = false;
      for (; *q; q++) {
        if (esc2) { esc2 = false; continue; }
        if (*q == '\\' && ins) { esc2 = true; continue; }
        if (*q == '"') ins = !ins;
        if (ins) continue;
        if (*q == '{') d2++;
        if (*q == '}' && --d2 == 0) { q++; break; }
      }
      char *pobj = malloc((size_t)(q - ps) + 1);
      memcpy(pobj, ps, (size_t)(q - ps)); pobj[q - ps] = '\0';
      if (owned_count == owned_cap) { size_t nc = owned_cap ? owned_cap * 2 : 8; owned = realloc(owned, nc * sizeof *owned); owned_cap = nc; }
      owned[owned_count++] = pobj;
      char account[128] = {0};
      const char *scorev = jfind(pobj, "score"), *wonv = jfind(pobj, "won"), *statsv = jfind(pobj, "statistics");
      if (!jstring(pobj, "account", account, sizeof account) || !is_account_id(account)
          || !scorev || !wonv || (strncmp(wonv, "true", 4) != 0 && strncmp(wonv, "false", 5) != 0)
          || !statsv || *statsv != '[')
        qa_range_error("Invalid ranked player");
      /* Statistics pairs: ["key",value],... */
      qa_stat_entry *stats = NULL; size_t stat_count = 0, stat_cap = 0;
      const char *sp = statsv + 1;
      for (;;) {
        while (*sp == ' ' || *sp == ',' || *sp == '\n') sp++;
        if (*sp == ']' || *sp == '\0') break;
        if (*sp != '[') qa_range_error("Invalid rank statistic");
        sp++;
        while (*sp == ' ') sp++;
        if (*sp != '"') qa_range_error("Invalid rank statistic");
        sp++;
        const char *ks = sp;
        while (*sp && *sp != '"') sp++;
        if (*sp != '"') qa_range_error("Invalid rank statistic");
        char *skey = malloc((size_t)(sp - ks) + 1);
        memcpy(skey, ks, (size_t)(sp - ks)); skey[sp - ks] = '\0';
        sp++;
        while (*sp == ' ' || *sp == ',') sp++;
        char *end = NULL;
        double val = strtod(sp, &end);
        if (end == sp) qa_range_error("Invalid rank statistic");
        sp = end;
        while (*sp == ' ') sp++;
        if (*sp != ']') qa_range_error("Invalid rank statistic");
        sp++;
        if (owned_count == owned_cap) { size_t nc = owned_cap ? owned_cap * 2 : 8; owned = realloc(owned, nc * sizeof *owned); owned_cap = nc; }
        owned[owned_count++] = skey;
        if (stat_count == stat_cap) { size_t nc = stat_cap ? stat_cap * 2 : 4; stats = realloc(stats, nc * sizeof *stats); stat_cap = nc; }
        stats[stat_count].key = skey; stats[stat_count].value = val; stat_count++;
      }
      if (player_count == player_cap) { size_t nc = player_cap ? player_cap * 2 : 4; players = realloc(players, nc * sizeof *players); player_cap = nc; }
      players[player_count].account = dupstr(account);
      players[player_count].score = strtod(scorev, NULL);
      players[player_count].won = strncmp(wonv, "true", 4) == 0;
      players[player_count].statistics = stats;
      players[player_count].statistic_count = stat_count;
      player_count++;
      /* keep stats allocations reachable for free via all_stats chain: store in owned as raw ptrs not possible;
         leak-free handled below by freeing stats arrays after submit copies them. */
      (void)all_stats;
    }
    qa_online_ranking_report report;
    report.match = match; report.rules = rules; report.players = players; report.player_count = player_count;
    bool added = qa_local_ranking_service_submit(&restored, &report);
    for (size_t i = 0; i < player_count; i++) { free((void *)players[i].account); free((void *)players[i].statistics); }
    free(players);
    for (size_t i = 0; i < owned_count; i++) free(owned[i]);
    free(owned);
    free(obj);
    if (!added) qa_range_error("Duplicate saved match");
  }
  for (size_t i = 0; i < s->count; i++) free_report(&s->reports[i]);
  free(s->reports);
  *s = restored;
}

/* Q3 discovery decoders and pak reference string parsing against TS oracle values
 * (bun on the TS source), plus fixed-seed truncate/mutate loops: every decode of a
 * hostile packet runs under qa_try and must neither crash nor leak (ASan/LSan).
 * GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/client_server_command.h"
#include "qa/network/q3/discovery.h"
#include "qa/network/q3/game_state.h"
#include "qa/network/q3/pak_references.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint8_t *from_hex(const char *hex, size_t *len) {
  *len = strlen(hex) / 2;
  uint8_t *out = malloc(*len ? *len : 1);
  for (size_t i = 0; i < *len; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
  return out;
}
static bool same_bytes(const uint8_t *a, size_t an, const char *hex) {
  size_t n; uint8_t *b = from_hex(hex, &n);
  bool same = n == an && memcmp(a, b, n) == 0;
  free(b);
  return same;
}
static const char *rule(const qa_server_status *s, const char *key) {
  return qa_qw_info_get(&s->rules, key);
}

/* xorshift32, fixed seed. */
static uint32_t rng_state = 0x5eed1234u;
static uint32_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static const char STATUS_HEX[] =
  "ffffffff737461747573526573706f6e73650a5c73765f686f73746e616d655c5465737420486f73745c6d61706e616d655c7133"
  "646d31375c73765f6d6178636c69656e74735c31365c675f67616d65747970655c305c6368616c6c656e67655c6162635c6d6170"
  "6e616d655c7133646d360a352034302022506c61796572204f6e65220a2d3220393939202278220a";
static const char INFO_HEX[] =
  "ffffffff696e666f526573706f6e73650a5c70726f746f636f6c5c36385c686f73746e616d655c426f785c6d61706e616d655c71"
  "33746f75726e6579325c636c69656e74735c335c73765f6d6178636c69656e74735c385c6368616c6c656e67655c2d3137";

static void test_status_oracle(void) {
  qa_q3_status_player players[2] = { { 5, 40, "Player One" }, { -2, 999, "x" } };
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_status("\\sv_hostname\\Test Host\\mapname\\q3dm17\\sv_maxclients\\16\\g_gametype\\0\\challenge\\abc\\mapname\\q3dm6",
                            players, 2, &bytes, &len));
  CHECK(same_bytes(bytes, len, STATUS_HEX));
  char *challenge = NULL; qa_server_status status;
  CHECK(qa_q3_decode_server_status(bytes, len, &challenge, &status));
  CHECK(challenge && strcmp(challenge, "abc") == 0);
  CHECK(status.name && strcmp(status.name, "Test Host") == 0);
  CHECK(status.map && strcmp(status.map, "q3dm17") == 0); /* q3InfoValue takes the first key */
  CHECK(status.players == 2 && status.max_players == 16);
  CHECK(status.rules.count == 5);
  CHECK(status.rules.count == 5 && strcmp(status.rules.entries[0].key, "sv_hostname") == 0 && strcmp(status.rules.entries[1].key, "mapname") == 0);
  CHECK(rule(&status, "mapname") && strcmp(rule(&status, "mapname"), "q3dm6") == 0); /* Map.set overwrites in place */
  CHECK(status.player_count == 2);
  if (status.player_count == 2) {
    CHECK(strcmp(status.player_details[0].name, "Player One") == 0 && status.player_details[0].score == 5 && status.player_details[0].ping == 40);
    CHECK(strcmp(status.player_details[1].name, "x") == 0 && status.player_details[1].score == -2 && status.player_details[1].ping == 999);
  }
  free(challenge); qa_server_status_destroy(&status); free(bytes);

  CHECK(qa_q3_encode_info("\\protocol\\68\\hostname\\Box\\mapname\\q3tourney2\\clients\\3\\sv_maxclients\\8\\challenge\\-17", &bytes, &len));
  CHECK(same_bytes(bytes, len, INFO_HEX));
  CHECK(qa_q3_decode_server_status(bytes, len, &challenge, &status));
  CHECK(challenge && strcmp(challenge, "-17") == 0);
  CHECK(status.name && strcmp(status.name, "Box") == 0 && status.map && strcmp(status.map, "q3tourney2") == 0);
  CHECK(status.players == 3 && status.max_players == 8 && status.rules.count == 6 && status.player_count == 0);
  free(challenge); qa_server_status_destroy(&status); free(bytes);
}

typedef struct { const uint8_t *bytes; size_t len; char *challenge; qa_server_status status; bool ok; } status_scope;
static void status_body(void *ctx) {
  status_scope *s = ctx;
  s->ok = qa_q3_decode_server_status(s->bytes, s->len, &s->challenge, &s->status);
}
/* Decodes under qa_try; on success releases the result. Returns whether it raised. */
static bool decode_status_caught(const uint8_t *bytes, size_t len, qa_caught_error *caught) {
  status_scope scope = { bytes, len, NULL, { 0 }, false };
  bool ok = qa_try(status_body, &scope, caught);
  if (ok) { free(scope.challenge); qa_server_status_destroy(&scope.status); }
  return !ok;
}

static void test_status_errors(void) {
  qa_caught_error caught;
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_info("\\protocol\\67\\hostname\\Box", &bytes, &len));
  CHECK(decode_status_caught(bytes, len, &caught));
  CHECK(strcmp(caught.name, "RangeError") == 0 && strcmp(caught.message, "Server uses another Q3 wire version") == 0);
  free(bytes);
  /* An infostring past BIG_INFO_STRING reaches q3InfoValue: TS CommonError, not a crash or leak. */
  size_t big_len = 4 + 13 + 9000;
  uint8_t *big = malloc(big_len);
  memset(big, 255, 4); memcpy(big + 4, "infoResponse\n", 13); memset(big + 17, 'A', 9000);
  CHECK(decode_status_caught(big, big_len, &caught));
  CHECK(strcmp(caught.name, "CommonError") == 0 && strcmp(caught.message, "Info_ValueForKey: oversize infostring") == 0);
  /* Same for a statusResponse carrying player lines before the oversize lookup. */
  static const char tail[] = "\n1 2 \"name\"\n3 4 \"other\"\n\\a\\b\\c\\d\\e\\fg";
  size_t sbig_len = 4 + 15 + 9000 + (sizeof tail - 1);
  uint8_t *sbig = malloc(sbig_len);
  memset(sbig, 255, 4); memcpy(sbig + 4, "statusResponse\n", 15); memset(sbig + 19, 'B', 9000);
  memcpy(sbig + 19 + 9000, tail, sizeof tail - 1);
  CHECK(decode_status_caught(sbig, sbig_len, &caught));
  CHECK(strcmp(caught.name, "CommonError") == 0);
  free(sbig);
  /* Not a browser response. */
  static const uint8_t other[] = { 255, 255, 255, 255, 'p', 'r', 'i', 'n', 't', '\n', 'x' };
  CHECK(decode_status_caught(other, sizeof other, &caught));
  CHECK(strcmp(caught.name, "RangeError") == 0 && strcmp(caught.message, "Not a Q3 server browser response") == 0);
  free(big);
}

static void fuzz_status(const char *hex) {
  size_t len; uint8_t *base = from_hex(hex, &len);
  uint8_t *work = malloc(len + 64);
  qa_caught_error caught;
  for (int round = 0; round < 1500; round++) {
    size_t n = len;
    memcpy(work, base, len);
    uint32_t mode = rng() % 3;
    if (mode == 0) n = rng() % (len + 1);
    else {
      int edits = 1 + (int)(rng() % 6);
      for (int e = 0; e < edits; e++) {
        size_t at = rng() % len;
        static const uint8_t picks[] = { 0, '\\', '\n', '"', ' ', 255, '%', 0x80 };
        work[at] = (rng() & 1) ? (uint8_t)rng() : picks[rng() % sizeof picks];
      }
      if (mode == 2) { size_t extra = rng() % 64; for (size_t i = 0; i < extra; i++) work[len + i] = (uint8_t)rng(); n = len + extra; }
    }
    decode_status_caught(work, n, &caught);
  }
  free(work); free(base);
  checks++;
}

typedef struct { const uint8_t *bytes; size_t len; qa_net_address *out; size_t count; bool complete, ok; } master_scope;
static void master_body(void *ctx) {
  master_scope *s = ctx;
  s->ok = qa_q3_decode_master_packet(s->bytes, s->len, &s->out, &s->count, &s->complete);
}
static void test_master(void) {
  static const uint8_t packet[] = { 255, 255, 255, 255, 'g', 'e', 't', 's', 'e', 'r', 'v', 'e', 'r', 's', 'R', 'e', 's', 'p', 'o', 'n', 's', 'e',
    92, 1, 2, 3, 4, 0x6d, 0x38, 92, 10, 0, 0, 1, 0x00, 0x50, 92, 69, 79, 84, 0, 0, 0 };
  qa_net_address *out = NULL; size_t count = 0; bool complete = false;
  CHECK(qa_q3_decode_master_packet(packet, sizeof packet, &out, &count, &complete));
  CHECK(complete && count == 2);
  if (count == 2) {
    CHECK(out[0].kind == QA_NET_IPV4 && out[0].ipv4[0] == 1 && out[0].ipv4[1] == 2 && out[0].ipv4[2] == 3 && out[0].ipv4[3] == 4 && out[0].port == 27960);
    CHECK(out[1].ipv4[0] == 10 && out[1].ipv4[3] == 1 && out[1].port == 80);
  }
  free(out);
  CHECK(qa_q3_decode_master_response(packet, sizeof packet, &out, &count) && count == 2);
  free(out);
  qa_caught_error caught;
  master_scope bad = { packet, 20, NULL, 0, false, false };
  CHECK(!qa_try(master_body, &bad, &caught));
  CHECK(strcmp(caught.name, "RangeError") == 0 && strcmp(caught.message, "Not a Q3 master response") == 0);
  /* 300 entries: the decoder stops at 256 like the source. */
  size_t big_len = 22 + 300 * 7 + 1;
  uint8_t *big = malloc(big_len);
  memcpy(big, packet, 22);
  for (int i = 0; i < 300; i++) { uint8_t *e = big + 22 + i * 7; e[0] = 92; e[1] = (uint8_t)i; e[2] = 1; e[3] = 2; e[4] = 3; e[5] = 0; e[6] = 1; }
  big[big_len - 1] = 92;
  CHECK(qa_q3_decode_master_packet(big, big_len, &out, &count, &complete) && count == 256 && !complete);
  free(out); free(big);
  uint8_t work[sizeof packet + 32];
  for (int round = 0; round < 3000; round++) {
    size_t n = sizeof packet;
    memcpy(work, packet, sizeof packet);
    if (rng() & 1) n = rng() % (sizeof packet + 1);
    else for (int e = 0; e < 1 + (int)(rng() % 4); e++) work[rng() % sizeof packet] = (rng() & 1) ? 92 : (uint8_t)rng();
    master_scope scope = { work, n, NULL, 0, false, false };
    if (qa_try(master_body, &scope, &caught) && scope.ok) {
      CHECK(scope.count <= 256);
      free(scope.out);
    }
  }
}

typedef struct { qa_q3_server_pak_set *set; const char *text; bool names; } pak_scope;
static void pak_body(void *ctx) {
  pak_scope *s = ctx;
  if (s->names) qa_q3_server_pak_set_set_names(s->set, s->text, -1);
  else qa_q3_server_pak_set_set_checksums(s->set, s->text);
}
static void test_pak_strings(void) {
  qa_q3_server_pak_set *set = NULL;
  CHECK(qa_q3_server_pak_set_init(&set));
  CHECK(qa_q3_server_pak_set_set_checksums(set, "12 -7 0x10 99999999999 \"quoted 5\" -2147483649") == 6);
  size_t count = 0;
  const int64_t *sums = qa_q3_server_pak_set_checksums(set, &count);
  static const int64_t want[6] = { 12, -7, 0, 2147483647, 0, -2147483648LL };
  CHECK(count == 6);
  for (size_t i = 0; i < 6 && count == 6; i++) CHECK(sums[i] == want[i]);
  qa_q3_server_pak_set_set_names(set, "pak0 \"two words\" pak2", -1);
  qa_q3_server_pak *snap = NULL;
  CHECK(qa_q3_server_pak_set_snapshot(set, &snap, &count) && count == 6);
  if (count == 6) {
    CHECK(snap[0].name && strcmp(snap[0].name, "pak0") == 0);
    CHECK(snap[1].name && strcmp(snap[1].name, "two words") == 0);
    CHECK(snap[2].name && strcmp(snap[2].name, "pak2") == 0);
    CHECK(snap[3].name == NULL && snap[5].name == NULL && snap[5].checksum == -2147483648LL);
  }
  free(snap);
  /* Hostile sv_paks / sv_pakNames text: long tokens, quotes, comments, 5000 words. */
  size_t cap = 40000;
  char *text = malloc(cap);
  qa_caught_error caught;
  static const char alphabet[] = "0123456789 -\"/*\\\n\tabcXYZ;";
  for (int round = 0; round < 400; round++) {
    size_t n = rng() % (round % 10 == 0 ? cap - 1 : 3000);
    for (size_t i = 0; i < n; i++) text[i] = (rng() % 50 == 0) ? (char)(1 + rng() % 255) : alphabet[rng() % (sizeof alphabet - 1)];
    text[n] = 0;
    pak_scope scope = { set, text, (round & 1) != 0 };
    qa_try(pak_body, &scope, &caught);
    sums = qa_q3_server_pak_set_checksums(set, &count);
    CHECK(count <= 4096);
    if (qa_q3_server_pak_set_snapshot(set, &snap, &count)) free(snap);
  }
  /* 5000 words: the source tokenizer keeps MAX_STRING_TOKENS (TS oracle: 1024, last 1023). */
  size_t o = 0;
  for (int i = 0; i < 5000; i++) o += (size_t)snprintf(text + o, cap - o, "%d ", i);
  CHECK(qa_q3_server_pak_set_set_checksums(set, text) == 1024);
  sums = qa_q3_server_pak_set_checksums(set, &count);
  CHECK(count == 1024 && sums[1023] == 1023);
  free(text);
  qa_q3_server_pak_set_destroy(set);
}

/* Server commands are wire text: bcs reassembly and drops must not leak when caught. */
static int sentinel;
static void bind_noop(void *ctx) { CHECK(ctx == &sentinel); }
static bool bind_false(void *ctx) { CHECK(ctx == &sentinel); return false; }
typedef struct { qa_q3_server_command_executor *e; qa_q3_client_game_state_storage *gs; const char *command;
  char **argv; size_t argc; bool result; } command_scope;
static void command_body(void *ctx) {
  command_scope *s = ctx;
  qa_q3_server_command_bindings b = { bind_noop, bind_noop, bind_noop, bind_noop, bind_false, &sentinel };
  s->result = qa_q3_server_command_executor_execute(s->e, s->command, s->gs, &b, &s->argv, &s->argc);
}
static bool run_command(qa_q3_server_command_executor *e, qa_q3_client_game_state_storage *gs, const char *command,
                        qa_caught_error *caught, bool *result) {
  command_scope s = { e, gs, command, NULL, 0, false };
  bool ok = qa_try(command_body, &s, caught);
  if (ok && s.result) { for (size_t i = 0; i < s.argc; i++) free(s.argv[i]); free(s.argv); }
  *result = s.result;
  return ok;
}
static void test_server_commands(void) {
  qa_q3_server_command_executor *e = NULL;
  qa_q3_client_game_state_storage *gs = NULL;
  CHECK(qa_q3_server_command_executor_init(&e));
  CHECK(qa_q3_client_game_state_storage_init(&gs, NULL, NULL));
  qa_q3_client_game_state_storage_begin_entries(gs);
  qa_caught_error caught; bool result = false;
  CHECK(run_command(e, gs, "bcs0 5 \"abc\"", &caught, &result) && !result);
  CHECK(run_command(e, gs, "bcs1 5 \"def\"", &caught, &result) && !result);
  CHECK(run_command(e, gs, "bcs2 5 \"ghi\"", &caught, &result) && result);
  char *value = NULL;
  CHECK(qa_q3_client_game_state_storage_get(gs, 5, &value) && value && strcmp(value, "abcdefghi") == 0);
  free(value);
  CHECK(!run_command(e, gs, "disconnect \"bye now\"", &caught, &result));
  CHECK(strcmp(caught.name, "CommonError") == 0 && strcmp(caught.message, "Server Disconnected - bye now") == 0);
  CHECK(!run_command(e, gs, "cs 2000 x", &caught, &result));
  CHECK(strcmp(caught.name, "CommonError") == 0 && strcmp(caught.message, "configstring > MAX_CONFIGSTRINGS") == 0);
  /* bcs past BIG_INFO_STRING drops. */
  char *chunk = malloc(1200);
  memset(chunk, 'z', 1000); chunk[1000] = 0;
  char *line = malloc(1300);
  snprintf(line, 1300, "bcs0 7 \"%s\"", chunk);
  CHECK(run_command(e, gs, line, &caught, &result));
  bool dropped = false;
  for (int i = 0; i < 10 && !dropped; i++) {
    snprintf(line, 1300, "bcs1 7 \"%s\"", chunk);
    dropped = !run_command(e, gs, line, &caught, &result);
  }
  CHECK(dropped && strcmp(caught.message, "bcs exceeded BIG_INFO_STRING") == 0);
  /* Random wire commands. */
  static const char *const heads[] = { "cs ", "bcs0 ", "bcs1 ", "bcs2 ", "disconnect ", "map_restart", "clientLevelShot", "print " };
  char *text = malloc(3000);
  for (int round = 0; round < 600; round++) {
    const char *head = heads[rng() % 8];
    size_t o = (size_t)snprintf(text, 3000, "%s", head);
    size_t n = rng() % 1400;
    for (size_t i = 0; i < n; i++) text[o++] = "0123456789 \"az\\;"[rng() % 16];
    text[o] = 0;
    run_command(e, gs, text, &caught, &result);
  }
  free(text); free(chunk); free(line);
  qa_q3_server_command_executor_destroy(e);
  qa_q3_client_game_state_storage_destroy(gs);
}

int main(void) {
  test_status_oracle();
  test_status_errors();
  fuzz_status(STATUS_HEX);
  fuzz_status(INFO_HEX);
  test_master();
  test_pak_strings();
  test_server_commands();
  printf("test_q3_review_inputs: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}

/* TS drift D7 checks for src/network: tests/network/common/peer-table.test.ts,
 * tests/network/q3/peer-userinfo.test.ts and the resumable-download cases of
 * tests/network/common/download-ranges.test.ts. Expected values are the TS test literals.
 * Temporary files live under build/agents/d7-net/tmp and are removed on every outcome.
 * GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/peer_table.h"
#include "qa/network/common/session.h"
#include "qa/network/q3/admission.h"
#include "qa/network/services/admin.h"
#include "qa/network/services/downloads.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

static qa_net_address v4(int d, int64_t port) {
  qa_net_address a; memset(&a, 0, sizeof a);
  a.kind = QA_NET_IPV4; a.ipv4[0] = 10; a.ipv4[3] = (uint8_t)d; a.port = port;
  return a;
}

/* "peer slots ignore ports, stay distinct per family, and report exhaustion" */
static void test_peer_table(void) {
  qa_peer_table table; CHECK(qa_peer_table_init(&table, 2));
  qa_net_address a100 = v4(1, 100), a200 = v4(1, 200), a1 = v4(1, 1);
  int64_t a = qa_peer_table_claim(&table, &a100);
  CHECK(qa_peer_table_claim(&table, &a200) == a);
  qa_net_address ipx; memset(&ipx, 0, sizeof ipx);
  ipx.kind = QA_NET_IPX; ipx.network = 1; ipx.port = 1;
  for (int i = 0; i < 6; i++) ipx.node[i] = (uint8_t)(i + 1);
  int64_t b = qa_peer_table_claim(&table, &ipx);
  CHECK(b != a);
  qa_net_address ipx2 = ipx; ipx2.node[5] = 7;
  CHECK(qa_peer_table_claim(&table, &ipx2) == -1);
  qa_peer_table_release(&table, a);
  CHECK(qa_peer_table_find(&table, &a1) == -1);
  qa_net_address x; memset(&x, 0, sizeof x); x.kind = QA_NET_LOOPBACK; x.host = "x";
  qa_net_address y = x; y.host = "y";
  CHECK(qa_peer_table_claim(&table, &x) == a);
  CHECK(qa_peer_table_find(&table, &y) == -1);
  qa_peer_table_destroy(&table);
}

/* "flood limiter table is bounded and refuses new sources once full and unexpired" */
static void test_flood_limiter(void) {
  qa_flood_limiter limiter; qa_flood_limiter_init_max(&limiter, 2, 1000, 2);
  qa_net_address one = v4(1, 1), two = v4(2, 1), three = v4(3, 1);
  CHECK(qa_flood_limiter_allow(&limiter, &one, 0));
  CHECK(qa_flood_limiter_allow(&limiter, &two, 0));
  CHECK(!qa_flood_limiter_allow(&limiter, &three, 0));
  CHECK(qa_flood_limiter_allow(&limiter, &three, 10000));
  qa_flood_limiter_destroy(&limiter);
}

/* "sameAddress compares fields like the formatted key" */
static void test_same_address(void) {
  qa_net_address p5 = v4(1, 5), p6 = v4(1, 6), p1 = v4(1, 1);
  CHECK(!qa_net_same_address(&p5, &p6, true));
  CHECK(qa_net_same_address(&p5, &p6, false));
  qa_net_address six; memset(&six, 0, sizeof six); six.kind = QA_NET_IPV6; six.host = "::1"; six.port = 1;
  CHECK(!qa_net_same_address(&six, &p1, true));
}

/* "userinfo updates take their ip from the transport address" */
static void quiet(void *ctx, const char *text) { (void)ctx; (void)text; }
static void test_peer_userinfo(void) {
  qa_q3_address remote; memset(&remote, 0, sizeof remote);
  remote.kind = QA_Q3_ADDR_IPV4; remote.host[0] = 203; remote.host[1] = 0; remote.host[2] = 113; remote.host[3] = 9; remote.port = 27960;
  char *info = qa_q3_peer_userinfo("\\name\\x\\ip\\localhost", &remote, quiet, NULL);
  char *ip = qa_q3_info_value(info, "ip"), *name = qa_q3_info_value(info, "name");
  CHECK(strcmp(ip, "203.0.113.9:27960") == 0);
  CHECK(strcmp(name, "x") == 0);
  free(ip); free(name); free(info);
  qa_q3_address loop; memset(&loop, 0, sizeof loop); loop.kind = QA_Q3_ADDR_LOOPBACK;
  info = qa_q3_peer_userinfo("\\name\\x", &loop, quiet, NULL);
  ip = qa_q3_info_value(info, "ip");
  CHECK(strcmp(ip, "localhost") == 0);
  free(ip); free(info);
}

static char root[200];
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st; (void)flag; (void)ftw; return remove(path);
}
static void reset_root(void) {
  nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  mkdir("build/agents", 0700); mkdir("build/agents/d7-net", 0700); mkdir("build/agents/d7-net/tmp", 0700);
  CHECK(mkdir(root, 0700) == 0);
}
static size_t list_dir(const char *path, char names[][64], size_t capacity) {
  DIR *dir = opendir(path); size_t n = 0;
  if (!dir) return 0;
  struct dirent *e;
  while ((e = readdir(dir)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (n < capacity) { size_t len = strnlen(e->d_name, 63); memcpy(names[n], e->d_name, len); names[n][len] = 0; }
    n++;
  }
  closedir(dir);
  for (size_t i = 0; i + 1 < n && i + 1 < capacity; i++)
    for (size_t j = i + 1; j < n && j < capacity; j++) if (strcmp(names[j], names[i]) < 0) { char t[64]; memcpy(t, names[i], 64); memcpy(names[i], names[j], 64); memcpy(names[j], t, 64); }
  return n;
}
static long file_size(const char *path) { struct stat st; return stat(path, &st) == 0 ? (long)st.st_size : -1; }
static void write_file(const char *path, const uint8_t *bytes, size_t length) {
  FILE *f = fopen(path, "wb"); if (f) { if (length) fwrite(bytes, 1, length, f); fclose(f); }
}

/* "an interrupted resumable download continues only from a hash-verified partial file" */
static void test_resume(void) {
  reset_root();
  static uint8_t bytes[150000];
  for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t)((i * 31) % 253);
  qa_download_expectation bounded = {true, NULL, 0, 1024 * 1024};
  qa_download_sink first; CHECK(qa_download_sink_create_resumable(&first, root, "maps/long.bsp", bounded));
  CHECK(first.resumed_from == 0);
  qa_download_sink_append(&first, bytes, 70000);
  qa_download_sink_suspend(&first);
  char maps[256]; snprintf(maps, sizeof maps, "%s/maps", root);
  char names[4][64];
  CHECK(list_dir(maps, names, 4) == 2 && strcmp(names[0], "long.bsp.part") == 0 && strcmp(names[1], "long.bsp.part.sha256") == 0);
  qa_download_sink second; CHECK(qa_download_sink_create_resumable(&second, root, "maps/long.bsp", bounded));
  CHECK(second.resumed_from == 70000);
  qa_download_sink_append(&second, bytes + 70000, sizeof bytes - 70000);
  char hex[65]; qa_net_sha256_hex(bytes, sizeof bytes, hex);
  char *expected = qa_create_content_digest(hex);
  char *digest = qa_download_sink_finish(&second);
  CHECK(digest && expected && strcmp(digest, expected) == 0);
  free(digest); free(expected);
  char path[256]; snprintf(path, sizeof path, "%s/maps/long.bsp", root);
  CHECK(file_size(path) == (long)sizeof bytes);
  CHECK(list_dir(maps, names, 4) == 1 && strcmp(names[0], "long.bsp") == 0);
  qa_download_sink_destroy(&first); qa_download_sink_destroy(&second);
}

/* "a corrupted, truncated or unrecorded partial download is discarded and restarted" */
static void stage(const uint8_t *bytes, size_t length) {
  qa_download_expectation bounded = {true, NULL, 0, 1024 * 1024};
  qa_download_sink sink; CHECK(qa_download_sink_create_resumable(&sink, root, "a.pk3", bounded));
  qa_download_sink_append(&sink, bytes, length);
  qa_download_sink_suspend(&sink);
  qa_download_sink_destroy(&sink);
}
static void test_resume_damage(void) {
  reset_root();
  static uint8_t bytes[5000];
  for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t)(i % 251);
  char part[256], record[256]; snprintf(part, sizeof part, "%s/a.pk3.part", root); snprintf(record, sizeof record, "%s/a.pk3.part.sha256", root);
  qa_download_expectation bounded = {true, NULL, 0, 1024 * 1024};
  char names[4][64];
  for (int damage = 0; damage < 4; damage++) {
    stage(bytes, sizeof bytes);
    if (damage == 0) { static uint8_t copy[5000]; memcpy(copy, bytes, sizeof copy); copy[100] ^= 1; write_file(part, copy, sizeof copy); }
    else if (damage == 1) write_file(part, bytes, 10);
    else if (damage == 2) remove(record);
    else write_file(record, (const uint8_t *)"garbage", 7);
    qa_download_sink sink; CHECK(qa_download_sink_create_resumable(&sink, root, "a.pk3", bounded));
    CHECK(sink.resumed_from == 0);
    CHECK(file_size(part) == 0);
    qa_download_sink_close(&sink);
    CHECK(list_dir(root, names, 4) == 0);
    qa_download_sink_destroy(&sink);
  }
  qa_download_sink plain; CHECK(qa_download_sink_create_resumable(&plain, root, "a.pk3", bounded));
  qa_download_sink_append(&plain, bytes, sizeof bytes);
  qa_download_sink_close(&plain);
  CHECK(list_dir(root, names, 4) == 0);
  qa_download_sink_destroy(&plain);
}

/* network.test.ts "file downloads verify bytes before installation": escaping the root through a
 * symlink throws "ENOTDIR: not a directory, open '<path>'" (Node's errno text). */
struct sink_scope { qa_download_sink *sink; };
static void create_outside(void *ctx) {
  qa_download_expectation expectation = {false, "sha256:0000000000000000000000000000000000000000000000000000000000000000", 1, 0};
  qa_download_sink_create(((struct sink_scope *)ctx)->sink, root, "outside/nope.dat", expectation);
}
static void test_outside_message(void) {
  reset_root();
  char link_path[256]; snprintf(link_path, sizeof link_path, "%s/outside", root);
  CHECK(symlink("/tmp", link_path) == 0);
  qa_download_sink sink; memset(&sink, 0, sizeof sink);
  struct sink_scope scope = {&sink};
  qa_caught_error caught;
  CHECK(!qa_try(create_outside, &scope, &caught));
  /* bun: "ENOTDIR: not a directory, open '/proc/self/fd/<fd>/outside'" (the fd number varies). */
  const char *prefix = "ENOTDIR: not a directory, open '/proc/self/fd/", *suffix = "/outside'";
  size_t length = strlen(caught.message);
  CHECK(strncmp(caught.message, prefix, strlen(prefix)) == 0 && length > strlen(suffix) &&
        strcmp(caught.message + length - strlen(suffix), suffix) == 0);
}

int main(void) {
  snprintf(root, sizeof root, "build/agents/d7-net/tmp/d7-drift-%ld", (long)getpid());
  test_peer_table();
  test_flood_limiter();
  test_same_address();
  test_peer_userinfo();
  test_resume();
  test_resume_damage();
  test_outside_message();
  nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  printf("test_d7_drift: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}

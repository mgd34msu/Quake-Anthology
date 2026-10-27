/* Port of src/network/q3/pure.ts. GPL-2.0-or-later. */
#include "qa/network/q3/pure.h"
#include "qa/core/md4.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_q3_archive_checksums(const qa_archive_handle *archive, int64_t checksum_feed, int64_t *checksum_out, int64_t *pure_checksum_out) {
  if (archive->format == QA_ARCHIVE_FORMAT_PAK)
    qa_range_error("Q3 pak checksums require a ZIP/PK3 central directory");
  size_t count = 0;
  for (size_t i = 0; i < archive->entry_count; i++) if (archive->entries[i].byte_length > 0) count++;
  uint8_t *bytes = calloc(count ? count * 4 : 1, 1);
  if (!bytes) qa_range_error("archive checksum allocation failed");
  size_t index = 0;
  for (size_t i = 0; i < archive->entry_count; i++) {
    const qa_archive_entry *entry = &archive->entries[i];
    if (entry->byte_length <= 0) continue;
    if (entry->format == QA_ARCHIVE_FORMAT_PAK) { free(bytes); qa_range_error("Q3 ZIP directory contains a PAK entry"); }
    uint32_t crc = entry->crc32;
    bytes[index*4] = (uint8_t)crc; bytes[index*4+1] = (uint8_t)(crc >> 8);
    bytes[index*4+2] = (uint8_t)(crc >> 16); bytes[index*4+3] = (uint8_t)(crc >> 24);
    index++;
  }
  *checksum_out = qa_block_checksum(bytes, count * 4);
  *pure_checksum_out = qa_block_checksum_key(bytes, count * 4, checksum_feed);
  free(bytes);
}
qa_q3_pure_result qa_q3_verify_pure_command(const qa_q3_pure_server *server, const char *const *argv, size_t argc) {
  qa_q3_pure_result r = { QA_Q3_PURE_IGNORED, QA_Q3_PURE_REASON_NONE, NULL };
  if (!server->enabled) { r.ignored_reason = QA_Q3_PURE_DISABLED; return r; }
  if (qa_native_atoi(argc > 1 ? argv[1] : "") < server->checksum_feed_server_id) { r.ignored_reason = QA_Q3_PURE_OUTDATED; return r; }
  qa_q3_pure_result rejected = { QA_Q3_PURE_REJECTED, QA_Q3_PURE_REASON_NONE, "Unpure client detected. Invalid .PK3 files referenced!" };
  if (!server->has_cgame_checksum || !server->has_ui_checksum || argc < 6 || argc - 5 > 1024) return rejected;
  const char *cgame = argc > 2 ? argv[2] : "", *ui = argc > 3 ? argv[3] : "";
  if (cgame[0] == '@' || qa_native_atoi(cgame) != (int32_t)server->cgame_checksum
      || ui[0] == '@' || qa_native_atoi(ui) != (int32_t)server->ui_checksum
      || (argc > 4 ? argv[4] : "")[0] != '@') return rejected;
  size_t n = argc - 5;
  int32_t *checksums = malloc((n ? n : 1) * sizeof *checksums);
  if (!checksums) qa_range_error("pure verify allocation failed");
  for (size_t i = 0; i < n; i++) checksums[i] = qa_native_atoi(argv[5 + i]);
  int64_t reference_count = (int64_t)n - 1;
  for (int64_t i = 0; i < reference_count; i++)
    for (int64_t j = 0; j < i; j++)
      if (checksums[i] == checksums[j]) { free(checksums); return rejected; }
  size_t loaded_n = server->loaded_count < 1024 ? server->loaded_count : 1024;
  int32_t checksum = (int32_t)server->checksum_feed;
  for (int64_t i = 0; i < reference_count; i++) {
    bool found = false;
    for (size_t j = 0; j < loaded_n; j++) if ((int32_t)server->loaded_pure_checksums[j] == checksums[i]) found = true;
    if (!found) { free(checksums); return rejected; }
    checksum ^= checksums[i];
  }
  checksum ^= (int32_t)reference_count;
  bool authentic = reference_count >= 0 && checksum == checksums[reference_count];
  free(checksums);
  if (authentic) { r.kind = QA_Q3_PURE_AUTHENTIC; return r; }
  return rejected;
}
void qa_q3_check_download_name(const char *name) {
  size_t len = strlen(name);
  bool bad = len == 0 || len >= 4096 || strstr(name, "..") != NULL || name[0] == '/';
  for (size_t i = 0; i < len && !bad; i++) {
    char c = name[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '.' || c == '/' || c == '-')) bad = true;
  }
  /* components: none empty or "." */
  if (!bad) {
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
      if (i == len || name[i] == '/') {
        size_t cn = i - start;
        if (cn == 0 || (cn == 1 && name[start] == '.')) bad = true;
        start = i + 1;
      }
    }
  }
  if (!bad) {
    if (len < 4) bad = true;
    else {
      const char *suf = name + len - 4;
      bad = !(suf[0] == '.' && tolower((unsigned char)suf[1]) == 'p' && tolower((unsigned char)suf[2]) == 'k' && suf[3] == '3');
    }
  }
  if (bad) {
    /* TS message JSON.stringify(name): quoted with escapes; approximated by raw name in quotes. */
    qa_range_error("Unsafe package download name: \"%s\"", name);
  }
}
bool qa_q3_stock_package(const char *name, qa_q3_product *out) {
  size_t len = strlen(name);
  char *norm = malloc(len + 1);
  if (!norm) return false;
  for (size_t i = 0; i < len; i++) {
    char c = name[i];
    if (c == '\\' || c == ':') c = '/';
    norm[i] = (char)tolower((unsigned char)c);
  }
  norm[len] = 0;
  struct { const char *prefix; qa_q3_product product; } cases[2] = {
    { "baseq3/pak", QA_Q3_PRODUCT_BASEQ3 }, { "missionpack/pak", QA_Q3_PRODUCT_MISSIONPACK }
  };
  bool matched = false;
  for (int k = 0; k < 2 && !matched; k++) {
    size_t pl = strlen(cases[k].prefix);
    if (strncmp(norm, cases[k].prefix, pl) != 0) continue;
    const char *rest = norm + pl;
    if (rest[0] >= '0' && rest[0] <= '8' && (rest[1] == 0 || strcmp(rest + 1, ".pk3") == 0)) {
      *out = cases[k].product; matched = true;
    }
  }
  free(norm);
  return matched;
}
char *qa_q3_compare_packages(const qa_q3_server_pak *referenced, size_t referenced_count,
                             const int64_t *loaded_checksums, size_t loaded_count,
                             qa_q3_package_exists_fn exists, void *exists_ctx, bool download, int64_t capacity) {
  if (capacity < 1) qa_range_error("Package comparison needs a nonempty buffer");
  char *result = calloc((size_t)capacity, 1);
  if (!result) return NULL;
  size_t result_len = 0;
  for (size_t i = 0; i < referenced_count; i++) {
    const qa_q3_server_pak *pack = &referenced[i];
    if (!pack->name || !pack->name[0]) continue;
    bool loaded = false;
    for (size_t j = 0; j < loaded_count; j++) if ((uint32_t)loaded_checksums[j] == (uint32_t)pack->checksum) loaded = true;
    if (loaded) continue;
    qa_q3_product prod;
    if (qa_q3_stock_package(pack->name, &prod)) continue;
    size_t rn = strlen(pack->name) + 5;
    char *remote = malloc(rn);
    if (!remote) { free(result); return NULL; }
    snprintf(remote, rn, "%s.pk3", pack->name);
    qa_q3_check_download_name(remote);
    bool present = exists(exists_ctx, remote);
    #define APPEND(txt) do { \
      const char *t_ = (txt); \
      int64_t room = capacity - 1 - (int64_t)result_len; \
      if (room > 0) { size_t tn_ = strlen(t_); if ((int64_t)tn_ > room) tn_ = (size_t)room; \
        memcpy(result + result_len, t_, tn_); result_len += tn_; result[result_len] = 0; } \
    } while (0)
    if (download) {
      char *at = malloc(rn + 2);
      if (!at) { free(remote); free(result); return NULL; }
      snprintf(at, rn + 2, "@%s@", remote);
      APPEND(at); free(at);
      if (present) {
        size_t an = strlen(pack->name) + 16;
        char *alt = malloc(an);
        if (!alt) { free(remote); free(result); return NULL; }
        snprintf(alt, an, "%s.%08x.pk3", pack->name, (uint32_t)pack->checksum);
        APPEND(alt); free(alt);
      } else APPEND(remote);
    } else {
      APPEND(remote);
      if (present) APPEND(" (local file exists with wrong checksum)");
      APPEND("\n");
    }
    #undef APPEND
    free(remote);
  }
  return result;
}

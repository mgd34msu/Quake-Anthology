/* Port of src/network/q3/pure.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_PURE_H
#define QA_NETWORK_Q3_PURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/archive.h"
#include "qa/network/q3/pak_references.h"
#include "qa/network/q3/state/product.h"
void qa_q3_archive_checksums(const qa_archive_handle *archive, int64_t checksum_feed, int64_t *checksum_out, int64_t *pure_checksum_out);
/* TS Q3PureServer. checksums have has_* for TS undefined. */
typedef struct qa_q3_pure_server {
  bool enabled;
  int64_t checksum_feed, checksum_feed_server_id;
  bool has_cgame_checksum; int64_t cgame_checksum;
  bool has_ui_checksum; int64_t ui_checksum;
  const int64_t *loaded_pure_checksums; size_t loaded_count;
} qa_q3_pure_server;
typedef enum qa_q3_pure_result_kind { QA_Q3_PURE_IGNORED, QA_Q3_PURE_AUTHENTIC, QA_Q3_PURE_REJECTED } qa_q3_pure_result_kind;
typedef enum qa_q3_pure_ignored_reason { QA_Q3_PURE_REASON_NONE, QA_Q3_PURE_DISABLED, QA_Q3_PURE_OUTDATED } qa_q3_pure_ignored_reason;
typedef struct qa_q3_pure_result {
  qa_q3_pure_result_kind kind;
  qa_q3_pure_ignored_reason ignored_reason;
  const char *reason; /* rejected message */
} qa_q3_pure_result;
qa_q3_pure_result qa_q3_verify_pure_command(const qa_q3_pure_server *server, const char *const *argv, size_t argc);
void qa_q3_check_download_name(const char *name);
/* q3StockPackage: false = null. */
bool qa_q3_stock_package(const char *name, qa_q3_product *out);
typedef bool (*qa_q3_package_exists_fn)(void *ctx, const char *name);
/* compareQ3Packages: malloc'd result. capacity default form uses 1024. */
char *qa_q3_compare_packages(const qa_q3_server_pak *referenced, size_t referenced_count,
                             const int64_t *loaded_checksums, size_t loaded_count,
                             qa_q3_package_exists_fn exists, void *exists_ctx, bool download, int64_t capacity);
#endif

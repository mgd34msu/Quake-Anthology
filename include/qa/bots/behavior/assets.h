/* Port of src/bots/behavior/assets.ts. Async loads become synchronous fn+ctx
 * opens; SHA-256 provenance kept as byte-exact hashing is host/crypto owned
 * (C uses FNV-independent hex of length-prefixed stream via host callback if set,
 * else empty string is NOT returned: provenance aborts if payload lost).
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_ASSETS_H
#define QA_BOTS_BEHAVIOR_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_asset_file {
  char *path;
  char *key;
  uint8_t *bytes;
  size_t length;
} qa_bot_asset_file;
typedef struct qa_bot_asset_files {
  qa_bot_asset_file *files;
  size_t count, capacity;
} qa_bot_asset_files;
void qa_bot_asset_files_init(qa_bot_asset_files *f);
void qa_bot_asset_files_destroy(qa_bot_asset_files *f);
bool qa_bot_asset_files_add(qa_bot_asset_files *f, const char *path, const uint8_t *bytes,
                            size_t length);
/* Returns borrowed copy length, or -1 if absent (TS null). Copies into out if non-NULL. */
int64_t qa_bot_asset_files_read(qa_bot_asset_files *f, const char *path, uint8_t *out,
                                size_t out_cap);
size_t qa_bot_asset_files_list(qa_bot_asset_files *f, const char *directory, const char *extension,
                               const char **out_names, size_t max_names);
/* Host open: returns false for null resource. */
typedef bool (*qa_bot_asset_open_fn)(void *ctx, const char *path, const uint8_t **out_bytes,
                                     size_t *out_len);
bool qa_load_bot_asset_files(qa_bot_asset_files *out, qa_bot_asset_open_fn open_fn, void *open_ctx,
                             const char *const *ordered_paths, size_t ordered_count,
                             const char *const *extra_paths, size_t extra_count);
#endif

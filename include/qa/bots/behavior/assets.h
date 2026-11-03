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
  size_t sequence;           /* position in TS `paths` (shared with unread entries) */
  bool has_legacy;           /* TS legacyOverrides entry */
  uint8_t *legacy_bytes;
  size_t legacy_length;
} qa_bot_asset_file;
/* Host open: returns false for null resource. Bytes stay valid until the next call on ctx. */
typedef bool (*qa_bot_asset_open_fn)(void *ctx, const char *path, const uint8_t **out_bytes,
                                     size_t *out_len);
/* TS unread entry: a listed path whose bytes load only for a legacy digest. */
typedef struct qa_bot_asset_unread {
  char *path;
  char *key;
  size_t sequence;
  qa_bot_asset_open_fn open;
  void *open_ctx; /* must outlive qa_bot_asset_files_prepare_legacy_provenance */
} qa_bot_asset_unread;
typedef struct qa_bot_asset_files {
  qa_bot_asset_file *files;
  size_t count, capacity;
  qa_bot_asset_unread *unread;
  size_t unread_count, unread_capacity;
  size_t next_sequence;
  bool has_legacy_digest;
  char legacy_digest[65];
  void *owned_open_ctx; /* opener state owned by these files (mounted loads); freed by destroy */
  void (*owned_open_free)(void *ctx);
} qa_bot_asset_files;
void qa_bot_asset_files_init(qa_bot_asset_files *f);
void qa_bot_asset_files_destroy(qa_bot_asset_files *f);
bool qa_bot_asset_files_add(qa_bot_asset_files *f, const char *path, const uint8_t *bytes,
                            size_t length);
/* TS add(path, bytes, legacyBytes): legacy bytes replace these bytes in the version 1 digest. */
bool qa_bot_asset_files_add_with_legacy(qa_bot_asset_files *f, const char *path, const uint8_t *bytes,
                                        size_t length, const uint8_t *legacy_bytes, size_t legacy_length);
/* TS addUnread: records a path whose bytes are not loaded unless a legacy digest is requested. */
bool qa_bot_asset_files_add_unread(qa_bot_asset_files *f, const char *path, qa_bot_asset_open_fn open,
                                   void *open_ctx);
/* TS provenance(): SHA-256 hex over every read file in listing order (unread paths skipped). */
void qa_bot_asset_files_provenance(const qa_bot_asset_files *f, char out[65]);
/* TS prepareLegacyProvenance(): the digest version 1 bot saves recorded, which included every
 * navigation file. False only on OOM. */
bool qa_bot_asset_files_prepare_legacy_provenance(qa_bot_asset_files *f);
/* TS legacyProvenance(): raises unless prepared. Borrowed. */
const char *qa_bot_asset_files_legacy_provenance(const qa_bot_asset_files *f);
/* Returns borrowed copy length, or -1 if absent (TS null). Copies into out if non-NULL. */
int64_t qa_bot_asset_files_read(qa_bot_asset_files *f, const char *path, uint8_t *out,
                                size_t out_cap);
size_t qa_bot_asset_files_list(qa_bot_asset_files *f, const char *directory, const char *extension,
                               const char **out_names, size_t max_names);
/* open_ctx must outlive a later qa_bot_asset_files_prepare_legacy_provenance (navigation paths
 * are recorded unread with it). */
bool qa_load_bot_asset_files(qa_bot_asset_files *out, qa_bot_asset_open_fn open_fn, void *open_ctx,
                             const char *const *ordered_paths, size_t ordered_count,
                             const char *const *extra_paths, size_t extra_count);
/* TS loadMountedBotAssetFiles(mounts, catalog, extraPaths): enumerates the source mounts
 * in precedence order (botfiles, bots, scripts) and resolves every payload through those
 * mounts. TS throws raise. False only on OOM. */
struct qa_content_mounted;
struct qa_content_catalog;
bool qa_load_mounted_bot_asset_files(qa_bot_asset_files *out, struct qa_content_mounted *mounts,
                                     const struct qa_content_catalog *catalog,
                                     const char *const *extra_paths, size_t extra_count);
#endif

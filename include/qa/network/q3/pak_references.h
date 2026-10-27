/* Port of src/network/q3/pak-references.ts (files.c). GPL-2.0-or-later.
 * Pack identity is the PakCatalogEntry pointer (TS object identity);
 * entries are caller-owned. Checksums are 32-bit patterns held in int64. */
#ifndef QA_NETWORK_Q3_PAK_REFERENCES_H
#define QA_NETWORK_Q3_PAK_REFERENCES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_q3_pak_reference_flag {
  QA_Q3_PAK_FLAG_GENERAL = 0x01, QA_Q3_PAK_FLAG_UI = 0x02, QA_Q3_PAK_FLAG_CGAME = 0x04, QA_Q3_PAK_FLAG_QAGAME = 0x08
} qa_q3_pak_reference_flag;
typedef struct qa_q3_pak_catalog_entry {
  const char *game, *basename, *archive_path;
  int64_t checksum, pure_checksum;
} qa_q3_pak_catalog_entry;
typedef struct qa_q3_pak_reference_snapshot { const qa_q3_pak_catalog_entry *pack; int64_t flags; } qa_q3_pak_reference_snapshot;
typedef struct qa_q3_server_pak { int64_t checksum; const char *name; /* NULL ok; borrowed */ } qa_q3_server_pak;
typedef struct qa_q3_server_pak_set qa_q3_server_pak_set;
bool qa_q3_server_pak_set_init(qa_q3_server_pak_set **out);
void qa_q3_server_pak_set_destroy(qa_q3_server_pak_set *s);
const int64_t *qa_q3_server_pak_set_checksums(const qa_q3_server_pak_set *s, size_t *count);
int64_t qa_q3_server_pak_set_set_checksums(qa_q3_server_pak_set *s, const char *text);
/* clear_count -1 = TS default (current sums length). */
void qa_q3_server_pak_set_set_names(qa_q3_server_pak_set *s, const char *text, int64_t clear_count);
/* snapshot: out array owned (names borrowed from set storage). */
bool qa_q3_server_pak_set_snapshot(const qa_q3_server_pak_set *s, qa_q3_server_pak **out, size_t *count);
typedef enum qa_q3_pure_search_path_kind { QA_Q3_PURE_DIRECTORY, QA_Q3_PURE_PAK } qa_q3_pure_search_path_kind;
/* TS PureSearchPath<T> with T = void*. */
typedef struct qa_q3_pure_search_path {
  qa_q3_pure_search_path_kind kind;
  void *value;
  int64_t checksum; /* pak */
} qa_q3_pure_search_path;
bool qa_q3_allowed_pure_loose_path(const char *path);
bool qa_q3_is_pak_pure(int64_t checksum, const int64_t *server_checksums, size_t count);
/* reorderPurePaks: writes reordered copy into out (same count). */
void qa_q3_reorder_pure_paks(const qa_q3_pure_search_path *search_paths, size_t count,
                             const int64_t *server_checksums, size_t checksum_count, qa_q3_pure_search_path *out);
typedef double (*qa_q3_pak_random_fn)(void *ctx);
typedef struct qa_q3_pak_references qa_q3_pak_references;
bool qa_q3_pak_references_init(qa_q3_pak_references **out, const qa_q3_pak_catalog_entry *const *packs, size_t pack_count,
                               int64_t checksum_feed, qa_q3_pak_random_fn random, void *random_ctx);
void qa_q3_pak_references_destroy(qa_q3_pak_references *r);
int64_t qa_q3_pak_references_checksum_feed(const qa_q3_pak_references *r);
void qa_q3_pak_references_prepend_pack(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *pack);
void qa_q3_pak_references_reorder_packs(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *const *packs, size_t pack_count);
void qa_q3_pak_references_retain_loose_reference(qa_q3_pak_references *r, const qa_q3_pak_references *previous);
void qa_q3_pak_references_record_packed_open(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *pack, const char *requested_path);
void qa_q3_pak_references_record_loose_open(qa_q3_pak_references *r, const char *requested_path);
void qa_q3_pak_references_clear(qa_q3_pak_references *r, int64_t flags);
bool qa_q3_pak_references_snapshot(const qa_q3_pak_references *r, qa_q3_pak_reference_snapshot **out, size_t *count);
char *qa_q3_pak_references_loaded_pak_checksums(const qa_q3_pak_references *r);
char *qa_q3_pak_references_loaded_pak_names(const qa_q3_pak_references *r);
char *qa_q3_pak_references_loaded_pak_pure_checksums(const qa_q3_pak_references *r);
char *qa_q3_pak_references_referenced_pak_checksums(const qa_q3_pak_references *r);
char *qa_q3_pak_references_referenced_pak_names(const qa_q3_pak_references *r);
char *qa_q3_pak_references_referenced_pak_pure_checksums(const qa_q3_pak_references *r);
char *qa_q3_pak_references_game_pure_checksum(const qa_q3_pak_references *r);
#endif

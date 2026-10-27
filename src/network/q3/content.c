/* Port of src/network/q3/content.ts. GPL-2.0-or-later. */
#include "qa/network/q3/content.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/pure.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
qa_q3_mounted_pak qa_q3_register_pak(const qa_archive_mount *mount, const qa_archive_handle *archive,
                                     const char *game, const char *basename, int64_t checksum_feed) {
  int64_t checksum = 0, pure = 0;
  qa_q3_archive_checksums(archive, checksum_feed, &checksum, &pure);
  qa_q3_mounted_pak r;
  r.mount = *mount;
  r.pack.game = game; r.pack.basename = basename; r.pack.archive_path = mount->archive_path;
  r.pack.checksum = checksum; r.pack.pure_checksum = pure;
  return r;
}
struct qa_q3_content_references {
  qa_q3_mounted_pak *packs; size_t pack_count;
  qa_q3_pak_references *references;
};
bool qa_q3_content_references_init(qa_q3_content_references **out, qa_q3_mounted_pak *packs, size_t pack_count,
                                   int64_t checksum_feed, qa_q3_pak_random_fn random, void *random_ctx) {
  for (size_t i = 0; i < pack_count; i++)
    for (size_t j = i + 1; j < pack_count; j++)
      if (strcmp(packs[i].mount.identity.id, packs[j].mount.identity.id) == 0)
        qa_range_error("Duplicate Q3 mounted pak identity");
  qa_q3_content_references *r = calloc(1, sizeof *r);
  if (!r) return false;
  const qa_q3_pak_catalog_entry **entries = calloc(pack_count ? pack_count : 1, sizeof *entries);
  if (!entries) { free(r); return false; }
  for (size_t i = 0; i < pack_count; i++) entries[i] = &packs[i].pack;
  bool ok = qa_q3_pak_references_init(&r->references, entries, pack_count, checksum_feed, random, random_ctx);
  free(entries);
  if (!ok) { free(r); return false; }
  r->packs = packs; r->pack_count = pack_count;
  *out = r; return true;
}
void qa_q3_content_references_destroy(qa_q3_content_references *r) {
  if (!r) return;
  qa_q3_pak_references_destroy(r->references);
  free(r);
}
qa_q3_pak_references *qa_q3_content_references_get_references(const qa_q3_content_references *r) { return r->references; }
qa_q3_mounted_pak *qa_q3_content_references_packs(const qa_q3_content_references *r, size_t *count) { *count = r->pack_count; return r->packs; }
static bool digest_eq(qa_content_digest a, qa_content_digest b) {
  if (a == b) return true;
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}
void qa_q3_content_references_opened(qa_q3_content_references *r, const qa_resolved_resource_reference *reference) {
  const qa_resource_provenance *prov = &reference->provenance;
  if (prov->kind == QA_CONTENT_MOUNT_LOOSE) {
    qa_q3_pak_references_record_loose_open(r->references, reference->requested_path);
    return;
  }
  const qa_archive_mount *mount = prov->archive_mount;
  qa_q3_mounted_pak *pak = NULL;
  if (mount) for (size_t i = 0; i < r->pack_count; i++)
    if (strcmp(r->packs[i].mount.identity.id, mount->identity.id) == 0) { pak = &r->packs[i]; break; }
  if (!pak || !digest_eq(pak->mount.archive_digest, mount->archive_digest)
      || pak->mount.identity.generation != mount->identity.generation)
    qa_range_error("Opened source resource does not belong to this Q3 pak catalog");
  qa_q3_pak_references_record_packed_open(r->references, &pak->pack, reference->requested_path);
}
bool qa_q3_content_references_pure_mount_policy(const qa_q3_content_references *r, const int64_t *server_checksums, size_t count,
                                                qa_pure_mount_policy *out, qa_content_digest **out_storage) {
  *out_storage = NULL;
  out->archives = NULL; out->archive_count = 0;
  if (count == 0) return true;
  qa_q3_pure_search_path *paths = calloc(r->pack_count ? r->pack_count : 1, sizeof *paths);
  qa_q3_pure_search_path *reordered = calloc(r->pack_count ? r->pack_count : 1, sizeof *reordered);
  qa_content_digest *archives = calloc(r->pack_count ? r->pack_count : 1, sizeof *archives);
  if (!paths || !reordered || !archives) { free(paths); free(reordered); free(archives); return false; }
  for (size_t i = 0; i < r->pack_count; i++) {
    paths[i].kind = QA_Q3_PURE_PAK; paths[i].checksum = r->packs[i].pack.checksum; paths[i].value = &r->packs[i];
  }
  qa_q3_reorder_pure_paks(paths, r->pack_count, server_checksums, count, reordered);
  size_t n = 0;
  for (size_t i = 0; i < r->pack_count; i++) {
    if (reordered[i].kind != QA_Q3_PURE_PAK) continue;
    bool accepted = false;
    for (size_t j = 0; j < count; j++) if ((uint32_t)server_checksums[j] == (uint32_t)reordered[i].checksum) accepted = true;
    if (accepted) archives[n++] = ((qa_q3_mounted_pak *)reordered[i].value)->mount.archive_digest;
  }
  free(paths); free(reordered);
  if (n == 0) { free(archives); qa_range_error("No installed Q3 archives match the server pure list"); }
  *out_storage = archives;
  out->archives = archives; out->archive_count = n;
  return true;
}
char *qa_q3_content_references_referenced_pure_command(const qa_q3_content_references *r, int64_t server_id) {
  char *checksums = qa_q3_pak_references_referenced_pak_pure_checksums(r->references);
  if (!checksums) return NULL;
  size_t n = strlen(checksums) + 32;
  char *buf = malloc(n);
  if (!buf) { free(checksums); return NULL; }
  snprintf(buf, n, "cp %lld %s", (long long)server_id, checksums);
  free(checksums);
  return buf;
}

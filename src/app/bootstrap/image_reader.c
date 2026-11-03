/* Port of src/app/bootstrap/image-reader.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/image_reader.h"
#include <stdlib.h>
#include <string.h>
typedef struct { char *path; const void *mount; const char *content; bool known; } winner_row;
struct qa_app_mounted_image_reader {
  qa_app_image_reader_host host;
  winner_row *winners; size_t winner_count, winner_cap;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_mounted_image_reader *qa_app_mounted_image_reader_create(qa_app_image_reader_host host) {
  qa_app_mounted_image_reader *reader = calloc(1, sizeof *reader);
  if (!reader) return NULL;
  reader->host = host;
  return reader;
}
void qa_app_mounted_image_reader_destroy(qa_app_mounted_image_reader *reader) {
  if (!reader) return;
  for (size_t i = 0; i < reader->winner_count; i++) free(reader->winners[i].path);
  free(reader->winners);
  free(reader);
}
static winner_row *winner_for(qa_app_mounted_image_reader *reader, const char *path, bool create) {
  for (size_t i = 0; i < reader->winner_count; i++)
    if (strcmp(reader->winners[i].path, path) == 0) return &reader->winners[i];
  if (!create) return NULL;
  if (reader->winner_count == reader->winner_cap) {
    reader->winner_cap = reader->winner_cap ? reader->winner_cap * 2 : 16;
    reader->winners = realloc(reader->winners, reader->winner_cap * sizeof *reader->winners);
  }
  winner_row *row = &reader->winners[reader->winner_count++];
  *row = (winner_row){dup(path), NULL, NULL, false};
  return row;
}
bool qa_app_mounted_image_reader_read(qa_app_mounted_image_reader *reader, const char *path, qa_app_scene_asset_bytes *out) {
  const void *mount = NULL;
  const char *content = NULL;
  bool found = reader->host.open(reader->host.ctx, path, NULL, false, &out->bytes, &out->byte_count, &mount, &content);
  winner_row *row = winner_for(reader, path, true);
  row->mount = found ? mount : NULL;
  row->content = content;
  row->known = true;
  return found;
}
bool qa_app_mounted_image_reader_read_original(qa_app_mounted_image_reader *reader, const char *path, qa_app_scene_asset_bytes *out) {
  reader->host.assert_open(reader->host.ctx);
  winner_row *row = winner_for(reader, path, false);
  if (!row || !row->known) {
    uint8_t *bytes = NULL; size_t count = 0;
    const void *mount = NULL;
    const char *content = NULL;
    bool found = reader->host.open(reader->host.ctx, path, NULL, false, &bytes, &count, &mount, &content);
    free(bytes);
    row = winner_for(reader, path, true);
    row->mount = found ? mount : NULL;
    row->content = content;
    row->known = true;
  }
  if (!row->mount || !reader->host.is_user_mount(reader->host.ctx, row->mount)) return false;
  const void *mount = NULL;
  const char *content = NULL;
  return reader->host.open(reader->host.ctx, path, row->content, true, &out->bytes, &out->byte_count, &mount, &content);
}

/* ---- mountedImageReader(catalog, mounts) ------------------------------------------ */
#include "qa/foundation/support.h"
struct qa_app_scene_image_reader {
  const qa_content_catalog *catalog;
  qa_content_mounted *mounts;
  const qa_resolved_resource_reference *last; /* the reference of the last opened asset */
  qa_app_mounted_image_reader *reader;
};
static const qa_mount_identity *provenance_identity(const qa_resource_provenance *provenance) {
  return provenance->kind == QA_CONTENT_MOUNT_ARCHIVE ? &provenance->archive_mount->identity : &provenance->loose_mount->identity;
}
/* const userMount = (mount) => user !== null && (loose ? rootPath === user.root : user.archives.some(...)) */
static bool user_mount(const qa_content_catalog *catalog, const qa_resource_provenance *provenance) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, provenance_identity(provenance)->content);
  const qa_catalog_directory_content *user = product->user_content;
  if (user == NULL) return false;
  if (provenance->kind == QA_CONTENT_MOUNT_LOOSE) return strcmp(provenance->loose_mount->root_path, user->root) == 0;
  for (size_t i = 0; i < user->archive_count; i++)
    if (strcmp(user->archives[i]->path, provenance->archive_mount->archive_path) == 0) return true;
  return false;
}
typedef struct original_filter { const qa_content_catalog *catalog; const char *content; } original_filter;
/* mount => mount.identity.content === content && !userMount(mount) */
static bool accept_original(void *ctx, const qa_content_mount *mount) {
  original_filter *filter = ctx;
  const qa_mount_identity *identity = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive.identity : &mount->as.loose.identity;
  if (strcmp(identity->content, filter->content) != 0) return false;
  qa_resource_provenance provenance;
  memset(&provenance, 0, sizeof provenance);
  provenance.kind = mount->kind;
  provenance.archive_mount = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive : NULL;
  provenance.loose_mount = mount->kind == QA_CONTENT_MOUNT_LOOSE ? &mount->as.loose : NULL;
  return !user_mount(filter->catalog, &provenance);
}
static bool scene_open(void *ctx, const char *path, const void *only_content, bool exclude_user,
    uint8_t **out_bytes, size_t *out_count, const void **out_mount, const char **out_content) {
  qa_app_scene_image_reader *adapter = ctx;
  original_filter filter = {adapter->catalog, only_content};
  qa_content_opened_resource opened;
  bool found = qa_content_mounted_open(adapter->mounts, path, exclude_user ? accept_original : NULL, exclude_user ? &filter : NULL, &opened);
  adapter->last = found ? opened.reference : NULL;
  if (!found) { *out_bytes = NULL; *out_count = 0; *out_mount = NULL; *out_content = NULL; return false; }
  *out_bytes = opened.bytes;
  *out_count = opened.byte_length;
  *out_mount = opened.reference;
  *out_content = provenance_identity(&opened.reference->provenance)->content;
  return true;
}
static void scene_assert_open(void *ctx) { qa_content_mounted_assert_open(((qa_app_scene_image_reader *)ctx)->mounts); }
static bool scene_is_user_mount(void *ctx, const void *mount) {
  qa_app_scene_image_reader *adapter = ctx;
  return user_mount(adapter->catalog, &((const qa_resolved_resource_reference *)mount)->provenance);
}
/* image(asset): { bytes, source: { kind: "resource", resource: asset.reference } } */
static bool scene_asset(qa_app_scene_image_reader *adapter, bool found, qa_app_scene_asset_bytes bytes, qa_scene_asset *out,
    uint8_t **owned_bytes) {
  if (!found) return false;
  out->bytes = bytes.bytes;
  out->length = bytes.byte_count;
  memset(&out->source_sample, 0, sizeof out->source_sample);
  out->source_sample.source_is_resource = true;
  out->source_sample.source_resource = *adapter->last;
  *owned_bytes = bytes.bytes;
  return true;
}
static bool scene_read(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned_bytes) {
  qa_app_scene_image_reader *adapter = ctx;
  qa_app_scene_asset_bytes bytes = {NULL, 0};
  bool found = qa_app_mounted_image_reader_read(adapter->reader, path, &bytes);
  return scene_asset(adapter, found, bytes, out, owned_bytes);
}
static bool scene_read_original(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned_bytes) {
  qa_app_scene_image_reader *adapter = ctx;
  qa_app_scene_asset_bytes bytes = {NULL, 0};
  bool found = qa_app_mounted_image_reader_read_original(adapter->reader, path, &bytes);
  return scene_asset(adapter, found, bytes, out, owned_bytes);
}
qa_app_scene_image_reader *qa_app_scene_image_reader_open(const qa_content_catalog *catalog, qa_content_mounted *mounts) {
  qa_app_scene_image_reader *adapter = calloc(1, sizeof *adapter);
  if (adapter == NULL) qa_range_error("Out of memory");
  adapter->catalog = catalog;
  adapter->mounts = mounts;
  qa_app_image_reader_host host = {adapter, scene_open, scene_assert_open, scene_is_user_mount};
  adapter->reader = qa_app_mounted_image_reader_create(host);
  if (adapter->reader == NULL) qa_range_error("Out of memory");
  return adapter;
}
qa_scene_asset_reader qa_app_scene_image_reader_reader(qa_app_scene_image_reader *reader) {
  qa_scene_asset_reader out = {reader, scene_read, scene_read_original};
  return out;
}
void qa_app_scene_image_reader_destroy(qa_app_scene_image_reader *reader) {
  if (reader == NULL) return;
  qa_app_mounted_image_reader_destroy(reader->reader);
  free(reader);
}

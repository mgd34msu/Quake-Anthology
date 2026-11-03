/* Port of src/app/bootstrap/image-reader.ts. Mount/catalog facts are
 * host callbacks; winner caching and original-resolution selection
 * are kept here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_IMAGE_READER_H
#define QA_APP_BOOTSTRAP_IMAGE_READER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS SceneAsset subset used here: bytes + winning mount token. */
typedef struct qa_app_scene_asset_bytes { uint8_t *bytes; size_t byte_count; } qa_app_scene_asset_bytes;
typedef struct qa_app_image_reader_host {
  void *ctx;
  /* open: malloc'd bytes + mount token (NULL mount = TS null asset). */
  bool (*open)(void *ctx, const char *path, const void *only_content, bool exclude_user,
               uint8_t **out_bytes, size_t *out_count, const void **out_mount, const char **out_content);
  void (*assert_open)(void *ctx);
  bool (*is_user_mount)(void *ctx, const void *mount);
} qa_app_image_reader_host;
typedef struct qa_app_mounted_image_reader qa_app_mounted_image_reader;
qa_app_mounted_image_reader *qa_app_mounted_image_reader_create(qa_app_image_reader_host host);
void qa_app_mounted_image_reader_destroy(qa_app_mounted_image_reader *reader);
/* False = TS null. Bytes owned by caller. */
bool qa_app_mounted_image_reader_read(qa_app_mounted_image_reader *reader, const char *path, qa_app_scene_asset_bytes *out);
bool qa_app_mounted_image_reader_read_original(qa_app_mounted_image_reader *reader, const char *path, qa_app_scene_asset_bytes *out);
/* TS mountedImageReader(catalog, mounts) as the SceneAssetReader it returns: the
 * host above implemented over the catalog's user-content facts and the mounts.
 * catalog and mounts are borrowed and must outlive the reader. */
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/render/scene/textures.h"
typedef struct qa_app_scene_image_reader qa_app_scene_image_reader;
qa_app_scene_image_reader *qa_app_scene_image_reader_open(const qa_content_catalog *catalog, qa_content_mounted *mounts);
qa_scene_asset_reader qa_app_scene_image_reader_reader(qa_app_scene_image_reader *reader);
void qa_app_scene_image_reader_destroy(qa_app_scene_image_reader *reader);
#endif

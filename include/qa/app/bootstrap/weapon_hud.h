/* Port of src/app/bootstrap/weapon-hud.ts. ApplicationAssets
 * (assets.ts, sibling slice) is mirrored as a provider host; qpic/wad
 * decoding and icon keying/caching are kept here. Async -> synchronous
 * (pending promise map becomes a retained result cache).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_WEAPON_HUD_H
#define QA_APP_BOOTSTRAP_WEAPON_HUD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/ui.h"
#include "qa/text/draw2d.h"
typedef struct qa_app_weapon_hud_provider {
  void *ctx;
  const char *family; /* "q1"|"q2"|"q3" */
  bool has_palette; qa_palette palette;
  /* mounts.open(path): malloc'd bytes + reference, false = TS null. */
  bool (*open)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length, qa_resolved_resource_reference *out_reference);
  qa_picture_asset (*register_shader_picture)(void *ctx, const char *name);
  /* textures.load: false = TS null. */
  bool (*load_texture)(void *ctx, const char *path, qa_renderer_image *out_image, double *out_width, double *out_height);
} qa_app_weapon_hud_provider;
typedef struct qa_app_weapon_hud_assets_host {
  void *ctx;
  qa_app_weapon_hud_provider (*provider)(void *ctx, qa_content_id content);
  /* assets.images.register for qpic images. */
  qa_renderer_image (*register_image)(void *ctx, const char *key, qa_render_image image, qa_resolved_resource_reference source);
  /* catalog.product(content).expectation */
  const qa_catalog_product_expectation *(*expectation)(void *ctx, qa_content_id content);
  /* Material aspect walk over the opaque compiled material. */
  bool (*material_aspect)(void *ctx, const qa_material_picture *material, double *out_aspect);
  /* image picture width/height from a renderer image handle. */
  bool (*image_size)(void *ctx, qa_renderer_image image, double *out_width, double *out_height);
} qa_app_weapon_hud_assets_host;
typedef struct qa_app_weapon_hud_assets qa_app_weapon_hud_assets;
qa_app_weapon_hud_assets *qa_app_weapon_hud_assets_create(qa_app_weapon_hud_assets_host host);
void qa_app_weapon_hud_assets_destroy(qa_app_weapon_hud_assets *a);
const qa_picture_asset *qa_app_weapon_hud_assets_picture(qa_app_weapon_hud_assets *a, qa_resource_id id);
/* TS prepareImageRefresh returns the publish closure. */
typedef struct qa_app_weapon_hud_refresh qa_app_weapon_hud_refresh;
qa_app_weapon_hud_refresh *qa_app_weapon_hud_assets_prepare_image_refresh(qa_app_weapon_hud_assets *a);
void qa_app_weapon_hud_refresh_publish(qa_app_weapon_hud_refresh *refresh);
double qa_app_weapon_hud_assets_aspect(qa_app_weapon_hud_assets *a, const qa_resource_id *id);
typedef struct qa_app_weapon_hud_prepared { qa_resource_id weapon; qa_resource_id ammo; bool has_weapon, has_ammo; } qa_app_weapon_hud_prepared;
qa_app_weapon_hud_prepared qa_app_weapon_hud_assets_prepare(qa_app_weapon_hud_assets *a, const qa_weapon_hud_status *status);
qa_resource_id qa_app_weapon_hud_assets_load(qa_app_weapon_hud_assets *a, qa_weapon_hud_icon icon);
#endif

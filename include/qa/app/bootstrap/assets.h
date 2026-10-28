/* Port of src/app/bootstrap/assets.ts. Provider/world/model/font
 * loading is wired through the host below (landed scene/image/model
 * services); policy state, world guard and close state are kept
 * here. The pure palette/archive-path helpers are host rows over
 * landed mounts. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_ASSETS_H
#define QA_APP_BOOTSTRAP_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/contracts/content.h"
/* TS ImagePolicy / ModelReplacementPolicy value rows. */
typedef struct qa_app_image_policy {
  double override_level, override_mask;
  const char *formats; /* borrowed text */
} qa_app_image_policy;
typedef struct qa_app_model_policy {
  bool q1_enhanced, q2_load, q2_use;
  double q2_distance;
  bool distance_is_source; double distance;
} qa_app_model_policy;
/* TS ProviderSceneAssets / ModelAsset borrowed rows. */
typedef struct qa_app_provider_scene_assets {
  qa_app_model_policy model_policy;
  qa_game_family family;
  const void *mounts, *palette, *textures, *shaders;
} qa_app_provider_scene_assets;
typedef struct qa_app_model_asset {
  const void *resource, *model, *brush_scene;
  qa_app_provider_scene_assets provider;
} qa_app_model_asset;
typedef struct qa_app_application_assets qa_app_application_assets;
typedef struct qa_app_assets_host {
  void *ctx;
  double (*now)(void *ctx); /* media clock */
  const char *(*provider)(void *ctx, qa_app_application_assets *assets, qa_content_id content, qa_app_provider_scene_assets *out);
  const char *(*load_world)(void *ctx, qa_app_application_assets *assets, const void **out_world);
  const char *(*load_font)(void *ctx, qa_app_application_assets *assets, const void **out_font);
  const char *(*load_typography)(void *ctx, qa_app_application_assets *assets, const void **out_typography);
  const char *(*prepare_refresh)(void *ctx, qa_app_application_assets *assets, qa_app_image_policy policy, qa_app_model_policy model_policy, const void **out_prepared);
  void (*commit_refresh)(void *ctx, const void *prepared);
  void (*discard_refresh)(void *ctx, const void *prepared);
  void (*finish_refresh)(void *ctx, qa_app_application_assets *assets);
  const char *(*model)(void *ctx, qa_app_application_assets *assets, qa_content_id content, const char *path, qa_app_model_asset *out);
  void (*close_assets)(void *ctx, qa_app_application_assets *assets);
} qa_app_assets_host;
qa_app_application_assets *qa_app_application_assets_create(qa_app_loaded_content *content, qa_app_assets_host host, qa_app_model_policy model_policy);
void qa_app_application_assets_destroy(qa_app_application_assets *assets);
qa_app_loaded_content *qa_app_application_assets_content(const qa_app_application_assets *assets);
bool qa_app_application_assets_image_policy(const qa_app_application_assets *assets, qa_app_image_policy *out);
void qa_app_application_assets_set_image_policy(qa_app_application_assets *assets, qa_app_image_policy policy);
qa_app_model_policy qa_app_application_assets_model_policy(const qa_app_application_assets *assets);
void qa_app_application_assets_set_model_policy(qa_app_application_assets *assets, qa_app_model_policy policy);
/* TS world getter: aborts before loadWorld (message from TS). */
const void *qa_app_application_assets_world(qa_app_application_assets *assets);
void qa_app_application_assets_set_world(qa_app_application_assets *assets, const void *world);
qa_app_provider_scene_assets qa_app_application_assets_provider(qa_app_application_assets *assets, qa_content_id content);
const void *qa_app_application_assets_load_world(qa_app_application_assets *assets);
const void *qa_app_application_assets_load_console_font(qa_app_application_assets *assets);
const void *qa_app_application_assets_load_menu_typography(qa_app_application_assets *assets);
const void *qa_app_application_assets_prepare_image_refresh(qa_app_application_assets *assets, qa_app_image_policy policy, bool has_model_policy, qa_app_model_policy model_policy);
void qa_app_application_assets_commit_image_refresh(qa_app_application_assets *assets, const void *prepared);
void qa_app_application_assets_discard_image_refresh(qa_app_application_assets *assets, const void *prepared);
void qa_app_application_assets_finish_image_refresh(qa_app_application_assets *assets);
qa_app_model_asset qa_app_application_assets_model(qa_app_application_assets *assets, qa_content_id content, const char *path);
void qa_app_application_assets_close(qa_app_application_assets *assets);
#endif

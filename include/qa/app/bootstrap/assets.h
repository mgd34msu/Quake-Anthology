/* Port of src/app/bootstrap/assets.ts. Presentation caches are distinct
 * from authoritative map and game state. Async loads are synchronous;
 * the TS promise caches keep only settled successes (a failure raises
 * and leaves no entry, as the TS eviction does), except material movies,
 * whose TS cache keeps a rejection: a failed movie load re-raises its
 * error on every later request. Errors raise through qa_raise with the
 * TS class name and message. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_ASSETS_H
#define QA_APP_BOOTSTRAP_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/menu_font.h"
#include "qa/app/bootstrap/model_loader.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/core/cvars/index.h"
#include "qa/materials/material_finish.h"
#include "qa/media/types.h"
#include "qa/render/scene/image_policy.h"
#include "qa/render/scene/material_registrations.h"
#include "qa/render/scene/models/replacements.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/resources.h"
#include "qa/render/scene/shaders.h"
#include "qa/render/scene/textures.h"
#include "qa/render/scene/world.h"
#include "qa/text/atlas.h"
/* TS ProviderSceneAssets. textures is read live (TS getter over
 * shaders.textures) and model_policy is the assets' current policy at
 * the time the value is returned. Every pointer is owned by the assets
 * (mounts by the loaded content). */
typedef struct qa_app_provider_scene_assets {
  qa_model_replacement_policy model_policy;
  qa_game_family family;
  qa_content_mounted *mounts;
  const qa_palette *palette; /* NULL is TS null */
  qa_scene_texture_loader *textures;
  qa_scene_shader_registry *shaders;
} qa_app_provider_scene_assets;
/* TS ModelAsset (the LoadedApplicationModel spread plus provider and
 * brushScene). model reflects the variants' committed replacement at
 * the time the value is returned. */
typedef struct qa_app_model_asset {
  qa_resolved_resource_reference resource;
  qa_decoded_model model;
  qa_app_model_variants *variants; /* NULL is TS undefined; owned by the assets */
  qa_app_provider_scene_assets provider;
  qa_world_scene *brush_scene;     /* NULL is TS null */
} qa_app_model_asset;
/* TS constructor options. NULL members are TS undefined. */
typedef struct qa_app_application_assets_options {
  const qa_scene_image_policy *image_policy;        /* copied */
  const qa_model_replacement_policy *model_policy;  /* copied */
  qa_scene_image_registry *image_registry;          /* forked, not owned */
  qa_finish_shader_profile (*shader_profile)(void *ctx); void *shader_profile_ctx;
  void (*warn)(void *ctx, const char *text); void *warn_ctx;
} qa_app_application_assets_options;
typedef struct qa_app_application_assets qa_app_application_assets;
/* media_clock NULL is the TS performance.now() clock; options NULL is
 * TS {}. Raises "Application images belong to another renderer" when
 * image_registry belongs to another owner. NULL only on OOM. */
qa_app_application_assets *qa_app_application_assets_create(qa_app_loaded_content *content,
    qa_renderer_resource_owner owner, const qa_media_clock *media_clock,
    const qa_app_application_assets_options *options);
/* Closes, then frees. */
void qa_app_application_assets_destroy(qa_app_application_assets *assets);
qa_app_loaded_content *qa_app_application_assets_content(const qa_app_application_assets *assets);
qa_scene_image_registry *qa_app_application_assets_images(qa_app_application_assets *assets);
qa_scene_material_registrations *qa_app_application_assets_material_registrations(qa_app_application_assets *assets);
/* TS warnOnce. */
void qa_app_application_assets_warn_once(qa_app_application_assets *assets, const char *key, const char *text);
/* NULL is TS undefined. */
const qa_scene_image_policy *qa_app_application_assets_image_policy(const qa_app_application_assets *assets);
qa_model_replacement_policy qa_app_application_assets_model_policy(const qa_app_application_assets *assets);
void qa_app_application_assets_set_model_policy(qa_app_application_assets *assets, qa_model_replacement_policy policy);
/* TS world getter: raises "World presentation has not loaded". */
qa_world_scene *qa_app_application_assets_world(qa_app_application_assets *assets);
qa_app_provider_scene_assets qa_app_application_assets_provider(qa_app_application_assets *assets, qa_content_id content);
/* TS assets.provider(content) as the ModelRenderProvider SceneModelRenderer
 * takes (ProviderSceneAssets is one): the same provider, with textures and
 * modelPolicy read through the TS getters on every use. Valid until the
 * assets close. */
qa_model_render_provider qa_app_application_assets_render_provider(qa_app_application_assets *assets, qa_content_id content);
qa_world_scene *qa_app_application_assets_load_world(qa_app_application_assets *assets);
qa_text_font_selection qa_app_application_assets_load_console_font(qa_app_application_assets *assets);
const qa_app_menu_typography *qa_app_application_assets_load_menu_typography(qa_app_application_assets *assets);
/* TS PreparedApplicationImages. commit consumes the handle on success
 * (a raise from its validation leaves it for discard); discard always
 * consumes it. */
typedef struct qa_app_prepared_images qa_app_prepared_images;
/* model_policy NULL is TS default (the current policy). */
qa_app_prepared_images *qa_app_application_assets_prepare_image_refresh(qa_app_application_assets *assets,
    const qa_scene_image_policy *policy, const qa_model_replacement_policy *model_policy);
qa_app_provider_scene_assets qa_app_prepared_images_provider(qa_app_prepared_images *prepared, qa_content_id content);
qa_text_font_selection qa_app_prepared_images_font(const qa_app_prepared_images *prepared);
const qa_app_menu_typography *qa_app_prepared_images_typography(const qa_app_prepared_images *prepared);
const qa_scene_image_policy *qa_app_prepared_images_policy(const qa_app_prepared_images *prepared);
void qa_app_prepared_images_commit(qa_app_prepared_images *prepared);
void qa_app_prepared_images_discard(qa_app_prepared_images *prepared);
void qa_app_application_assets_finish_image_refresh(qa_app_application_assets *assets);
qa_app_model_asset qa_app_application_assets_model(qa_app_application_assets *assets, qa_content_id content, const char *path);
/* TS assets.content.catalog. */
const qa_content_catalog *qa_app_application_assets_catalog(const qa_app_application_assets *assets);
/* TS loadMenuFont({ catalog, mounts: provider.mounts, family: provider.family,
 * rerelease, images: assets.images, imagePolicy }) for content, as the world
 * text fonts of a seat load it. policy NULL is TS undefined. */
typedef struct qa_app_world_font qa_app_world_font;
qa_app_world_font *qa_app_application_assets_load_world_font(qa_app_application_assets *assets, qa_content_id content,
                                                             const qa_scene_image_policy *policy);
qa_text_font_selection qa_app_world_font_selection(const qa_app_world_font *font);
void qa_app_world_font_close(qa_app_world_font *font);
void qa_app_application_assets_close(qa_app_application_assets *assets);
/* TS createWorldAssets images argument (NULL is TS null). Policies are
 * copied; cvars is borrowed for the assets' life (shaderProfileFromCvars). */
typedef struct qa_app_world_asset_images {
  qa_scene_image_policy policy;
  qa_model_replacement_policy model_policy;
  qa_cvar_registry *cvars;
} qa_app_world_asset_images;
/* TS createWorldAssets: assets for a world that shares a renderer's image
 * registry and follows the live image and shader policy. */
qa_app_application_assets *qa_app_create_world_assets(qa_app_loaded_content *content,
    qa_renderer_resource_owner owner, qa_scene_image_registry *images,
    void (*warn)(void *ctx, const char *text), void *warn_ctx, const qa_app_world_asset_images *world_images);
#endif

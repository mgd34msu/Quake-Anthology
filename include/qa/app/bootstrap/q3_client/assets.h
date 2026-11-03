/* Port of src/app/bootstrap/q3-client/assets.ts. The ApplicationAssets
 * and ProviderSceneAssets surface this file reads is the
 * qa_app_q3_application_assets_host table;
 * qa_app_q3_application_assets_host_create builds it from the real
 * qa_app_application_assets (../assets.h), and
 * qa_app_application_q3_assets_create_from_application is the TS
 * ApplicationQ3Assets.create(assets, ...). Async loads are synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_ASSETS_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/audio/types.h"
#include "qa/content/mounts.h"
#include "qa/content/q3/presentation/audio.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/contracts/content.h"
#include "qa/text/q3_font.h"
#include "qa/text/q3_font_registry.h"
/* TS registerQ3ModelRequest: load false only on OOM; an invalid
 * resource path (RangeError) yields DEFAULT_MODEL without loading. */
bool qa_app_register_q3_model_request(const char *path,
    bool (*load)(void *ctx, const char *path, qa_q3_pres_scene_model *out), void *load_ctx,
    qa_q3_pres_scene_model *out);
/* TS registerQ3ShaderRequest<T> over an opaque T slot: invalid
 * path returns false (TS null) without loading. */
bool qa_app_register_q3_shader_request(const char *path,
    bool (*load)(void *ctx, const char *path, void *out), void *load_ctx, void *out);
/* TS ProviderSceneAssets surface used here (../assets.ts mirror). */
typedef struct qa_app_q3_provider_scene_assets {
  void *ctx;
  qa_content_mounted *mounts;
  /* shaders.registerPicture / registerSourcePicture. */
  bool (*register_picture)(void *ctx, const char *path, qa_material_picture *out);
  bool (*register_source_picture)(void *ctx, const char *path, bool mip, qa_material_picture *out);
  /* shaders.registerGeneratedPicture(name, rgba8 256x256). */
  bool (*register_generated_picture)(void *ctx, const char *name, qa_byte_view rgba, qa_material_picture *out);
  qa_material_picture source_default_picture;
  /* mounts.plan.mounts archive paths / loose roots. */
  const char *const *archive_paths; size_t archive_path_count;
  const char *const *loose_roots; size_t loose_root_count;
} qa_app_q3_provider_scene_assets;
/* TS ApplicationAssets surface used here (../assets.ts mirror). */
typedef struct qa_app_q3_application_assets_host {
  void *ctx;
  /* assets.provider(content). */
  void (*provider)(void *ctx, qa_content_id content, qa_app_q3_provider_scene_assets *out);
  /* assets.model(content, path): fills the SceneModel-shaped asset:
   * brush-model assets set is_brush + world/index. */
  void (*model)(void *ctx, qa_content_id content, const char *path, qa_q3_pres_scene_model *out);
  /* catalog.products[].archives: entry names for one archive path,
   * or false when the archive is not in the catalog list. */
  bool (*catalog_archive_entries)(void *ctx, const char *archive_path,
                                  const char *const **out_names, size_t *out_count);
  /* catalog.product(content).userContent?.root (NULL = null) and
   * expectation.family == "q3" test. */
  const char *(*user_content_root)(void *ctx, qa_content_id content);
  bool (*product_is_q3)(void *ctx, qa_content_id content);
  /* recipe.character.appearance.content / first q3 weapon content
   * (false = absent). */
  qa_content_id (*character_content)(void *ctx);
  bool (*weapon_content)(void *ctx, qa_content_id *out);
  /* assets.world.map.models for the world() host reply. */
  void (*world_map_models)(void *ctx, qa_q3_pres_world_scene *out);
  /* assets.world.remapShader(original, replacement, timeOffset,
   * { source: source's shaders, current }): false is the TS "stale". */
  bool (*remap_shader)(void *ctx, const char *original, const char *replacement, double time_offset,
                       const qa_app_q3_provider_scene_assets *source, bool (*current)(void *current_ctx),
                       void *current_ctx);
} qa_app_q3_application_assets_host;
/* The host over a real ApplicationAssets (borrowed; must outlive the
 * host). host.ctx owns the interned provider/catalog/world views; free
 * with qa_app_q3_application_assets_host_destroy. NULL ctx only on OOM. */
qa_app_q3_application_assets_host qa_app_q3_application_assets_host_create(qa_app_application_assets *assets);
void qa_app_q3_application_assets_host_destroy(qa_app_q3_application_assets_host *host);
/* TS ApplicationQ3Assets. */
typedef struct qa_app_application_q3_assets qa_app_application_q3_assets;
/* mode: "source-sync"|"guest-async"; scope: "selected"|"source".
 * NULL mode/scope select the TS defaults. */
qa_app_application_q3_assets *qa_app_application_q3_assets_create(
    qa_app_q3_application_assets_host host, qa_content_id content,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    bool (*save_font_data)(void *ctx), void *save_font_data_ctx,
    const char *mode, const char *scope);
/* TS ApplicationQ3Assets.create(assets, content, print, saveFontData,
 * mode, scope) over a real ApplicationAssets; the built host is owned by
 * the result. */
qa_app_application_q3_assets *qa_app_application_q3_assets_create_from_application(
    qa_app_application_assets *assets, qa_content_id content,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    bool (*save_font_data)(void *ctx), void *save_font_data_ctx,
    const char *mode, const char *scope);
void qa_app_application_q3_assets_destroy(qa_app_application_q3_assets *assets);
/* TS ApplicationQ3Assets.assets: the ApplicationAssets the assets were
 * created over (create_from_application, or a host from
 * qa_app_q3_application_assets_host_create); NULL for any other host. */
qa_app_application_assets *qa_app_application_q3_assets_application(const qa_app_application_q3_assets *assets);
/* TS readonly fields. */
qa_content_id qa_app_application_q3_assets_content(const qa_app_application_q3_assets *assets);
/* TS assets.content.catalog.product(content).userContent?.root: NULL is null. */
const char *qa_app_application_q3_assets_user_content_root(const qa_app_application_q3_assets *assets);
void qa_app_application_q3_assets_print(const qa_app_application_q3_assets *assets, const char *text);
qa_q3_pres_presentation_sound_bank *qa_app_application_q3_assets_bank(qa_app_application_q3_assets *assets);
qa_q3_renderer_font_registry *qa_app_application_q3_assets_fonts(qa_app_application_q3_assets *assets);
qa_q3_ui_asset_registry *qa_app_application_q3_assets_font_registry(qa_app_application_q3_assets *assets);
const qa_app_q3_provider_scene_assets *qa_app_application_q3_assets_provider(const qa_app_application_q3_assets *assets);
/* modelProviders / modelContents lookups by SceneModel identity
 * (kind+path+resource). False when unmapped. */
bool qa_app_application_q3_assets_model_provider(qa_app_application_q3_assets *assets,
    const qa_q3_pres_scene_model *model, qa_app_q3_provider_scene_assets *out);
bool qa_app_application_q3_assets_model_content(qa_app_application_q3_assets *assets,
    const qa_q3_pres_scene_model *model, qa_content_id *out);
/* TS has / list (malloc'd list of malloc'd strings). */
bool qa_app_application_q3_assets_has(qa_app_application_q3_assets *assets, const char *path);
char **qa_app_application_q3_assets_list(qa_app_application_q3_assets *assets, const char *prefix, size_t *out_count);
/* TS read: malloc'd bytes (empty when missing). */
uint8_t *qa_app_application_q3_assets_read(qa_app_application_q3_assets *assets, const char *path, size_t *out_length);
double qa_app_application_q3_assets_read_file_length(qa_app_application_q3_assets *assets, const char *path);
/* TS readSync: aborts with the TS Error when unretained. Bytes borrow. */
const uint8_t *qa_app_application_q3_assets_read_sync(qa_app_application_q3_assets *assets,
                                                      const char *path, size_t *out_length);
/* TS SoundAssetReader sound load (the bank's sync loader): NULL = TS null. */
const qa_sound_asset *qa_app_application_q3_assets_sound(qa_app_application_q3_assets *assets, const char *name);
/* TS resourceHost(scene): scene is the recorder mirror token. */
qa_q3_pres_resource_host qa_app_application_q3_assets_resource_host(
    qa_app_application_q3_assets *assets, qa_q3_pres_scene_recorder_view scene);
void qa_app_application_q3_assets_close(qa_app_application_q3_assets *assets);
#endif

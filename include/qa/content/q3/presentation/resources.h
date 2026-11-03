/* Port of src/content/q3/presentation/resources.ts. GPL-2.0-or-later.
 * TS async registrations are synchronous here; register_* return false
 * only on OOM. SceneShader identity is the address of the registry's
 * per-picture shader record. The scene recorder is the scene.h view. */
#ifndef QA_CONTENT_Q3_PRESENTATION_RESOURCES_H
#define QA_CONTENT_Q3_PRESENTATION_RESOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/scene.h"
#include "qa/content/q3/presentation/scene_host.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/core/common_parse.h"
#include "qa/persistence/value.h"
#include "qa/text/draw2d.h"
#include "qa/world/collision/q3/topology.h"
/* TS AssetReader. read/list results are caller-freed (free() the bytes;
 * list: free each string then the array). list prefix NULL is TS omitted.
 * read false is OOM; missing assets abort in the reader, as a TS throw. */
typedef struct qa_q3_pres_asset_reader {
  void *ctx;
  bool (*read)(void *ctx, const char *path, uint8_t **out, size_t *out_length);
  bool (*has)(void *ctx, const char *path);
  bool (*list)(void *ctx, const char *prefix, char ***out, size_t *out_count);
} qa_q3_pres_asset_reader;
/* TS SoundAssetReader extends AssetReader. */
typedef struct qa_q3_pres_sound_asset_reader {
  qa_q3_pres_asset_reader base;
  double (*read_file_length)(void *ctx, const char *path);
  bool (*read_sync)(void *ctx, const char *path, uint8_t **out, size_t *out_length);
} qa_q3_pres_sound_asset_reader;
/* TS ClientSoundBank. register false only on OOM; *out NULL is TS null. */
typedef struct qa_q3_pres_client_sound_bank {
  void *ctx;
  bool (*register_sound)(void *ctx, const char *path, bool compressed, qa_pcm_sound **out);
  double (*index_for_sound)(void *ctx, qa_pcm_sound *sound);
} qa_q3_pres_client_sound_bank;
/* TS WorldScene: map.models[].bounds (borrowed span). */
typedef struct qa_q3_pres_world_scene {
  const qa_bounds *model_bounds;
  size_t model_count;
} qa_q3_pres_world_scene;
/* TS Q3ResourceWorld: map borrows a landed Q3WorldGeometry (only
 * entities/nodes/leaves/planes are read). */
typedef struct qa_q3_pres_resource_world {
  const qa_q3_world_geometry *map;
  void *ctx;
  qa_source_cluster_pvs (*cluster_pvs)(void *ctx, double cluster);
} qa_q3_pres_resource_world;
/* TS Q3SceneRecorder (scene.ts): the landed recorder view. */
/* TS Q3ResourceHost. model false only on OOM; skin/shader *out NULL is
 * TS null (shader picture is host-owned). world false only on OOM. */
typedef struct qa_q3_pres_resource_host {
  qa_q3_pres_scene_recorder_view scene;
  qa_material_picture zero_picture;
  void *ctx;
  bool (*model)(void *ctx, const char *path, qa_q3_pres_scene_model *out);
  bool (*skin)(void *ctx, const char *path, const qa_q3_pres_scene_skin **out);
  bool (*shader)(void *ctx, const char *path, bool mip, const qa_material_picture **out);
  bool (*world)(void *ctx, const char *requested_path, qa_q3_pres_world_scene *out);
  void (*remap_shader)(void *ctx, const char *original, const char *replacement, const char *offset);
} qa_q3_pres_resource_host;
/* TS RendererResources interface. register_* false only on OOM;
 * skin/shader *out NULL is TS null; model path NULL is TS null. */
typedef struct qa_q3_pres_renderer_resources {
  void *ctx;
  bool (*register_model)(void *ctx, const char *path, qa_q3_pres_scene_model *out);
  bool (*register_skin)(void *ctx, const char *path, const qa_q3_pres_scene_skin **out);
  bool (*register_shader)(void *ctx, const char *path, const qa_q3_pres_scene_shader **out);
  bool (*register_shader_no_mip)(void *ctx, const char *path, const qa_q3_pres_scene_shader **out);
  qa_material_picture (*picture)(void *ctx, const qa_q3_pres_scene_shader *shader);
  double (*model_handle)(void *ctx, const qa_q3_pres_scene_model *model);
  qa_q3_pres_scene_model (*model_for_handle)(void *ctx, double handle);
  const qa_q3_pres_scene_shader *(*shader_for_handle)(void *ctx, double handle);
  void (*clear_scene)(void *ctx);
  void (*add_ref_entity)(void *ctx, const qa_q3_pres_admitted_ref_entity *entity);
  void (*add_poly)(void *ctx, const qa_q3_pres_ref_poly *poly);
  void (*add_light)(void *ctx, qa_q3_pres_dynamic_light light);
  void (*remap_shader)(void *ctx, const char *original, const char *replacement, const char *offset);
  void (*render_scene)(void *ctx, const qa_q3_pres_refdef *refdef);
  bool (*load_world)(void *ctx, const char *path, qa_q3_pres_world_scene *out);
} qa_q3_pres_renderer_resources;
typedef enum qa_q3_pres_resource_handles {
  QA_Q3_PRES_HANDLES_RENDERER, /* "renderer" */
  QA_Q3_PRES_HANDLES_CLIENT    /* "client" */
} qa_q3_pres_resource_handles;
typedef struct qa_q3_pres_q3_renderer_resources qa_q3_pres_q3_renderer_resources;
/* world NULL is the TS omitted world argument. False only on OOM. */
bool qa_q3_pres_q3_renderer_resources_init(qa_q3_pres_q3_renderer_resources **out, qa_q3_pres_resource_host host,
    const qa_q3_pres_resource_world *world, qa_q3_pres_resource_handles handles);
void qa_q3_pres_q3_renderer_resources_destroy(qa_q3_pres_q3_renderer_resources *resources);
qa_q3_pres_renderer_resources qa_q3_pres_q3_renderer_resources_as_resources(qa_q3_pres_q3_renderer_resources *resources);
/* Owned checkpoint tree (qa_save_value_free); NULL only on OOM. Aborts
 * with the TS Error text unless an idle client handle owner. */
qa_save_value *qa_q3_pres_q3_renderer_resources_capture_checkpoint(qa_q3_pres_q3_renderer_resources *resources);
/* TS async restoreCheckpoint, synchronous. False only on OOM. */
bool qa_q3_pres_q3_renderer_resources_restore_checkpoint(qa_q3_pres_q3_renderer_resources *resources,
    const qa_save_value *value);
typedef struct qa_q3_pres_registered_model {
  const char *path; /* borrowed */
  double handle;
  qa_q3_pres_scene_model model;
} qa_q3_pres_registered_model;
typedef struct qa_q3_pres_registered_skin {
  const char *path; /* borrowed */
  double handle;
  const qa_q3_pres_scene_skin *skin;
} qa_q3_pres_registered_skin;
/* Caller frees the array only. False only on OOM. */
bool qa_q3_pres_q3_renderer_resources_registered_models(qa_q3_pres_q3_renderer_resources *resources,
    qa_q3_pres_registered_model **out, size_t *out_count);
bool qa_q3_pres_q3_renderer_resources_registered_skins(qa_q3_pres_q3_renderer_resources *resources,
    qa_q3_pres_registered_skin **out, size_t *out_count);
bool qa_q3_pres_q3_renderer_resources_register_model(qa_q3_pres_q3_renderer_resources *resources,
    const char *path, qa_q3_pres_scene_model *out);
bool qa_q3_pres_q3_renderer_resources_register_skin(qa_q3_pres_q3_renderer_resources *resources,
    const char *path, const qa_q3_pres_scene_skin **out);
bool qa_q3_pres_q3_renderer_resources_register_shader(qa_q3_pres_q3_renderer_resources *resources,
    const char *path, const qa_q3_pres_scene_shader **out);
bool qa_q3_pres_q3_renderer_resources_register_shader_no_mip(qa_q3_pres_q3_renderer_resources *resources,
    const char *path, const qa_q3_pres_scene_shader **out);
qa_material_picture qa_q3_pres_q3_renderer_resources_picture(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_scene_shader *shader);
double qa_q3_pres_q3_renderer_resources_model_handle(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_scene_model *model);
qa_q3_pres_scene_model qa_q3_pres_q3_renderer_resources_model_for_handle(
    qa_q3_pres_q3_renderer_resources *resources, double handle);
const qa_q3_pres_scene_shader *qa_q3_pres_q3_renderer_resources_shader_for_handle(
    qa_q3_pres_q3_renderer_resources *resources, double handle);
double qa_q3_pres_q3_renderer_resources_shader_handle(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_scene_shader *shader);
double qa_q3_pres_q3_renderer_resources_skin_handle(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_scene_skin *skin);
const qa_q3_pres_scene_skin *qa_q3_pres_q3_renderer_resources_skin_for_handle(
    qa_q3_pres_q3_renderer_resources *resources, double handle);
void qa_q3_pres_q3_renderer_resources_clear_scene(qa_q3_pres_q3_renderer_resources *resources);
void qa_q3_pres_q3_renderer_resources_add_ref_entity(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_admitted_ref_entity *entity);
void qa_q3_pres_q3_renderer_resources_add_poly(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_ref_poly *poly);
void qa_q3_pres_q3_renderer_resources_add_light(qa_q3_pres_q3_renderer_resources *resources,
    qa_q3_pres_dynamic_light light);
void qa_q3_pres_q3_renderer_resources_remap_shader(qa_q3_pres_q3_renderer_resources *resources,
    const char *original, const char *replacement, const char *offset);
void qa_q3_pres_q3_renderer_resources_render_scene(qa_q3_pres_q3_renderer_resources *resources,
    const qa_q3_pres_refdef *refdef);
bool qa_q3_pres_q3_renderer_resources_load_world(qa_q3_pres_q3_renderer_resources *resources,
    const char *path, qa_q3_pres_world_scene *out);
bool qa_q3_pres_q3_renderer_resources_get_entity_token(qa_q3_pres_q3_renderer_resources *resources,
    void (*write)(void *ctx, const char *token), void *write_ctx);
bool qa_q3_pres_q3_renderer_resources_in_pvs(qa_q3_pres_q3_renderer_resources *resources,
    qa_vec3 (*read_first)(void *ctx), qa_vec3 (*read_second)(void *ctx), void *read_ctx);
#endif

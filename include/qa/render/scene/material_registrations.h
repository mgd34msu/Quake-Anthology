/* Port of src/render/scene/material-registrations.ts. GPL-2.0-or-later.
 * async remap/bind flows are synchronous (PORTING.md). SceneShaderBinding
 * (TS owner: shaders.ts) and the WorldScene remap surface are mirrored here
 * as service structs to break the shaders/world include cycle; shaders.c
 * and world.c implement them. */
#ifndef QA_RENDER_SCENE_MATERIAL_REGISTRATIONS_H
#define QA_RENDER_SCENE_MATERIAL_REGISTRATIONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/compile.h"
typedef struct qa_scene_material_registrations qa_scene_material_registrations;
typedef struct qa_provider_shader_registrations qa_provider_shader_registrations;
typedef struct qa_shader_replacement_stage qa_shader_replacement_stage;
typedef struct qa_shader_registration {
  qa_provider_shader_registrations *provider;
  char *name; /* owned normalized */
} qa_shader_registration;
typedef struct qa_shader_world_identity {
  qa_scene_material_registrations *owner;
  double ordinal;
} qa_shader_world_identity;
/* TS MaterialHandle: replaceable current compiled material (borrowed). */
typedef struct qa_scene_material_handle {
  qa_shader_registration *registration;
  const qa_compiled_material *current;
} qa_scene_material_handle;
/* TS RegisteredSceneMaterial = CompiledMaterial & { registration }. */
typedef struct qa_registered_scene_material {
  qa_shader_registration *registration;
  qa_scene_material_handle *handle; /* NULL for plain (unregistered) values */
  const qa_compiled_material *compiled; /* current when handle NULL */
} qa_registered_scene_material;
const qa_mat_registered_explicit_shader *qa_registered_scene_material_registered(qa_registered_scene_material m);
const qa_mat_finished_shader *qa_registered_scene_material_finished(qa_registered_scene_material m);
const qa_render_material *qa_registered_scene_material_material(qa_registered_scene_material m);
const qa_compiled_material *qa_registered_scene_material_compiled(qa_registered_scene_material m);
/* Mirrored SceneShaderBinding (TS owner: shaders.ts). Texture payload is
 * an opaque SceneTexture pointer (qa_scene_texture, textures.h). */
typedef struct qa_scene_texture qa_scene_texture;
typedef struct qa_scene_shader_binding {
  bool is_world; /* false "unlit" */
  double lightmap_index; bool mipmap;          /* unlit */
  qa_shader_world_identity *world;              /* world */
  bool has_lightmap; qa_renderer_image lightmap;
  const qa_scene_texture *base_texture;
} qa_scene_shader_binding;
typedef struct qa_scene_material_remap {
  qa_registered_scene_material material; double time_offset; void *source; /* SceneShaderRegistry* */
} qa_scene_material_remap;
typedef enum qa_shader_remap_result { QA_REMAP_COMMITTED, QA_REMAP_UNCHANGED, QA_REMAP_STALE } qa_shader_remap_result;
const char *qa_shader_remap_result_name(qa_shader_remap_result result);
typedef enum qa_shader_binding_key_kind { QA_BINDING_UNLIT, QA_BINDING_SOURCE, QA_BINDING_WORLD } qa_shader_binding_key_kind;
typedef struct qa_shader_logical_key {
  const char *name;
  qa_shader_binding_key_kind kind;
  double lightmap_index;       /* unlit / world */
  double request;              /* source */
  qa_shader_world_identity *world; const char *base_texture_name; /* world; NULL base = TS null */
} qa_shader_logical_key;
/* Prepared remap (TS PreparedSceneRemap, owner shaders.ts). */
typedef struct qa_prepared_scene_remap {
  void *ctx;
  qa_registered_scene_material *materials; size_t material_count; /* borrowed view */
  bool accepted;
  bool (*current)(void *ctx);
  void (*commit)(void *ctx);
  void (*discard)(void *ctx);
} qa_prepared_scene_remap;
/* Remap source service (implemented by SceneShaderRegistry). */
typedef struct qa_scene_remap_source {
  void *ctx;                    /* qa_scene_shader_registry* */
  qa_provider_shader_registrations *registrations;
  void (*prepare_remap_materials)(void *ctx, const char *name, const qa_scene_shader_binding *bindings, size_t count, qa_prepared_scene_remap *out);
  const qa_registered_scene_material *(*material_bindings_materials)(void *ctx, const qa_scene_shader_binding **bindings_out, size_t *out_count);
} qa_scene_remap_source;
/* World service (implemented by WorldScene). */
typedef struct qa_scene_raw_remap_binding { double surface; qa_scene_shader_binding binding; } qa_scene_raw_remap_binding;
typedef struct qa_scene_world_service {
  void *ctx; /* qa_world_scene* */
  qa_scene_raw_remap_binding *(*raw_remap_bindings)(void *ctx, const char *name, size_t *out_count);
  void (*publish_raw_remap)(void *ctx, const char *name, const qa_registered_scene_material *materials, size_t count, void *source, double time_offset);
} qa_scene_world_service;
struct qa_provider_shader_registrations {
  qa_scene_material_registrations *owner;
  void *_registrations; /* key string -> registration */
};
struct qa_scene_material_registrations {
  void *_providers, *_worlds, *_admitted, *_bindings, *_remaps, *_remap_requests, *_scenes;
  double world_ordinal, binding_revision, remap_revision;
};
struct qa_shader_replacement_stage {
  qa_provider_shader_registrations *provider;
  int state; /* 0 preparing, 1 committed, 2 discarded */
  void *_entries;
};
void qa_scene_material_registrations_init(qa_scene_material_registrations *r);
void qa_scene_material_registrations_destroy(qa_scene_material_registrations *r);
double qa_scene_material_registrations_revision(const qa_scene_material_registrations *r);
/* currentRemap free function: false is TS null. Non-handle materials -> null. */
bool qa_current_remap(const qa_compiled_material *material, const qa_scene_material_handle *handle, qa_scene_material_remap *out);
bool qa_scene_material_registrations_current_remap(qa_scene_material_registrations *r, qa_registered_scene_material material, qa_scene_material_remap *out);
typedef struct qa_scene_remap_definition { const char *replacement; void *source; double time_offset; } qa_scene_remap_definition;
bool qa_scene_material_registrations_remap_definition(qa_scene_material_registrations *r, const char *name, qa_scene_remap_definition *out);
void qa_scene_material_registrations_bind_world(qa_scene_material_registrations *r, qa_scene_world_service world);
void qa_scene_material_registrations_release_world(qa_scene_material_registrations *r, qa_scene_world_service world);
void qa_scene_material_registrations_bind_material(qa_scene_material_registrations *r, qa_scene_remap_source source, qa_registered_scene_material material, qa_scene_shader_binding binding);
typedef bool (*qa_scene_current_fn)(void *ctx);
typedef struct qa_scene_remap_request {
  const char *original, *replacement; double time_offset;
  qa_scene_remap_source source; qa_scene_current_fn current; void *current_ctx;
} qa_scene_remap_request;
qa_shader_remap_result qa_scene_material_registrations_remap(qa_scene_material_registrations *r, qa_scene_remap_request request);
typedef struct qa_remap_refresh_source { qa_scene_remap_source old_source, new_source; } qa_remap_refresh_source;
typedef struct qa_prepared_remap_refresh { void *_impl; qa_scene_material_registrations *owner; } qa_prepared_remap_refresh;
void qa_scene_material_registrations_prepare_remap_refresh(qa_scene_material_registrations *r, const qa_remap_refresh_source *sources, size_t count, qa_prepared_remap_refresh *out);
void qa_prepared_remap_refresh_validate(qa_prepared_remap_refresh *p);
void qa_prepared_remap_refresh_commit(qa_prepared_remap_refresh *p);
qa_provider_shader_registrations *qa_scene_material_registrations_provider(qa_scene_material_registrations *r, qa_content_id content);
qa_shader_world_identity *qa_scene_material_registrations_world(qa_scene_material_registrations *r, qa_decoded_world map);
void qa_scene_material_registrations_admit(qa_scene_material_registrations *r, qa_shader_registration *registration);
qa_registered_scene_material qa_scene_material_registrations_publish(qa_scene_material_registrations *r, qa_shader_registration *registration, const qa_compiled_material *compiled);
qa_registered_scene_material qa_scene_material_registrations_retained(qa_scene_material_registrations *r, qa_shader_registration *registration);
qa_registered_scene_material qa_scene_material_registrations_require_material(qa_scene_material_registrations *r, qa_registered_scene_material material);
void qa_scene_material_registrations_cancel(qa_scene_material_registrations *r, qa_shader_registration *registration);
/* sorted flag mirrors TS snapshot(sorted=true). Array borrowed from scratch; copy before next call. */
const qa_registered_scene_material *qa_scene_material_registrations_snapshot(qa_scene_material_registrations *r, bool sorted, size_t *out_count);
void qa_provider_shader_registrations_init(qa_provider_shader_registrations *p, qa_scene_material_registrations *owner);
void qa_provider_shader_registrations_destroy(qa_provider_shader_registrations *p);
char *qa_provider_shader_registrations_key(qa_provider_shader_registrations *p, qa_shader_logical_key key);
qa_shader_registration *qa_provider_shader_registrations_find(qa_provider_shader_registrations *p, qa_shader_logical_key key);
qa_shader_registration *qa_provider_shader_registrations_reserve(qa_provider_shader_registrations *p, qa_shader_logical_key key);
qa_registered_scene_material qa_provider_shader_registrations_publish(qa_provider_shader_registrations *p, qa_shader_registration *registration, const qa_compiled_material *compiled);
void qa_provider_shader_registrations_begin_replacement(qa_provider_shader_registrations *p, qa_shader_replacement_stage *out);
void qa_shader_replacement_stage_destroy(qa_shader_replacement_stage *s);
void qa_provider_shader_registrations_cancel(qa_provider_shader_registrations *p, qa_shader_logical_key key, qa_shader_registration *registration);
qa_registered_scene_material qa_provider_shader_registrations_commit(qa_provider_shader_registrations *p, qa_shader_logical_key key, qa_shader_registration *registration, const qa_compiled_material *compiled);
qa_shader_registration *qa_shader_replacement_stage_reserve(qa_shader_replacement_stage *s, qa_shader_logical_key key);
qa_registered_scene_material qa_shader_replacement_stage_publish(qa_shader_replacement_stage *s, qa_shader_registration *registration, const qa_compiled_material *compiled);
void qa_shader_replacement_stage_cancel(qa_shader_replacement_stage *s, qa_shader_logical_key key, qa_shader_registration *registration);
void qa_shader_replacement_stage_validate(qa_shader_replacement_stage *s);
void qa_shader_replacement_stage_commit(qa_shader_replacement_stage *s);
void qa_shader_replacement_stage_discard(qa_shader_replacement_stage *s);
#endif

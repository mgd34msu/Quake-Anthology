/* Port of src/render/scene/shaders.ts. GPL-2.0-or-later.
 * async/Promise flows are synchronous (PORTING.md); the TS promise caches
 * become result caches with identical values. Compiled materials are
 * registry-owned and freed on destroy. */
#ifndef QA_RENDER_SCENE_SHADERS_H
#define QA_RENDER_SCENE_SHADERS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/materials/compile.h"
#include "qa/materials/material_finish.h"
#include "qa/materials/sky.h"
#include "qa/render/scene/material_registrations.h"
#include "qa/render/scene/textures.h"
#include "qa/text/draw2d.h"
typedef struct qa_source_scene_materials {
  qa_registered_scene_material default_material, stencil_shadow, projection_shadow, flare, sun;
} qa_source_scene_materials;
typedef bool (*qa_scene_play_cinematic_fn)(void *ctx, const char *name, qa_mat_registered_video *out);
typedef struct qa_scene_shader_registry {
  qa_scene_texture_loader *textures; /* public, rebindable (TS textures) */
  qa_provider_shader_registrations *registrations;
  qa_finish_shader_profile profile;
  qa_sky_builder sky;
  char **warnings; size_t warning_count, warning_capacity;
  qa_mat_registered_sun sun_value; bool has_sun;
  void *_impl;
} qa_scene_shader_registry;
/* family NULL is the TS default "q3"; stage non-NULL builds a replacement. */
void qa_scene_shader_registry_init(qa_scene_shader_registry *r, qa_scene_texture_loader *textures, qa_provider_shader_registrations *registrations, const qa_finish_shader_profile *profile, qa_scene_play_cinematic_fn play_cinematic, void *play_ctx, const char *family, qa_shader_replacement_stage *stage);
void qa_scene_shader_registry_destroy(qa_scene_shader_registry *r);
qa_source_scene_materials qa_scene_shader_registry_source_materials(qa_scene_shader_registry *r);
bool qa_scene_shader_registry_preparing_replacement(const qa_scene_shader_registry *r);
qa_registered_scene_material qa_scene_shader_registry_source_world_material(qa_scene_shader_registry *r, qa_registered_scene_material material);
typedef void (*qa_scene_load_scripts_fn)(void *ctx);
void qa_scene_shader_registry_initialize_source_materials(qa_scene_shader_registry *r, qa_scene_load_scripts_fn load_scripts, void *ctx);
void qa_scene_shader_registry_add_script(qa_scene_shader_registry *r, const char *text, const char *source);
/* TS addScriptFiles: id ScanAndLoadShaderFiles precedence over files in FS_ListFilteredFiles order. */
void qa_scene_shader_registry_add_script_files(qa_scene_shader_registry *r, const char *const *texts, const char *const *sources, size_t count);
bool qa_scene_shader_registry_has_authored(qa_scene_shader_registry *r, const char *name);
bool qa_scene_shader_registry_has_cinematic(qa_scene_shader_registry *r, const char *name);
typedef struct qa_scene_material_binding { qa_registered_scene_material material; qa_scene_shader_binding binding; } qa_scene_material_binding;
const qa_scene_material_binding *qa_scene_shader_registry_material_bindings(qa_scene_shader_registry *r, size_t *out_count);
void qa_scene_shader_registry_prepare_remap_materials(qa_scene_shader_registry *r, const char *name, const qa_scene_shader_binding *bindings, size_t count, qa_prepared_scene_remap *out);
qa_registered_scene_material qa_scene_shader_registry_register(qa_scene_shader_registry *r, const char *name, const qa_scene_shader_binding *binding);
qa_registered_scene_material qa_scene_shader_registry_register_unlit(qa_scene_shader_registry *r, const char *name);
/* Stage replacement with fresh textures; replacement storage is caller's. */
void qa_scene_shader_registry_replacement(qa_scene_shader_registry *r, qa_scene_texture_loader *textures, qa_scene_shader_registry *replacement);
void qa_scene_shader_registry_prepare_replacement(qa_scene_shader_registry *r, qa_scene_shader_registry *replacement);
void qa_scene_shader_registry_validate_replacement(qa_scene_shader_registry *r, qa_scene_shader_registry *replacement);
void qa_scene_shader_registry_commit_replacement(qa_scene_shader_registry *r, qa_scene_shader_registry *replacement);
void qa_scene_shader_registry_discard_replacement(qa_scene_shader_registry *replacement);
qa_picture_asset qa_scene_shader_registry_register_picture(qa_scene_shader_registry *r, const char *name, bool mipmap);
qa_picture_asset qa_scene_shader_registry_register_generated_picture(qa_scene_shader_registry *r, const char *name, qa_render_image image);
/* NULL picture name signals TS null (source default). */
bool qa_scene_shader_registry_register_source_picture(qa_scene_shader_registry *r, const char *name, bool mipmap, qa_picture_asset *out);
qa_picture_asset qa_scene_shader_registry_source_default_picture(qa_scene_shader_registry *r);
/* The qa_scene_remap_source service view of this registry. */
qa_scene_remap_source qa_scene_shader_registry_remap_source(qa_scene_shader_registry *r);
#endif

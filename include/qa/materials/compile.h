/*
 * Port of src/materials/compile.ts. inspectShaderScript (TS owner:
 * material.ts) and finishShader/implicitShaderInput (TS owner:
 * material-finish.ts) are not ported in this slice, so compilation
 * takes inspected catalog entries plus finish/implicit preparation as
 * fn+ctx services; the compile.ts logic and defaults are ported here.
 * shader_render_material stages/modifiers are malloc'd; free with
 * qa_shader_render_material_destroy. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_COMPILE_H
#define QA_MATERIALS_COMPILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/materials/shader_types.h"

typedef qa_mat_compiled_material qa_compiled_material;

extern const qa_mat_finish_profile qa_default_shader_profile;

/* TS ShaderCatalogEntry.program reduced to its register operation. */
typedef struct qa_mat_catalog_entry {
  void *ctx;
  qa_mat_registered_explicit_shader (*register_shader)(
      void *ctx, const qa_mat_registration_host *host);
} qa_mat_catalog_entry;

typedef struct qa_mat_finish_service {
  void *ctx;
  qa_mat_finished_shader (*finish)(void *ctx, qa_mat_finish_input input);
  qa_mat_finish_input (*implicit_input)(void *ctx,
                                        qa_mat_finish_implicit_input input);
} qa_mat_finish_service;

typedef struct qa_compile_shader_options {
  const char *source; /* accepted, owned by the unported inspector */
  bool has_lightmap_index;
  double lightmap_index;
  const qa_mat_finish_profile *profile; /* NULL = default */
} qa_compile_shader_options;

qa_render_material qa_shader_render_material(qa_mat_shader_definition definition);
void qa_shader_render_material_destroy(qa_render_material *material);

/* Malloc'd result array; destroy each material then free the array. */
qa_mat_compiled_material *qa_compile_shader_script(
    const qa_mat_catalog_entry *entries, size_t entry_count,
    const qa_mat_registration_host *host, qa_mat_finish_service finish,
    qa_compile_shader_options options, size_t *out_count);
void qa_compiled_material_destroy(qa_mat_compiled_material *material);

typedef struct qa_surface_parameter_flags {
  uint32_t surface, contents;
  bool clear_solid;
} qa_surface_parameter_flags;
qa_surface_parameter_flags qa_shader_surface_flags(
    const char *const *parameters, size_t parameter_count);

qa_mat_compiled_material qa_compile_implicit_material(
    qa_mat_finish_implicit_input input, qa_mat_finish_service finish);

#endif

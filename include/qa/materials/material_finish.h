/*
 * Port of src/materials/material-finish.ts: FinishShader,
 * VertexLightingCollapse and the implicit (internal) shader layouts.
 * Record types reuse shader_types.h (qa_mat_finish_* / qa_mat_finished_*
 * chosen by slice-a). Results own their stage/pass/diagnostic storage;
 * input definitions and images are borrowed and must outlive a result
 * only where the TS shares the object (stage maps, bindings, strings).
 * The CollapseMultitexture iterator (TS owner material-iterator.ts,
 * not yet ported) is implemented privately in material_finish.c.
 * GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_MATERIAL_FINISH_H
#define QA_MATERIALS_MATERIAL_FINISH_H

#include "qa/materials/material.h"
#include "qa/materials/shader_types.h"

typedef qa_mat_registered_stage qa_finish_image_metadata;
typedef qa_mat_registered_stage qa_finish_loaded_image_metadata;
typedef qa_mat_finish_profile qa_finish_shader_profile;
typedef qa_mat_finished_iterator_stage qa_finished_shader_stage;
typedef qa_mat_finish_diagnostic qa_finish_shader_diagnostic;
typedef qa_mat_finished_shader qa_finished_shader;
typedef qa_mat_finish_input qa_finish_shader_input;
typedef qa_mat_finish_implicit_input qa_finish_implicit_shader_input;

/* TS FinishFailedShaderInput (not mirrored in shader_types.h). */
typedef struct qa_finish_failed_shader_input {
  const char *name;
  double lightmap_index;
  qa_finish_shader_profile profile;
} qa_finish_failed_shader_input;

typedef struct qa_finished_shader_result {
  qa_finished_shader shader;
  void *_storage;
} qa_finished_shader_result;

typedef struct qa_finish_shader_input_result {
  qa_finish_shader_input input;
  void *_storage;
} qa_finish_shader_input_result;

/* TS finishShader. RangeError validation aborts via qa_range_error. */
void qa_finish_shader(qa_finish_shader_input input, qa_finished_shader_result *out);
void qa_finished_shader_result_free(qa_finished_shader_result *result);
/* TS implicitShaderInput; the result owns the built definition/images. */
void qa_implicit_shader_input(qa_finish_implicit_shader_input input,
                              qa_finish_shader_input_result *out);
void qa_finish_shader_input_result_free(qa_finish_shader_input_result *result);

#endif

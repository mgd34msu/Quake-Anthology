/*
 * Shared shader types for the materials slice-a port. TS owners
 * src/materials/material.ts and src/materials/material-finish.ts are not
 * ported in this slice, so the types slice-a files consume are mirrored
 * here with collision-proof qa_mat_* names (contracts/render.h already
 * owns the semantic qa_waveform/qa_shader_stage forms). Reconcile when
 * material.ts/material-finish.ts land. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_SHADER_TYPES_H
#define QA_MATERIALS_SHADER_TYPES_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qa/contracts/render.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/materials/cinematic.h"

/* JS ToInt32/ToUint32 for bitwise operators. */
static inline int32_t qa_mat_to_int32(double value) {
  if (!isfinite(value)) return 0;
  double m = fmod(trunc(value), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  if (m >= 2147483648.0) m -= 4294967296.0;
  return (int32_t)m;
}
static inline uint32_t qa_mat_to_uint32(double value) {
  return (uint32_t)qa_mat_to_int32(value);
}
static inline float qa_mat_f(double value) { return (float)value; }
static inline bool qa_mat_kind_is(const char *kind, const char *name) {
  return kind != NULL && strcmp(kind, name) == 0;
}

/* TS SourceWaveFunction (material.ts), exact numeric values. */
enum {
  QA_MAT_WAVE_NONE = 0, QA_MAT_WAVE_SIN = 1, QA_MAT_WAVE_SQUARE = 2,
  QA_MAT_WAVE_TRIANGLE = 3, QA_MAT_WAVE_SAWTOOTH = 4,
  QA_MAT_WAVE_INVERSE_SAWTOOTH = 5, QA_MAT_WAVE_NOISE = 6
};
/* TS SourceColorGenerator (material.ts). */
enum {
  QA_MAT_CGEN_BAD = 0, QA_MAT_CGEN_IDENTITY_LIGHTING = 1,
  QA_MAT_CGEN_IDENTITY = 2, QA_MAT_CGEN_ENTITY = 3,
  QA_MAT_CGEN_ONE_MINUS_ENTITY = 4, QA_MAT_CGEN_EXACT_VERTEX = 5,
  QA_MAT_CGEN_VERTEX = 6, QA_MAT_CGEN_ONE_MINUS_VERTEX = 7,
  QA_MAT_CGEN_WAVEFORM = 8, QA_MAT_CGEN_LIGHTING_DIFFUSE = 9,
  QA_MAT_CGEN_FOG = 10, QA_MAT_CGEN_CONST = 11
};
/* TS SourceAlphaGenerator (material.ts). */
enum {
  QA_MAT_AGEN_IDENTITY = 0, QA_MAT_AGEN_SKIP = 1, QA_MAT_AGEN_ENTITY = 2,
  QA_MAT_AGEN_ONE_MINUS_ENTITY = 3, QA_MAT_AGEN_VERTEX = 4,
  QA_MAT_AGEN_ONE_MINUS_VERTEX = 5, QA_MAT_AGEN_LIGHTING_SPECULAR = 6,
  QA_MAT_AGEN_WAVEFORM = 7, QA_MAT_AGEN_PORTAL = 8, QA_MAT_AGEN_CONST = 9
};
/* TS SourceTexCoordGenerator (material.ts). */
enum {
  QA_MAT_TCGEN_BAD = 0, QA_MAT_TCGEN_IDENTITY = 1, QA_MAT_TCGEN_LIGHTMAP = 2,
  QA_MAT_TCGEN_TEXTURE = 3, QA_MAT_TCGEN_ENVIRONMENT_MAPPED = 4,
  QA_MAT_TCGEN_FOG = 5, QA_MAT_TCGEN_VECTOR = 6
};

/* TS SourceWaveStorage (material.ts). func is SourceWaveFunction. */
typedef struct qa_mat_wave_storage {
  int func;
  double base, amplitude, phase, frequency;
} qa_mat_wave_storage;

/* TS SourceShaderStageState (material.ts). */
typedef struct qa_mat_source_stage_state {
  bool active;
  uint32_t state_bits;
  int rgb_gen, alpha_gen, tc_gen;
  qa_mat_wave_storage rgb_wave, alpha_wave;
  bool is_lightmap, vertex_lightmap;
} qa_mat_source_stage_state;

/* TS TexCoordModifier (material.ts), transform fields flattened. */
typedef struct qa_mat_tc_modifier {
  const char *kind;
  qa_vec2 amount;
  qa_waveform wave;
  double degrees_per_second;
  double m00, m01, m10, m11;
  qa_vec2 translation;
} qa_mat_tc_modifier;

/* TS ShaderStage (material.ts); generator payloads reuse contracts. */
typedef struct qa_mat_shader_stage {
  qa_shader_map map;
  qa_blend_state blend;
  const char *depth_func;
  bool depth_write;
  const char *alpha_func;
  bool detail;
  qa_color_generator rgb_gen;
  qa_alpha_generator alpha_gen;
  qa_tex_coord_generator tc_gen;
  const qa_mat_tc_modifier *tc_mods; size_t tc_mod_count;
} qa_mat_shader_stage;

/* TS ParsedShaderStage (material.ts). */
typedef struct qa_mat_parsed_stage {
  qa_mat_shader_stage stage;
  qa_mat_source_stage_state source_state;
} qa_mat_parsed_stage;

typedef struct qa_mat_shader_diagnostic {
  const char *source;
  double line, column;
  const char *message;
} qa_mat_shader_diagnostic;

typedef struct qa_mat_compiler_directive {
  const char *name;
  qa_string_view_list arguments;
} qa_mat_compiler_directive;

typedef struct qa_mat_shader_sky_params {
  const char *outer_box; /* nullable */
  double cloud_height;
  const char *inner_box; /* nullable */
} qa_mat_shader_sky_params;

/* TS ShaderDefinition (material.ts). sky/fog/sun NULL = TS null;
 * has_sort false = TS sort null. VertexDeformation reuses contracts. */
typedef struct qa_mat_shader_definition {
  const char *name;
  const qa_mat_parsed_stage *stages; size_t stage_count;
  qa_string_view_list surface_parms;
  const char *cull;
  bool has_sort;
  double sort;
  const qa_mat_shader_sky_params *sky;
  const qa_q3_material_fog *fog;
  const qa_q3_material_sun *sun;
  const qa_vertex_deformation *deforms; size_t deform_count;
  bool polygon_offset, no_mip_maps, no_pic_mip, entity_mergable;
  double portal_range, clamp_time;
  const qa_mat_shader_diagnostic *warnings; size_t warning_count;
  const qa_mat_compiler_directive *compiler_directives;
  size_t compiler_directive_count;
} qa_mat_shader_definition;

/* TS FinishedStageImage / FinishedImagePlayback / FinishedStageBinding
 * (material.ts). binding.kind: "images"|"video"|"retain-current-texture";
 * playback.kind: "single"|"animation". */
typedef struct qa_mat_finished_stage_image {
  qa_renderer_image image;
} qa_mat_finished_stage_image;
typedef struct qa_mat_finished_playback {
  const char *kind;
  qa_mat_finished_stage_image image;
  double frequency;
  const qa_mat_finished_stage_image *frames; size_t frame_count;
} qa_mat_finished_playback;
typedef struct qa_mat_finished_binding {
  const char *kind;
  qa_mat_finished_playback playback;
  qa_shader_cinematic_source source;
} qa_mat_finished_binding;

/* TS RegisteredImage / RegisteredShaderVideo / RegisteredShaderStage
 * (material.ts). stage.kind: "loaded"|"missing". */
typedef struct qa_mat_registered_image {
  qa_mat_finished_stage_image frame;
  double tmu;
} qa_mat_registered_image;
typedef struct qa_mat_registered_video {
  qa_shader_cinematic_source source;
  qa_mat_registered_image image;
} qa_mat_registered_video;
typedef struct qa_mat_registered_stage {
  const char *kind;
  double tmu;
  qa_mat_finished_binding binding;
} qa_mat_registered_stage;

/* TS SourceImageRequest (material.ts). */
typedef struct qa_mat_source_image_request {
  const char *name;
  bool mipmap, allow_picmip;
  const char *wrap; /* "repeat"|"clamp" */
} qa_mat_source_image_request;

/* TS RegisteredSun (material.ts). */
typedef struct qa_mat_registered_sun {
  qa_vec3 light, direction;
} qa_mat_registered_sun;

/* TS RegisteredSkyBox: image(face) closure as fn+ctx. face is a
 * SourceSkyFaceName string ("rt"|"bk"|"lf"|"ft"|"up"|"dn"). */
typedef struct qa_mat_registered_sky_box {
  void *ctx;
  qa_mat_finished_stage_image (*image)(void *ctx, const char *face);
} qa_mat_registered_sky_box;
typedef struct qa_mat_registered_sky {
  const qa_mat_registered_sky_box *outer; /* nullable */
  const qa_mat_registered_sky_box *inner; /* nullable */
  double cloud_height;
} qa_mat_registered_sky;

/* TS ShaderRegistrationFailure (material.ts): "source-text" carries a
 * ShaderParseError, reduced here to its formatted message. */
typedef struct qa_mat_registration_failure {
  const char *kind;
  const char *error_message;
  qa_mat_source_image_request request;
} qa_mat_registration_failure;

/* TS RegisteredExplicitShader (material.ts). kind:
 * "defined"|"defaulted"; sky NULL = TS null. */
typedef struct qa_mat_registered_explicit_shader {
  const char *kind;
  qa_mat_registration_failure failure;
  qa_mat_shader_definition definition;
  const qa_mat_registered_stage *stages; size_t stage_count;
  const qa_mat_registered_sky *sky;
} qa_mat_registered_explicit_shader;

/* TS ShaderRegistrationHost (material.ts); async find/play become
 * synchronous callbacks returning false for TS null. */
typedef struct qa_mat_registration_host {
  qa_mat_registered_image white_image, default_image, lightmap_image;
  void *ctx;
  bool (*find_image)(void *ctx, qa_mat_source_image_request request,
                     qa_mat_registered_image *out);
  bool (*play_shader_cinematic)(void *ctx, const char *name,
                                qa_mat_registered_video *out);
  void (*apply_sun)(void *ctx, qa_mat_registered_sun sun);
  void (*initialize_sky_tex_coords)(void *ctx, double height);
  void (*print_warning)(void *ctx, const char *message);
} qa_mat_registration_host;

/* TS MaterialIteratorProfile (material-iterator.ts). */
typedef struct qa_mat_iterator_profile {
  bool ignore_fast_path, multitexture, texture_env_add;
  const char *driver; /* "generic"|"voodoo" */
} qa_mat_iterator_profile;

/* TS FinishedIteratorStage (material-iterator.ts). fog_adjustment and
 * alpha_gen are exact TS strings ("rgb"|...; generator kind or "skip").
 * image_tmu is -1 when inactive; binding valid only when active. */
typedef struct qa_mat_finished_iterator_stage {
  qa_mat_shader_stage stage;
  uint32_t state_bits;
  int rgb_gen;
  const char *fog_adjustment;
  const char *alpha_gen;
  int tc_gen;
  qa_mat_wave_storage rgb_wave, alpha_wave;
  bool is_lightmap, vertex_lightmap;
  bool active;
  int image_tmu;
  qa_mat_finished_binding binding;
} qa_mat_finished_iterator_stage;

/* TS IteratorPass / MaterialIterator (material-iterator.ts). */
typedef struct qa_mat_iterator_pass {
  qa_mat_shader_stage stage;
  uint32_t state_bits;
  int rgb_gen;
  const char *fog_adjustment;
  const char *alpha_gen;
  const qa_mat_finished_iterator_stage *bundles[2];
  size_t bundle_count;
} qa_mat_iterator_pass;
typedef struct qa_mat_material_iterator {
  const char *kind; /* "generic"|"vertex-lit"|"lightmapped-multitexture"|"sky" */
  const qa_mat_iterator_pass *passes; size_t pass_count;
  const char *multitexture_env; /* "none"|"modulate"|"add" */
} qa_mat_material_iterator;

/* TS FinishShaderProfile (material-finish.ts). */
typedef struct qa_mat_finish_profile {
  bool detail_textures, vertex_light, ui_fullscreen;
  const char *hardware; /* "generic"|"permedia2" */
  qa_mat_iterator_profile iterator;
} qa_mat_finish_profile;

typedef struct qa_mat_finish_diagnostic {
  const char *kind; /* "missing-image"|"lightmap-cleared" */
  bool has_stage;
  double stage;
  const char *message;
} qa_mat_finish_diagnostic;

/* TS FinishedShaderStage = FinishedIteratorStage & fields already in
 * qa_mat_finished_iterator_stage (fog_adjustment, rgb_gen). */
/* TS FinishedShader (material-finish.ts). */
typedef struct qa_mat_finished_shader {
  double sort, lightmap_index;
  bool has_lightmap_stage;
  const qa_mat_finished_iterator_stage *source_stages;
  size_t source_stage_count;
  double num_unfogged_passes;
  qa_mat_material_iterator iterator;
  const char *fog_pass; /* "none"|"equal"|"less-equal" */
  const qa_mat_finish_diagnostic *diagnostics; size_t diagnostic_count;
} qa_mat_finished_shader;

/* TS FinishShaderInput (material-finish.ts). */
typedef struct qa_mat_finish_input {
  qa_mat_shader_definition definition;
  double lightmap_index;
  const qa_mat_registered_stage *images; size_t image_count;
  qa_mat_finish_profile profile;
} qa_mat_finish_input;

/* TS FinishImplicitShaderInput (material-finish.ts); white_image /
 * lightmap_image / lightmap_index used per kind. */
typedef struct qa_mat_finish_implicit_input {
  const char *kind;
  const char *name;
  qa_mat_registered_stage base_image;
  qa_mat_registered_stage white_image;
  qa_mat_registered_stage lightmap_image;
  double lightmap_index;
  qa_mat_finish_profile profile;
} qa_mat_finish_implicit_input;

/* TS CompiledMaterial (compile.ts); declared here so dlight/evaluate
 * can consume it without a circular include. */
typedef struct qa_mat_compiled_material {
  qa_mat_registered_explicit_shader registered;
  qa_mat_finished_shader finished;
  qa_render_material material; /* the "q3" RenderMaterial */
} qa_mat_compiled_material;

/* Mirrored evaluateWaveform (TS owner: material.ts), inlined until the
 * material port lands. Exact Math.fround staging. */
static inline int32_t qa_mat_shader_table_index(double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    qa_range_error("Shader table index reaches undefined source float-to-int conversion");
  return (int32_t)integer & 1023;
}
static inline double qa_mat_evaluate_waveform(qa_waveform wave, double time) {
  int32_t index = qa_mat_shader_table_index(
      (double)qa_mat_f((double)qa_mat_f(wave.phase + (double)qa_mat_f(time * wave.frequency)) * 1024.0));
  double value;
  if (qa_mat_kind_is(wave.kind, "none"))
    qa_range_error("EvalWaveForm does not support GF_NONE");
  else if (qa_mat_kind_is(wave.kind, "sin")) value = qa_renderer_sine(index);
  else if (qa_mat_kind_is(wave.kind, "square")) value = index < 512 ? 1 : -1;
  else if (qa_mat_kind_is(wave.kind, "sawtooth")) value = index / 1024.0;
  else if (qa_mat_kind_is(wave.kind, "inversesawtooth")) value = 1 - index / 1024.0;
  else if (qa_mat_kind_is(wave.kind, "triangle"))
    value = index < 256 ? index / 256.0 : index < 768 ? 2 - index / 256.0 : index / 256.0 - 4;
  else /* "noise" */
    qa_range_error("Noise waveforms require the renderer's seeded R_NoiseGet4f context; EvalWaveForm does not support GF_NOISE");
  return qa_mat_f(wave.base + (double)qa_mat_f(value * wave.amplitude));
}

#endif

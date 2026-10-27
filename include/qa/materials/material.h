/*
 * Port of src/materials/material.ts: Q3 shader script parsing, shader
 * registration against a host, waveform/texcoord evaluation and stage
 * render-state projection. GPL-2.0-or-later.
 * C accommodations:
 *  - The semantic/mirrored record types live in shader_types.h
 *    (qa_mat_*, chosen by slice-a because contracts/render.h already
 *    owns qa_waveform/qa_shader_stage); this header reuses them and
 *    adds the parser/registration surface. Waveform, the generator
 *    payloads, ShaderMap and VertexDeformation reuse contracts types.
 *  - ShaderRegistrationProgram (abstract class + async register)
 *    becomes fn+ctx with a synchronous register; the TS generator of
 *    RegistrationInstructions is executed live against the host at
 *    each yield point (identical observable host call order). The
 *    compiled program re-parses its retained text on register instead
 *    of replaying stored instructions; only the timing of the tcMod
 *    1024-byte RangeError differs (register time, not inspect time).
 *  - ShaderParseError throws become a captured error record for
 *    inspect/rejected results; the uncaught paths (parseShaderScript,
 *    "Expected shader name") abort via qa_range_error with the exact
 *    "source:line:column: message" text. Plain TS Error throws in
 *    evaluation/registration also abort via qa_range_error.
 *  - Strings are NUL-terminated bytes: embedded-NUL and non-ASCII
 *    toLowerCase cases in the TS cannot be represented; ASCII folding
 *    matches the TS regex/charCode logic exactly.
 *  - Parsed/registered results own their strings and arrays; free
 *    with the matching *_free. Allocation failure aborts (the TS GC
 *    never reports it; the parser has no per-call OOM sentinel).
 */
#ifndef QA_MATERIALS_MATERIAL_H
#define QA_MATERIALS_MATERIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/core/common_parse.h"
#include "qa/materials/shader_types.h"

/* TS enums, canonical names for the shader_types.h QA_MAT_* values. */
typedef int qa_source_wave_function;
typedef int qa_source_color_generator;
typedef int qa_source_alpha_generator;
typedef int qa_source_tex_coord_generator;
enum {
  QA_SOURCE_WAVE_NONE = QA_MAT_WAVE_NONE, QA_SOURCE_WAVE_SIN = QA_MAT_WAVE_SIN,
  QA_SOURCE_WAVE_SQUARE = QA_MAT_WAVE_SQUARE,
  QA_SOURCE_WAVE_TRIANGLE = QA_MAT_WAVE_TRIANGLE,
  QA_SOURCE_WAVE_SAWTOOTH = QA_MAT_WAVE_SAWTOOTH,
  QA_SOURCE_WAVE_INVERSE_SAWTOOTH = QA_MAT_WAVE_INVERSE_SAWTOOTH,
  QA_SOURCE_WAVE_NOISE = QA_MAT_WAVE_NOISE
};
enum {
  QA_SOURCE_CGEN_BAD = QA_MAT_CGEN_BAD,
  QA_SOURCE_CGEN_IDENTITY_LIGHTING = QA_MAT_CGEN_IDENTITY_LIGHTING,
  QA_SOURCE_CGEN_IDENTITY = QA_MAT_CGEN_IDENTITY,
  QA_SOURCE_CGEN_ENTITY = QA_MAT_CGEN_ENTITY,
  QA_SOURCE_CGEN_ONE_MINUS_ENTITY = QA_MAT_CGEN_ONE_MINUS_ENTITY,
  QA_SOURCE_CGEN_EXACT_VERTEX = QA_MAT_CGEN_EXACT_VERTEX,
  QA_SOURCE_CGEN_VERTEX = QA_MAT_CGEN_VERTEX,
  QA_SOURCE_CGEN_ONE_MINUS_VERTEX = QA_MAT_CGEN_ONE_MINUS_VERTEX,
  QA_SOURCE_CGEN_WAVEFORM = QA_MAT_CGEN_WAVEFORM,
  QA_SOURCE_CGEN_LIGHTING_DIFFUSE = QA_MAT_CGEN_LIGHTING_DIFFUSE,
  QA_SOURCE_CGEN_FOG = QA_MAT_CGEN_FOG, QA_SOURCE_CGEN_CONST = QA_MAT_CGEN_CONST
};
enum {
  QA_SOURCE_AGEN_IDENTITY = QA_MAT_AGEN_IDENTITY,
  QA_SOURCE_AGEN_SKIP = QA_MAT_AGEN_SKIP,
  QA_SOURCE_AGEN_ENTITY = QA_MAT_AGEN_ENTITY,
  QA_SOURCE_AGEN_ONE_MINUS_ENTITY = QA_MAT_AGEN_ONE_MINUS_ENTITY,
  QA_SOURCE_AGEN_VERTEX = QA_MAT_AGEN_VERTEX,
  QA_SOURCE_AGEN_ONE_MINUS_VERTEX = QA_MAT_AGEN_ONE_MINUS_VERTEX,
  QA_SOURCE_AGEN_LIGHTING_SPECULAR = QA_MAT_AGEN_LIGHTING_SPECULAR,
  QA_SOURCE_AGEN_WAVEFORM = QA_MAT_AGEN_WAVEFORM,
  QA_SOURCE_AGEN_PORTAL = QA_MAT_AGEN_PORTAL,
  QA_SOURCE_AGEN_CONST = QA_MAT_AGEN_CONST
};
enum {
  QA_SOURCE_TCGEN_BAD = QA_MAT_TCGEN_BAD,
  QA_SOURCE_TCGEN_IDENTITY = QA_MAT_TCGEN_IDENTITY,
  QA_SOURCE_TCGEN_LIGHTMAP = QA_MAT_TCGEN_LIGHTMAP,
  QA_SOURCE_TCGEN_TEXTURE = QA_MAT_TCGEN_TEXTURE,
  QA_SOURCE_TCGEN_ENVIRONMENT_MAPPED = QA_MAT_TCGEN_ENVIRONMENT_MAPPED,
  QA_SOURCE_TCGEN_FOG = QA_MAT_TCGEN_FOG,
  QA_SOURCE_TCGEN_VECTOR = QA_MAT_TCGEN_VECTOR
};

/* TS record types (see shader_types.h for field details). */
typedef qa_mat_wave_storage qa_source_wave_storage;
typedef qa_mat_source_stage_state qa_source_shader_stage_state;
typedef qa_mat_finished_stage_image qa_finished_stage_image;
typedef qa_mat_finished_playback qa_finished_image_playback;
typedef qa_mat_finished_binding qa_finished_stage_binding;
typedef qa_mat_source_image_request qa_source_image_request;
typedef qa_mat_registered_image qa_registered_image;
typedef qa_mat_registered_video qa_registered_shader_video;
typedef qa_mat_registered_stage qa_registered_shader_stage;
typedef qa_mat_registered_sun qa_registered_sun;
typedef qa_mat_registered_sky_box qa_registered_sky_box;
typedef qa_mat_registered_sky qa_registered_sky;
typedef qa_mat_shader_stage qa_shader_stage_def;
typedef qa_mat_parsed_stage qa_parsed_shader_stage;
typedef qa_mat_shader_definition qa_shader_definition;
typedef qa_mat_shader_diagnostic qa_shader_diagnostic;
typedef qa_mat_registration_host qa_shader_registration_host;
typedef qa_mat_registration_failure qa_shader_registration_failure;
typedef qa_mat_registered_explicit_shader qa_registered_explicit_shader;

/* TS ShaderParseError. formatted is "source:line:column: message";
 * reason is "source-rejection" or "validation". Owned by its result. */
typedef struct qa_shader_parse_error {
  char *source;
  int64_t line, column;
  char *message;
  const char *reason;
  char *formatted;
} qa_shader_parse_error;

/* TS ShaderRegistrationProgram. register_fn fills out (owned by out);
 * destroy frees ctx. Programs in catalog entries are owned by the
 * inspection and must not be destroyed separately. */
typedef struct qa_shader_registration_result qa_shader_registration_result;
typedef struct qa_shader_registration_program {
  void *ctx;
  void (*register_fn)(void *ctx, qa_shader_registration_host *host,
                      qa_shader_registration_result *out);
  void (*destroy)(void *ctx);
} qa_shader_registration_program;

struct qa_shader_registration_result {
  qa_registered_explicit_shader shader;
  qa_shader_parse_error error; /* valid when failure.kind is source-text */
  void *_storage;               /* opaque; freed by *_free */
};

void qa_shader_registration_result_free(qa_shader_registration_result *result);
void qa_shader_program_register(qa_shader_registration_program *program,
                                qa_shader_registration_host *host,
                                qa_shader_registration_result *out);
void qa_shader_program_destroy(qa_shader_registration_program *program);

/* TS SourceShaderRegistrationProgram over a borrowed common-parse
 * cursor. has_offset false is TS offset null. The program owns copies
 * of source/name; the cursor stays caller-owned and must outlive it. */
void qa_source_shader_program_init(qa_shader_registration_program *program,
                                   qa_common_parse_cursor *cursor,
                                   bool has_offset, size_t offset,
                                   const char *source, const char *name);

/* TS ShaderCatalogEntry. accepted false: definition is the partial and
 * error is set. name/program/definition are owned by the inspection. */
typedef struct qa_shader_catalog_entry {
  char *name;
  qa_shader_registration_program program;
  bool accepted;
  qa_shader_definition definition;
  qa_shader_parse_error error;
} qa_shader_catalog_entry;

typedef struct qa_shader_script_inspection {
  qa_shader_catalog_entry *entries;
  size_t count;
  void *_storage;
} qa_shader_script_inspection;

typedef struct qa_shader_definitions {
  qa_shader_definition *items;
  size_t count;
  void *_storage;
} qa_shader_definitions;

/* TS parseShaderScript (source NULL means "<shader>"). Aborts with the
 * ShaderParseError text on the first rejected shader. */
void qa_parse_shader_script(const char *text, const char *source,
                            qa_shader_definitions *out);
void qa_shader_definitions_free(qa_shader_definitions *definitions);
/* TS inspectShaderScript (registration_name NULL = TS null). */
void qa_inspect_shader_script(const char *text, const char *source,
                              const char *registration_name,
                              qa_shader_script_inspection *out);
void qa_shader_script_inspection_free(qa_shader_script_inspection *inspection);

/* Malloc'd; caller frees. */
char *qa_strip_shader_extension(const char *name);
char *qa_normalize_shader_name(const char *name);
/* size is 1024 or 2048 (TS literal union; used as size-1 mask). */
int64_t qa_shader_name_hash(const char *name, int64_t size);
bool qa_same_shader_name(const char *first, const char *second);

/* TS evaluateWaveform. Aborts for none/noise kinds and for table
 * indices outside the defined float-to-int range. */
double qa_evaluate_waveform(qa_waveform wave, double time);

/* TS TexCoordContext; has_* false is TS undefined. */
typedef struct qa_tex_coord_context {
  bool has_lightmap;
  qa_vec2 lightmap;
  bool has_view_origin;
  qa_vec3 view_origin;
  bool has_shader_tex_coord;
  qa_vec2 shader_tex_coord;
} qa_tex_coord_context;

/* TS evaluateTexCoords. context NULL is the TS default {}. */
qa_vec2 qa_evaluate_tex_coords(const qa_shader_stage_def *stage, qa_vec2 uv,
                               qa_vec3 position, qa_vec3 normal, double time,
                               const qa_tex_coord_context *context);
/* TS stageState (cull NULL means the TS default "front"). */
qa_render_state qa_stage_state(const qa_shader_stage_def *stage, const char *cull);

#endif

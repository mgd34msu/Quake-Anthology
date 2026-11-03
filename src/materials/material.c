/* Port of src/materials/material.ts. GPL-2.0-or-later. */
#include "qa/materials/material.h"

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/materials/source_state.h"
#include "qa/foundation/js_case.h"

#define QA_PI 3.14159265358979323846

/* Allocation registry: every retained block is freed with its result. */
typedef struct storage {
  void **blocks;
  size_t count, cap;
} storage;

static storage *storage_new(void) {
  storage *st = (storage *)calloc(1, sizeof *st);
  if (st == NULL) qa_range_error("shader parser allocation failed");
  return st;
}
static void storage_track(storage *st, void *ptr) {
  if (ptr == NULL) return;
  if (st->count == st->cap) {
    size_t grown = st->cap == 0 ? 32 : st->cap * 2;
    void **blocks = (void **)realloc(st->blocks, grown * sizeof *blocks);
    if (blocks == NULL) qa_range_error("shader parser allocation failed");
    st->blocks = blocks;
    st->cap = grown;
  }
  st->blocks[st->count++] = ptr;
}
static void *s_alloc(storage *st, size_t size) {
  void *ptr = calloc(1, size == 0 ? 1 : size);
  if (ptr == NULL) qa_range_error("shader parser allocation failed");
  storage_track(st, ptr);
  return ptr;
}
static char *s_strdup(storage *st, const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)s_alloc(st, length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}
static void *s_grow(storage *st, const void *old, size_t old_bytes, size_t new_bytes) {
  void *fresh = s_alloc(st, new_bytes);
  if (old != NULL && old_bytes != 0) memcpy(fresh, old, old_bytes);
  return fresh;
}
static void storage_free(storage *st) {
  if (st == NULL) return;
  for (size_t i = st->count; i > 0; i--) free(st->blocks[i - 1]);
  free(st->blocks);
  free(st);
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) qa_range_error("shader parser allocation failed");
  memcpy(copy, text, length + 1);
  return copy;
}

static float f32(double value) { return (float)value; }
static bool kind_is(const char *kind, const char *name) {
  return kind != NULL && strcmp(kind, name) == 0;
}
static void ascii_lower(char *text) {
  for (; *text != '\0'; text++) {
    if (*text >= 'A' && *text <= 'Z') *text = (char)(*text + 32);
  }
}

/* TS stripShaderExtension / normalizeShaderName / shaderNameHash /
 * sameShaderName. */
char *qa_strip_shader_extension(const char *name) {
  const char *dot = strchr(name, '.');
  if (dot == NULL) return xstrdup(name);
  size_t length = (size_t)(dot - name);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) qa_range_error("shader parser allocation failed");
  memcpy(copy, name, length);
  copy[length] = '\0';
  return copy;
}
char *qa_normalize_shader_name(const char *name) {
  char *copy = qa_strip_shader_extension(name);
  ascii_lower(copy);
  return copy;
}
int64_t qa_shader_name_hash(const char *name, int64_t size) {
  int32_t hash = 0;
  size_t length = strlen(name);
  for (size_t index = 0; index < length; index++) {
    int32_t letter = (unsigned char)name[index];
    if (letter == 0 || letter == 46) break;
    if (letter >= 65 && letter <= 90) letter += 32;
    if (letter == 92) letter = 47;
    if (letter >= 128 && letter <= 255) letter -= 256;
    hash = (int32_t)((uint32_t)hash +
                     (uint32_t)(letter * (int32_t)(index + 119)));
  }
  return (hash ^ (hash >> 10) ^ (hash >> 20)) & (int32_t)(size - 1);
}
bool qa_same_shader_name(const char *first, const char *second) {
  size_t first_length = strlen(first), second_length = strlen(second);
  for (size_t index = 0; index < 99999; index++) {
    int a = index < first_length ? (unsigned char)first[index] : 0;
    int b = index < second_length ? (unsigned char)second[index] : 0;
    if (a >= 65 && a <= 90) a += 32;
    if (b >= 65 && b <= 90) b += 32;
    if (a != b) return false;
    if (a == 0) return true;
  }
  return true;
}

/* TS normalizeImageName (JS toLowerCase; ASCII folding here). */
static char *normalize_image_name(storage *st, const char *name) {
  if (name[0] == '*') return s_strdup(st, name);
  char *copy = s_strdup(st, name);
  for (char *p = copy; *p != '\0'; p++) {
    if (*p == '\\') *p = '/';
    if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
  }
  return copy;
}

static qa_mat_wave_storage source_wave(qa_waveform wave) {
  int func = QA_MAT_WAVE_SIN;
  if (kind_is(wave.kind, "none")) func = QA_MAT_WAVE_NONE;
  else if (kind_is(wave.kind, "sin")) func = QA_MAT_WAVE_SIN;
  else if (kind_is(wave.kind, "square")) func = QA_MAT_WAVE_SQUARE;
  else if (kind_is(wave.kind, "triangle")) func = QA_MAT_WAVE_TRIANGLE;
  else if (kind_is(wave.kind, "sawtooth")) func = QA_MAT_WAVE_SAWTOOTH;
  else if (kind_is(wave.kind, "inversesawtooth")) func = QA_MAT_WAVE_INVERSE_SAWTOOTH;
  else if (kind_is(wave.kind, "noise")) func = QA_MAT_WAVE_NOISE;
  qa_mat_wave_storage out = {func, wave.base, wave.amplitude, wave.phase, wave.frequency};
  return out;
}
static int source_color_generator(qa_color_generator gen) {
  if (kind_is(gen.kind, "identitylighting")) return QA_MAT_CGEN_IDENTITY_LIGHTING;
  if (kind_is(gen.kind, "identity")) return QA_MAT_CGEN_IDENTITY;
  if (kind_is(gen.kind, "entity")) return QA_MAT_CGEN_ENTITY;
  if (kind_is(gen.kind, "oneminusentity")) return QA_MAT_CGEN_ONE_MINUS_ENTITY;
  if (kind_is(gen.kind, "exactvertex")) return QA_MAT_CGEN_EXACT_VERTEX;
  if (kind_is(gen.kind, "vertex")) return QA_MAT_CGEN_VERTEX;
  if (kind_is(gen.kind, "oneminusvertex")) return QA_MAT_CGEN_ONE_MINUS_VERTEX;
  if (kind_is(gen.kind, "wave")) return QA_MAT_CGEN_WAVEFORM;
  if (kind_is(gen.kind, "lightingdiffuse")) return QA_MAT_CGEN_LIGHTING_DIFFUSE;
  return QA_MAT_CGEN_CONST;
}
static int source_alpha_generator(qa_alpha_generator gen) {
  if (kind_is(gen.kind, "identity")) return QA_MAT_AGEN_IDENTITY;
  if (kind_is(gen.kind, "entity")) return QA_MAT_AGEN_ENTITY;
  if (kind_is(gen.kind, "oneminusentity")) return QA_MAT_AGEN_ONE_MINUS_ENTITY;
  if (kind_is(gen.kind, "vertex")) return QA_MAT_AGEN_VERTEX;
  if (kind_is(gen.kind, "oneminusvertex")) return QA_MAT_AGEN_ONE_MINUS_VERTEX;
  if (kind_is(gen.kind, "lightingspecular")) return QA_MAT_AGEN_LIGHTING_SPECULAR;
  if (kind_is(gen.kind, "wave")) return QA_MAT_AGEN_WAVEFORM;
  if (kind_is(gen.kind, "portal")) return QA_MAT_AGEN_PORTAL;
  return QA_MAT_AGEN_CONST;
}
static int source_tex_coord_generator(qa_tex_coord_generator gen) {
  if (kind_is(gen.kind, "texture")) return QA_MAT_TCGEN_TEXTURE;
  if (kind_is(gen.kind, "lightmap")) return QA_MAT_TCGEN_LIGHTMAP;
  if (kind_is(gen.kind, "environment")) return QA_MAT_TCGEN_ENVIRONMENT_MAPPED;
  return QA_MAT_TCGEN_VECTOR;
}

/* TS shaderTableIndex / evaluateWaveform. */
static int32_t shader_table_index(double value) {
  double integer = trunc(value);
  /* The source's float-to-int conversion yields the x86 "integer indefinite"
     (INT_MIN) here, which masks to zero. */
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    return 0;
  return (int32_t)integer & 1023;
}
double qa_evaluate_waveform(qa_waveform wave, double time) {
  int32_t index = shader_table_index(
      (double)f32((double)f32(wave.phase + (double)f32(time * wave.frequency)) * 1024.0));
  double value;
  if (kind_is(wave.kind, "none")) qa_range_error("EvalWaveForm does not support GF_NONE");
  else if (kind_is(wave.kind, "sin")) value = qa_renderer_sine(index);
  else if (kind_is(wave.kind, "square")) value = index < 512 ? 1 : -1;
  else if (kind_is(wave.kind, "sawtooth")) value = index / 1024.0;
  else if (kind_is(wave.kind, "inversesawtooth")) value = 1 - index / 1024.0;
  else if (kind_is(wave.kind, "triangle"))
    value = index < 256 ? index / 256.0 : index < 768 ? 2 - index / 256.0 : index / 256.0 - 4;
  else
    qa_range_error("Noise waveforms require the renderer's seeded R_NoiseGet4f context; EvalWaveForm does not support GF_NOISE");
  return f32(wave.base + (double)f32(value * wave.amplitude));
}

qa_vec2 qa_evaluate_tex_coords(const qa_shader_stage_def *stage, qa_vec2 uv,
                               qa_vec3 position, qa_vec3 normal, double time,
                               const qa_tex_coord_context *context) {
  qa_tex_coord_context empty = {false, {0, 0}, false, {0, 0, 0}, false, {0, 0}};
  if (context == NULL) context = &empty;
  qa_vec2 result;
  if (kind_is(stage->tc_gen.kind, "texture")) result = uv;
  else if (kind_is(stage->tc_gen.kind, "lightmap")) {
    if (!context->has_lightmap)
      qa_range_error("Lightmap tcGen requires lightmap coordinates");
    result = context->lightmap;
  } else if (kind_is(stage->tc_gen.kind, "vector")) {
    result.x = qa_dot3(position, stage->tc_gen.s);
    result.y = qa_dot3(position, stage->tc_gen.t);
  } else {
    if (!context->has_view_origin)
      qa_range_error("Environment tcGen requires the view origin in model coordinates");
    qa_vec3 delta = qa_sub3(context->view_origin, position);
    qa_vec3 viewer = qa_normalize_fast3(delta);
    double d = qa_dot3(normal, viewer);
    double reflected_y = f32((double)f32((double)f32(normal.y * 2) * d) - viewer.y);
    double reflected_z = f32((double)f32((double)f32(normal.z * 2) * d) - viewer.z);
    result.x = 0.5 + reflected_y * 0.5;
    result.y = 0.5 - reflected_z * 0.5;
  }
  result.x = f32(result.x);
  result.y = f32(result.y);
  for (size_t i = 0; i < stage->tc_mod_count; i++) {
    const qa_mat_tc_modifier *mod = &stage->tc_mods[i];
    double s = result.x, t = result.y;
    if (kind_is(mod->kind, "none")) {
      qa_vec2 out = {f32(result.x), f32(result.y)};
      return out;
    } else if (kind_is(mod->kind, "scale")) {
      result.x = s * mod->amount.x;
      result.y = t * mod->amount.y;
    } else if (kind_is(mod->kind, "scroll")) {
      double x = f32(mod->amount.x * time), y = f32(mod->amount.y * time);
      result.x = s + f32(x - floor(x));
      result.y = t + f32(y - floor(y));
    } else if (kind_is(mod->kind, "entitytranslate")) {
      if (!context->has_shader_tex_coord)
        qa_range_error("entityTranslate requires entity shaderTexCoord");
      double x = f32(context->shader_tex_coord.x * time);
      double y = f32(context->shader_tex_coord.y * time);
      result.x = s + f32(x - floor(x));
      result.y = t + f32(y - floor(y));
    } else if (kind_is(mod->kind, "transform")) {
      result.x = f32((double)f32(s * mod->m00) + (double)f32(t * mod->m10)) + mod->translation.x;
      result.y = f32((double)f32(s * mod->m01) + (double)f32(t * mod->m11)) + mod->translation.y;
    } else if (kind_is(mod->kind, "rotate")) {
      int32_t index = shader_table_index(
          (double)f32((double)f32(-mod->degrees_per_second * time) * (double)f32(1024.0 / 360.0)));
      double sine = qa_renderer_sine(index), cosine = qa_renderer_sine(index + 256);
      result.x = f32((double)f32(s * cosine) + (double)f32(t * -sine)) +
                 f32(0.5 - 0.5 * cosine + 0.5 * sine);
      result.y = f32((double)f32(s * sine) + (double)f32(t * cosine)) +
                 f32(0.5 - 0.5 * sine - 0.5 * cosine);
    } else if (kind_is(mod->kind, "stretch")) {
      double scale = f32(1.0 / qa_evaluate_waveform(mod->wave, time));
      double translate = f32(0.5 - (double)f32(0.5 * scale));
      result.x = f32((double)f32(s * scale) + (double)f32(t * 0)) + translate;
      result.y = f32((double)f32(s * 0) + (double)f32(t * scale)) + translate;
    } else if (kind_is(mod->kind, "turb")) {
      double now = f32(mod->wave.phase + (double)f32(time * mod->wave.frequency));
      int32_t sx = shader_table_index(((double)f32(position.x + position.z) / 1024.0 + now) * 1024.0);
      int32_t sy = shader_table_index((position.y / 1024.0 + now) * 1024.0);
      result.x = s + f32(qa_renderer_sine(sx) * mod->wave.amplitude);
      result.y = t + f32(qa_renderer_sine(sy) * mod->wave.amplitude);
    }
    result.x = f32(result.x);
    result.y = f32(result.y);
  }
  /* finiteOrZero: texture coordinates stay finite for upload. */
  double out_x = f32(result.x), out_y = f32(result.y);
  qa_vec2 out = {isfinite(out_x) ? out_x : 0, isfinite(out_y) ? out_y : 0};
  return out;
}

qa_render_state qa_stage_state(const qa_shader_stage_def *stage, const char *cull) {
  qa_render_state state;
  state.blend = stage->blend;
  state.depth_test = stage->depth_func;
  state.depth_write = stage->depth_write;
  state.alpha_test = stage->alpha_func;
  state.cull = cull == NULL ? "front" : cull;
  state.depth_range[0] = 0;
  state.depth_range[1] = 1;
  state.polygon_offset = NULL;
  return state;
}

/* Registration driver state (TS registerShader locals). */
typedef struct working_bundle {
  bool has_image[8];
  qa_mat_registered_image images[8];
  int animation_count;
  double animation_frequency;
  bool video_enabled;
  bool has_video;
  qa_mat_registered_video video;
} working_bundle;
typedef struct working_sky_box {
  bool has[6];
  qa_mat_finished_stage_image image[6];
} working_sky_box;
typedef struct driver {
  qa_shader_registration_host *host;
  working_bundle bundles[8];
  working_sky_box outer, inner;
  double cloud_height;
  bool missing;
  qa_mat_source_image_request missing_request;
  qa_shader_definition missing_definition;
  jmp_buf early;
} driver;

typedef struct token {
  char *value;
  int64_t line, column;
  bool quoted;
} token;

typedef struct parser parser;
typedef void (*warn_fn)(parser *p, const char *message);

typedef struct def_build {
  qa_mat_parsed_stage stages[8];
  size_t stage_count;
  char **surface_parms;
  size_t surface_count, surface_cap;
  qa_vertex_deformation deforms[3];
  size_t deform_count;
  qa_mat_compiler_directive *directives;
  size_t directive_count, directive_cap;
  const char *cull;
  bool has_sort;
  double sort;
  bool has_sky;
  qa_mat_shader_sky_params sky;
  bool has_fog;
  qa_q3_material_fog fog;
  bool has_sun;
  qa_q3_material_sun sun;
  bool polygon_offset, no_mip_maps, no_pic_mip, entity_mergable;
  double clamp_time;
  char *name;
  bool has_current;
  qa_mat_parsed_stage current;
} def_build;

struct parser {
  bool common_mode;
  qa_tokenizer tokenizer;
  bool tokenizer_owned;
  qa_common_parse_state common;
  qa_common_parse_cursor *cursor;
  bool has_offset;
  size_t offset;
  bool owns_cursor;
  qa_common_parse_cursor owned_cursor;
  token pending;
  bool has_pending;
  token location;
  int depth;
  qa_shader_diagnostic *warnings;
  size_t warning_count, warning_cap;
  double portal_range;
  char *shader_name;
  const char *source_name;
  const char *registration_name;
  void (*print_warning)(void *ctx, const char *message);
  void *print_warning_ctx;
  driver *exec;
  storage *result, *scratch;
  jmp_buf *catch_buf;
  qa_shader_parse_error error;
  def_build def;
};

static char *s_format(storage *st, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("shader parser allocation failed");
  char *text = (char *)s_alloc(st, (size_t)length + 1);
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  return text;
}

static void set_error(parser *p, const char *message, const char *reason) {
  p->error.source = s_strdup(p->result, p->source_name);
  p->error.line = p->location.line;
  p->error.column = p->location.column;
  p->error.message = s_strdup(p->result, message);
  p->error.reason = reason;
  p->error.formatted = s_format(p->result, "%s:%lld:%lld: %s", p->source_name,
                                (long long)p->location.line,
                                (long long)p->location.column, message);
}
static _Noreturn void fail_with(parser *p, const char *message, const char *reason) {
  set_error(p, message, reason);
  if (p->catch_buf != NULL) longjmp(*p->catch_buf, 1);
  qa_range_error("%s", p->error.formatted);
}
static _Noreturn void p_fail(parser *p, const char *message) {
  fail_with(p, message, "source-rejection");
}
static void p_warn(parser *p, const char *message) {
  if (p->warning_count == p->warning_cap) {
    size_t grown = p->warning_cap == 0 ? 8 : p->warning_cap * 2;
    p->warnings = (qa_shader_diagnostic *)s_grow(
        p->scratch, p->warnings, p->warning_count * sizeof *p->warnings,
        grown * sizeof *p->warnings);
    p->warning_cap = grown;
  }
  qa_shader_diagnostic *d = &p->warnings[p->warning_count++];
  d->source = s_strdup(p->result, p->source_name);
  d->line = (double)p->location.line;
  d->column = (double)p->location.column;
  d->message = s_strdup(p->result, message);
  if (p->print_warning != NULL) p->print_warning(p->print_warning_ctx, message);
}
static _Noreturn void p_reject(parser *p, const char *message) {
  char *copy = s_strdup(p->scratch, message);
  p_warn(p, copy);
  p_fail(p, copy);
}

static bool source_next(parser *p, bool allow_line_breaks, token *out) {
  if (!p->common_mode) {
    qa_text_token raw;
    if (!qa_tokenizer_next(&p->tokenizer, allow_line_breaks, &raw)) return false;
    storage_track(p->scratch, raw.value);
    out->value = raw.value;
    out->line = raw.line;
    out->column = raw.column;
    out->quoted = raw.quoted;
    return true;
  }
  if (p->has_offset) qa_common_parse_cursor_set_offset(p->cursor, p->offset);
  else qa_common_parse_cursor_set_null(p->cursor);
  const char *value = qa_common_parse_state_parse(&p->common, p->cursor, allow_line_breaks);
  p->has_offset = p->cursor->has_offset;
  p->offset = p->cursor->offset;
  if (value == NULL || value[0] == '\0') return false;
  out->value = s_strdup(p->scratch, value);
  out->line = (int64_t)p->common.line + 1;
  out->column = 1;
  out->quoted = false;
  return true;
}
static bool p_next(parser *p, bool allow_line_breaks, token *out) {
  bool was_pending = p->has_pending;
  token value;
  if (was_pending) value = p->pending;
  else if (!source_next(p, allow_line_breaks, &value)) return false;
  p->has_pending = false;
  p->location = value;
  if (!was_pending) {
    if (value.value[0] == '{') p->depth++;
    if (value.value[0] == '}') p->depth--;
  }
  *out = value;
  return true;
}
static void p_push_pending(parser *p, token value) {
  p->pending = value;
  p->has_pending = true;
}
static const char *stage_parameter(parser *p, const char *keyword) {
  token value;
  if (!p_next(p, false, &value)) {
    p_reject(p, s_format(p->scratch,
                         "WARNING: missing parameter for '%s' keyword in shader '%s'\n",
                         keyword, p->shader_name));
  }
  return value.value;
}
static const char *required_token(parser *p, bool allow_line_breaks) {
  token value;
  if (!p_next(p, allow_line_breaks, &value)) p_fail(p, "Missing shader parameter");
  return value.value;
}
static double source_atof(const char *value) { return f32(qa_native_atof(value)); }
static double p_number(parser *p) { return source_atof(required_token(p, false)); }
typedef struct vector_result { qa_vec3 value; bool complete; } vector_result;
static vector_result source_vector(parser *p, qa_vec3 previous) {
  token value;
  vector_result out;
  if (!p_next(p, false, &value) || strcmp(value.value, "(") != 0) {
    p_warn(p, s_format(p->scratch, "WARNING: missing parenthesis in shader '%s'\n",
                       p->shader_name));
    out.value = previous;
    out.complete = false;
    return out;
  }
  qa_vec3 current = previous;
  double *fields[3] = {&current.x, &current.y, &current.z};
  for (int i = 0; i < 3; i++) {
    if (!p_next(p, false, &value)) {
      p_warn(p, s_format(p->scratch,
                         "WARNING: missing vector element in shader '%s'\n",
                         p->shader_name));
      out.value = current;
      out.complete = false;
      return out;
    }
    *fields[i] = source_atof(value.value);
  }
  if (!p_next(p, false, &value) || strcmp(value.value, ")") != 0) {
    p_warn(p, s_format(p->scratch, "WARNING: missing parenthesis in shader '%s'\n",
                       p->shader_name));
    out.value = current;
    out.complete = false;
    return out;
  }
  out.value = current;
  out.complete = true;
  return out;
}
static const char *source_line_value(parser *p, bool *has) {
  token value;
  if (!p_next(p, false, &value)) {
    *has = false;
    return NULL;
  }
  *has = true;
  return value.value;
}
/* TS lineArguments; returns result-storage strings. */
static qa_string_view_list line_arguments(parser *p) {
  qa_string_view_list out = {NULL, 0};
  if (p->common_mode) {
    if (p->has_offset) qa_common_parse_cursor_set_offset(p->cursor, p->offset);
    else qa_common_parse_cursor_set_null(p->cursor);
    qa_common_parse_state_skip_rest_of_line(&p->common, p->cursor);
    p->has_offset = p->cursor->has_offset;
    p->offset = p->cursor->offset;
    return out;
  }
  const char **args = NULL;
  size_t count = 0, cap = 0;
  for (;;) {
    token value;
    if (!p_next(p, false, &value)) break;
    if (strcmp(value.value, "}") == 0 || strcmp(value.value, "{") == 0) {
      p_push_pending(p, value);
      break;
    }
    if (count == cap) {
      size_t grown = cap == 0 ? 4 : cap * 2;
      args = (const char **)s_grow(p->result, args, count * sizeof *args,
                                   grown * sizeof *args);
      cap = grown;
    }
    args[count++] = s_strdup(p->result, value.value);
  }
  out.values = args;
  out.count = count;
  return out;
}
/* TS token.toLowerCase(): Unicode lowering of shader text. */
static char *lower_copy(parser *p, const char *text) {
  char *lowered = qa_js_to_lower_case(text);
  if (lowered == NULL) qa_range_error("Out of memory");
  char *copy = s_strdup(p->scratch, lowered);
  free(lowered);
  return copy;
}

/* Executor: TS RegistrationInstruction handling in registerShader. */
static const char *const sky_faces[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
static void host_warn(parser *p, const char *message) {
  if (p->exec != NULL && p->exec->host->print_warning != NULL)
    p->exec->host->print_warning(p->exec->host->ctx, message);
}
static working_bundle *bundle_at(parser *p, int stage) {
  if (stage < 0 || stage >= 8)
    qa_range_error("shader stage slot %d is unavailable", stage);
  return &p->exec->bundles[stage];
}
static void exec_find_stage_image(parser *p, qa_mat_source_image_request request,
                                  bool animation, int stage, int frame,
                                  int count_after, qa_shader_definition missing) {
  if (p->exec == NULL) return;
  qa_mat_registered_image image;
  memset(&image, 0, sizeof image);
  bool found = p->exec->host->find_image != NULL &&
               p->exec->host->find_image(p->exec->host->ctx, request, &image);
  working_bundle *bundle = bundle_at(p, stage);
  int slot = animation ? frame : 0;
  bundle->has_image[slot] = found;
  if (found) bundle->images[slot] = image;
  if (!found) {
    host_warn(p, s_format(p->scratch,
                          "WARNING: R_FindImageFile could not find '%s' in shader '%s'\n",
                          request.name, missing.name));
    p->exec->missing = true;
    p->exec->missing_request = request;
    p->exec->missing_definition = missing;
    longjmp(p->exec->early, 1);
  }
  if (animation) bundle->animation_count = count_after;
}
static void exec_set_animation_frequency(parser *p, int stage, double frequency) {
  if (p->exec != NULL) bundle_at(p, stage)->animation_frequency = frequency;
}
static void exec_bind(parser *p, int stage, bool lightmap) {
  if (p->exec == NULL) return;
  working_bundle *bundle = bundle_at(p, stage);
  bundle->has_image[0] = true;
  bundle->images[0] = lightmap ? p->exec->host->lightmap_image : p->exec->host->white_image;
}
static void exec_play_video(parser *p, int stage, const char *name) {
  if (p->exec == NULL) return;
  qa_mat_registered_video video;
  memset(&video, 0, sizeof video);
  bool found = p->exec->host->play_shader_cinematic != NULL &&
               p->exec->host->play_shader_cinematic(p->exec->host->ctx, name, &video);
  working_bundle *bundle = bundle_at(p, stage);
  bundle->has_video = found;
  if (found) {
    bundle->video = video;
    bundle->video_enabled = true;
    bundle->has_image[0] = true;
    bundle->images[0] = video.image;
  }
}
static void exec_find_sky_image(parser *p, bool outer, int face,
                                qa_mat_source_image_request request) {
  if (p->exec == NULL) return;
  qa_mat_registered_image image;
  memset(&image, 0, sizeof image);
  bool found = p->exec->host->find_image != NULL &&
               p->exec->host->find_image(p->exec->host->ctx, request, &image);
  working_sky_box *box = outer ? &p->exec->outer : &p->exec->inner;
  box->has[face] = true;
  box->image[face] = found ? image.frame : p->exec->host->default_image.frame;
}
static void exec_initialize_sky(parser *p, double height) {
  if (p->exec == NULL) return;
  p->exec->cloud_height = height;
  if (p->exec->host->initialize_sky_tex_coords != NULL)
    p->exec->host->initialize_sky_tex_coords(p->exec->host->ctx, height);
}
static void exec_apply_sun(parser *p, qa_mat_registered_sun sun) {
  if (p->exec != NULL && p->exec->host->apply_sun != NULL)
    p->exec->host->apply_sun(p->exec->host->ctx, sun);
}
static void exec_drop(parser *p, const char *message) {
  if (p->exec != NULL) qa_common_error(QA_COMMON_ERROR_DROP, message);
}

typedef struct sun_directive { qa_q3_material_sun source; qa_mat_registered_sun registered; } sun_directive;
static sun_directive p_sun_directive(parser *p) {
  bool has;
  const char *v;
  v = source_line_value(p, &has);
  double red = source_atof(has ? v : "");
  v = source_line_value(p, &has);
  double green = source_atof(has ? v : "");
  v = source_line_value(p, &has);
  double blue = source_atof(has ? v : "");
  v = source_line_value(p, &has);
  double intensity = source_atof(has ? v : "");
  v = source_line_value(p, &has);
  double azimuth = source_atof(has ? v : "");
  v = source_line_value(p, &has);
  double elevation = source_atof(has ? v : "");
  qa_vec3 color = {red, green, blue};
  qa_vec3 light = qa_scale3(qa_normalize3(color), intensity);
  double azimuth_rad = f32((double)f32(azimuth / 180.0) * QA_PI);
  double elevation_rad = f32((double)f32(elevation / 180.0) * QA_PI);
  qa_vec3 direction = {f32(cos(azimuth_rad) * cos(elevation_rad)),
                       f32(sin(azimuth_rad) * cos(elevation_rad)),
                       f32(sin(elevation_rad))};
  sun_directive out;
  out.source.color = color;
  out.source.intensity = intensity;
  out.source.azimuth = azimuth;
  out.source.elevation = elevation;
  out.registered.light = light;
  out.registered.direction = direction;
  return out;
}
static void sky_images(parser *p, bool outer, const char *base, const char *wrap) {
  for (int face = 0; face < 6; face++) {
    qa_mat_source_image_request request;
    request.name = s_format(p->result, "%s_%s.tga", base, sky_faces[face]);
    request.mipmap = true;
    request.allow_picmip = true;
    request.wrap = wrap;
    exec_find_sky_image(p, outer, face, request);
  }
}
/* TS skyDirective; returns has_sky in p->def terms via out params. */
static void sky_directive(parser *p, bool has_previous, qa_mat_shader_sky_params previous,
                          bool *has_out, qa_mat_shader_sky_params *out) {
  token outer_token;
  if (!p_next(p, false, &outer_token)) {
    p_warn(p, s_format(p->scratch,
                       "WARNING: 'skyParms' missing parameter in shader '%s'\n",
                       p->shader_name));
    *has_out = has_previous;
    *out = previous;
    return;
  }
  bool outer_dash = strcmp(outer_token.value, "-") == 0;
  const char *outer_name = outer_dash ? (has_previous ? previous.outer_box : NULL)
                                      : normalize_image_name(p->result, outer_token.value);
  if (!outer_dash) sky_images(p, true, outer_token.value, "clamp");
  token height_token;
  if (!p_next(p, false, &height_token)) {
    p_warn(p, s_format(p->scratch,
                       "WARNING: 'skyParms' missing parameter in shader '%s'\n",
                       p->shader_name));
    if (!has_previous) {
      *has_out = false;
      memset(out, 0, sizeof *out);
      return;
    }
    *has_out = true;
    *out = previous;
    out->outer_box = outer_name;
    return;
  }
  double parsed_height = source_atof(height_token.value);
  double cloud_height = parsed_height == 0 ? 512 : parsed_height;
  exec_initialize_sky(p, cloud_height);
  token inner_token;
  if (!p_next(p, false, &inner_token)) {
    p_warn(p, s_format(p->scratch,
                       "WARNING: 'skyParms' missing parameter in shader '%s'\n",
                       p->shader_name));
    if (!has_previous) {
      *has_out = false;
      memset(out, 0, sizeof *out);
      return;
    }
    *has_out = true;
    *out = previous;
    out->outer_box = outer_name;
    out->cloud_height = cloud_height;
    return;
  }
  bool inner_dash = strcmp(inner_token.value, "-") == 0;
  const char *inner_name = inner_dash ? (has_previous ? previous.inner_box : NULL)
                                      : normalize_image_name(p->result, inner_token.value);
  if (!inner_dash) sky_images(p, false, inner_token.value, "repeat");
  *has_out = true;
  out->outer_box = outer_name;
  out->cloud_height = cloud_height;
  out->inner_box = inner_name;
}

static qa_waveform p_wave(parser *p, bool has_previous, qa_waveform previous,
                          const char *missing_or_null) {
  char *missing = missing_or_null != NULL
                      ? s_strdup(p->scratch, missing_or_null)
                      : s_format(p->scratch,
                                 "WARNING: missing waveform parm in shader '%s'\n",
                                 p->shader_name);
  token value;
  if (!p_next(p, false, &value)) {
    p_warn(p, missing);
    if (!has_previous) p_fail(p, missing);
    return previous;
  }
  char *name = lower_copy(p, value.value);
  const char *kind;
  if (strcmp(name, "sin") == 0) kind = "sin";
  else if (strcmp(name, "square") == 0) kind = "square";
  else if (strcmp(name, "triangle") == 0) kind = "triangle";
  else if (strcmp(name, "sawtooth") == 0) kind = "sawtooth";
  else if (strcmp(name, "inversesawtooth") == 0) kind = "inversesawtooth";
  else if (strcmp(name, "noise") == 0) kind = "noise";
  else {
    p_warn(p, s_format(p->scratch,
                       "WARNING: invalid genfunc name '%s' in shader '%s'\n",
                       value.value, p->shader_name));
    kind = "sin";
  }
  qa_waveform result = has_previous ? previous : (qa_waveform){kind, 0, 0, 0, 0};
  result.kind = kind;
  double *fields[4] = {&result.base, &result.amplitude, &result.phase, &result.frequency};
  for (int i = 0; i < 4; i++) {
    token field;
    if (!p_next(p, false, &field)) {
      p_warn(p, missing);
      if (!has_previous) p_fail(p, missing);
      return result;
    }
    p_push_pending(p, field);
    *fields[i] = p_number(p);
  }
  return result;
}

static bool p_rgb(parser *p, qa_waveform previous_wave, qa_color_generator *out) {
  token value;
  if (!p_next(p, false, &value)) {
    p_warn(p, s_format(p->scratch,
                       "WARNING: missing parameters for rgbGen in shader '%s'\n",
                       p->shader_name));
    return false;
  }
  char *kind = lower_copy(p, value.value);
  memset(out, 0, sizeof *out);
  if (strcmp(kind, "identity") == 0 || strcmp(kind, "identitylighting") == 0 ||
      strcmp(kind, "entity") == 0 || strcmp(kind, "oneminusentity") == 0 ||
      strcmp(kind, "vertex") == 0 || strcmp(kind, "exactvertex") == 0 ||
      strcmp(kind, "lightingdiffuse") == 0 || strcmp(kind, "oneminusvertex") == 0) {
    out->kind = s_strdup(p->result, kind);
    return true;
  }
  if (strcmp(kind, "const") == 0) {
    out->kind = "const";
    out->color = source_vector(p, (qa_vec3){0, 0, 0}).value;
    return true;
  }
  if (strcmp(kind, "wave") == 0) {
    out->kind = "wave";
    out->wave = p_wave(p, true, previous_wave, NULL);
    return true;
  }
  p_warn(p, s_format(p->scratch,
                     "WARNING: unknown rgbGen parameter '%s' in shader '%s'\n",
                     value.value, p->shader_name));
  return false;
}
static bool p_alpha(parser *p, qa_waveform previous_wave, qa_alpha_generator *out) {
  token value;
  if (!p_next(p, false, &value)) {
    p_warn(p, s_format(p->scratch,
                       "WARNING: missing parameters for alphaGen in shader '%s'\n",
                       p->shader_name));
    return false;
  }
  char *kind = lower_copy(p, value.value);
  memset(out, 0, sizeof *out);
  if (strcmp(kind, "identity") == 0 || strcmp(kind, "entity") == 0 ||
      strcmp(kind, "oneminusentity") == 0 || strcmp(kind, "vertex") == 0 ||
      strcmp(kind, "lightingspecular") == 0 || strcmp(kind, "oneminusvertex") == 0) {
    out->kind = s_strdup(p->result, kind);
    return true;
  }
  if (strcmp(kind, "const") == 0) {
    bool has;
    const char *raw = source_line_value(p, &has);
    double alpha = qa_native_atof(has ? raw : "");
    double integer = trunc(255.0 * alpha);
    if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
      fail_with(p, "alphaGen const reaches undefined source byte conversion",
                "validation");
    out->kind = "const";
    out->alpha = alpha;
    return true;
  }
  if (strcmp(kind, "wave") == 0) {
    out->kind = "wave";
    out->wave = p_wave(p, true, previous_wave, NULL);
    return true;
  }
  if (strcmp(kind, "portal") == 0) {
    token range_token;
    if (!p_next(p, false, &range_token)) {
      p->portal_range = 256;
      p_warn(p, s_format(p->scratch,
                         "WARNING: missing range parameter for alphaGen portal in shader '%s', defaulting to 256\n",
                         p->shader_name));
      out->kind = "portal";
      out->range = 256;
      return true;
    }
    p_push_pending(p, range_token);
    double range = p_number(p);
    p->portal_range = range;
    out->kind = "portal";
    out->range = range;
    return true;
  }
  p_warn(p, s_format(p->scratch,
                     "WARNING: unknown alphaGen parameter '%s' in shader '%s'\n",
                     value.value, p->shader_name));
  return false;
}
static bool p_tcgen(parser *p, qa_tex_coord_generator vectors, qa_tex_coord_generator *out) {
  token value;
  if (!p_next(p, false, &value)) {
    p_warn(p, s_format(p->scratch, "WARNING: missing texgen parm in shader '%s'\n",
                       p->shader_name));
    return false;
  }
  char *kind = lower_copy(p, value.value);
  memset(out, 0, sizeof *out);
  if (strcmp(kind, "environment") == 0 || strcmp(kind, "lightmap") == 0 ||
      strcmp(kind, "texture") == 0) {
    out->kind = s_strdup(p->result, kind);
    return true;
  }
  if (strcmp(kind, "base") == 0) {
    out->kind = "texture";
    return true;
  }
  if (strcmp(kind, "vector") == 0) {
    out->kind = "vector";
    out->s = source_vector(p, vectors.s).value;
    out->t = source_vector(p, vectors.t).value;
    return true;
  }
  p_warn(p, s_format(p->scratch, "WARNING: unknown texgen parm in shader '%s'\n",
                     p->shader_name));
  return false;
}
static double tcmod_number(parser *p, const char *kind, int *count) {
  token value;
  if (p_next(p, false, &value)) {
    (*count)++;
    return source_atof(value.value);
  }
  const char *label = strcmp(kind, "scale") == 0    ? "scale parms"
                      : strcmp(kind, "scroll") == 0 ? "scale scroll parms"
                      : strcmp(kind, "transform") == 0 ? "transform parms"
                      : strcmp(kind, "turb") == 0
                          ? (*count == 0 ? "tcMod turb parms" : "tcMod turb")
                          : "tcMod rotate parms";
  char *message = s_format(p->scratch, "WARNING: missing %s in shader '%s'\n", label,
                           p->shader_name);
  p_warn(p, message);
  p_fail(p, message);
}
static qa_mat_tc_modifier p_tcmod(parser *p) {
  token value;
  const char *raw = p_next(p, false, &value) ? value.value : "";
  char *kind = lower_copy(p, raw);
  int count = 0;
  qa_mat_tc_modifier out;
  memset(&out, 0, sizeof out);
  out.wave.kind = "none";
  if (strcmp(kind, "scale") == 0 || strcmp(kind, "scroll") == 0) {
    out.kind = s_strdup(p->result, kind);
    out.amount.x = tcmod_number(p, kind, &count);
    out.amount.y = tcmod_number(p, kind, &count);
    return out;
  }
  if (strcmp(kind, "stretch") == 0) {
    out.kind = "stretch";
    char *missing = s_format(p->scratch,
                             "WARNING: missing stretch parms in shader '%s'\n",
                             p->shader_name);
    qa_waveform zero = {"none", 0, 0, 0, 0};
    out.wave = p_wave(p, false, zero, missing);
    return out;
  }
  if (strcmp(kind, "turb") == 0) {
    out.kind = "turb";
    out.wave.kind = "sin";
    out.wave.base = tcmod_number(p, kind, &count);
    out.wave.amplitude = tcmod_number(p, kind, &count);
    out.wave.phase = tcmod_number(p, kind, &count);
    out.wave.frequency = tcmod_number(p, kind, &count);
    return out;
  }
  if (strcmp(kind, "rotate") == 0) {
    out.kind = "rotate";
    out.degrees_per_second = tcmod_number(p, kind, &count);
    return out;
  }
  if (strcmp(kind, "transform") == 0) {
    out.kind = "transform";
    out.m00 = tcmod_number(p, kind, &count);
    out.m01 = tcmod_number(p, kind, &count);
    out.m10 = tcmod_number(p, kind, &count);
    out.m11 = tcmod_number(p, kind, &count);
    out.translation.x = tcmod_number(p, kind, &count);
    out.translation.y = tcmod_number(p, kind, &count);
    return out;
  }
  if (strcmp(kind, "entitytranslate") == 0) {
    out.kind = "entitytranslate";
    return out;
  }
  p_warn(p, s_format(p->scratch, "WARNING: unknown tcMod '%s' in shader '%s'\n", raw,
                     p->shader_name));
  out.kind = "none";
  return out;
}

typedef struct blend_factor_result { const char *value; uint32_t bits; } blend_factor_result;
static blend_factor_result blend_factor(parser *p, const char *value, bool destination) {
  char *name = lower_copy(p, value);
  blend_factor_result out;
  if (strcmp(name, "gl_zero") == 0) {
    out.value = "zero";
    out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_ZERO : QA_SOURCE_STATE_SRCBLEND_ZERO;
    return out;
  }
  if (strcmp(name, "gl_one") == 0) {
    out.value = "one";
    out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_ONE : QA_SOURCE_STATE_SRCBLEND_ONE;
    return out;
  }
  if (strcmp(name, "gl_src_alpha") == 0) {
    out.value = "src-alpha";
    out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_SRC_ALPHA : QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA;
    return out;
  }
  if (strcmp(name, "gl_one_minus_src_alpha") == 0) {
    out.value = "one-minus-src-alpha";
    out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA
                           : QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_SRC_ALPHA;
    return out;
  }
  if (strcmp(name, "gl_dst_alpha") == 0) {
    out.value = "dst-alpha";
    out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_DST_ALPHA : QA_SOURCE_STATE_SRCBLEND_DST_ALPHA;
    return out;
  }
  if (strcmp(name, "gl_one_minus_dst_alpha") == 0) {
    out.value = "one-minus-dst-alpha";
    out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_DST_ALPHA
                           : QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_ALPHA;
    return out;
  }
  if (!destination && strcmp(name, "gl_dst_color") == 0) {
    out.value = "dst-color";
    out.bits = QA_SOURCE_STATE_SRCBLEND_DST_COLOR;
    return out;
  }
  if (!destination && strcmp(name, "gl_one_minus_dst_color") == 0) {
    out.value = "one-minus-dst-color";
    out.bits = QA_SOURCE_STATE_SRCBLEND_ONE_MINUS_DST_COLOR;
    return out;
  }
  if (!destination && strcmp(name, "gl_src_alpha_saturate") == 0) {
    out.value = "src-alpha-saturate";
    out.bits = QA_SOURCE_STATE_SRCBLEND_ALPHA_SATURATE;
    return out;
  }
  if (destination && strcmp(name, "gl_src_color") == 0) {
    out.value = "src-color";
    out.bits = QA_SOURCE_STATE_DSTBLEND_SRC_COLOR;
    return out;
  }
  if (destination && strcmp(name, "gl_one_minus_src_color") == 0) {
    out.value = "one-minus-src-color";
    out.bits = QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR;
    return out;
  }
  p_warn(p, s_format(p->scratch,
                     "WARNING: unknown blend mode '%s' in shader '%s', substituting GL_ONE\n",
                     value, p->shader_name));
  out.value = "one";
  out.bits = destination ? QA_SOURCE_STATE_DSTBLEND_ONE : QA_SOURCE_STATE_SRCBLEND_ONE;
  return out;
}
typedef struct blend_result {
  qa_blend_state blend;
  uint32_t source_bits, destination_bits;
  bool complete;
} blend_result;
static blend_result p_blend(parser *p, qa_blend_state previous, uint32_t previous_dst_bits) {
  const char *value = required_token(p, false);
  char *lower = lower_copy(p, value);
  blend_result out;
  if (strcmp(lower, "add") == 0) {
    out.blend.source = "one";
    out.blend.destination = "one";
    out.source_bits = QA_SOURCE_STATE_SRCBLEND_ONE;
    out.destination_bits = QA_SOURCE_STATE_DSTBLEND_ONE;
    out.complete = true;
    return out;
  }
  if (strcmp(lower, "filter") == 0) {
    out.blend.source = "dst-color";
    out.blend.destination = "zero";
    out.source_bits = QA_SOURCE_STATE_SRCBLEND_DST_COLOR;
    out.destination_bits = QA_SOURCE_STATE_DSTBLEND_ZERO;
    out.complete = true;
    return out;
  }
  if (strcmp(lower, "blend") == 0) {
    out.blend.source = "src-alpha";
    out.blend.destination = "one-minus-src-alpha";
    out.source_bits = QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA;
    out.destination_bits = QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA;
    out.complete = true;
    return out;
  }
  blend_factor_result source = blend_factor(p, value, false);
  token second;
  if (!p_next(p, false, &second)) {
    p_warn(p, s_format(p->scratch,
                       "WARNING: missing parm for blendFunc in shader '%s'\n",
                       p->shader_name));
    out.blend.source = source.value;
    out.blend.destination = previous.destination;
    out.source_bits = source.bits;
    out.destination_bits = previous_dst_bits;
    out.complete = false;
    return out;
  }
  blend_factor_result destination = blend_factor(p, second.value, true);
  out.blend.source = source.value;
  out.blend.destination = destination.value;
  out.source_bits = source.bits;
  out.destination_bits = destination.bits;
  out.complete = true;
  return out;
}

static void init_parser_fields(parser *p, storage *result, storage *scratch,
                               const char *source_name, const char *registration_name) {
  memset(p, 0, sizeof *p);
  p->result = result;
  p->scratch = scratch;
  p->source_name = source_name;
  p->registration_name = registration_name;
  p->shader_name = "";
  p->location.value = "";
  p->location.line = 1;
  p->location.column = 1;
  qa_common_parse_state_init(&p->common);
}

static qa_shader_definition snapshot_definition(parser *p, bool include_current) {
  def_build *d = &p->def;
  qa_shader_definition out;
  memset(&out, 0, sizeof out);
  out.name = d->name;
  size_t count = d->stage_count + (include_current && d->has_current ? 1 : 0);
  if (count != 0) {
    qa_mat_parsed_stage *stages =
        (qa_mat_parsed_stage *)s_alloc(p->result, count * sizeof *stages);
    for (size_t i = 0; i < d->stage_count; i++) stages[i] = d->stages[i];
    if (count > d->stage_count) stages[d->stage_count] = d->current;
    out.stages = stages;
  }
  out.stage_count = count;
  if (d->surface_count != 0) {
    const char **parms = (const char **)s_alloc(p->result, d->surface_count * sizeof *parms);
    for (size_t i = 0; i < d->surface_count; i++) parms[i] = d->surface_parms[i];
    out.surface_parms.values = parms;
  }
  out.surface_parms.count = d->surface_count;
  out.cull = d->cull;
  out.has_sort = d->has_sort;
  out.sort = d->sort;
  if (d->has_sky) {
    qa_mat_shader_sky_params *sky =
        (qa_mat_shader_sky_params *)s_alloc(p->result, sizeof *sky);
    *sky = d->sky;
    out.sky = sky;
  }
  if (d->has_fog) {
    qa_q3_material_fog *fog = (qa_q3_material_fog *)s_alloc(p->result, sizeof *fog);
    *fog = d->fog;
    out.fog = fog;
  }
  if (d->has_sun) {
    qa_q3_material_sun *sun = (qa_q3_material_sun *)s_alloc(p->result, sizeof *sun);
    *sun = d->sun;
    out.sun = sun;
  }
  if (d->deform_count != 0) {
    qa_vertex_deformation *deforms =
        (qa_vertex_deformation *)s_alloc(p->result, d->deform_count * sizeof *deforms);
    for (size_t i = 0; i < d->deform_count; i++) deforms[i] = d->deforms[i];
    out.deforms = deforms;
  }
  out.deform_count = d->deform_count;
  out.polygon_offset = d->polygon_offset;
  out.no_mip_maps = d->no_mip_maps;
  out.no_pic_mip = d->no_pic_mip;
  out.entity_mergable = d->entity_mergable;
  out.portal_range = p->portal_range;
  out.clamp_time = d->clamp_time;
  if (p->warning_count != 0) {
    qa_shader_diagnostic *warnings =
        (qa_shader_diagnostic *)s_alloc(p->result, p->warning_count * sizeof *warnings);
    for (size_t i = 0; i < p->warning_count; i++) warnings[i] = p->warnings[i];
    out.warnings = warnings;
  }
  out.warning_count = p->warning_count;
  if (d->directive_count != 0) {
    qa_mat_compiler_directive *directives = (qa_mat_compiler_directive *)s_alloc(
        p->result, d->directive_count * sizeof *directives);
    for (size_t i = 0; i < d->directive_count; i++) {
      directives[i] = d->directives[i];
      if (d->directives[i].arguments.count != 0) {
        const char **args = (const char **)s_alloc(
            p->result, d->directives[i].arguments.count * sizeof *args);
        for (size_t j = 0; j < d->directives[i].arguments.count; j++)
          args[j] = d->directives[i].arguments.values[j];
        directives[i].arguments.values = args;
      }
    }
    out.compiler_directives = directives;
  }
  out.compiler_directive_count = d->directive_count;
  return out;
}
static qa_shader_definition update_partial(parser *p, qa_mat_parsed_stage stage) {
  p->def.current = stage;
  p->def.has_current = true;
  return snapshot_definition(p, true);
}

typedef struct stage_build {
  qa_shader_map map;
  qa_blend_state blend;
  uint32_t source_bits, destination_bits;
  const char *depth_func;
  bool depth_write, explicit_depth_write;
  const char *alpha_func;
  bool detail;
  bool has_rgb;
  qa_color_generator rgb_gen;
  qa_alpha_generator alpha_gen;
  bool has_tc;
  qa_tex_coord_generator tc_gen;
  qa_tex_coord_generator tc_vectors;
  int raw_rgb, raw_alpha, raw_tc;
  qa_waveform rgb_wave, alpha_wave;
  bool is_lightmap;
  int animation_count;
  qa_mat_tc_modifier tc_mods[4];
  size_t tc_mod_count;
} stage_build;

static qa_mat_parsed_stage stage_value(parser *p, stage_build *b, bool completed) {
  qa_color_generator stage_rgb = b->has_rgb ? b->rgb_gen
                                            : (qa_color_generator){"identitylighting", {0, 0, 0}, {"none", 0, 0, 0, 0}};
  int stage_raw_rgb = b->raw_rgb;
  int stage_raw_alpha = b->raw_alpha;
  qa_tex_coord_generator stage_tc = b->tc_gen;
  if (!b->has_tc) {
    memset(&stage_tc, 0, sizeof stage_tc);
    stage_tc.kind = b->is_lightmap ? "lightmap" : "texture";
  }
  qa_blend_state stage_blend = b->blend;
  const char *stage_depth_func = b->depth_func;
  bool stage_depth_write = b->depth_write;
  const char *stage_alpha_func = b->alpha_func;
  uint32_t state_bits = 0;
  if (completed) {
    if (!b->has_rgb) {
      bool lighting = b->source_bits == 0 ||
                      b->source_bits == QA_SOURCE_STATE_SRCBLEND_ONE ||
                      b->source_bits == QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA;
      memset(&stage_rgb, 0, sizeof stage_rgb);
      stage_rgb.kind = lighting ? "identitylighting" : "identity";
      stage_raw_rgb = source_color_generator(stage_rgb);
    }
    uint32_t blend_bits = b->source_bits | b->destination_bits;
    if (b->source_bits == QA_SOURCE_STATE_SRCBLEND_ONE &&
        b->destination_bits == QA_SOURCE_STATE_DSTBLEND_ZERO) {
      blend_bits = 0;
      stage_depth_write = true;
    }
    qa_source_state_input input = {b->depth_func, stage_depth_write, false, NULL, NULL,
                                   stage_alpha_func};
    state_bits = blend_bits | qa_source_state_bits(input, "fill", true);
    if (stage_raw_alpha == QA_MAT_AGEN_ENTITY &&
        (stage_raw_rgb == QA_MAT_CGEN_IDENTITY ||
         stage_raw_rgb == QA_MAT_CGEN_LIGHTING_DIFFUSE))
      stage_raw_alpha = QA_MAT_AGEN_SKIP;
  } else {
    stage_blend.source = "one";
    stage_blend.destination = "zero";
    stage_depth_func = "less-equal";
    stage_depth_write = false;
    stage_alpha_func = "none";
  }
  qa_mat_parsed_stage out;
  memset(&out, 0, sizeof out);
  out.stage.map = b->map;
  if (b->map.frames.count != 0) {
    const char **frames =
        (const char **)s_alloc(p->result, b->map.frames.count * sizeof *frames);
    for (size_t i = 0; i < b->map.frames.count; i++) frames[i] = b->map.frames.values[i];
    out.stage.map.frames.values = frames;
  }
  out.stage.blend = stage_blend;
  out.stage.depth_func = stage_depth_func;
  out.stage.depth_write = stage_depth_write;
  out.stage.alpha_func = stage_alpha_func;
  out.stage.detail = b->detail;
  out.stage.rgb_gen = stage_rgb;
  out.stage.alpha_gen = b->alpha_gen;
  out.stage.tc_gen = stage_tc;
  if (b->tc_mod_count != 0) {
    qa_mat_tc_modifier *mods =
        (qa_mat_tc_modifier *)s_alloc(p->result, b->tc_mod_count * sizeof *mods);
    for (size_t i = 0; i < b->tc_mod_count; i++) mods[i] = b->tc_mods[i];
    out.stage.tc_mods = mods;
  }
  out.stage.tc_mod_count = b->tc_mod_count;
  out.source_state.active = true;
  out.source_state.state_bits = state_bits;
  out.source_state.rgb_gen = stage_raw_rgb;
  out.source_state.alpha_gen = stage_raw_alpha;
  out.source_state.tc_gen = b->raw_tc;
  out.source_state.rgb_wave = source_wave(b->rgb_wave);
  out.source_state.alpha_wave = source_wave(b->alpha_wave);
  out.source_state.is_lightmap = b->is_lightmap;
  out.source_state.vertex_lightmap = false;
  return out;
}

static qa_mat_source_image_request image_request(parser *p, const char *name,
                                                 const char *wrap) {
  qa_mat_source_image_request request;
  request.name = s_strdup(p->result, name);
  request.mipmap = !p->def.no_mip_maps;
  request.allow_picmip = !p->def.no_pic_mip;
  request.wrap = wrap;
  return request;
}
static void sub_warn_trampoline(void *ctx, const char *message) {
  p_warn((parser *)ctx, message);
}

static qa_mat_parsed_stage parse_stage(parser *p, int slot) {
  if (slot < 0 || slot >= 8) qa_range_error("shader stage slot must be 0..7");
  stage_build b;
  memset(&b, 0, sizeof b);
  b.map.kind = "none";
  b.blend.source = "one";
  b.blend.destination = "zero";
  b.depth_func = "less-equal";
  b.depth_write = true;
  b.alpha_func = "none";
  b.alpha_gen.kind = "identity";
  b.tc_vectors.kind = "vector";
  b.raw_rgb = QA_MAT_CGEN_BAD;
  b.raw_alpha = QA_MAT_AGEN_IDENTITY;
  b.raw_tc = QA_MAT_TCGEN_BAD;
  b.rgb_wave.kind = "none";
  b.alpha_wave.kind = "none";
  (void)update_partial(p, stage_value(p, &b, false));
  for (;;) {
    token value;
    if (!p_next(p, true, &value)) p_reject(p, "WARNING: no matching '}' found\n");
    char *keyword = lower_copy(p, value.value);
    if (keyword[0] == '}') {
      qa_mat_parsed_stage result = stage_value(p, &b, true);
      (void)update_partial(p, result);
      return result;
    }
    if (strcmp(keyword, "map") == 0 || strcmp(keyword, "clampmap") == 0) {
      const char *source_name = stage_parameter(p, keyword);
      char *name = normalize_image_name(p->result, source_name);
      if (strcmp(keyword, "map") == 0 && strcmp(name, "$lightmap") == 0) {
        memset(&b.map, 0, sizeof b.map);
        b.map.kind = "lightmap";
        b.is_lightmap = true;
        (void)update_partial(p, stage_value(p, &b, false));
        exec_bind(p, slot, true);
      } else if (strcmp(keyword, "map") == 0 && strcmp(name, "$whiteimage") == 0) {
        memset(&b.map, 0, sizeof b.map);
        b.map.kind = "whiteimage";
        (void)update_partial(p, stage_value(p, &b, false));
        exec_bind(p, slot, false);
      } else {
        bool clamp = strcmp(keyword, "clampmap") == 0;
        memset(&b.map, 0, sizeof b.map);
        b.map.kind = "image";
        b.map.name = name;
        b.map.clamp = clamp;
        qa_shader_definition snapshot = update_partial(p, stage_value(p, &b, false));
        exec_find_stage_image(p, image_request(p, source_name, clamp ? "clamp" : "repeat"),
                              false, slot, 0, 0, snapshot);
      }
    } else if (strcmp(keyword, "animmap") == 0) {
      double frequency = source_atof(stage_parameter(p, "animMmap"));
      exec_set_animation_frequency(p, slot, frequency);
      const char *frames[8];
      size_t frame_count = 0;
      memset(&b.map, 0, sizeof b.map);
      b.map.kind = "animation";
      b.map.frequency = frequency;
      for (;;) {
        token frame_token;
        if (!p_next(p, false, &frame_token)) break;
        if (!p->common_mode &&
            (strcmp(frame_token.value, "}") == 0 || strcmp(frame_token.value, "{") == 0)) {
          p_push_pending(p, frame_token);
          break;
        }
        if (frame_count < 8)
          frames[frame_count++] = normalize_image_name(p->result, frame_token.value);
        const char **list = (const char **)s_alloc(p->result, frame_count * sizeof *list);
        for (size_t i = 0; i < frame_count; i++) list[i] = frames[i];
        b.map.frames.values = list;
        b.map.frames.count = frame_count;
        if (b.animation_count >= 8) continue;
        qa_shader_definition snapshot = update_partial(p, stage_value(p, &b, false));
        int frame = b.animation_count;
        if (frame < 0 || frame >= 8)
          qa_range_error("shader animation slot must be 0..7");
        b.animation_count++;
        exec_find_stage_image(p, image_request(p, frame_token.value, "repeat"), true,
                              slot, frame, b.animation_count, snapshot);
      }
    } else if (strcmp(keyword, "videomap") == 0) {
      const char *source_name = stage_parameter(p, "videoMmap");
      memset(&b.map, 0, sizeof b.map);
      b.map.kind = "video";
      b.map.name = normalize_image_name(p->result, source_name);
      (void)update_partial(p, stage_value(p, &b, false));
      exec_play_video(p, slot, source_name);
    } else if (strcmp(keyword, "blendfunc") == 0) {
      token first;
      if (!p_next(p, false, &first)) {
        p_warn(p, s_format(p->scratch,
                           "WARNING: missing parm for blendFunc in shader '%s'\n",
                           p->shader_name));
      } else {
        p_push_pending(p, first);
        blend_result parsed = p_blend(p, b.blend, b.destination_bits);
        b.blend = parsed.blend;
        b.source_bits = parsed.source_bits;
        b.destination_bits = parsed.destination_bits;
        if (parsed.complete && !b.explicit_depth_write) b.depth_write = false;
      }
    } else if (strcmp(keyword, "depthwrite") == 0) {
      b.depth_write = true;
      b.explicit_depth_write = true;
    } else if (strcmp(keyword, "depthfunc") == 0) {
      char *lower = lower_copy(p, stage_parameter(p, "depthfunc"));
      if (strcmp(lower, "equal") == 0) b.depth_func = "equal";
      else if (strcmp(lower, "lequal") == 0) b.depth_func = "less-equal";
      else
        p_warn(p, s_format(p->scratch,
                           "WARNING: unknown depthfunc '%s' in shader '%s'\n",
                           p->location.value, p->shader_name));
    } else if (strcmp(keyword, "alphafunc") == 0) {
      char *lower = lower_copy(p, stage_parameter(p, "alphaFunc"));
      if (strcmp(lower, "gt0") == 0 || strcmp(lower, "lt128") == 0 ||
          strcmp(lower, "ge128") == 0)
        b.alpha_func = s_strdup(p->result, lower);
      else {
        p_warn(p, s_format(p->scratch,
                           "WARNING: invalid alphaFunc name '%s' in shader '%s'\n",
                           p->location.value, p->shader_name));
        b.alpha_func = "none";
      }
    } else if (strcmp(keyword, "detail") == 0) {
      b.detail = true;
    } else if (strcmp(keyword, "rgbgen") == 0) {
      qa_color_generator parsed;
      if (p_rgb(p, b.rgb_wave, &parsed)) {
        b.rgb_gen = parsed;
        b.has_rgb = true;
        b.raw_rgb = source_color_generator(parsed);
        if (kind_is(parsed.kind, "wave")) b.rgb_wave = parsed.wave;
        if (kind_is(parsed.kind, "vertex") && b.raw_alpha == QA_MAT_AGEN_IDENTITY) {
          memset(&b.alpha_gen, 0, sizeof b.alpha_gen);
          b.alpha_gen.kind = "vertex";
          b.raw_alpha = QA_MAT_AGEN_VERTEX;
        }
      }
    } else if (strcmp(keyword, "alphagen") == 0) {
      qa_alpha_generator parsed;
      if (p_alpha(p, b.alpha_wave, &parsed)) {
        b.alpha_gen = parsed;
        b.raw_alpha = source_alpha_generator(parsed);
        if (kind_is(parsed.kind, "wave")) b.alpha_wave = parsed.wave;
      }
    } else if (strcmp(keyword, "tcgen") == 0 || strcmp(keyword, "texgen") == 0) {
      qa_tex_coord_generator parsed;
      if (p_tcgen(p, b.tc_vectors, &parsed)) {
        b.tc_gen = parsed;
        b.has_tc = true;
        b.raw_tc = source_tex_coord_generator(parsed);
        if (kind_is(parsed.kind, "vector")) b.tc_vectors = parsed;
      }
    } else if (strcmp(keyword, "tcmod") == 0) {
      parser *volatile which = p;
      parser *volatile sub = NULL;
      if (p->common_mode) {
        const char **arguments = NULL;
        size_t arg_count = 0, arg_cap = 0, length = 0;
        for (;;) {
          token arg;
          if (!p_next(p, false, &arg)) break;
          length += strlen(arg.value) + 1;
          if (length >= 1024)
            qa_range_error("tcMod arguments exceed the source 1024-byte buffer");
          if (arg_count == arg_cap) {
            size_t grown = arg_cap == 0 ? 4 : arg_cap * 2;
            arguments = (const char **)s_grow(p->scratch, arguments,
                                              arg_count * sizeof *arguments,
                                              grown * sizeof *arguments);
            arg_cap = grown;
          }
          arguments[arg_count++] = arg.value;
        }
        size_t text_length = 1;
        for (size_t i = 0; i < arg_count; i++) text_length += strlen(arguments[i]) + 1;
        char *text = (char *)s_alloc(p->scratch, text_length + 1);
        text[0] = '\0';
        for (size_t i = 0; i < arg_count; i++) {
          strcat(text, arguments[i]);
          strcat(text, " ");
        }
        if (arg_count == 0) strcat(text, " ");
        sub = (parser *)s_alloc(p->scratch, sizeof *sub);
        init_parser_fields(sub, p->result, p->scratch, p->source_name, p->shader_name);
        sub->common_mode = true;
        sub->owns_cursor = true;
        if (!qa_common_parse_cursor_init(&sub->owned_cursor, text, strlen(text), false))
          qa_range_error("shader parser allocation failed");
        sub->cursor = &sub->owned_cursor;
        sub->has_offset = true;
        sub->offset = 0;
        sub->print_warning = sub_warn_trampoline;
        sub->print_warning_ctx = p;
        sub->shader_name = p->shader_name;
        which = sub;
      }
      if (b.tc_mod_count == 4) {
        qa_shader_definition shader = update_partial(p, stage_value(p, &b, false));
        exec_drop(p, s_format(p->scratch,
                              "ERROR: too many tcMod stages in shader '%s'\n",
                              shader.name));
        p_fail(p, "A stage cannot exceed four tcMod directives");
      }
      size_t index = b.tc_mod_count;
      memset(&b.tc_mods[index], 0, sizeof b.tc_mods[index]);
      b.tc_mods[index].kind = "none";
      b.tc_mods[index].wave.kind = "none";
      b.tc_mod_count++;
      (void)update_partial(p, stage_value(p, &b, false));
      jmp_buf sub_catch;
      jmp_buf *saved_catch = which->catch_buf;
      which->catch_buf = &sub_catch;
      if (setjmp(sub_catch) == 0) {
        b.tc_mods[index] = p_tcmod(which);
        which->catch_buf = saved_catch;
        if (which == p) (void)line_arguments(p);
      } else {
        which->catch_buf = saved_catch;
        if (strcmp(which->error.reason, "source-rejection") != 0) {
          qa_shader_parse_error error = which->error;
          if (sub != NULL && sub->owns_cursor) qa_common_parse_cursor_destroy(&sub->owned_cursor);
          p->error = error;
          if (p->catch_buf != NULL) longjmp(*p->catch_buf, 1);
          qa_range_error("%s", error.formatted);
        }
      }
      if (sub != NULL && sub->owns_cursor) qa_common_parse_cursor_destroy(&sub->owned_cursor);
    } else {
      p_reject(p, s_format(p->scratch,
                           "WARNING: unknown parameter '%s' in shader '%s'\n",
                           value.value, p->shader_name));
    }
    (void)update_partial(p, stage_value(p, &b, false));
  }
}

static bool deform_number(parser *p, bool bulge, double *out) {
  token value;
  if (!p_next(p, false, &value)) {
    p_warn(p, s_format(p->scratch, "WARNING: missing deformVertexes %sparm in shader '%s'\n",
                       bulge ? "bulge " : "", p->shader_name));
    return false;
  }
  *out = source_atof(value.value);
  return true;
}
static qa_vertex_deformation p_deform(parser *p) {
  const char *raw = required_token(p, false);
  char *kind = lower_copy(p, raw);
  qa_vertex_deformation out;
  memset(&out, 0, sizeof out);
  out.wave.kind = "none";
  qa_waveform zero_wave = {"none", 0, 0, 0, 0};
  if (strcmp(kind, "projectionshadow") == 0 || strcmp(kind, "autosprite") == 0 ||
      strcmp(kind, "autosprite2") == 0) {
    out.kind = s_strdup(p->result, kind);
    return out;
  }
  if (strcmp(kind, "bulge") == 0) {
    double width, height, speed;
    if (!deform_number(p, true, &width)) { out.kind = "none"; return out; }
    if (!deform_number(p, true, &height)) { out.kind = "none"; return out; }
    if (!deform_number(p, true, &speed)) { out.kind = "none"; return out; }
    out.kind = "bulge";
    out.width = width;
    out.height = height;
    out.speed = speed;
    return out;
  }
  if (strcmp(kind, "normal") == 0) {
    double amplitude, frequency;
    if (!deform_number(p, false, &amplitude)) { out.kind = "none"; return out; }
    if (!deform_number(p, false, &frequency)) { out.kind = "none"; return out; }
    out.kind = "normal";
    out.amplitude = amplitude;
    out.frequency = frequency;
    return out;
  }
  if (strcmp(kind, "move") == 0) {
    double x, y, z;
    if (!deform_number(p, false, &x)) { out.kind = "none"; return out; }
    if (!deform_number(p, false, &y)) { out.kind = "none"; return out; }
    if (!deform_number(p, false, &z)) { out.kind = "none"; return out; }
    out.kind = "move";
    out.direction.x = x;
    out.direction.y = y;
    out.direction.z = z;
    out.wave = p_wave(p, true, zero_wave, NULL);
    return out;
  }
  if (strcmp(kind, "wave") == 0) {
    double divisor;
    if (!deform_number(p, false, &divisor)) { out.kind = "none"; return out; }
    if (divisor == 0)
      p_warn(p, s_format(p->scratch,
                         "WARNING: illegal div value of 0 in deformVertexes command for shader '%s'\n",
                         p->shader_name));
    out.kind = "wave";
    out.spread = divisor == 0 ? 100 : f32(1.0 / divisor);
    out.wave = p_wave(p, true, zero_wave, NULL);
    return out;
  }
  if (strncmp(kind, "text", 4) == 0) {
    int character = kind[4] != '\0' ? (unsigned char)kind[4] : -1;
    out.kind = "text";
    out.index = character >= 48 && character <= 55 ? character - 48 : 0;
    return out;
  }
  p_warn(p, s_format(p->scratch,
                     "WARNING: unknown deformVertexes subtype '%s' found in shader '%s'\n",
                     raw, p->shader_name));
  out.kind = "none";
  return out;
}
static bool p_sort(parser *p, double *out) {
  token value;
  if (!p_next(p, false, &value)) {
    p_warn(p, s_format(p->scratch, "WARNING: missing sort parameter in shader '%s'\n",
                       p->shader_name));
    return false;
  }
  char *lower = lower_copy(p, value.value);
  if (strcmp(lower, "portal") == 0) *out = 1;
  else if (strcmp(lower, "sky") == 0) *out = 2;
  else if (strcmp(lower, "opaque") == 0) *out = 3;
  else if (strcmp(lower, "decal") == 0) *out = 4;
  else if (strcmp(lower, "seethrough") == 0) *out = 5;
  else if (strcmp(lower, "banner") == 0) *out = 6;
  else if (strcmp(lower, "underwater") == 0) *out = 8;
  else if (strcmp(lower, "additive") == 0) *out = 10;
  else if (strcmp(lower, "nearest") == 0) *out = 16;
  else *out = source_atof(lower);
  return true;
}

typedef struct definition_result {
  bool accepted;
  qa_shader_definition definition;
} definition_result;

static void def_push_surface(parser *p, const char *value) {
  def_build *d = &p->def;
  if (d->surface_count == d->surface_cap) {
    size_t grown = d->surface_cap == 0 ? 4 : d->surface_cap * 2;
    d->surface_parms = (char **)s_grow(p->scratch, d->surface_parms,
                                       d->surface_count * sizeof *d->surface_parms,
                                       grown * sizeof *d->surface_parms);
    d->surface_cap = grown;
  }
  d->surface_parms[d->surface_count++] = s_strdup(p->result, value);
}
static void def_push_directive(parser *p, const char *name, qa_string_view_list args) {
  def_build *d = &p->def;
  if (d->directive_count == d->directive_cap) {
    size_t grown = d->directive_cap == 0 ? 4 : d->directive_cap * 2;
    d->directives = (qa_mat_compiler_directive *)s_grow(
        p->scratch, d->directives, d->directive_count * sizeof *d->directives,
        grown * sizeof *d->directives);
    d->directive_cap = grown;
  }
  d->directives[d->directive_count].name = s_strdup(p->result, name);
  d->directives[d->directive_count].arguments = args;
  d->directive_count++;
}
static bool surface_includes_fog(parser *p) {
  for (size_t i = 0; i < p->def.surface_count; i++)
    if (strcmp(p->def.surface_parms[i], "fog") == 0) return true;
  return false;
}

static definition_result run_definition(parser *p, const char *name) {
  p->warning_count = 0;
  const char *effective = p->registration_name != NULL ? p->registration_name : name;
  p->shader_name = s_strdup(p->scratch, effective);
  p->portal_range = 0;
  memset(&p->def, 0, sizeof p->def);
  p->def.cull = "front";
  p->def.name = s_strdup(p->result, effective);
  definition_result out;
  memset(&out, 0, sizeof out);
  jmp_buf catch_buf;
  jmp_buf *saved = p->catch_buf;
  p->catch_buf = &catch_buf;
  if (setjmp(catch_buf) != 0) {
    p->catch_buf = saved;
    out.accepted = false;
    out.definition = snapshot_definition(p, p->def.has_current);
    return out;
  }
  token opening_token;
  const char *opening = p_next(p, true, &opening_token) ? opening_token.value : "";
  if (opening[0] != '{')
    p_reject(p, s_format(p->scratch,
                         "WARNING: expecting '{', found '%s' instead in shader '%s'\n",
                         opening, p->shader_name));
  for (;;) {
    token next;
    if (!p_next(p, true, &next))
      p_reject(p, s_format(p->scratch, "WARNING: no concluding '}' in shader %s\n",
                           p->shader_name));
    char *lower = lower_copy(p, next.value);
    if (lower[0] == '}') break;
    const char *keyword = lower[0] == '{' ? "{" : lower;
    if (strcmp(keyword, "{") == 0) {
      if (p->def.stage_count == 8)
        fail_with(p, "A shader cannot exceed eight stages", "validation");
      int slot = (int)p->def.stage_count;
      qa_mat_parsed_stage parsed = parse_stage(p, slot);
      p->def.stages[p->def.stage_count++] = parsed;
      p->def.has_current = false;
    } else if (strcmp(keyword, "surfaceparm") == 0) {
      token value;
      if (p_next(p, false, &value)) def_push_surface(p, lower_copy(p, value.value));
    } else if (strcmp(keyword, "nomipmaps") == 0) {
      p->def.no_mip_maps = true;
      p->def.no_pic_mip = true;
    } else if (strcmp(keyword, "nopicmip") == 0) {
      p->def.no_pic_mip = true;
    } else if (strcmp(keyword, "polygonoffset") == 0) {
      p->def.polygon_offset = true;
    } else if (strcmp(keyword, "entitymergable") == 0) {
      p->def.entity_mergable = true;
    } else if (strcmp(keyword, "clamptime") == 0) {
      token value;
      if (p_next(p, false, &value)) p->def.clamp_time = source_atof(value.value);
    } else if (strcmp(keyword, "portal") == 0) {
      p->def.has_sort = true;
      p->def.sort = 1;
    } else if (strcmp(keyword, "sort") == 0) {
      double parsed;
      if (p_sort(p, &parsed)) {
        p->def.has_sort = true;
        p->def.sort = parsed;
      }
    } else if (strcmp(keyword, "cull") == 0) {
      token value;
      if (!p_next(p, false, &value)) {
        p_warn(p, s_format(p->scratch, "WARNING: missing cull parms in shader '%s'\n",
                           p->shader_name));
      } else {
        char *lower_value = lower_copy(p, value.value);
        if (strcmp(lower_value, "none") == 0 || strcmp(lower_value, "twosided") == 0 ||
            strcmp(lower_value, "disable") == 0)
          p->def.cull = "none";
        else if (strcmp(lower_value, "back") == 0 ||
                 strcmp(lower_value, "backside") == 0 ||
                 strcmp(lower_value, "backsided") == 0)
          p->def.cull = "back";
        else
          p_warn(p, s_format(p->scratch,
                             "WARNING: invalid cull parm '%s' in shader '%s'\n",
                             value.value, p->shader_name));
      }
    } else if (strcmp(keyword, "deformvertexes") == 0) {
      token value;
      if (!p_next(p, false, &value)) {
        p_warn(p, s_format(p->scratch, "WARNING: missing deform parm in shader '%s'\n",
                           p->shader_name));
      } else if (p->def.deform_count == 3) {
        p_warn(p, s_format(p->scratch, "WARNING: MAX_SHADER_DEFORMS in '%s'\n",
                           p->shader_name));
      } else {
        p_push_pending(p, value);
        p->def.deforms[p->def.deform_count++] = p_deform(p);
      }
    } else if (strcmp(keyword, "skyparms") == 0) {
      bool has_out;
      qa_mat_shader_sky_params sky;
      sky_directive(p, p->def.has_sky, p->def.sky, &has_out, &sky);
      p->def.has_sky = has_out;
      if (has_out) p->def.sky = sky;
    } else if (strcmp(keyword, "fogparms") == 0) {
      qa_vec3 previous = p->def.has_fog ? p->def.fog.color : (qa_vec3){0, 0, 0};
      vector_result color = source_vector(p, previous);
      double depth = p->def.has_fog ? p->def.fog.depth_for_opaque : 0;
      p->def.has_fog = true;
      p->def.fog.color = color.value;
      p->def.fog.depth_for_opaque = depth;
      if (!color.complete) p_fail(p, "Incomplete fogParms color vector");
      token value;
      if (!p_next(p, false, &value)) {
        p_warn(p, s_format(p->scratch,
                           "WARNING: missing parm for 'fogParms' keyword in shader '%s'\n",
                           p->shader_name));
      } else {
        p->def.fog.depth_for_opaque = source_atof(value.value);
        (void)line_arguments(p);
      }
    } else if (strcmp(keyword, "q3map_sun") == 0) {
      sun_directive parsed = p_sun_directive(p);
      p->def.has_sun = true;
      p->def.sun = parsed.source;
      exec_apply_sun(p, parsed.registered);
    } else if (strcmp(keyword, "light") == 0) {
      token value;
      qa_string_view_list args = {NULL, 0};
      if (p_next(p, false, &value)) {
        const char **list = (const char **)s_alloc(p->result, sizeof *list);
        list[0] = s_strdup(p->result, value.value);
        args.values = list;
        args.count = 1;
      }
      def_push_directive(p, keyword, args);
    } else if (strncmp(keyword, "qer", 3) == 0 || strncmp(keyword, "q3map", 5) == 0 ||
               strcmp(keyword, "tesssize") == 0) {
      def_push_directive(p, keyword, line_arguments(p));
    } else {
      p_reject(p, s_format(p->scratch,
                           "WARNING: unknown general shader parameter '%s' in '%s'\n",
                           next.value, p->shader_name));
    }
  }
  if (p->def.stage_count == 0 && !p->def.has_sky && !surface_includes_fog(p))
    p_fail(p, "Shader has no stages and is neither sky nor fog; source uses an implicit material");
  p->catch_buf = saved;
  out.accepted = true;
  out.definition = snapshot_definition(p, false);
  return out;
}

/* TS registeredSkyBox / registeredSky / registeredStage(s). */
typedef struct sky_box_ctx { qa_mat_finished_stage_image image[6]; } sky_box_ctx;
static qa_mat_finished_stage_image sky_box_image(void *ctx, const char *face) {
  sky_box_ctx *box = (sky_box_ctx *)ctx;
  for (int i = 0; i < 6; i++)
    if (strcmp(face, sky_faces[i]) == 0) return box->image[i];
  qa_range_error("unknown source sky face '%s'", face);
}
static const qa_mat_registered_sky_box *registered_sky_box(storage *st, working_sky_box *box) {
  bool any = false, all = true;
  for (int i = 0; i < 6; i++) {
    any = any || box->has[i];
    all = all && box->has[i];
  }
  if (!any) return NULL;
  if (!all) qa_range_error("source sky box registration became incomplete");
  sky_box_ctx *ctx = (sky_box_ctx *)s_alloc(st, sizeof *ctx);
  for (int i = 0; i < 6; i++) ctx->image[i] = box->image[i];
  qa_mat_registered_sky_box *out =
      (qa_mat_registered_sky_box *)s_alloc(st, sizeof *out);
  out->ctx = ctx;
  out->image = sky_box_image;
  return out;
}
static const qa_mat_registered_sky *registered_sky(storage *st, driver *drv) {
  bool has_outer = false, has_inner = false;
  for (int i = 0; i < 6; i++) {
    has_outer = has_outer || drv->outer.has[i];
    has_inner = has_inner || drv->inner.has[i];
  }
  if (!has_outer && !has_inner && drv->cloud_height == 0) return NULL;
  qa_mat_registered_sky *out = (qa_mat_registered_sky *)s_alloc(st, sizeof *out);
  out->outer = registered_sky_box(st, &drv->outer);
  out->inner = registered_sky_box(st, &drv->inner);
  out->cloud_height = drv->cloud_height;
  return out;
}
static qa_mat_registered_stage registered_stage(storage *st, working_bundle *bundle) {
  qa_mat_registered_stage out;
  memset(&out, 0, sizeof out);
  if (!bundle->has_image[0]) {
    out.kind = "missing";
    return out;
  }
  qa_mat_registered_image primary = bundle->images[0];
  out.kind = "loaded";
  out.tmu = primary.tmu;
  if (bundle->video_enabled) {
    if (!bundle->has_video) out.binding.kind = "retain-current-texture";
    else {
      out.binding.kind = "video";
      out.binding.source = bundle->video.source;
    }
    return out;
  }
  out.binding.kind = "images";
  if (bundle->animation_count > 0) {
    qa_mat_finished_stage_image *frames = (qa_mat_finished_stage_image *)s_alloc(
        st, (size_t)bundle->animation_count * sizeof *frames);
    frames[0] = primary.frame;
    for (int i = 1; i < bundle->animation_count; i++) {
      if (!bundle->has_image[i])
        qa_range_error("source animation count includes an unregistered frame");
      frames[i] = bundle->images[i].frame;
    }
    out.binding.playback.kind = "animation";
    out.binding.playback.frequency = bundle->animation_frequency;
    out.binding.playback.frames = frames;
    out.binding.playback.frame_count = (size_t)bundle->animation_count;
    return out;
  }
  out.binding.playback.kind = "single";
  out.binding.playback.image = primary.frame;
  return out;
}
static const qa_mat_registered_stage *registered_stages(storage *st,
                                                        qa_shader_definition def,
                                                        driver *drv, size_t *count) {
  *count = def.stage_count;
  if (def.stage_count == 0) return NULL;
  if (def.stage_count > 8)
    qa_range_error("shader definition exceeds the registered stage store");
  qa_mat_registered_stage *out =
      (qa_mat_registered_stage *)s_alloc(st, def.stage_count * sizeof *out);
  for (size_t i = 0; i < def.stage_count; i++) out[i] = registered_stage(st, &drv->bundles[i]);
  return out;
}

static void print_to_host(void *ctx, const char *message) {
  host_warn((parser *)ctx, message);
}

/* TS registerShader driver over a live parser run. */
static void run_register(parser *p, const char *definition_name,
                         qa_shader_registration_host *host,
                         qa_shader_registration_result *out) {
  driver drv;
  memset(&drv, 0, sizeof drv);
  drv.host = host;
  p->exec = &drv;
  p->print_warning = print_to_host;
  p->print_warning_ctx = p;
  memset(out, 0, sizeof *out);
  out->_storage = p->result;
  if (setjmp(drv.early) != 0) {
    size_t count = 0;
    const qa_mat_registered_stage *stages =
        registered_stages(p->result, drv.missing_definition, &drv, &count);
    out->shader.kind = "defaulted";
    out->shader.failure.kind = "missing-image";
    out->shader.failure.request = drv.missing_request;
    out->shader.definition = drv.missing_definition;
    out->shader.stages = stages;
    out->shader.stage_count = count;
    out->shader.sky = registered_sky(p->result, &drv);
    p->exec = NULL;
    return;
  }
  definition_result terminal = run_definition(p, definition_name);
  size_t count = 0;
  const qa_mat_registered_stage *stages =
      registered_stages(p->result, terminal.definition, &drv, &count);
  out->shader.definition = terminal.definition;
  out->shader.stages = stages;
  out->shader.stage_count = count;
  out->shader.sky = registered_sky(p->result, &drv);
  if (!terminal.accepted) {
    out->shader.kind = "defaulted";
    out->shader.failure.kind = "source-text";
    out->shader.failure.error_message = p->error.formatted;
    out->error = p->error;
  } else {
    out->shader.kind = "defined";
  }
  p->exec = NULL;
}

void qa_shader_registration_result_free(qa_shader_registration_result *result) {
  if (result == NULL) return;
  storage_free((storage *)result->_storage);
  memset(result, 0, sizeof *result);
}
void qa_shader_program_register(qa_shader_registration_program *program,
                                qa_shader_registration_host *host,
                                qa_shader_registration_result *out) {
  program->register_fn(program->ctx, host, out);
}
void qa_shader_program_destroy(qa_shader_registration_program *program) {
  if (program->destroy != NULL) program->destroy(program->ctx);
  program->ctx = NULL;
  program->register_fn = NULL;
  program->destroy = NULL;
}

/* Compiled program: re-parses retained text from the recorded resume
 * point (see header note). ctx lives in the inspection storage. */
typedef struct compiled_ctx {
  char *text;
  char *source;
  char *name;
  char *registration_name;
  size_t offset;
  int64_t line, column;
  int depth;
  char *location_value;
  int64_t location_line, location_column;
} compiled_ctx;
static void compiled_register(void *raw, qa_shader_registration_host *host,
                              qa_shader_registration_result *out) {
  compiled_ctx *ctx = (compiled_ctx *)raw;
  storage *result = storage_new();
  storage *scratch = storage_new();
  parser *p = (parser *)s_alloc(scratch, sizeof *p);
  init_parser_fields(p, result, scratch, ctx->source, ctx->registration_name);
  if (!qa_tokenizer_init(&p->tokenizer, ctx->text, strlen(ctx->text), ctx->source))
    qa_range_error("shader parser allocation failed");
  p->tokenizer_owned = true;
  p->tokenizer.offset = ctx->offset;
  p->tokenizer.line = ctx->line;
  p->tokenizer.column = ctx->column;
  p->depth = ctx->depth;
  p->location.value = ctx->location_value;
  p->location.line = ctx->location_line;
  p->location.column = ctx->location_column;
  run_register(p, ctx->name, host, out);
  qa_tokenizer_destroy(&p->tokenizer);
  storage_free(scratch);
}

/* Source program over a borrowed common-parse cursor. */
typedef struct source_ctx {
  qa_common_parse_cursor *cursor;
  bool has_offset;
  size_t offset;
  char *source;
  char *name;
} source_ctx;
static void source_register(void *raw, qa_shader_registration_host *host,
                            qa_shader_registration_result *out) {
  source_ctx *ctx = (source_ctx *)raw;
  storage *result = storage_new();
  storage *scratch = storage_new();
  parser *p = (parser *)s_alloc(scratch, sizeof *p);
  init_parser_fields(p, result, scratch, ctx->source, ctx->name);
  p->common_mode = true;
  p->cursor = ctx->cursor;
  p->has_offset = ctx->has_offset;
  p->offset = ctx->offset;
  run_register(p, ctx->name, host, out);
  storage_free(scratch);
}
static void source_destroy(void *raw) {
  source_ctx *ctx = (source_ctx *)raw;
  free(ctx->source);
  free(ctx->name);
  free(ctx);
}
void qa_source_shader_program_init(qa_shader_registration_program *program,
                                   qa_common_parse_cursor *cursor, bool has_offset,
                                   size_t offset, const char *source, const char *name) {
  source_ctx *ctx = (source_ctx *)calloc(1, sizeof *ctx);
  if (ctx == NULL) qa_range_error("shader parser allocation failed");
  ctx->cursor = cursor;
  ctx->has_offset = has_offset;
  ctx->offset = offset;
  ctx->source = xstrdup(source);
  ctx->name = xstrdup(name);
  program->ctx = ctx;
  program->register_fn = source_register;
  program->destroy = source_destroy;
}

/* TS ShaderParser.parse / parseShaderScript / inspectShaderScript. */
static void inspect_impl(const char *text, const char *source,
                         const char *registration_name, bool recover,
                         qa_shader_script_inspection *out) {
  const char *source_name = source == NULL ? "<shader>" : source;
  storage *result = storage_new();
  storage *scratch = storage_new();
  parser *p = (parser *)s_alloc(scratch, sizeof *p);
  char *retained_source = s_strdup(result, source_name);
  char *retained_text = s_strdup(result, text);
  char *retained_registration =
      registration_name == NULL ? NULL : s_strdup(result, registration_name);
  init_parser_fields(p, result, scratch, retained_source, retained_registration);
  if (!qa_tokenizer_init(&p->tokenizer, text, strlen(text), retained_source))
    qa_range_error("shader parser allocation failed");
  p->tokenizer_owned = true;
  memset(out, 0, sizeof *out);
  out->_storage = result;
  qa_shader_catalog_entry *entries = NULL;
  size_t count = 0, cap = 0;
  for (;;) {
    token name_token;
    if (!p_next(p, true, &name_token)) break;
    if (strcmp(name_token.value, "{") == 0 || strcmp(name_token.value, "}") == 0) {
      if (!recover) p_fail(p, "Expected shader name");
      /* A stray brace is skipped with its section, as SkipBracedSection does,
         so sibling shaders survive. */
      if (strcmp(name_token.value, "}") == 0) {
        p->depth = 0;
      } else {
        bool ended = false;
        while (p->depth > 0) {
          token skipped;
          if (!p_next(p, true, &skipped)) { ended = true; break; }
        }
        if (ended) break;
      }
      continue;
    }
    compiled_ctx *ctx = (compiled_ctx *)s_alloc(result, sizeof *ctx);
    ctx->text = retained_text;
    ctx->source = retained_source;
    ctx->name = s_strdup(result, name_token.value);
    ctx->registration_name = retained_registration;
    ctx->offset = p->tokenizer.offset;
    ctx->line = p->tokenizer.line;
    ctx->column = p->tokenizer.column;
    ctx->depth = p->depth;
    ctx->location_value = s_strdup(result, p->location.value);
    ctx->location_line = p->location.line;
    ctx->location_column = p->location.column;
    definition_result terminal = run_definition(p, name_token.value);
    if (!terminal.accepted && !recover) qa_range_error("%s", p->error.formatted);
    if (count == cap) {
      size_t grown = cap == 0 ? 4 : cap * 2;
      entries = (qa_shader_catalog_entry *)s_grow(result, entries,
                                                  count * sizeof *entries,
                                                  grown * sizeof *entries);
      cap = grown;
    }
    qa_shader_catalog_entry *entry = &entries[count++];
    memset(entry, 0, sizeof *entry);
    entry->name = ctx->name;
    entry->program.ctx = ctx;
    entry->program.register_fn = compiled_register;
    entry->program.destroy = NULL;
    entry->accepted = terminal.accepted;
    entry->definition = terminal.definition;
    if (!terminal.accepted) entry->error = p->error;
    if (!terminal.accepted) {
      bool done = false;
      while (p->depth > 0) {
        token skipped;
        if (!p_next(p, true, &skipped)) {
          done = true;
          break;
        }
      }
      if (done) break;
    }
  }
  out->entries = entries;
  out->count = count;
  qa_tokenizer_destroy(&p->tokenizer);
  storage_free(scratch);
}
void qa_inspect_shader_script(const char *text, const char *source,
                              const char *registration_name,
                              qa_shader_script_inspection *out) {
  inspect_impl(text, source, registration_name, true, out);
}
void qa_shader_script_inspection_free(qa_shader_script_inspection *inspection) {
  if (inspection == NULL) return;
  storage_free((storage *)inspection->_storage);
  memset(inspection, 0, sizeof *inspection);
}
void qa_parse_shader_script(const char *text, const char *source,
                            qa_shader_definitions *out) {
  qa_shader_script_inspection inspection;
  inspect_impl(text, source, NULL, false, &inspection);
  memset(out, 0, sizeof *out);
  out->_storage = inspection._storage;
  out->count = inspection.count;
  if (inspection.count != 0) {
    qa_shader_definition *items = (qa_shader_definition *)s_alloc(
        (storage *)inspection._storage, inspection.count * sizeof *items);
    for (size_t i = 0; i < inspection.count; i++) {
      if (!inspection.entries[i].accepted)
        qa_range_error("%s", inspection.entries[i].error.formatted);
      items[i] = inspection.entries[i].definition;
    }
    out->items = items;
  }
}
void qa_shader_definitions_free(qa_shader_definitions *definitions) {
  if (definitions == NULL) return;
  storage_free((storage *)definitions->_storage);
  memset(definitions, 0, sizeof *definitions);
}






/* qa_mat_* helpers declared in qa/materials/shader_types.h (C-only
 * shared header). qa_mat_evaluate_waveform / qa_mat_shader_table_index
 * mirror evaluateWaveform / shaderTableIndex in TS material.ts. */
int32_t qa_mat_to_int32(double value) {
  if (!isfinite(value)) return 0;
  double m = fmod(trunc(value), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  if (m >= 2147483648.0) m -= 4294967296.0;
  return (int32_t)m;
}
uint32_t qa_mat_to_uint32(double value) { return (uint32_t)qa_mat_to_int32(value); }
float qa_mat_f(double value) { return (float)value; }
bool qa_mat_kind_is(const char *kind, const char *name) {
  return kind != NULL && strcmp(kind, name) == 0;
}
int32_t qa_mat_shader_table_index(double value) {
  double integer = trunc(value);
  /* x86 "integer indefinite" (INT_MIN) masks to zero. */
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    return 0;
  return (int32_t)integer & 1023;
}
double qa_mat_evaluate_waveform(qa_waveform wave, double time) {
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

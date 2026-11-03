/* Port of src/materials/material-finish.ts. The CollapseMultitexture
 * iterator below ports src/materials/material-iterator.ts privately;
 * that module has no C home yet. GPL-2.0-or-later. */
#include "qa/materials/material_finish.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/materials/source_state.h"

typedef struct storage {
  void **blocks;
  size_t count, cap;
} storage;
static storage *storage_new(void) {
  storage *st = (storage *)calloc(1, sizeof *st);
  if (st == NULL) qa_range_error("FinishShader allocation failed");
  return st;
}
static void *s_alloc(storage *st, size_t size) {
  void *ptr = calloc(1, size == 0 ? 1 : size);
  if (ptr == NULL) qa_range_error("FinishShader allocation failed");
  if (st->count == st->cap) {
    size_t grown = st->cap == 0 ? 32 : st->cap * 2;
    void **blocks = (void **)realloc(st->blocks, grown * sizeof *blocks);
    if (blocks == NULL) qa_range_error("FinishShader allocation failed");
    st->blocks = blocks;
    st->cap = grown;
  }
  st->blocks[st->count++] = ptr;
  return ptr;
}
static char *s_format(storage *st, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("FinishShader allocation failed");
  char *text = (char *)s_alloc(st, (size_t)length + 1);
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  return text;
}
static void storage_free(storage *st) {
  if (st == NULL) return;
  for (size_t i = st->count; i > 0; i--) free(st->blocks[i - 1]);
  free(st->blocks);
  free(st);
}
static bool kind_is(const char *kind, const char *name) {
  return kind != NULL && strcmp(kind, name) == 0;
}

static qa_mat_wave_storage copy_wave(qa_mat_wave_storage wave) { return wave; }
static qa_mat_shader_stage copy_stage(storage *st, qa_mat_shader_stage stage) {
  qa_mat_shader_stage out = stage;
  if (stage.tc_mod_count != 0) {
    qa_mat_tc_modifier *mods =
        (qa_mat_tc_modifier *)s_alloc(st, stage.tc_mod_count * sizeof *mods);
    for (size_t i = 0; i < stage.tc_mod_count; i++) mods[i] = stage.tc_mods[i];
    out.tc_mods = mods;
  }
  return out;
}
static const char *alpha_kind(int source) {
  switch (source) {
    case QA_MAT_AGEN_SKIP: return "skip";
    case QA_MAT_AGEN_ENTITY: return "entity";
    case QA_MAT_AGEN_ONE_MINUS_ENTITY: return "oneminusentity";
    case QA_MAT_AGEN_VERTEX: return "vertex";
    case QA_MAT_AGEN_ONE_MINUS_VERTEX: return "oneminusvertex";
    case QA_MAT_AGEN_LIGHTING_SPECULAR: return "lightingspecular";
    case QA_MAT_AGEN_WAVEFORM: return "wave";
    case QA_MAT_AGEN_PORTAL: return "portal";
    case QA_MAT_AGEN_CONST: return "const";
    default: return "identity";
  }
}

typedef struct working_stage {
  qa_mat_shader_stage stage;
  uint32_t state_bits;
  bool active;
  int image_tmu; /* -1 is TS null */
  bool has_binding;
  qa_mat_finished_binding binding;
  const char *alpha_gen;
  int rgb_gen;
  qa_mat_wave_storage rgb_wave, alpha_wave;
  bool is_lightmap, vertex_lightmap;
  int tc_gen;
  const char *fog_adjustment;
} working_stage;

static working_stage *make_working_stage(storage *st, qa_mat_parsed_stage parsed,
                                         qa_mat_registered_stage image,
                                         double portal_range) {
  working_stage *out = (working_stage *)s_alloc(st, sizeof *out);
  qa_mat_shader_stage stage = copy_stage(st, parsed.stage);
  if (kind_is(stage.alpha_gen.kind, "portal")) stage.alpha_gen.range = portal_range;
  bool loaded = kind_is(image.kind, "loaded");
  out->stage = stage;
  out->state_bits = parsed.source_state.state_bits;
  out->active = parsed.source_state.active && loaded;
  out->image_tmu = loaded ? (int)image.tmu : -1;
  out->has_binding = loaded;
  if (loaded) out->binding = image.binding;
  out->alpha_gen = alpha_kind(parsed.source_state.alpha_gen);
  out->rgb_gen = parsed.source_state.rgb_gen;
  out->rgb_wave = copy_wave(parsed.source_state.rgb_wave);
  out->alpha_wave = copy_wave(parsed.source_state.alpha_wave);
  out->is_lightmap = parsed.source_state.is_lightmap;
  out->vertex_lightmap = parsed.source_state.vertex_lightmap;
  out->tc_gen = parsed.source_state.tc_gen;
  out->fog_adjustment = "none";
  return out;
}

enum { BLEND_MASK = QA_SOURCE_STATE_SRCBLEND_BITS | QA_SOURCE_STATE_DSTBLEND_BITS };
static bool is_blended(const working_stage *stage) {
  return (stage->state_bits & BLEND_MASK) != 0;
}
static const char *fog_adjustment_of(const working_stage *stage) {
  uint32_t blend = stage->state_bits & BLEND_MASK;
  if (blend == (QA_SOURCE_STATE_SRCBLEND_ONE | QA_SOURCE_STATE_DSTBLEND_ONE) ||
      blend == (QA_SOURCE_STATE_SRCBLEND_ZERO |
                QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR))
    return "rgb";
  if (blend == (QA_SOURCE_STATE_SRCBLEND_SRC_ALPHA |
                QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA))
    return "alpha";
  if (blend == (QA_SOURCE_STATE_SRCBLEND_ONE |
                QA_SOURCE_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA))
    return "rgba";
  return "none";
}
static qa_mat_shader_stage with_bundle(storage *st, qa_mat_shader_stage target,
                                       qa_mat_shader_stage source) {
  qa_mat_shader_stage out = target;
  out.map = source.map;
  out.tc_gen = source.tc_gen;
  out.tc_mods = NULL;
  out.tc_mod_count = source.tc_mod_count;
  if (source.tc_mod_count != 0) {
    qa_mat_tc_modifier *mods =
        (qa_mat_tc_modifier *)s_alloc(st, source.tc_mod_count * sizeof *mods);
    for (size_t i = 0; i < source.tc_mod_count; i++) mods[i] = source.tc_mods[i];
    out.tc_mods = mods;
  }
  return out;
}
static working_stage *copy_working(storage *st, const working_stage *source) {
  working_stage *out = (working_stage *)s_alloc(st, sizeof *out);
  *out = *source;
  out->stage = copy_stage(st, source->stage);
  return out;
}

static void vertex_lighting_collapse(storage *st, working_stage **stages, double sort,
                                     double lightmap_index) {
  working_stage *first = stages[0];
  if (first == NULL) return;
  if (sort == 3) {
    working_stage *best = first;
    int best_rank = -999999;
    for (int index = 0; index < 8; index++) {
      working_stage *candidate = stages[index];
      if (candidate == NULL || !candidate->active) break;
      int rank = 0;
      if (candidate->is_lightmap) rank -= 100;
      if (candidate->tc_gen != QA_MAT_TCGEN_TEXTURE) rank -= 5;
      if (candidate->stage.tc_mod_count != 0) rank -= 5;
      if (candidate->rgb_gen != QA_MAT_CGEN_IDENTITY &&
          candidate->rgb_gen != QA_MAT_CGEN_IDENTITY_LIGHTING)
        rank -= 3;
      if (rank > best_rank) {
        best_rank = rank;
        best = candidate;
      }
    }
    first->stage = with_bundle(st, first->stage, best->stage);
    first->image_tmu = best->image_tmu;
    first->has_binding = best->has_binding;
    first->binding = best->binding;
    first->is_lightmap = best->is_lightmap;
    first->vertex_lightmap = best->vertex_lightmap;
    first->tc_gen = best->tc_gen;
    first->stage.blend.source = "one";
    first->stage.blend.destination = "zero";
    first->stage.depth_write = true;
    memset(&first->stage.rgb_gen, 0, sizeof first->stage.rgb_gen);
    first->stage.rgb_gen.kind = lightmap_index == -1 ? "lightingdiffuse" : "exactvertex";
    first->state_bits =
        (first->state_bits & ~(uint32_t)BLEND_MASK) | QA_SOURCE_STATE_DEPTHMASK_TRUE;
    first->rgb_gen = lightmap_index == -1 ? QA_MAT_CGEN_LIGHTING_DIFFUSE
                                          : QA_MAT_CGEN_EXACT_VERTEX;
    first->alpha_gen = "skip";
  } else {
    working_stage *second = stages[1];
    if (second == NULL) return;
    if (first->is_lightmap) stages[0] = copy_working(st, second);
    working_stage *collapsed = stages[0];
    if (collapsed == NULL) return;
    if (collapsed->rgb_gen == QA_MAT_CGEN_ONE_MINUS_ENTITY ||
        second->rgb_gen == QA_MAT_CGEN_ONE_MINUS_ENTITY) {
      memset(&collapsed->stage.rgb_gen, 0, sizeof collapsed->stage.rgb_gen);
      collapsed->stage.rgb_gen.kind = "identitylighting";
      collapsed->rgb_gen = QA_MAT_CGEN_IDENTITY_LIGHTING;
    }
    if (collapsed->rgb_gen == QA_MAT_CGEN_WAVEFORM &&
        second->rgb_gen == QA_MAT_CGEN_WAVEFORM) {
      int a = collapsed->rgb_wave.func, b = second->rgb_wave.func;
      if ((a == QA_MAT_WAVE_SAWTOOTH && b == QA_MAT_WAVE_INVERSE_SAWTOOTH) ||
          (a == QA_MAT_WAVE_INVERSE_SAWTOOTH && b == QA_MAT_WAVE_SAWTOOTH)) {
        memset(&collapsed->stage.rgb_gen, 0, sizeof collapsed->stage.rgb_gen);
        collapsed->stage.rgb_gen.kind = "identitylighting";
        collapsed->rgb_gen = QA_MAT_CGEN_IDENTITY_LIGHTING;
      }
    }
  }
  for (int index = 1; index < 8; index++) stages[index] = NULL;
}

static qa_mat_finished_iterator_stage finished_stage(const working_stage *stage) {
  qa_mat_finished_iterator_stage out;
  memset(&out, 0, sizeof out);
  out.stage = stage->stage;
  out.state_bits = stage->state_bits;
  out.alpha_gen = stage->alpha_gen;
  out.rgb_gen = stage->rgb_gen;
  out.tc_gen = stage->tc_gen;
  out.rgb_wave = stage->rgb_wave;
  out.alpha_wave = stage->alpha_wave;
  out.is_lightmap = stage->is_lightmap;
  out.vertex_lightmap = stage->vertex_lightmap;
  out.fog_adjustment = stage->fog_adjustment;
  if (stage->active && stage->image_tmu != -1 && stage->has_binding) {
    out.active = true;
    out.image_tmu = stage->image_tmu;
    out.binding = stage->binding;
  } else {
    out.active = false;
    out.image_tmu = -1;
  }
  return out;
}

/* Port of src/materials/material-iterator.ts sourceMaterialIterator
 * (TS owner module not yet ported; see header note). */
static bool same_float_object_is(double a, double b) {
  float fa = (float)a, fb = (float)b;
  if (isnan(fa) && isnan(fb)) return true;
  uint32_t ba, bb;
  memcpy(&ba, &fa, sizeof ba);
  memcpy(&bb, &fb, sizeof bb);
  return ba == bb;
}
static bool same_wave(qa_mat_wave_storage a, qa_mat_wave_storage b) {
  return a.func == b.func && same_float_object_is(a.base, b.base) &&
         same_float_object_is(a.amplitude, b.amplitude) &&
         same_float_object_is(a.phase, b.phase) &&
         same_float_object_is(a.frequency, b.frequency);
}
typedef struct collapse_result {
  const char *env;
  qa_blend_state blend;
  uint32_t bits;
} collapse_result;
static bool collapse_blend(uint32_t first, uint32_t second, collapse_result *out) {
  const uint32_t filter_source =
      QA_SOURCE_STATE_SRCBLEND_DST_COLOR | QA_SOURCE_STATE_DSTBLEND_ZERO;
  const uint32_t filter_destination =
      QA_SOURCE_STATE_SRCBLEND_ZERO | QA_SOURCE_STATE_DSTBLEND_SRC_COLOR;
  const uint32_t additive =
      QA_SOURCE_STATE_SRCBLEND_ONE | QA_SOURCE_STATE_DSTBLEND_ONE;
  if (second == filter_source || second == filter_destination) {
    if (first == 0) {
      out->env = "modulate";
      out->blend.source = "one";
      out->blend.destination = "zero";
      out->bits = 0;
      return true;
    }
    if (first == filter_source || first == filter_destination) {
      out->env = "modulate";
      out->blend.source = "dst-color";
      out->blend.destination = "zero";
      out->bits = filter_source;
      return true;
    }
  }
  if (second == additive) {
    if (first == 0) {
      out->env = "add";
      out->blend.source = "one";
      out->blend.destination = "zero";
      out->bits = 0;
      return true;
    }
    if (first == additive) {
      out->env = "add";
      out->blend.source = "one";
      out->blend.destination = "one";
      out->bits = additive;
      return true;
    }
  }
  return false;
}
static qa_mat_material_iterator source_material_iterator(
    storage *st, const qa_mat_finished_iterator_stage *stages, size_t stage_count,
    bool sky, bool polygon_offset, size_t deform_count,
    qa_mat_iterator_profile profile) {
  qa_mat_iterator_pass *passes = NULL;
  size_t pass_count = stage_count;
  if (stage_count != 0) {
    passes = (qa_mat_iterator_pass *)s_alloc(st, stage_count * sizeof *passes);
    for (size_t i = 0; i < stage_count; i++) {
      passes[i].stage = stages[i].stage;
      passes[i].state_bits = stages[i].state_bits;
      passes[i].rgb_gen = stages[i].rgb_gen;
      passes[i].fog_adjustment = stages[i].fog_adjustment;
      passes[i].alpha_gen = stages[i].alpha_gen;
      passes[i].bundles[0] = &stages[i];
      passes[i].bundles[1] = NULL;
      passes[i].bundle_count = 1;
    }
  }
  const char *env = "none";
  if (profile.multitexture && stage_count >= 2) {
    const qa_mat_finished_iterator_stage *a = &stages[0], *b = &stages[1];
    uint32_t keep = ~(uint32_t)(BLEND_MASK | QA_SOURCE_STATE_DEPTHMASK_TRUE);
    if (a->active && b->active &&
        (!kind_is(profile.driver, "voodoo") || a->image_tmu != b->image_tmu) &&
        (a->state_bits & keep) == (b->state_bits & keep) && a->rgb_gen == b->rgb_gen &&
        strcmp(a->alpha_gen, b->alpha_gen) == 0 &&
        (a->rgb_gen != QA_MAT_CGEN_WAVEFORM || same_wave(a->rgb_wave, b->rgb_wave)) &&
        (!kind_is(a->alpha_gen, "portal") || same_wave(a->alpha_wave, b->alpha_wave))) {
      collapse_result collapse;
      if (collapse_blend(a->state_bits & BLEND_MASK, b->state_bits & BLEND_MASK,
                         &collapse) &&
          (!kind_is(collapse.env, "add") ||
           (profile.texture_env_add && a->rgb_gen == QA_MAT_CGEN_IDENTITY))) {
        env = collapse.env;
        qa_mat_iterator_pass merged;
        memset(&merged, 0, sizeof merged);
        merged.stage = a->stage;
        merged.stage.blend = collapse.blend;
        merged.state_bits = (a->state_bits & ~(uint32_t)BLEND_MASK) | collapse.bits;
        merged.rgb_gen = a->rgb_gen;
        merged.fog_adjustment = a->fog_adjustment;
        merged.alpha_gen = a->alpha_gen;
        merged.bundles[0] = a->is_lightmap ? b : a;
        merged.bundles[1] = a->is_lightmap ? a : b;
        merged.bundle_count = 2;
        qa_mat_iterator_pass *fresh =
            (qa_mat_iterator_pass *)s_alloc(st, (stage_count - 1) * sizeof *fresh);
        fresh[0] = merged;
        for (size_t i = 2; i < stage_count; i++) fresh[i - 1] = passes[i];
        passes = fresh;
        pass_count = stage_count - 1;
      }
    }
  }
  const char *kind = "generic";
  if (sky) kind = "sky";
  else if (!profile.ignore_fast_path && pass_count == 1 && !polygon_offset &&
           deform_count == 0) {
    const qa_mat_iterator_pass *first = &passes[0];
    if (first->rgb_gen == QA_MAT_CGEN_LIGHTING_DIFFUSE &&
        kind_is(first->alpha_gen, "identity") &&
        first->bundles[0]->tc_gen == QA_MAT_TCGEN_TEXTURE && kind_is(env, "none"))
      kind = "vertex-lit";
    if (first->rgb_gen == QA_MAT_CGEN_IDENTITY && kind_is(first->alpha_gen, "identity") &&
        first->bundles[0]->tc_gen == QA_MAT_TCGEN_TEXTURE &&
        first->bundle_count >= 2 &&
        first->bundles[1]->tc_gen == QA_MAT_TCGEN_LIGHTMAP && !kind_is(env, "none"))
      kind = "lightmapped-multitexture";
  }
  qa_mat_material_iterator out;
  out.kind = kind;
  out.passes = passes;
  out.pass_count = pass_count;
  out.multitexture_env = env;
  return out;
}

void qa_finish_shader(qa_finish_shader_input input, qa_finished_shader_result *out) {
  if (trunc(input.lightmap_index) != input.lightmap_index ||
      input.lightmap_index < -4 || input.lightmap_index > 2147483647.0)
    qa_range_error("FinishShader lightmapIndex must be a source lightmap sentinel or non-negative int32");
  if (input.image_count != input.definition.stage_count)
    qa_range_error("FinishShader needs exactly one image result for each parsed stage");
  storage *st = storage_new();
  memset(out, 0, sizeof *out);
  out->_storage = st;
  working_stage *stages[8];
  for (int i = 0; i < 8; i++) stages[i] = NULL;
  for (size_t i = 0; i < input.definition.stage_count && i < 8; i++)
    stages[i] = make_working_stage(st, input.definition.stages[i], input.images[i],
                                   input.definition.portal_range);
  qa_mat_finish_diagnostic *diagnostics =
      (qa_mat_finish_diagnostic *)s_alloc(st, 16 * sizeof *diagnostics);
  size_t diagnostic_count = 0;
  double sort = input.definition.has_sort ? input.definition.sort : 0;
  if (input.definition.sky != NULL) sort = 2;
  if (input.definition.polygon_offset && sort == 0) sort = 4;
  bool has_lightmap_stage = false;
  int stage_index = 0;
  while (stage_index < 8) {
    working_stage *current = stages[stage_index];
    if (current == NULL) break;
    if (!current->active) {
      diagnostics[diagnostic_count].kind = "missing-image";
      diagnostics[diagnostic_count].has_stage = true;
      diagnostics[diagnostic_count].stage = stage_index;
      diagnostics[diagnostic_count].message =
          s_format(st, "Shader %s has a stage with no image", input.definition.name);
      diagnostic_count++;
      stage_index++;
      continue;
    }
    if (current->stage.detail && !input.profile.detail_textures) {
      if (stage_index < 7) {
        for (int move = stage_index; move < 7; move++) stages[move] = stages[move + 1];
        stages[stage_index + 1] = NULL;
      }
      stage_index++;
      continue;
    }
    if (current->tc_gen == QA_MAT_TCGEN_BAD) {
      current->tc_gen = current->is_lightmap ? QA_MAT_TCGEN_LIGHTMAP : QA_MAT_TCGEN_TEXTURE;
      memset(&current->stage.tc_gen, 0, sizeof current->stage.tc_gen);
      current->stage.tc_gen.kind = current->is_lightmap ? "lightmap" : "texture";
    }
    if (current->is_lightmap) has_lightmap_stage = true;
    working_stage *first = stages[0];
    if (first != NULL && is_blended(current) && is_blended(first)) {
      current->fog_adjustment = fog_adjustment_of(current);
      if (sort == 0)
        sort = (current->state_bits & QA_SOURCE_STATE_DEPTHMASK_TRUE) != 0 ? 5 : 9;
    }
    stage_index++;
  }
  if (sort == 0) sort = 3;
  if (stage_index > 1 &&
      ((input.profile.vertex_light && !input.profile.ui_fullscreen) ||
       kind_is(input.profile.hardware, "permedia2"))) {
    vertex_lighting_collapse(st, stages, sort, input.lightmap_index);
    stage_index = 1;
    has_lightmap_stage = false;
  }
  double lightmap_index = input.lightmap_index;
  if (lightmap_index >= 0 && !has_lightmap_stage) {
    diagnostics[diagnostic_count].kind = "lightmap-cleared";
    diagnostics[diagnostic_count].has_stage = false;
    diagnostics[diagnostic_count].stage = 0;
    diagnostics[diagnostic_count].message = s_format(
        st, "Shader %s has lightmap but no lightmap stage", input.definition.name);
    diagnostic_count++;
    lightmap_index = -1;
  }
  size_t source_count = 0;
  qa_mat_finished_iterator_stage *source_stages = NULL;
  if (stage_index > 0) {
    source_stages = (qa_mat_finished_iterator_stage *)s_alloc(
        st, (size_t)stage_index * sizeof *source_stages);
    for (int i = 0; i < stage_index; i++) {
      if (stages[i] == NULL) break;
      source_stages[source_count++] = finished_stage(stages[i]);
    }
  }
  qa_mat_material_iterator iterator = source_material_iterator(
      st, source_stages, source_count, input.definition.sky != NULL,
      input.definition.polygon_offset, input.definition.deform_count,
      input.profile.iterator);
  double num_unfogged = (double)iterator.pass_count;
  if (num_unfogged == 0) sort = 7;
  bool surface_fog = false;
  for (size_t i = 0; i < input.definition.surface_parms.count; i++)
    if (strcmp(input.definition.surface_parms.values[i], "fog") == 0) surface_fog = true;
  out->shader.sort = sort;
  out->shader.lightmap_index = lightmap_index;
  out->shader.has_lightmap_stage = has_lightmap_stage;
  out->shader.source_stages = source_stages;
  out->shader.source_stage_count = source_count;
  out->shader.num_unfogged_passes = num_unfogged;
  out->shader.iterator = iterator;
  out->shader.fog_pass = sort <= 3 ? "equal" : surface_fog ? "less-equal" : "none";
  out->shader.diagnostics = diagnostic_count != 0 ? diagnostics : NULL;
  out->shader.diagnostic_count = diagnostic_count;
}
void qa_finished_shader_result_free(qa_finished_shader_result *result) {
  if (result == NULL) return;
  storage_free((storage *)result->_storage);
  memset(result, 0, sizeof *result);
}

static qa_mat_wave_storage zero_wave(void) {
  qa_mat_wave_storage out = {QA_MAT_WAVE_NONE, 0, 0, 0, 0};
  return out;
}
typedef struct implicit_fields {
  qa_shader_map map;
  qa_color_generator rgb_gen;
  int source_rgb_gen;
  qa_alpha_generator alpha_gen;
  int source_alpha_gen;
  qa_blend_state blend;
  const char *depth_func;
  bool depth_write;
  bool is_lightmap;
} implicit_fields;
static qa_mat_parsed_stage implicit_stage(implicit_fields fields) {
  qa_mat_parsed_stage out;
  memset(&out, 0, sizeof out);
  out.stage.map = fields.map;
  out.stage.blend = fields.blend;
  out.stage.depth_func = fields.depth_func;
  out.stage.depth_write = fields.depth_write;
  out.stage.alpha_func = "none";
  out.stage.detail = false;
  out.stage.rgb_gen = fields.rgb_gen;
  out.stage.alpha_gen = fields.alpha_gen;
  out.stage.tc_gen.kind = fields.is_lightmap ? "lightmap" : "texture";
  bool opaque = kind_is(fields.blend.source, "one") && kind_is(fields.blend.destination, "zero");
  qa_source_state_input input;
  input.depth_test = kind_is(fields.depth_func, "always") ? "less-equal" : fields.depth_func;
  input.depth_write = fields.depth_write;
  input.has_blend = !opaque;
  input.blend_source = fields.blend.source;
  input.blend_destination = fields.blend.destination;
  input.alpha_test = "none";
  out.source_state.active = true;
  out.source_state.state_bits =
      qa_source_state_bits(input, "fill", !kind_is(fields.depth_func, "always"));
  out.source_state.rgb_gen = fields.source_rgb_gen;
  out.source_state.alpha_gen = fields.source_alpha_gen;
  out.source_state.tc_gen = QA_MAT_TCGEN_BAD;
  out.source_state.rgb_wave = zero_wave();
  out.source_state.alpha_wave = zero_wave();
  out.source_state.is_lightmap = fields.is_lightmap;
  out.source_state.vertex_lightmap = false;
  return out;
}
static qa_mat_shader_definition implicit_definition(storage *st, const char *name,
                                                  const qa_mat_parsed_stage *stages,
                                                  size_t stage_count, bool has_sort,
                                                  double sort) {
  qa_mat_shader_definition out;
  memset(&out, 0, sizeof out);
  out.name = name;
  if (stage_count != 0) {
    qa_mat_parsed_stage *copy =
        (qa_mat_parsed_stage *)s_alloc(st, stage_count * sizeof *copy);
    for (size_t i = 0; i < stage_count; i++) copy[i] = stages[i];
    out.stages = copy;
  }
  out.stage_count = stage_count;
  out.cull = "front";
  out.has_sort = has_sort;
  out.sort = sort;
  return out;
}
static qa_color_generator color_gen(const char *kind) {
  qa_color_generator out;
  memset(&out, 0, sizeof out);
  out.kind = kind;
  out.wave.kind = "none";
  return out;
}
static qa_alpha_generator alpha_gen(const char *kind) {
  qa_alpha_generator out;
  memset(&out, 0, sizeof out);
  out.kind = kind;
  out.wave.kind = "none";
  return out;
}

void qa_implicit_shader_input(qa_finish_implicit_shader_input input,
                              qa_finish_shader_input_result *out) {
  storage *st = storage_new();
  memset(out, 0, sizeof *out);
  out->_storage = st;
  qa_shader_map base_map;
  memset(&base_map, 0, sizeof base_map);
  base_map.kind = "image";
  base_map.name = input.name;
  base_map.clamp = kind_is(input.kind, "picture");
  const qa_blend_state opaque = {"one", "zero"};
  const qa_blend_state filter = {"dst-color", "zero"};
  qa_mat_parsed_stage stages[2];
  qa_mat_registered_stage images[2];
  size_t stage_count = 1;
  double lightmap_index;
  bool has_sort = false;
  double sort = 0;
  if (kind_is(input.kind, "default") || kind_is(input.kind, "stencil-shadow")) {
    lightmap_index = -1;
    implicit_fields f = {base_map, color_gen("identitylighting"), QA_MAT_CGEN_BAD,
                         alpha_gen("identity"), QA_MAT_AGEN_IDENTITY, opaque,
                         "less-equal", true, false};
    stages[0] = implicit_stage(f);
    images[0] = input.base_image;
    if (kind_is(input.kind, "stencil-shadow")) {
      has_sort = true;
      sort = 14;
    }
  } else if (kind_is(input.kind, "dynamic")) {
    lightmap_index = -1;
    implicit_fields f = {base_map, color_gen("lightingdiffuse"),
                         QA_MAT_CGEN_LIGHTING_DIFFUSE, alpha_gen("identity"),
                         QA_MAT_AGEN_IDENTITY, opaque, "less-equal", true, false};
    stages[0] = implicit_stage(f);
    images[0] = input.base_image;
  } else if (kind_is(input.kind, "vertex")) {
    lightmap_index = -3;
    implicit_fields f = {base_map, color_gen("exactvertex"), QA_MAT_CGEN_EXACT_VERTEX,
                         alpha_gen("identity"), QA_MAT_AGEN_SKIP, opaque,
                         "less-equal", true, false};
    stages[0] = implicit_stage(f);
    images[0] = input.base_image;
  } else if (kind_is(input.kind, "picture")) {
    lightmap_index = -4;
    const qa_blend_state blend = {"src-alpha", "one-minus-src-alpha"};
    implicit_fields f = {base_map, color_gen("vertex"), QA_MAT_CGEN_VERTEX,
                         alpha_gen("vertex"), QA_MAT_AGEN_VERTEX, blend, "always",
                         false, false};
    stages[0] = implicit_stage(f);
    images[0] = input.base_image;
  } else if (kind_is(input.kind, "white")) {
    lightmap_index = -2;
    qa_shader_map white_map;
    memset(&white_map, 0, sizeof white_map);
    white_map.kind = "whiteimage";
    implicit_fields white = {white_map, color_gen("identitylighting"),
                             QA_MAT_CGEN_IDENTITY_LIGHTING, alpha_gen("identity"),
                             QA_MAT_AGEN_IDENTITY, opaque, "less-equal", true, false};
    implicit_fields base = {base_map, color_gen("identity"), QA_MAT_CGEN_IDENTITY,
                            alpha_gen("identity"), QA_MAT_AGEN_IDENTITY, filter,
                            "less-equal", false, false};
    stages[0] = implicit_stage(white);
    stages[1] = implicit_stage(base);
    images[0] = input.white_image;
    images[1] = input.base_image;
    stage_count = 2;
  } else if (kind_is(input.kind, "lightmap")) {
    if (trunc(input.lightmap_index) != input.lightmap_index ||
        input.lightmap_index < 0 || input.lightmap_index > 2147483647.0)
      qa_range_error("Implicit lightmap index must be a non-negative int32");
    lightmap_index = input.lightmap_index;
    qa_shader_map lightmap_map;
    memset(&lightmap_map, 0, sizeof lightmap_map);
    lightmap_map.kind = "lightmap";
    implicit_fields lightmap = {lightmap_map, color_gen("identity"),
                                QA_MAT_CGEN_IDENTITY, alpha_gen("identity"),
                                QA_MAT_AGEN_IDENTITY, opaque, "less-equal", true, true};
    implicit_fields base = {base_map, color_gen("identity"), QA_MAT_CGEN_IDENTITY,
                            alpha_gen("identity"), QA_MAT_AGEN_IDENTITY, filter,
                            "less-equal", false, false};
    stages[0] = implicit_stage(lightmap);
    stages[1] = implicit_stage(base);
    images[0] = input.lightmap_image;
    images[1] = input.base_image;
    stage_count = 2;
  } else {
    qa_range_error("unknown implicit shader kind '%s'", input.kind);
  }
  qa_mat_registered_stage *image_copy =
      (qa_mat_registered_stage *)s_alloc(st, stage_count * sizeof *image_copy);
  for (size_t i = 0; i < stage_count; i++) image_copy[i] = images[i];
  out->input.definition =
      implicit_definition(st, input.name, stages, stage_count, has_sort, sort);
  out->input.lightmap_index = lightmap_index;
  out->input.images = image_copy;
  out->input.image_count = stage_count;
  out->input.profile = input.profile;
}
void qa_finish_shader_input_result_free(qa_finish_shader_input_result *result) {
  if (result == NULL) return;
  storage_free((storage *)result->_storage);
  memset(result, 0, sizeof *result);
}


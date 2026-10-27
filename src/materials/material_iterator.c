/* Port of src/materials/material-iterator.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/materials/material_iterator.h"
#include "qa/materials/source_state.h"

static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}
static bool same_float(double a, double b) {
  float fa = (float)a, fb = (float)b;
  /* Object.is on fround results: distinguishes -0, equates NaN. */
  if (isnan(fa) && isnan(fb)) return true;
  return memcmp(&fa, &fb, sizeof fa) == 0;
}
static bool same_wave(qa_mat_wave_storage a, qa_mat_wave_storage b) {
  return a.func == b.func && same_float(a.base, b.base) &&
         same_float(a.amplitude, b.amplitude) && same_float(a.phase, b.phase) &&
         same_float(a.frequency, b.frequency);
}

struct collapse {
  const char *env;
  qa_blend_state blend;
  uint32_t bits;
};
static bool collapse_blend(uint32_t first, uint32_t second,
                           struct collapse *out) {
  const uint32_t filter_source = QA_SOURCE_STATE_SRCBLEND_DST_COLOR |
                                 QA_SOURCE_STATE_DSTBLEND_ZERO;
  const uint32_t filter_destination = QA_SOURCE_STATE_SRCBLEND_ZERO |
                                      QA_SOURCE_STATE_DSTBLEND_SRC_COLOR;
  const uint32_t additive = QA_SOURCE_STATE_SRCBLEND_ONE |
                            QA_SOURCE_STATE_DSTBLEND_ONE;
  if (second == filter_source || second == filter_destination) {
    if (first == 0) {
      *out = (struct collapse){"modulate", {"one", "zero"}, 0};
      return true;
    }
    if (first == filter_source || first == filter_destination) {
      *out = (struct collapse){"modulate", {"dst-color", "zero"}, filter_source};
      return true;
    }
  }
  if (second == additive) {
    if (first == 0) {
      *out = (struct collapse){"add", {"one", "zero"}, 0};
      return true;
    }
    if (first == additive) {
      *out = (struct collapse){"add", {"one", "one"}, additive};
      return true;
    }
  }
  return false;
}

qa_mat_material_iterator qa_source_material_iterator(
    qa_material_iterator_input input, qa_mat_iterator_profile profile) {
  const uint32_t blend_mask = QA_SOURCE_STATE_SRCBLEND_BITS |
                              QA_SOURCE_STATE_DSTBLEND_BITS;
  size_t count = input.stage_count;
  qa_mat_iterator_pass *passes = NULL;
  if (count > 0) {
    passes = malloc(count * sizeof *passes);
    if (passes == NULL) qa_range_error("material iterator allocation failed");
  }
  for (size_t i = 0; i < count; i++) {
    const qa_mat_finished_iterator_stage *stage = &input.stages[i];
    passes[i].stage = stage->stage;
    passes[i].state_bits = stage->state_bits;
    passes[i].rgb_gen = stage->rgb_gen;
    passes[i].fog_adjustment = stage->fog_adjustment;
    passes[i].alpha_gen = stage->alpha_gen;
    passes[i].bundles[0] = stage;
    passes[i].bundles[1] = NULL;
    passes[i].bundle_count = 1;
  }
  const char *env = "none";
  if (profile.multitexture && count >= 2) {
    const qa_mat_finished_iterator_stage *a = &input.stages[0];
    const qa_mat_finished_iterator_stage *b = &input.stages[1];
    uint32_t mask = ~(blend_mask | QA_SOURCE_STATE_DEPTHMASK_TRUE);
    if (a->active && b->active &&
        (!eq(profile.driver, "voodoo") || a->image_tmu != b->image_tmu) &&
        (a->state_bits & mask) == (b->state_bits & mask) &&
        a->rgb_gen == b->rgb_gen && eq(a->alpha_gen, b->alpha_gen) &&
        (a->rgb_gen != QA_MAT_CGEN_WAVEFORM || same_wave(a->rgb_wave, b->rgb_wave)) &&
        (!eq(a->alpha_gen, "portal") || same_wave(a->alpha_wave, b->alpha_wave))) {
      struct collapse collapse;
      if (collapse_blend(a->state_bits & blend_mask,
                         b->state_bits & blend_mask, &collapse) &&
          (!eq(collapse.env, "add") ||
           (profile.texture_env_add && a->rgb_gen == QA_MAT_CGEN_IDENTITY))) {
        env = collapse.env;
        passes[0].stage = a->stage;
        passes[0].stage.blend = collapse.blend;
        passes[0].state_bits = (a->state_bits & ~blend_mask) | collapse.bits;
        passes[0].rgb_gen = a->rgb_gen;
        passes[0].fog_adjustment = a->fog_adjustment;
        passes[0].alpha_gen = a->alpha_gen;
        passes[0].bundles[0] = a->is_lightmap ? b : a;
        passes[0].bundles[1] = a->is_lightmap ? a : b;
        passes[0].bundle_count = 2;
        if (count > 2)
          memmove(passes + 1, passes + 2, (count - 2) * sizeof *passes);
        count--;
      }
    }
  }
  const char *kind = "generic";
  if (input.sky) {
    kind = "sky";
  } else if (!profile.ignore_fast_path && count == 1 && !input.polygon_offset &&
             input.deform_count == 0) {
    const qa_mat_iterator_pass *first = &passes[0];
    if (first->rgb_gen == QA_MAT_CGEN_LIGHTING_DIFFUSE &&
        eq(first->alpha_gen, "identity") &&
        first->bundles[0]->tc_gen == QA_MAT_TCGEN_TEXTURE &&
        eq(env, "none"))
      kind = "vertex-lit";
    if (first->rgb_gen == QA_MAT_CGEN_IDENTITY &&
        eq(first->alpha_gen, "identity") &&
        first->bundles[0]->tc_gen == QA_MAT_TCGEN_TEXTURE &&
        first->bundle_count >= 2 &&
        first->bundles[1]->tc_gen == QA_MAT_TCGEN_LIGHTMAP && !eq(env, "none"))
      kind = "lightmapped-multitexture";
  }
  qa_mat_material_iterator out = {kind, passes, count, env};
  return out;
}

void qa_mat_material_iterator_destroy(qa_mat_material_iterator *iterator) {
  free((void *)iterator->passes);
  iterator->passes = NULL;
  iterator->pass_count = 0;
}

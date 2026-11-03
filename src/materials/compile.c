/* Port of src/materials/compile.ts. GPL-2.0-or-later. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/materials/compile.h"
#include "qa/foundation/js_case.h"

const qa_mat_finish_profile qa_default_shader_profile = {
    true, false, false, "generic", {false, true, true, "generic"}};

static qa_waveform semantic_wave(qa_mat_wave_storage wave) {
  static const char *const kinds[7] = {"none", "sin", "square", "triangle",
                                       "sawtooth", "inversesawtooth", "noise"};
  if (wave.func < 0 || wave.func > 6)
    qa_range_error("Unknown source waveform %d", wave.func);
  qa_waveform out = {kinds[wave.func], wave.base, wave.amplitude, wave.phase,
                     wave.frequency};
  return out;
}

qa_render_material qa_shader_render_material(
    qa_mat_shader_definition definition) {
  qa_shader_stage *stages = NULL;
  if (definition.stage_count > 0) {
    stages = calloc(definition.stage_count, sizeof *stages);
    if (stages == NULL) qa_range_error("compile allocation failed");
  }
  for (size_t i = 0; i < definition.stage_count; i++) {
    const qa_mat_parsed_stage *stage = &definition.stages[i];
    qa_tex_coord_modifier *modifiers = NULL;
    if (stage->stage.tc_mod_count > 0) {
      modifiers = malloc(stage->stage.tc_mod_count * sizeof *modifiers);
      if (modifiers == NULL) qa_range_error("compile allocation failed");
    }
    for (size_t m = 0; m < stage->stage.tc_mod_count; m++) {
      const qa_mat_tc_modifier *modifier = &stage->stage.tc_mods[m];
      memset(&modifiers[m], 0, sizeof modifiers[m]);
      modifiers[m].kind = modifier->kind;
      modifiers[m].amount = modifier->amount;
      modifiers[m].wave = modifier->wave;
      modifiers[m].degrees_per_second = modifier->degrees_per_second;
      modifiers[m].translation = modifier->translation;
      modifiers[m].matrix[0] = modifier->m00;
      modifiers[m].matrix[1] = modifier->m01;
      modifiers[m].matrix[2] = modifier->m10;
      modifiers[m].matrix[3] = modifier->m11;
    }
    stages[i].map = stage->stage.map;
    stages[i].blend = stage->stage.blend;
    stages[i].depth_test = stage->stage.depth_func;
    stages[i].depth_write = stage->stage.depth_write;
    stages[i].alpha_test = stage->stage.alpha_func;
    stages[i].detail = stage->stage.detail;
    stages[i].color = stage->stage.rgb_gen;
    stages[i].alpha = stage->stage.alpha_gen;
    stages[i].coordinates = stage->stage.tc_gen;
    stages[i].modifiers = modifiers;
    stages[i].modifier_count = stage->stage.tc_mod_count;
    stages[i].source_state.active = stage->source_state.active;
    stages[i].source_state.state_bits = stage->source_state.state_bits;
    stages[i].source_state.rgb_gen = stage->source_state.rgb_gen;
    stages[i].source_state.alpha_gen = stage->source_state.alpha_gen;
    stages[i].source_state.tc_gen = stage->source_state.tc_gen;
    stages[i].source_state.rgb_wave = semantic_wave(stage->source_state.rgb_wave);
    stages[i].source_state.alpha_wave =
        semantic_wave(stage->source_state.alpha_wave);
    stages[i].source_state.is_lightmap = stage->source_state.is_lightmap;
    stages[i].source_state.vertex_lightmap =
        stage->source_state.vertex_lightmap;
  }
  qa_render_material out;
  memset(&out, 0, sizeof out);
  out.kind = "q3";
  out.name = definition.name;
  out.stages = stages;
  out.stage_count = definition.stage_count;
  out.deformations = definition.deforms;
  out.deformation_count = definition.deform_count;
  out.surface_parameters = definition.surface_parms;
  out.has_sort = definition.has_sort;
  out.sort = definition.sort;
  out.cull = definition.cull;
  out.polygon_offset = definition.polygon_offset;
  out.no_mipmaps = definition.no_mip_maps;
  out.no_picmip = definition.no_pic_mip;
  out.entity_mergeable = definition.entity_mergable;
  out.portal_range = definition.portal_range;
  out.clamp_time = definition.clamp_time;
  if (definition.sky != NULL) {
    qa_q3_material_sky *sky = malloc(sizeof *sky);
    if (sky == NULL) qa_range_error("compile allocation failed");
    sky->outer_box = definition.sky->outer_box;
    sky->inner_box = definition.sky->inner_box;
    sky->cloud_height = definition.sky->cloud_height;
    out.sky = sky;
  }
  out.fog = definition.fog;
  out.sun = definition.sun;
  return out;
}

void qa_shader_render_material_destroy(qa_render_material *material) {
  for (size_t i = 0; i < material->stage_count; i++)
    free((void *)material->stages[i].modifiers);
  free((void *)material->stages);
  free((void *)material->sky);
  material->stages = NULL;
  material->stage_count = 0;
  material->sky = NULL;
}

qa_mat_compiled_material *qa_compile_shader_script(
    const qa_mat_catalog_entry *entries, size_t entry_count,
    const qa_mat_registration_host *host, qa_mat_finish_service finish,
    qa_compile_shader_options options, size_t *out_count) {
  qa_mat_compiled_material *materials = NULL;
  if (entry_count > 0) {
    materials = malloc(entry_count * sizeof *materials);
    if (materials == NULL) qa_range_error("compile allocation failed");
  }
  for (size_t i = 0; i < entry_count; i++) {
    qa_mat_registered_explicit_shader registered =
        entries[i].register_shader(entries[i].ctx, host);
    qa_mat_finish_input input;
    input.definition = registered.definition;
    input.images = registered.stages;
    input.image_count = registered.stage_count;
    input.lightmap_index = options.has_lightmap_index
        ? options.lightmap_index : -1;
    input.profile = options.profile != NULL ? *options.profile
                                             : qa_default_shader_profile;
    materials[i].registered = registered;
    materials[i].finished = finish.finish(finish.ctx, input);
    materials[i].material = qa_shader_render_material(registered.definition);
  }
  *out_count = entry_count;
  return materials;
}

void qa_compiled_material_destroy(qa_mat_compiled_material *material) {
  qa_shader_render_material_destroy(&material->material);
}

struct flag_entry { const char *name; uint32_t value; };
static const struct flag_entry surface_flags[] = {
    {"nodamage", 1}, {"slick", 2}, {"sky", 4}, {"ladder", 8},
    {"noimpact", 0x10}, {"nomarks", 0x20}, {"flesh", 0x40},
    {"nodraw", 0x80}, {"hint", 0x100}, {"nolightmap", 0x400},
    {"pointlight", 0x800}, {"metalsteps", 0x1000}, {"nosteps", 0x2000},
    {"nonsolid", 0x4000}, {"lightfilter", 0x8000}, {"alphashadow", 0x10000},
    {"nodlight", 0x20000}, {"dust", 0x40000}};
static const struct flag_entry content_flags[] = {
    {"lava", 8}, {"slime", 16}, {"water", 32}, {"fog", 64},
    {"areaportal", 0x8000}, {"playerclip", 0x10000}, {"monsterclip", 0x20000},
    {"clusterportal", 0x100000}, {"donotenter", 0x200000},
    {"origin", 0x1000000}, {"detail", 0x8000000}, {"structural", 0x10000000},
    {"trans", 0x20000000}, {"nodrop", 0x80000000u}};
static const char *const non_solid[] = {
    "water", "slime", "lava", "playerclip", "monsterclip", "nodrop",
    "nonsolid", "origin", "areaportal", "clusterportal", "donotenter", "fog"};

static uint32_t flag_value(const struct flag_entry *table, size_t count,
                           const char *key, size_t length) {
  for (size_t i = 0; i < count; i++)
    if (strlen(table[i].name) == length &&
        memcmp(table[i].name, key, length) == 0)
      return table[i].value;
  return 0;
}

qa_surface_parameter_flags qa_shader_surface_flags(
    const char *const *parameters, size_t parameter_count) {
  qa_surface_parameter_flags out = {0, 0, false};
  for (size_t i = 0; i < parameter_count; i++) {
    const char *parameter = parameters[i];
    size_t length = strlen(parameter);
    /* TS parameter.toLowerCase(): Unicode lowering (U+212A KELVIN SIGN becomes the 'k'
     * of "sky", "skip", "slick"). */
    char *key = qa_js_to_lower_case(parameter);
    if (key == NULL) qa_range_error("compile allocation failed");
    length = strlen(key);
    out.surface |= flag_value(surface_flags,
        sizeof surface_flags / sizeof surface_flags[0], key, length);
    out.contents |= flag_value(content_flags,
        sizeof content_flags / sizeof content_flags[0], key, length);
    for (size_t n = 0; n < sizeof non_solid / sizeof non_solid[0]; n++)
      if (strcmp(key, non_solid[n]) == 0) out.clear_solid = true;
    free(key);
  }
  return out;
}

qa_mat_compiled_material qa_compile_implicit_material(
    qa_mat_finish_implicit_input input, qa_mat_finish_service finish) {
  qa_mat_finish_input prepared = finish.implicit_input(finish.ctx, input);
  qa_mat_compiled_material out;
  memset(&out, 0, sizeof out);
  out.registered.kind = "defined";
  out.registered.definition = prepared.definition;
  out.registered.stages = prepared.images;
  out.registered.stage_count = prepared.image_count;
  out.registered.sky = NULL;
  out.finished = finish.finish(finish.ctx, prepared);
  out.material = qa_shader_render_material(prepared.definition);
  return out;
}

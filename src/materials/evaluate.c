/* Port of src/materials/evaluate.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/materials/evaluate.h"
#include "qa/materials/fog.h"
#include "qa/materials/material_iterator.h"
#include "qa/materials/source_state.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}

void qa_mat_draw_batches_destroy(qa_draw_batch *batches, size_t count) {
  if (batches == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free((void *)batches[i].vertices);
    free((void *)batches[i].pair_vertices);
    free((void *)batches[i].indices.values);
    free((void *)batches[i].fog);
  }
  free(batches);
}

/* Mirrored evaluateTexCoords (TS owner: material.ts). */
struct tex_context {
  const qa_vec2 *lightmap;
  const qa_vec3 *view_origin;
  const qa_vec2 *shader_tex_coord;
};
static qa_vec2 evaluate_tex_coords(qa_mat_shader_stage stage, qa_vec2 uv,
                                   qa_vec3 position, qa_vec3 normal,
                                   double time, struct tex_context context) {
  qa_vec2 result;
  if (eq(stage.tc_gen.kind, "texture")) {
    result = uv;
  } else if (eq(stage.tc_gen.kind, "lightmap")) {
    if (context.lightmap == NULL)
      qa_range_error("Lightmap tcGen requires lightmap coordinates");
    result = *context.lightmap;
  } else if (eq(stage.tc_gen.kind, "vector")) {
    result.x = qa_dot3(position, stage.tc_gen.s);
    result.y = qa_dot3(position, stage.tc_gen.t);
  } else { /* environment */
    if (context.view_origin == NULL)
      qa_range_error("Environment tcGen requires the view origin in model coordinates");
    qa_vec3 viewer = qa_normalize_fast3(qa_sub3(*context.view_origin, position));
    double d = qa_dot3(normal, viewer);
    double ry = fr(fr(fr(normal.y * 2) * d) - viewer.y);
    double rz = fr(fr(fr(normal.z * 2) * d) - viewer.z);
    result.x = 0.5 + ry * 0.5;
    result.y = 0.5 - rz * 0.5;
  }
  result = (qa_vec2){fr(result.x), fr(result.y)};
  for (size_t m = 0; m < stage.tc_mod_count; m++) {
    qa_mat_tc_modifier mod = stage.tc_mods[m];
    double s = result.x, t = result.y;
    if (eq(mod.kind, "none")) {
      qa_vec2 done = {fr(result.x), fr(result.y)};
      return done;
    } else if (eq(mod.kind, "scale")) {
      result = (qa_vec2){s * mod.amount.x, t * mod.amount.y};
    } else if (eq(mod.kind, "scroll")) {
      double x = fr(mod.amount.x * time), y = fr(mod.amount.y * time);
      result = (qa_vec2){s + fr(x - floor(x)), t + fr(y - floor(y))};
    } else if (eq(mod.kind, "entitytranslate")) {
      if (context.shader_tex_coord == NULL)
        qa_range_error("entityTranslate requires entity shaderTexCoord");
      double x = fr(context.shader_tex_coord->x * time);
      double y = fr(context.shader_tex_coord->y * time);
      result = (qa_vec2){s + fr(x - floor(x)), t + fr(y - floor(y))};
    } else if (eq(mod.kind, "transform")) {
      result = (qa_vec2){fr(fr(s * mod.m00) + fr(t * mod.m10)) + mod.translation.x,
                         fr(fr(s * mod.m01) + fr(t * mod.m11)) + mod.translation.y};
    } else if (eq(mod.kind, "rotate")) {
      int32_t index = qa_mat_shader_table_index(
          fr(fr(-mod.degrees_per_second * time) * fr(1024.0 / 360)));
      double sine = qa_renderer_sine(index);
      double cosine = qa_renderer_sine(index + 256);
      result = (qa_vec2){
          fr(fr(s * cosine) + fr(t * -sine)) + fr(0.5 - 0.5 * cosine + 0.5 * sine),
          fr(fr(s * sine) + fr(t * cosine)) + fr(0.5 - 0.5 * sine - 0.5 * cosine)};
    } else if (eq(mod.kind, "stretch")) {
      double scale = fr(1.0 / qa_mat_evaluate_waveform(mod.wave, time));
      double translate = fr(0.5 - fr(0.5 * scale));
      result = (qa_vec2){fr(fr(s * scale) + fr(t * 0)) + translate,
                         fr(fr(s * 0) + fr(t * scale)) + translate};
    } else if (eq(mod.kind, "turb")) {
      double now = fr(mod.wave.phase + fr(time * mod.wave.frequency));
      int32_t sx = qa_mat_shader_table_index(
          (fr(position.x + position.z) / 1024 + now) * 1024);
      int32_t sy = qa_mat_shader_table_index(
          (position.y / 1024 + now) * 1024);
      result = (qa_vec2){s + fr(qa_renderer_sine(sx) * mod.wave.amplitude),
                         t + fr(qa_renderer_sine(sy) * mod.wave.amplitude)};
    }
    result = (qa_vec2){fr(result.x), fr(result.y)};
  }
  qa_vec2 done = {fr(result.x), fr(result.y)};
  return done;
}

/* Mirrored stageState (TS owner: material.ts). */
static qa_render_state stage_state(qa_mat_shader_stage stage,
                                   const char *cull) {
  qa_render_state out = {stage.blend, stage.depth_func, stage.depth_write,
                         stage.alpha_func, cull, {0, 1}, NULL};
  return out;
}

static qa_texture_binding texture_binding(
    const qa_mat_finished_iterator_stage *bundle, double time) {
  qa_mat_finished_binding binding = bundle->binding;
  qa_texture_binding out;
  memset(&out, 0, sizeof out);
  if (eq(binding.kind, "retain-current-texture")) {
    out.kind = QA_TEXTURE_BINDING_RETAIN_CURRENT;
    return out;
  }
  if (eq(binding.kind, "video")) {
    out.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE;
    out.source = binding.source.source;
    return out;
  }
  qa_mat_finished_playback playback = binding.playback;
  out.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
  if (eq(playback.kind, "single")) {
    out.image = playback.image.image;
    return out;
  }
  double index = qa_animated_picture_index(time, playback.frequency,
                                           (double)playback.frame_count);
  if ((size_t)index >= playback.frame_count)
    qa_range_error("Animation frame is outside the registered image bundle");
  out.image = playback.frames[(size_t)index].image;
  return out;
}

static qa_vec2 coordinates(const qa_mat_finished_iterator_stage *bundle,
                           qa_material_vertex vertex, double time,
                           const qa_material_draw_context *context) {
  qa_mat_shader_stage stage = bundle->stage;
  qa_vec2 input = vertex.tex_coord;
  switch (bundle->tc_gen) {
    case QA_MAT_TCGEN_BAD:
      qa_range_error("Uninitialized source texture coordinates need retained tess storage");
    case QA_MAT_TCGEN_IDENTITY:
      input = (qa_vec2){0, 0};
      stage.tc_gen.kind = "texture";
      break;
    case QA_MAT_TCGEN_TEXTURE: stage.tc_gen.kind = "texture"; break;
    case QA_MAT_TCGEN_LIGHTMAP: stage.tc_gen.kind = "lightmap"; break;
    case QA_MAT_TCGEN_ENVIRONMENT_MAPPED:
      stage.tc_gen.kind = "environment";
      break;
    case QA_MAT_TCGEN_FOG:
      if (context->fog == NULL)
        qa_range_error("Fog texture coordinates require the current volume");
      input = context->fog->coordinates(context->fog->ctx, vertex.position);
      stage.tc_gen.kind = "texture";
      break;
    case QA_MAT_TCGEN_VECTOR:
      if (!eq(stage.tc_gen.kind, "vector"))
        qa_range_error("Vector texture coordinates lost source projection vectors");
      break;
    default: break;
  }
  struct tex_context tc = {&vertex.lightmap_coord, &context->local_view_origin,
                           &context->shader_tex_coord};
  return evaluate_tex_coords(stage, input, vertex.position, vertex.normal,
                             time, tc);
}

struct retained_ctx { qa_render_state result; };
static void apply_change(qa_source_state_change change, void *ctx) {
  struct retained_ctx *state = ctx;
  if (eq(change.kind, "depth-function")) {
    state->result.depth_test = change.value;
  } else if (eq(change.kind, "blend")) {
    state->result.blend.source = change.enabled ? change.source : "one";
    state->result.blend.destination =
        change.enabled ? change.destination : "zero";
  } else if (eq(change.kind, "depth-write")) {
    state->result.depth_write = change.bool_value;
  } else if (eq(change.kind, "depth-test")) {
    if (!change.enabled) {
      state->result.depth_test = "always";
      state->result.depth_write = false;
    }
  } else if (eq(change.kind, "alpha-test")) {
    state->result.alpha_test = change.value;
  } else if (eq(change.kind, "polygon-mode")) {
    if (eq(change.value, "line"))
      qa_range_error("Material polygon-line state requires explicit line geometry");
  }
}
static qa_render_state retained_state(uint32_t bits, qa_render_state initial) {
  struct retained_ctx state = {initial};
  qa_source_state_changes(false, 0, bits, apply_change, &state);
  return state.result;
}

struct batch_list {
  qa_draw_batch *batches;
  size_t count, capacity;
};
static qa_draw_batch *push_batch(struct batch_list *list) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    list->batches = realloc(list->batches,
                            list->capacity * sizeof *list->batches);
    if (list->batches == NULL) qa_range_error("evaluate allocation failed");
  }
  qa_draw_batch *batch = &list->batches[list->count++];
  memset(batch, 0, sizeof *batch);
  return batch;
}
static void append_batches(struct batch_list *list, qa_draw_batch *batches,
                           size_t count) {
  for (size_t i = 0; i < count; i++) *push_batch(list) = batches[i];
  free(batches);
}
static double *copy_indices(qa_material_geometry geometry) {
  double *indices = malloc(
      (geometry.index_count > 0 ? geometry.index_count : 1) * sizeof *indices);
  if (indices == NULL) qa_range_error("evaluate allocation failed");
  if (geometry.index_count > 0)
    memcpy(indices, geometry.indices,
           geometry.index_count * sizeof *indices);
  return indices;
}
static qa_batch_fog *make_fog(const char *effect, qa_vec3 color,
                              double density) {
  qa_batch_fog *fog = malloc(sizeof *fog);
  if (fog == NULL) qa_range_error("evaluate allocation failed");
  memset(fog, 0, sizeof *fog);
  fog->kind = "exp2";
  fog->effect = effect;
  fog->color = color;
  fog->density = density;
  return fog;
}

qa_draw_batch *qa_prepare_material_batches(
    const qa_mat_compiled_material *compiled, qa_material_geometry input,
    const qa_material_draw_context *context, size_t *out_count) {
  if (eq(compiled->finished.iterator.kind, "sky"))
    qa_range_error("Sky materials require sky-box/cloud geometry preparation before ordinary stage evaluation");
  return qa_evaluate_material_passes(compiled, input, context, out_count);
}

qa_draw_batch *qa_evaluate_material_passes(
    const qa_mat_compiled_material *compiled, qa_material_geometry input,
    const qa_material_draw_context *context, size_t *out_count) {
  qa_mat_shader_definition definition = compiled->registered.definition;
  double time = context->time - context->time_offset;
  if (definition.clamp_time != 0 && time >= definition.clamp_time)
    time = definition.clamp_time;
  time = fr(time);
  qa_material_deform_state state;
  qa_material_deform_state_init(&state, input, context->refdef_time,
      context->render_text, context->render_text_count, definition.name);
  qa_deform_geometry geometry = qa_deform_geometry_apply(&state,
      definition.deforms, definition.deform_count, context->deform_view, time,
      context->noise, context->projection_shadow);
  /* Snapshot geometry values before later state use ends: copy the
   * vertex/index views' contents lives in state until destroy below. */
  size_t vcount = geometry.vertex_count;
  qa_material_vertex *owned_vertices = malloc(
      (vcount > 0 ? vcount : 1) * sizeof *owned_vertices);
  double *owned_indices = copy_indices(geometry);
  if (owned_vertices == NULL) qa_range_error("evaluate allocation failed");
  if (vcount > 0)
    memcpy(owned_vertices, geometry.vertices, vcount * sizeof *owned_vertices);
  geometry.vertices = owned_vertices;
  geometry.indices = owned_indices;
  qa_material_deform_state_destroy(&state);

  struct batch_list list = {NULL, 0, 0};
  qa_vec4 *previous_colors = calloc(vcount > 0 ? vcount : 1,
                                    sizeof *previous_colors);
  qa_vec4 *first_positions = malloc(
      (vcount > 0 ? vcount : 1) * sizeof *first_positions);
  if (previous_colors == NULL || first_positions == NULL)
    qa_range_error("evaluate allocation failed");
  bool have_first_positions = false;

  bool has_lightmap_lighting = false, has_model_lighting = false;
  qa_batch_lighting lightmap_lighting, model_lighting;
  memset(&lightmap_lighting, 0, sizeof lightmap_lighting);
  memset(&model_lighting, 0, sizeof model_lighting);
  if (context->lightmap_lighting != NULL && compiled->finished.has_lightmap_stage) {
    lightmap_lighting = context->lightmap_lighting(
        context->lightmap_lighting_ctx, geometry);
    has_lightmap_lighting = true;
  }
  bool any_diffuse = false;
  for (size_t i = 0; i < compiled->finished.source_stage_count; i++)
    if (compiled->finished.source_stages[i].rgb_gen ==
        QA_MAT_CGEN_LIGHTING_DIFFUSE)
      any_diffuse = true;
  if (context->model_lighting != NULL && any_diffuse) {
    model_lighting = context->model_lighting(context->model_lighting_ctx,
                                              geometry);
    has_model_lighting = true;
  }
  qa_mat_material_iterator owned_iterator;
  const qa_mat_material_iterator *iterator = &compiled->finished.iterator;
  bool own_iterator = false;
  if (has_lightmap_lighting || has_model_lighting) {
    qa_material_iterator_input iterator_input = {
        compiled->finished.source_stages,
        compiled->finished.source_stage_count, definition.sky != NULL,
        definition.polygon_offset, (double)definition.deform_count};
    qa_mat_iterator_profile profile = {true, false, false, "generic"};
    owned_iterator = qa_source_material_iterator(iterator_input, profile);
    iterator = &owned_iterator;
    own_iterator = true;
  }

  for (size_t pi = 0; pi < iterator->pass_count; pi++) {
    const qa_mat_iterator_pass *pass = &iterator->passes[pi];
    if (pass->bundle_count == 0) continue;
    const qa_mat_finished_iterator_stage *first = pass->bundles[0];
    if (!first->active) continue;
    const qa_mat_finished_iterator_stage *second =
        pass->bundle_count >= 2 ? pass->bundles[1] : NULL;
    qa_render_state initial = stage_state(pass->stage, definition.cull);
    initial.depth_range[0] = context->depth_range[0];
    initial.depth_range[1] = context->depth_range[1];
    initial.polygon_offset = definition.polygon_offset
        ? context->polygon_offset : NULL;
    qa_render_state render_state = retained_state(pass->state_bits, initial);
    qa_texture_binding texture = texture_binding(first, time);
    const char *adjustment = pass->fog_adjustment;
    bool fragment_fog = context->has_q1_fog && context->q1_fog.density > 0;
    qa_render_vertex *vertices = malloc(
        (vcount > 0 ? vcount : 1) * sizeof *vertices);
    if (vertices == NULL) qa_range_error("evaluate allocation failed");
    for (size_t vi = 0; vi < vcount; vi++) {
      qa_material_vertex vertex = geometry.vertices[vi];
      qa_stage_color_context color_ctx;
      color_ctx.time = time;
      color_ctx.identity_light = context->identity_light;
      color_ctx.entity_rgba = context->entity_rgba;
      color_ctx.lighting = context->lighting;
      color_ctx.view_origin = context->view_origin;
      color_ctx.local_view_origin = context->local_view_origin;
      color_ctx.noise = context->noise;
      color_ctx.previous_color = previous_colors[vi];
      qa_vec4 color = qa_evaluate_stage_color(pass->stage, vertex, color_ctx,
          eq(pass->alpha_gen, "skip"), true, pass->rgb_gen);
      if (context->fog != NULL)
        color = qa_attenuate_fog_color(color, adjustment,
            context->fog->coordinates(context->fog->ctx, vertex.position));
      previous_colors[vi] = color;
      vertices[vi].position = have_first_positions ? first_positions[vi]
          : context->project.project(context->project.ctx, vertex.position);
      vertices[vi].color = color;
      vertices[vi].tex_coord = coordinates(first, vertex, time, context);
    }
    if (!have_first_positions) {
      for (size_t vi = 0; vi < vcount; vi++)
        first_positions[vi] = vertices[vi].position;
      have_first_positions = true;
    }
    qa_draw_batch *batch = push_batch(&list);
    batch->state = render_state;
    batch->texture = texture;
    batch->indices.values = copy_indices(geometry);
    batch->indices.count = geometry.index_count;
    if (fragment_fog)
      batch->fog = make_fog(adjustment, context->q1_fog.color,
                            context->q1_fog.density);
    if (second == NULL) {
      if (first->is_lightmap && has_lightmap_lighting) {
        batch->lighting = lightmap_lighting;
        batch->lighting.pass = "material-lightmap";
      } else if (pass->rgb_gen == QA_MAT_CGEN_LIGHTING_DIFFUSE &&
                 has_model_lighting) {
        batch->lighting = model_lighting;
      } else {
        batch->lighting.kind = "vertex";
      }
      batch->vertices = vertices;
      batch->vertex_count = vcount;
    } else {
      if (!second->active)
        qa_range_error("Collapsed stage lost its second registered texture");
      qa_texture_binding second_texture = texture_binding(second, time);
      qa_multitexture_vertex *paired = malloc(
          (vcount > 0 ? vcount : 1) * sizeof *paired);
      if (paired == NULL) qa_range_error("evaluate allocation failed");
      for (size_t vi = 0; vi < vcount; vi++) {
        paired[vi].vertex = vertices[vi];
        paired[vi].tex_coord2 = coordinates(second, geometry.vertices[vi],
                                             time, context);
      }
      free(vertices);
      batch->lighting.kind = "vertex";
      batch->texturing_is_pair = true;
      batch->pair_vertices = paired;
      batch->pair_vertex_count = vcount;
      batch->second_texture.binding = second_texture;
      batch->second_texture.environment =
          eq(iterator->multitexture_env, "add") ? "add" : "modulate";
    }
  }
  if (own_iterator) qa_mat_material_iterator_destroy(&owned_iterator);

  if (context->dynamic_light_batches != NULL &&
      qa_receives_projected_dlights(compiled)) {
    size_t extra_count = 0;
    qa_draw_batch *extra = context->dynamic_light_batches(
        context->dynamic_light_batches_ctx, geometry, &extra_count);
    append_batches(&list, extra, extra_count);
  } else if (context->has_dynamic_lights &&
             qa_receives_projected_dlights(compiled)) {
    size_t extra_count = 0;
    qa_draw_batch *extra = qa_project_dlight_texture(geometry,
        context->dynamic_light_mask, context->dynamic_lights,
        context->dynamic_light_count, context->dynamic_light_image,
        context->project, definition.cull, &extra_count);
    for (size_t i = 0; i < extra_count; i++) {
      extra[i].state.depth_range[0] = context->depth_range[0];
      extra[i].state.depth_range[1] = context->depth_range[1];
      extra[i].state.polygon_offset = definition.polygon_offset
          ? context->polygon_offset : NULL;
    }
    append_batches(&list, extra, extra_count);
  }

  if (context->fog != NULL && !eq(compiled->finished.fog_pass, "none")) {
    qa_draw_batch *batch = push_batch(&list);
    batch->lighting.kind = "vertex";
    batch->texture = context->fog->texture;
    batch->indices.values = copy_indices(geometry);
    batch->indices.count = geometry.index_count;
    batch->state = qa_fog_pass_state(compiled->finished.fog_pass,
                                     definition.cull);
    batch->state.depth_range[0] = context->depth_range[0];
    batch->state.depth_range[1] = context->depth_range[1];
    batch->state.polygon_offset = definition.polygon_offset
        ? context->polygon_offset : NULL;
    qa_render_vertex *vertices = malloc(
        (vcount > 0 ? vcount : 1) * sizeof *vertices);
    if (vertices == NULL) qa_range_error("evaluate allocation failed");
    for (size_t vi = 0; vi < vcount; vi++) {
      vertices[vi].position = context->project.project(
          context->project.ctx, geometry.vertices[vi].position);
      vertices[vi].tex_coord = context->fog->coordinates(
          context->fog->ctx, geometry.vertices[vi].position);
      vertices[vi].color = context->fog->color;
    }
    batch->vertices = vertices;
    batch->vertex_count = vcount;
  }

  if (context->has_q1_fog && context->q1_fog.density > 0) {
    for (size_t i = 0; i < list.count; i++)
      if (list.batches[i].fog == NULL)
        list.batches[i].fog = make_fog("none", context->q1_fog.color,
                                       context->q1_fog.density);
    if (!eq(compiled->finished.fog_pass, "none")) {
      qa_draw_batch *batch = push_batch(&list);
      batch->lighting.kind = "vertex";
      batch->texture = context->q1_fog.texture;
      batch->indices.values = copy_indices(geometry);
      batch->indices.count = geometry.index_count;
      batch->fog = make_fog("overlay", context->q1_fog.color,
                            context->q1_fog.density);
      batch->state = qa_fog_pass_state(compiled->finished.fog_pass,
                                       definition.cull);
      batch->state.depth_range[0] = context->depth_range[0];
      batch->state.depth_range[1] = context->depth_range[1];
      batch->state.polygon_offset = definition.polygon_offset
          ? context->polygon_offset : NULL;
      qa_render_vertex *vertices = malloc(
          (vcount > 0 ? vcount : 1) * sizeof *vertices);
      if (vertices == NULL) qa_range_error("evaluate allocation failed");
      for (size_t vi = 0; vi < vcount; vi++) {
        vertices[vi].position = context->project.project(
            context->project.ctx, geometry.vertices[vi].position);
        vertices[vi].tex_coord = (qa_vec2){0, 0};
        vertices[vi].color = (qa_vec4){1, 1, 1, 1};
      }
      batch->vertices = vertices;
      batch->vertex_count = vcount;
    }
  }

  free(previous_colors);
  free(first_positions);
  free(owned_vertices);
  free(owned_indices);
  *out_count = list.count;
  return list.batches;
}

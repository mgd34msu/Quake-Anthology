/* Port of src/compat/qvm/client-render-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_render_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

typedef struct resource_traps {
  int32_t model, skin, shader, shader_no_mip, color, picture, bounds, tag, remap;
} resource_traps;

static const resource_traps ui_traps = {18, 19, -1, 20, 26, 27, 56, 29, 80};
static const resource_traps cgame_traps = {37, 38, 39, 57, 45, 46, 47, 48, 79};

static char *read_string(qa_qvm_memory *memory, int32_t word) {
  char *text = qa_qvm_memory_read_string(memory, word);
  if (text == NULL) qa_range_error("out of memory");
  return text;
}

static int32_t word_at(const qa_qvm_host_call *call, size_t index) {
  return qa_qvm_host_call_word(call, index);
}
static float float_at(const qa_qvm_host_call *call, size_t byte_offset) {
  return qa_qvm_host_call_word_f32(call, byte_offset);
}

static void out_of_memory(bool ok) {
  if (!ok) qa_range_error("out of memory");
}

typedef struct picture_ctx {
  qa_q3_pres_q3_renderer_resources *resources;
  int32_t shader_word;
} picture_ctx;

/* TS () => resources.picture(resources.shaderForHandle(shaderWord)). */
static qa_picture_asset picture_thunk(void *ctx) {
  picture_ctx *picture = ctx;
  qa_picture_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_MATERIAL;
  asset.material = qa_q3_pres_q3_renderer_resources_picture(
      picture->resources,
      qa_q3_pres_q3_renderer_resources_shader_for_handle(picture->resources, picture->shader_word));
  return asset;
}

static void write_vector(qa_qvm_writable_view *view, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, 8, value.z, true);
}

static bool resource_syscall(const qa_qvm_host_call *call, qa_q3_pres_q3_renderer_resources *resources,
                             const qa_draw2d *commands, qa_qvm_host_result *out) {
  /* role "game" never reaches here: the caller rejects qagame. */
  int32_t trap = word_at(call, 0);
  const resource_traps *ids = strcmp(call->role, "ui") == 0 ? &ui_traps : &cgame_traps;
  qa_qvm_memory *memory = call->guest;
  if (trap == ids->model) {
    int32_t name_word = word_at(call, 1);
    char *name = name_word == 0 ? NULL : read_string(memory, name_word);
    qa_q3_pres_scene_model model;
    out_of_memory(qa_q3_pres_q3_renderer_resources_register_model(resources, name == NULL ? "" : name, &model));
    free(name);
    *out = qa_int32(qa_q3_pres_q3_renderer_resources_model_handle(resources, &model));
    return true;
  }
  if (trap == ids->skin) {
    int32_t name_word = word_at(call, 1);
    char *name = name_word == 0 ? NULL : read_string(memory, name_word);
    const qa_q3_pres_scene_skin *skin = NULL;
    out_of_memory(qa_q3_pres_q3_renderer_resources_register_skin(resources, name == NULL ? "" : name, &skin));
    free(name);
    *out = qa_int32(qa_q3_pres_q3_renderer_resources_skin_handle(resources, skin));
    return true;
  }
  if ((ids->shader >= 0 && trap == ids->shader) || trap == ids->shader_no_mip) {
    char *name = read_string(memory, word_at(call, 1));
    const qa_q3_pres_scene_shader *shader = NULL;
    out_of_memory(trap == ids->shader
                      ? qa_q3_pres_q3_renderer_resources_register_shader(resources, name, &shader)
                      : qa_q3_pres_q3_renderer_resources_register_shader_no_mip(resources, name, &shader));
    free(name);
    *out = qa_int32(qa_q3_pres_q3_renderer_resources_shader_handle(resources, shader));
    return true;
  }
  if (trap == ids->color) {
    int32_t word = word_at(call, 1);
    if (word == 0) {
      qa_draw2d_set_color(commands, NULL);
    } else {
      qa_qvm_writable_view color;
      qa_qvm_memory_view(memory, word, 16, 0, &color);
      qa_vec4 value = {qa_qvm_writable_view_get_float32(&color, 0, true),
                       qa_qvm_writable_view_get_float32(&color, 4, true),
                       qa_qvm_writable_view_get_float32(&color, 8, true),
                       qa_qvm_writable_view_get_float32(&color, 12, true)};
      qa_draw2d_set_color(commands, &value);
    }
    *out = 0;
    return true;
  }
  if (trap == ids->picture) {
    qa_rect rect = {float_at(call, 4), float_at(call, 8), float_at(call, 12), float_at(call, 16)};
    qa_text_texture_rect uv = {float_at(call, 20), float_at(call, 24), float_at(call, 28), float_at(call, 32)};
    picture_ctx picture = {resources, word_at(call, 9)};
    qa_draw2d_stretch_pixels_lazy(commands, rect, uv, picture_thunk, &picture);
    *out = 0;
    return true;
  }
  if (trap == ids->bounds) {
    int32_t model_word = word_at(call, 1);
    qa_qvm_writable_view min, max;
    qa_qvm_memory_view(memory, word_at(call, 2), 12, 0, &min);
    qa_qvm_memory_view(memory, word_at(call, 3), 12, 0, &max);
    qa_q3_pres_scene_model model = qa_q3_pres_q3_renderer_resources_model_for_handle(resources, model_word);
    qa_bounds bounds = qa_q3_pres_model_bounds(&model);
    write_vector(&min, bounds.min);
    write_vector(&max, bounds.max);
    *out = 0;
    return true;
  }
  if (trap == ids->tag) {
    qa_qvm_writable_view destination;
    qa_qvm_memory_view(memory, word_at(call, 1), QA_QVM_ORIENTATION_BYTES, 0, &destination);
    int32_t model_word = word_at(call, 2);
    int32_t start = word_at(call, 3);
    int32_t end = word_at(call, 4);
    float fraction = float_at(call, 20);
    int32_t name_word = word_at(call, 6);
    qa_q3_pres_scene_model model = qa_q3_pres_q3_renderer_resources_model_for_handle(resources, model_word);
    const qa_decoded_model *base = model.kind == QA_Q3_PRES_SCENE_MODEL_LOADED ? &model.model : NULL;
    /* R_LerpTag/R_GetTag do not consume tagName when no tag storage exists. */
    bool has_tag = false;
    qa_q3_pres_model_tag tag;
    if (base != NULL && base->kind == QA_DECODED_MODEL_Q3_MESH && base->as.q3_mesh->tag_row_count != 0) {
      char *name = read_string(memory, name_word);
      has_tag = qa_q3_pres_lerp_model_tag(&model, name, start, end, fraction, &tag);
      free(name);
    }
    if (!has_tag) {
      qa_vec3 zero = {0, 0, 0};
      qa_axis identity = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
      qa_qvm_write_orientation(&destination, zero, &identity);
    } else {
      qa_qvm_write_orientation(&destination, tag.origin, &tag.axes);
    }
    /* UI's switch discards LerpTag's result; cgame returns it. */
    *out = strcmp(call->role, "ui") == 0 || !has_tag ? 0 : 1;
    return true;
  }
  if (trap == ids->remap) {
    char *original = read_string(memory, word_at(call, 1));
    char *replacement = read_string(memory, word_at(call, 2));
    int32_t offset_word = word_at(call, 3);
    char *offset = offset_word == 0 ? NULL : read_string(memory, offset_word);
    qa_q3_pres_q3_renderer_resources_remap_shader(resources, original, replacement, offset == NULL ? "" : offset);
    free(original);
    free(replacement);
    free(offset);
    *out = 0;
    return true;
  }
  return false;
}

/* The source record's shading fields with the resolved custom shader. */
static qa_q3_pres_shading record_shading(const qa_q3_pres_source_ref_entity_record *source,
                                         const qa_q3_pres_scene_shader *custom_shader) {
  qa_q3_pres_shading shading;
  shading.render_flags = source->render_flags;
  shading.custom_shader = custom_shader;
  shading.shader_rgba = source->shader_rgba;
  shading.shader_tex_coord = source->shader_tex_coord;
  shading.shader_time = source->shader_time;
  return shading;
}

/* TS addRefEntity admission of one source record: poly records pass
 * through; portal surfaces keep their handles unresolved; every other
 * kind resolves customShader, and models also resolve model and
 * customSkin ({ ...source, customShader, model, customSkin }). */
static void add_source_ref_entity(qa_q3_pres_q3_renderer_resources *resources,
                                  const qa_q3_pres_source_ref_entity_record *source) {
  qa_q3_pres_admitted_ref_entity admitted;
  memset(&admitted, 0, sizeof admitted);
  if (source->kind == QA_Q3_PRES_REF_POLY) {
    admitted.is_poly_record = true;
    admitted.record = *source;
    qa_q3_pres_q3_renderer_resources_add_ref_entity(resources, &admitted);
    return;
  }
  admitted.entity.kind = source->kind;
  if (source->kind == QA_Q3_PRES_REF_PORTAL) {
    qa_q3_pres_ref_portal_entity *portal = &admitted.entity.as.portal;
    portal->render_flags = source->render_flags;
    portal->origin = source->origin;
    portal->old_origin = source->old_origin;
    portal->axis = source->axis;
    portal->frame = source->frame;
    portal->old_frame = source->old_frame;
    portal->skin_num = source->skin_num;
    qa_q3_pres_q3_renderer_resources_add_ref_entity(resources, &admitted);
    return;
  }
  const qa_q3_pres_scene_shader *custom_shader =
      qa_q3_pres_q3_renderer_resources_shader_for_handle(resources, source->custom_shader.handle);
  qa_q3_pres_shading shading = record_shading(source, custom_shader);
  switch (source->kind) {
    case QA_Q3_PRES_REF_MODEL: {
      qa_q3_pres_ref_model_entity *model = &admitted.entity.as.model;
      model->shading = shading;
      model->model = qa_q3_pres_q3_renderer_resources_model_for_handle(resources, source->model.handle);
      model->origin = source->origin;
      model->old_origin = source->old_origin;
      model->axis = source->axis;
      model->non_normalized_axes = source->non_normalized_axes;
      model->lighting_origin = source->lighting_origin;
      model->shadow_plane = source->shadow_plane;
      model->frame = source->frame;
      model->old_frame = source->old_frame;
      model->back_lerp = source->back_lerp;
      model->skin_num = source->skin_num;
      model->custom_skin = qa_q3_pres_q3_renderer_resources_skin_for_handle(resources, source->custom_skin.handle);
      break;
    }
    case QA_Q3_PRES_REF_SPRITE: {
      qa_q3_pres_ref_sprite_entity *sprite = &admitted.entity.as.sprite;
      sprite->shading = shading;
      sprite->origin = source->origin;
      sprite->radius = source->radius;
      sprite->rotation = source->rotation;
      break;
    }
    case QA_Q3_PRES_REF_BEAM: {
      qa_q3_pres_ref_beam_entity *beam = &admitted.entity.as.beam;
      beam->shading = shading;
      beam->origin = source->origin;
      beam->old_origin = source->old_origin;
      beam->axis = source->axis;
      beam->radius = source->radius;
      break;
    }
    default: { /* rail-core, rail-rings, lightning */
      qa_q3_pres_ref_rail_entity *rail = &admitted.entity.as.rail;
      rail->shading = shading;
      rail->origin = source->origin;
      rail->old_origin = source->old_origin;
      rail->radius = source->radius;
      break;
    }
  }
  qa_q3_pres_q3_renderer_resources_add_ref_entity(resources, &admitted);
}

typedef struct token_ctx {
  const qa_qvm_host_call *call;
} token_ctx;

static void write_token(void *ctx, const char *token) {
  token_ctx *write = ctx;
  qa_qvm_memory_write_string(write->call->guest, word_at(write->call, 1), token,
                             word_at(write->call, 2));
}

typedef struct pvs_ctx {
  const qa_qvm_host_call *call;
} pvs_ctx;

static qa_vec3 pvs_point(const qa_qvm_host_call *call, size_t word_index) {
  qa_qvm_writable_view point;
  qa_qvm_memory_view(call->guest, word_at(call, word_index), 12, 0, &point);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&point, 0, true),
               qa_qvm_writable_view_get_float32(&point, 4, true),
               qa_qvm_writable_view_get_float32(&point, 8, true)};
  return v;
}
static qa_vec3 pvs_first(void *ctx) { return pvs_point(((pvs_ctx *)ctx)->call, 1); }
static qa_vec3 pvs_second(void *ctx) { return pvs_point(((pvs_ctx *)ctx)->call, 2); }

/* Guest rendering shares the native client's resource identities and scene publication. */
bool qa_qvm_client_render_syscall(const qa_qvm_host_call *call,
                                  qa_q3_pres_q3_renderer_resources *resources,
                                  const qa_draw2d *draw, qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") == 0) return false;
  if (resource_syscall(call, resources, draw, out)) return true;
  int32_t trap = call->code;
  bool ui = strcmp(call->role, "ui") == 0;
  qa_qvm_memory *guest = call->guest;
  if (!ui && trap == 36) {
    char *name = read_string(guest, word_at(call, 1));
    qa_q3_pres_world_scene world;
    out_of_memory(qa_q3_pres_q3_renderer_resources_load_world(resources, name, &world));
    free(name);
    *out = 0;
    return true;
  }
  if (!ui && trap == 86) {
    token_ctx token = {call};
    *out = qa_q3_pres_q3_renderer_resources_get_entity_token(resources, write_token, &token) ? 1 : 0;
    return true;
  }
  if (!ui && trap == 88) {
    pvs_ctx pvs = {call};
    *out = qa_q3_pres_q3_renderer_resources_in_pvs(resources, pvs_first, pvs_second, &pvs) ? 1 : 0;
    return true;
  }
  if (trap == (ui ? 21 : 40)) {
    qa_q3_pres_q3_renderer_resources_clear_scene(resources);
    *out = 0;
    return true;
  }
  if (trap == (ui ? 22 : 41)) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(guest, word_at(call, 1), QA_QVM_REF_ENTITY_BYTES, 0, &view);
    qa_q3_pres_source_ref_entity_record source;
    qa_qvm_read_ref_entity(&view, &source);
    add_source_ref_entity(resources, &source);
    *out = 0;
    return true;
  }
  if (trap == (ui ? 23 : 42) || (!ui && trap == 87)) {
    int32_t shader_handle = word_at(call, 1);
    int32_t count = word_at(call, 2);
    int32_t pointer = word_at(call, 3);
    int32_t polys = trap == 87 ? word_at(call, 4) : 1;
    if (shader_handle == 0 || count <= 0 || polys <= 0) {
      *out = 0;
      return true;
    }
    const qa_q3_pres_scene_shader *shader =
        qa_q3_pres_q3_renderer_resources_shader_for_handle(resources, shader_handle);
    int64_t stride = (int64_t)count * QA_QVM_POLY_VERTEX_BYTES;
    (void)qa_qvm_memory_get_span(guest, pointer, stride * polys, 0); /* TS guest.span extent check */
    qa_q3_pres_ref_poly_vertex *vertices = malloc(sizeof *vertices * (size_t)count);
    if (vertices == NULL) qa_range_error("out of memory");
    for (int32_t index = 0; index < polys; index++) {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, pointer, stride, index * stride, &view);
      qa_qvm_read_poly_vertices(&view, count, vertices);
      qa_q3_pres_ref_poly poly = {shader, vertices, (size_t)count};
      qa_q3_pres_q3_renderer_resources_add_poly(resources, &poly);
    }
    free(vertices);
    *out = 0;
    return true;
  }
  if (trap == (ui ? 24 : 43) || (!ui && trap == 85)) {
    float radius = float_at(call, 8);
    if (radius <= 0) {
      *out = 0;
      return true;
    }
    qa_qvm_writable_view origin_view;
    qa_qvm_memory_view(guest, word_at(call, 1), 12, 0, &origin_view);
    qa_q3_pres_dynamic_light light;
    light.origin = (qa_vec3){qa_qvm_writable_view_get_float32(&origin_view, 0, true),
                              qa_qvm_writable_view_get_float32(&origin_view, 4, true),
                              qa_qvm_writable_view_get_float32(&origin_view, 8, true)};
    light.radius = radius;
    light.color = (qa_vec3){float_at(call, 12), float_at(call, 16), float_at(call, 20)};
    light.additive = trap == 85;
    qa_q3_pres_q3_renderer_resources_add_light(resources, light);
    *out = 0;
    return true;
  }
  if (trap == (ui ? 25 : 44)) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(guest, word_at(call, 1), QA_QVM_REFDEF_BYTES, 0, &view);
    qa_q3_pres_refdef refdef;
    qa_qvm_read_refdef(&view, &refdef);
    qa_q3_pres_q3_renderer_resources_render_scene(resources, &refdef);
    *out = 0;
    return true;
  }
  return false;
}

/* Port of src/compat/qvm/client-render-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_render_syscalls.h"

#include <stdlib.h>
#include <string.h>

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

typedef struct picture_ctx {
  qa_qvmx_renderer_resources *resources;
  const void *shader;
} picture_ctx;

static const void *picture_thunk(void *ctx) {
  picture_ctx *picture = ctx;
  return picture->resources->picture(picture->resources->ctx, picture->shader);
}

static void write_vector(qa_qvm_writable_view *view, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, 8, value.z, true);
}

static bool resource_syscall(const qa_qvm_host_call *call, qa_qvmx_renderer_resources *resources,
                             qa_qvm_client_render_draw *draw, qa_qvm_host_result *out) {
  /* role "game" never reaches here: the caller rejects qagame. */
  int32_t trap = word_at(call, 0);
  const resource_traps *ids = strcmp(call->role, "ui") == 0 ? &ui_traps : &cgame_traps;
  qa_qvm_memory *memory = call->guest;
  if (trap == ids->model) {
    int32_t name_word = word_at(call, 1);
    char *name = name_word == 0 ? NULL : read_string(memory, name_word);
    const void *model = resources->register_model(resources->ctx, name == NULL ? "" : name);
    free(name);
    *out = qa_int32(resources->model_handle(resources->ctx, model));
    return true;
  }
  if (trap == ids->skin) {
    int32_t name_word = word_at(call, 1);
    char *name = name_word == 0 ? NULL : read_string(memory, name_word);
    const void *skin = resources->register_skin(resources->ctx, name == NULL ? "" : name);
    free(name);
    *out = qa_int32(resources->skin_handle(resources->ctx, skin));
    return true;
  }
  if ((ids->shader >= 0 && trap == ids->shader) || trap == ids->shader_no_mip) {
    char *name = read_string(memory, word_at(call, 1));
    const void *shader = trap == ids->shader
                             ? resources->register_shader(resources->ctx, name)
                             : resources->register_shader_no_mip(resources->ctx, name);
    free(name);
    *out = qa_int32(resources->shader_handle(resources->ctx, shader));
    return true;
  }
  if (trap == ids->color) {
    int32_t word = word_at(call, 1);
    if (word == 0) {
      draw->set_color(draw->ctx, NULL);
    } else {
      qa_qvm_writable_view color;
      qa_qvm_memory_view(memory, word, 16, 0, &color);
      qa_vec4 value = {qa_qvm_writable_view_get_float32(&color, 0, true),
                       qa_qvm_writable_view_get_float32(&color, 4, true),
                       qa_qvm_writable_view_get_float32(&color, 8, true),
                       qa_qvm_writable_view_get_float32(&color, 12, true)};
      draw->set_color(draw->ctx, &value);
    }
    *out = 0;
    return true;
  }
  if (trap == ids->picture) {
    qa_rect rect = {float_at(call, 4), float_at(call, 8), float_at(call, 12), float_at(call, 16)};
    qa_qvm_render_uv_rect uv = {float_at(call, 20), float_at(call, 24), float_at(call, 28),
                                float_at(call, 32)};
    int32_t shader_word = word_at(call, 9);
    picture_ctx picture = {resources, resources->shader_for_handle(resources->ctx, shader_word)};
    draw->stretch_pixels(draw->ctx, rect, uv, picture_thunk, &picture);
    *out = 0;
    return true;
  }
  if (trap == ids->bounds) {
    int32_t model_word = word_at(call, 1);
    qa_qvm_writable_view min, max;
    qa_qvm_memory_view(memory, word_at(call, 2), 12, 0, &min);
    qa_qvm_memory_view(memory, word_at(call, 3), 12, 0, &max);
    qa_bounds bounds =
        resources->model_bounds(resources->ctx, resources->model_for_handle(resources->ctx, model_word));
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
    const void *model = resources->model_for_handle(resources->ctx, model_word);
    qa_qvmx_model_tag_info info = resources->model_tag_info(resources->ctx, model);
    bool has_tag = false;
    qa_vec3 origin = {0, 0, 0};
    qa_axis axes = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    if (info.is_model && info.is_md3 && info.tag_count != 0) {
      char *name = read_string(memory, name_word);
      has_tag = resources->lerp_model_tag(resources->ctx, model, name, start, end, fraction,
                                          &origin, &axes);
      free(name);
    }
    if (!has_tag) {
      qa_vec3 zero = {0, 0, 0};
      qa_axis identity = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
      qa_qvm_write_orientation(&destination, zero, &identity);
    } else {
      qa_qvm_write_orientation(&destination, origin, &axes);
    }
    *out = strcmp(call->role, "ui") == 0 || !has_tag ? 0 : 1;
    return true;
  }
  if (trap == ids->remap) {
    char *original = read_string(memory, word_at(call, 1));
    char *replacement = read_string(memory, word_at(call, 2));
    int32_t offset_word = word_at(call, 3);
    char *offset = offset_word == 0 ? NULL : read_string(memory, offset_word);
    resources->remap_shader(resources->ctx, original, replacement, offset == NULL ? "" : offset);
    free(original);
    free(replacement);
    free(offset);
    *out = 0;
    return true;
  }
  return false;
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

bool qa_qvm_client_render_syscall(const qa_qvm_host_call *call,
                                  qa_qvmx_renderer_resources resources,
                                  qa_qvm_client_render_draw draw, qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") == 0) return false;
  if (resource_syscall(call, &resources, &draw, out)) return true;
  int32_t trap = call->code;
  bool ui = strcmp(call->role, "ui") == 0;
  qa_qvm_memory *guest = call->guest;
  if (!ui && trap == 36) {
    char *name = read_string(guest, word_at(call, 1));
    resources.load_world(resources.ctx, name);
    free(name);
    *out = 0;
    return true;
  }
  if (!ui && trap == 86) {
    token_ctx token = {call};
    *out = resources.get_entity_token(resources.ctx, write_token, &token) ? 1 : 0;
    return true;
  }
  if (!ui && trap == 88) {
    pvs_ctx pvs = {call};
    *out = resources.in_pvs(resources.ctx, pvs_first, pvs_second, &pvs) ? 1 : 0;
    return true;
  }
  if (trap == (ui ? 21 : 40)) {
    resources.clear_scene(resources.ctx);
    *out = 0;
    return true;
  }
  if (trap == (ui ? 22 : 41)) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(guest, word_at(call, 1), QA_QVM_REF_ENTITY_BYTES, 0, &view);
    qa_qvm_source_ref_entity_record source;
    qa_qvm_read_ref_entity(&view, &source);
    qa_qvm_render_ref_entity entity;
    memset(&entity, 0, sizeof entity);
    entity.source = source;
    if (strcmp(source.kind, "poly") == 0 || strcmp(source.kind, "portal-surface") == 0) {
      resources.add_ref_entity(resources.ctx, entity);
      *out = 0;
      return true;
    }
    entity.custom_shader = resources.shader_for_handle(resources.ctx, source.custom_shader);
    if (strcmp(source.kind, "model") == 0) {
      entity.model = resources.model_for_handle(resources.ctx, source.model);
      entity.custom_skin = resources.skin_for_handle(resources.ctx, source.custom_skin);
    }
    resources.add_ref_entity(resources.ctx, entity);
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
    const void *shader = resources.shader_for_handle(resources.ctx, shader_handle);
    int64_t stride = (int64_t)count * QA_QVM_POLY_VERTEX_BYTES;
    (void)qa_qvm_memory_get_span(guest, pointer, stride * polys, 0);
    qa_qvm_ref_poly_vertex *vertices = malloc(sizeof *vertices * (size_t)count);
    if (vertices == NULL) qa_range_error("out of memory");
    for (int32_t index = 0; index < polys; index++) {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, pointer, stride, index * stride, &view);
      qa_qvm_read_poly_vertices(&view, count, vertices);
      qa_qvm_render_poly poly = {shader, vertices, (size_t)count};
      resources.add_poly(resources.ctx, poly);
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
    qa_qvm_render_light light;
    light.origin = (qa_vec3){qa_qvm_writable_view_get_float32(&origin_view, 0, true),
                              qa_qvm_writable_view_get_float32(&origin_view, 4, true),
                              qa_qvm_writable_view_get_float32(&origin_view, 8, true)};
    light.radius = radius;
    light.color = (qa_vec3){float_at(call, 12), float_at(call, 16), float_at(call, 20)};
    light.additive = trap == 85;
    resources.add_light(resources.ctx, light);
    *out = 0;
    return true;
  }
  if (trap == (ui ? 25 : 44)) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(guest, word_at(call, 1), QA_QVM_REFDEF_BYTES, 0, &view);
    qa_qvm_refdef refdef;
    qa_qvm_read_refdef(&view, &refdef);
    resources.render_scene(resources.ctx, refdef);
    *out = 0;
    return true;
  }
  return false;
}

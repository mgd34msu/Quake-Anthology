/*
 * Port of src/compat/qvm/client-render-syscalls.ts.
 * qa_qvmx_renderer_resources mirrors content/q3/presentation/
 * resources.ts Q3RendererResources and model-access.ts helpers;
 * qa_qvm_client_render_draw mirrors text/draw2d.ts Draw2D's used
 * surface (content/text presentation unported). Resource tokens
 * are opaque borrowed pointers. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_RENDER_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_RENDER_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/render_record.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"

typedef struct qa_qvm_render_uv_rect { double s, t, s2, t2; } qa_qvm_render_uv_rect;

typedef struct qa_qvm_client_render_draw {
  void *ctx;
  /* color NULL is the TS setColor(null). */
  void (*set_color)(void *ctx, const qa_vec4 *color);
  /* picture is the TS lazy picture thunk as fn+ctx returning a token. */
  void (*stretch_pixels)(void *ctx, qa_rect rect, qa_qvm_render_uv_rect uv,
                         const void *(*picture)(void *picture_ctx), void *picture_ctx);
} qa_qvm_client_render_draw;

typedef struct qa_qvm_render_light {
  qa_vec3 origin;
  double radius;
  qa_vec3 color;
  bool additive;
} qa_qvm_render_light;

typedef struct qa_qvm_render_poly {
  const void *shader;
  const qa_qvm_ref_poly_vertex *vertices;
  size_t vertex_count;
} qa_qvm_render_poly;

/* RefEntity with handles resolved through the resource mirrors. */
typedef struct qa_qvm_render_ref_entity {
  qa_qvm_source_ref_entity_record source;
  const void *model;        /* resolved when source.model was a handle */
  const void *custom_skin;
  const void *custom_shader;
} qa_qvm_render_ref_entity;

typedef struct qa_qvmx_model_tag_info {
  bool is_model, is_md3;
  size_t tag_count;
} qa_qvmx_model_tag_info;

typedef struct qa_qvmx_renderer_resources {
  void *ctx;
  const void *(*register_model)(void *ctx, const char *name);
  const void *(*register_skin)(void *ctx, const char *name);
  const void *(*register_shader)(void *ctx, const char *name);
  const void *(*register_shader_no_mip)(void *ctx, const char *name);
  double (*model_handle)(void *ctx, const void *model);
  double (*skin_handle)(void *ctx, const void *skin);
  double (*shader_handle)(void *ctx, const void *shader);
  const void *(*model_for_handle)(void *ctx, double handle);
  const void *(*skin_for_handle)(void *ctx, double handle);
  const void *(*shader_for_handle)(void *ctx, double handle);
  const void *(*picture)(void *ctx, const void *shader);
  qa_bounds (*model_bounds)(void *ctx, const void *model);
  qa_qvmx_model_tag_info (*model_tag_info)(void *ctx, const void *model);
  /* False is the TS null tag. Model token as for model_tag_info. */
  bool (*lerp_model_tag)(void *ctx, const void *model, const char *name, double start, double end,
                         double fraction, qa_vec3 *out_origin, qa_axis *out_axes);
  void (*remap_shader)(void *ctx, const char *original, const char *replacement,
                       const char *offset);
  void (*load_world)(void *ctx, const char *name);
  bool (*get_entity_token)(void *ctx, void (*write)(void *write_ctx, const char *token),
                           void *write_ctx);
  bool (*in_pvs)(void *ctx, qa_vec3 (*first)(void *point_ctx), qa_vec3 (*second)(void *point_ctx),
                 void *point_ctx);
  void (*clear_scene)(void *ctx);
  void (*add_ref_entity)(void *ctx, qa_qvm_render_ref_entity entity);
  void (*add_poly)(void *ctx, qa_qvm_render_poly poly);
  void (*add_light)(void *ctx, qa_qvm_render_light light);
  void (*render_scene)(void *ctx, qa_qvm_refdef refdef);
} qa_qvmx_renderer_resources;

bool qa_qvm_client_render_syscall(const qa_qvm_host_call *call,
                                  qa_qvmx_renderer_resources resources,
                                  qa_qvm_client_render_draw draw, qa_qvm_host_result *out);

#endif

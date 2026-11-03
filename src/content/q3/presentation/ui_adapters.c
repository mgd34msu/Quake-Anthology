/* Port of src/content/q3/presentation/ui-adapters.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/ui_adapters.h"
#include <math.h>
#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static float f32(double v) { return (float)v; }
void qa_q3_pres_engine_ui_model_painter_init(qa_q3_pres_engine_ui_model_painter *painter,
    qa_q3_pres_renderer_resources resources, const void *commands_token) {
  painter->resources = resources;
  painter->commands_token = commands_token;
}
void qa_q3_pres_engine_ui_model_painter_paint(qa_q3_pres_engine_ui_model_painter *painter,
                                              const qa_ui_model_paint_request *request) {
  if (request->draw->commands.ctx != painter->commands_token)
    qa_range_error("UI model painter must use its seat's drawing queue");
  qa_rect viewport = qa_draw2d_adjust(request->draw, request->rect);
  qa_q3_pres_refdef refdef = qa_q3_pres_create_refdef();
  refdef.render_flags = QA_Q3_PRES_RDF_NOWORLDMODEL;
  refdef.view_axis = (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  refdef.x = trunc(viewport.x);
  refdef.y = trunc(viewport.y);
  refdef.width = trunc(viewport.width);
  refdef.height = trunc(viewport.height);
  refdef.fov_x = request->field_of_view_x != 0 ? f32(request->field_of_view_x) : viewport.width;
  refdef.fov_y = request->field_of_view_y != 0 ? f32(request->field_of_view_y) : viewport.height;
  /* request.time | 0 */
  refdef.time = (double)qa_js_to_int32(request->time);
  qa_bounds bounds = qa_q3_pres_model_bounds(&request->model);
  qa_q3_pres_ref_model_entity entity = qa_q3_pres_create_model_entity(request->model);
  double length = f32(0.5 * f32(bounds.max.z - bounds.min.z));
  entity.origin = (qa_vec3){f32(length / f32(0.268)), f32(0.5 * f32(bounds.min.y + bounds.max.y)),
                            f32(-0.5 * f32(bounds.min.z + bounds.max.z))};
  entity.lighting_origin = entity.origin;
  entity.old_origin = entity.origin;
  entity.axis = qa_qvm_angles_to_axis((qa_vec3){0, request->angle, 0});
  entity.shading.render_flags = QA_Q3_PRES_RF_LIGHTING_ORIGIN | QA_Q3_PRES_RF_NOSHADOW;
  painter->resources.clear_scene(painter->resources.ctx);
  qa_q3_pres_admitted_ref_entity admitted;
  admitted.is_poly_record = false;
  admitted.entity = qa_q3_pres_ref_entity_from_model(entity);
  painter->resources.add_ref_entity(painter->resources.ctx, &admitted);
  painter->resources.render_scene(painter->resources.ctx, &refdef);
}
void qa_q3_pres_engine_ui_model_painter_paint_ctx(void *ctx, const qa_ui_model_paint_request *request) {
  qa_q3_pres_engine_ui_model_painter_paint(ctx, request);
}

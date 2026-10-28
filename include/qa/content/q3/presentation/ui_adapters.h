/* Port of src/content/q3/presentation/ui-adapters.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_PRESENTATION_UI_ADAPTERS_H
#define QA_CONTENT_Q3_PRESENTATION_UI_ADAPTERS_H
#include <stdbool.h>
#include "qa/content/q3/presentation/resources.h"
#include "qa/text/draw2d.h"
#include "qa/ui/common/legacy/runtime.h"
/* TS EngineUiCinematics owner pick: prepare returns a borrowed asset
 * handle (opaque), false only on OOM. */
typedef struct qa_q3_pres_engine_ui_cinematics {
  qa_ui_runtime_cinematics base;
  void *owner_ctx;
  bool (*prepare)(void *ctx, const char *path, const qa_ui_cinematic_asset **out);
  void (*stop_slot)(void *ctx, double index);
} qa_q3_pres_engine_ui_cinematics;
/* TS EngineUiModelPainter. commands identity is its ctx token. */
typedef struct qa_q3_pres_engine_ui_model_painter {
  qa_q3_pres_renderer_resources resources;
  const void *commands_token;
} qa_q3_pres_engine_ui_model_painter;
void qa_q3_pres_engine_ui_model_painter_init(qa_q3_pres_engine_ui_model_painter *painter,
    qa_q3_pres_renderer_resources resources, const void *commands_token);
/* Queue mismatch aborts with the TS Error text. */
void qa_q3_pres_engine_ui_model_painter_paint(qa_q3_pres_engine_ui_model_painter *painter,
                                              const qa_ui_model_paint_request *request);
/* qa_ui_paint_model_fn adapter. */
void qa_q3_pres_engine_ui_model_painter_paint_ctx(void *ctx, const qa_ui_model_paint_request *request);
#endif

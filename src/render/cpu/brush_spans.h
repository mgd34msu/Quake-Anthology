#ifndef QA_CPU_BRUSH_SPANS_H
#define QA_CPU_BRUSH_SPANS_H

#include "internal.h"

typedef void (*cpu_brush_rows_fn)(qa_cpu_renderer *, void *, int64_t, int64_t);
void cpu_raster_rows(qa_cpu_renderer *, int64_t, int64_t,
                     cpu_brush_rows_fn, void *);
bool cpu_brush_draw_queued(qa_cpu_renderer *, const qa_scene_draw *, qa_error *,
                           bool *handled);
bool cpu_brush_flush(qa_cpu_renderer *, qa_error *);
void cpu_brush_clear(qa_cpu_renderer *);
void cpu_brush_destroy(qa_cpu_renderer *);

#endif

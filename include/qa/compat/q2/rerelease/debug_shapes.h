/* Port of src/compat/q2/rerelease/debug-shapes.ts. GPL-2.0-or-later.
 * rereleaseDebugLifetime is re-exported from content (not landed on
 * the contracts side); mirrored here from its TS formula. */
#ifndef QA_COMPAT_Q2_RERELEASE_DEBUG_SHAPES_H
#define QA_COMPAT_Q2_RERELEASE_DEBUG_SHAPES_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/debug/shapes.h"

double qa_compat_q2_rerelease_debug_lifetime(double lifetime_seconds);
typedef struct qa_compat_q2_rerelease_debug_shapes_event {
  const qa_debug_line *lines; size_t line_count; double lifetime_milliseconds;
} qa_compat_q2_rerelease_debug_shapes_event;
typedef struct qa_compat_q2_rerelease_debug_shape_imports qa_compat_q2_rerelease_debug_shape_imports;
/* NULL only on OOM. */
qa_compat_q2_rerelease_debug_shape_imports *qa_compat_q2_rerelease_debug_shape_imports_create(
    qa_compat_q2_memory *memory, void *emit_ctx, void (*emit)(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event));
void qa_compat_q2_rerelease_debug_shape_imports_destroy(qa_compat_q2_rerelease_debug_shape_imports *d);
/* False is the TS undefined (unhandled call). */
bool qa_compat_q2_rerelease_debug_shape_imports_invoke(qa_compat_q2_rerelease_debug_shape_imports *d,
                                                       qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out);
#endif

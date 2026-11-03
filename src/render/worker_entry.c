/* Port of src/render/worker-entry.ts. GPL-2.0-or-later. */
#include "qa/render/worker_entry.h"
#include "qa/render/worker_runtime.h"
bool qa_render_worker_entry(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure) {
  return qa_create_render_worker_runtime(ctx, payload, worker, out, failure);
}

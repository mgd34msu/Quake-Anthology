/* Port of src/render/worker-entry.ts: the entry the render thread starts with.
 * With its ctx (NULL, or a qa_render_worker_platform) it stands in for the TS
 * worker URL given to RenderWorkerTransport.open. GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_ENTRY_H
#define QA_RENDER_WORKER_ENTRY_H
#include "qa/render/worker_transport.h"
bool qa_render_worker_entry(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure);
#endif

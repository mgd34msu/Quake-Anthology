/* Port of src/render/worker-entry.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/worker_entry.h"
#include "qa/render/worker_runtime.h"
#include "qa/render/worker_transport.h"
void qa_render_worker_entry_serve(void){ qa_serve_render_worker(qa_create_render_worker_runtime, 0); }

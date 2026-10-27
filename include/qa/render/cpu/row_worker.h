/* Port of src/render/cpu/row-worker.ts. Direct/pumped (PORTING.md):
 * no worker thread; qa_row_worker_run_job decodes and rasterizes bands
 * in-process. Validation messages match the TS. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_ROW_WORKER_H
#define QA_RENDER_CPU_ROW_WORKER_H
#include <stddef.h>
#include "qa/render/cpu/row_parallel.h"
typedef struct qa_row_worker qa_row_worker;
struct qa_row_worker { void *impl; };
bool qa_row_worker_init(qa_row_worker *worker, qa_row_shared_framebuffer shared);
void qa_row_worker_destroy(qa_row_worker *worker);
void qa_row_worker_run_job(qa_row_worker *worker, qa_row_job_message message);
#endif

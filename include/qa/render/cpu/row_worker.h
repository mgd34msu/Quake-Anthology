/* Port of src/render/cpu/row-worker.ts. A worker is a pthread parked on the shared
 * control block; qa_row_worker_run_job is the TS message handler. The control block
 * replaces the TS control Int32Array and each worker's message port. Texture and
 * lighting registrations are shallow: the coordinator holds the referenced memory
 * through the synchronous job, so the TS postMessage copies have no C counterpart.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_ROW_WORKER_H
#define QA_RENDER_CPU_ROW_WORKER_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/render/cpu/row_parallel.h"
typedef struct qa_row_control {
  atomic_int next;                 /* CONTROL_NEXT: the next unclaimed band */
  pthread_mutex_t lock;            /* guards every field below */
  pthread_cond_t job_ready;        /* a new generation or shutdown */
  pthread_cond_t progress;         /* done or ready advanced */
  size_t done, ready;              /* CONTROL_DONE, CONTROL_READY */
  uint64_t generation;
  bool shutdown;
  bool failed;                     /* CONTROL_ERROR */
  char error[1024];                /* the TS error buffer and CONTROL_ERROR_LENGTH */
  qa_row_job_message message;      /* the current postMessage payload */
} qa_row_control;
bool qa_row_control_init(qa_row_control *control);
void qa_row_control_destroy(qa_row_control *control);
typedef struct qa_row_worker qa_row_worker;
struct qa_row_worker { void *impl; };
bool qa_row_worker_init(qa_row_worker *worker, qa_row_shared_framebuffer shared, qa_row_control *control);
void qa_row_worker_destroy(qa_row_worker *worker);
/* The TS message handler: registers, decodes, claims bands until none remain, then
 * reports done. A raised error is stored for the coordinator, as in TS. */
void qa_row_worker_run_job(qa_row_worker *worker, qa_row_job_message message);
/* pthread entry. arg is a qa_row_worker. Runs each generation once, until shutdown. */
void *qa_row_worker_main(void *arg);
#endif

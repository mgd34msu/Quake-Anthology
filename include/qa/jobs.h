#ifndef QA_JOBS_H
#define QA_JOBS_H
#include "qa/common.h"

typedef struct qa_jobs qa_jobs;
typedef void (*qa_job_fn)(void *context, size_t index);

/* Create only during load. Participants include the caller; zero selects the
 * physical cores available in the current affinity mask, one runs inline.
 * If native workers cannot be created, the same dispatcher runs inline. */
qa_jobs *qa_jobs_create(unsigned participants, qa_error *);
unsigned qa_jobs_count(const qa_jobs *);
void qa_jobs_destroy(qa_jobs *);

/* One publisher borrows caller-owned jobs/outputs until return. Each claimed
 * index runs once and writes only its own output. Merge outputs in index order
 * after return. A zero participant limit uses the whole pool; the caller also
 * works. Nested dispatch on an active pool runs inline. NULL also runs inline.
 * No allocation or thread creation occurs here. Floating-point environments
 * are restored and generated exceptions are retained on the caller. */
void qa_jobs_dispatch(qa_jobs *, unsigned participants, size_t count, qa_job_fn, void *context);
#endif

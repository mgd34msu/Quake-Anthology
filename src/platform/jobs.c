#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "qa/jobs.h"
#include <SDL_thread.h>
#include <SDL_mutex.h>
#include <SDL_cpuinfo.h>
#include <stdatomic.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

typedef struct qa_job_worker {
    SDL_Thread *thread;
    SDL_sem *wake, *done;
    struct qa_jobs *pool;
    int exceptions;
    bool stop;
} qa_job_worker;
struct qa_jobs {
    unsigned workers;
    atomic_bool active;
    atomic_size_t next;
    size_t count;
    qa_job_fn function;
    void *context;
    fenv_t environment;
    qa_job_worker worker[];
};
static void jobs_claim(qa_jobs *pool)
{
    for (;;) {
        size_t index = atomic_fetch_add_explicit(&pool->next, 1, memory_order_relaxed);
        if (index >= pool->count) return;
        pool->function(pool->context, index);
    }
}
static int SDLCALL jobs_worker(void *context)
{
    qa_job_worker *worker = context;
    qa_jobs *pool = worker->pool;
    for (;;) {
        SDL_SemWait(worker->wake);
        if (worker->stop) return 0;
        fenv_t previous;
        worker->exceptions = 0;
        if (fegetenv(&previous) == 0) {
            if (fesetenv(&pool->environment) == 0) {
                jobs_claim(pool);
                worker->exceptions = fetestexcept(FE_ALL_EXCEPT);
            }
            (void)fesetenv(&previous);
        }
        SDL_SemPost(worker->done);
    }
}
static int jobs_physical_cores(void) {
#if defined(__linux__)
  cpu_set_t allowed;
  if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
    int logical = CPU_COUNT(&allowed);
    if (logical < 2) return logical;
    int packages[CPU_SETSIZE], cores[CPU_SETSIZE], count = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (!CPU_ISSET((size_t)cpu, &allowed)) continue;
      char path[128];
      int package, core;
      (void)snprintf(path, sizeof(path),
          "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
      FILE *file = fopen(path, "r");
      if (!file) return logical;
      int parsed = fscanf(file, "%d", &package);
      fclose(file);
      if (parsed != 1) return logical;
      (void)snprintf(path, sizeof(path),
          "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
      file = fopen(path, "r");
      if (!file) return logical;
      parsed = fscanf(file, "%d", &core);
      fclose(file);
      if (parsed != 1) return logical;
      int i = 0;
      while (i < count && (packages[i] != package || cores[i] != core)) ++i;
      if (i == count) { packages[count] = package; cores[count++] = core; }
    }
    return count ? count : logical;
  }
  return 1;
#elif defined(__APPLE__)
  int count = 0;
  size_t size = sizeof(count);
  if (sysctlbyname("hw.physicalcpu", &count, &size, NULL, 0) == 0 && count > 0)
    return count;
#elif defined(_WIN32)
  DWORD bytes = 0;
  (void)GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bytes);
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *topology = malloc(bytes);
  if (topology) {
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, topology, &bytes)) {
      DWORD offset = 0;
      int count = 0;
      while (offset < bytes) {
        const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entry =
            (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)
            ((const uint8_t *)topology + offset);
        if (!entry->Size || entry->Size > bytes - offset) break;
        if (entry->Relationship == RelationProcessorCore) ++count;
        offset += entry->Size;
      }
      free(topology);
      if (offset == bytes && count > 0) return count;
    } else free(topology);
  }
#endif
  return SDL_GetCPUCount();
}
qa_jobs *qa_jobs_create(unsigned participants, qa_error *error)
{
    if (!participants) {
        int cores = jobs_physical_cores();
        participants = cores > 0 ? (unsigned)cores : 1;
    }
    unsigned workers = participants - 1;
    if (workers && sizeof(qa_job_worker) > (SIZE_MAX - sizeof(qa_jobs)) / workers) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Job worker count exceeds addressable storage");
        return NULL;
    }
    qa_jobs *pool = calloc(1, sizeof(*pool) + (size_t)workers * sizeof(*pool->worker));
    if (!pool) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating platform job workers");
        return NULL;
    }
    atomic_init(&pool->active, false);
    atomic_init(&pool->next, 0);
    pool->workers = workers;
    for (unsigned i = 0; i < workers; ++i) {
        qa_job_worker *worker = pool->worker + i;
        worker->pool = pool;
        worker->wake = SDL_CreateSemaphore(0);
        worker->done = SDL_CreateSemaphore(0);
        if (worker->wake && worker->done)
            worker->thread = SDL_CreateThread(jobs_worker, "Quake jobs", worker);
        if (!worker->thread) {
            for (unsigned j = 0; j <= i; ++j) {
                qa_job_worker *created = pool->worker + j;
                if (created->thread) {
                    created->stop = true;
                    SDL_SemPost(created->wake);
                    SDL_WaitThread(created->thread, NULL);
                    created->thread = NULL;
                }
                if (created->wake) SDL_DestroySemaphore(created->wake);
                if (created->done) SDL_DestroySemaphore(created->done);
                created->wake = created->done = NULL;
            }
            pool->workers = 0;
            break;
        }
    }
    return pool;
}
unsigned qa_jobs_count(const qa_jobs *pool)
{ return pool ? pool->workers + 1 : 1; }
void qa_jobs_destroy(qa_jobs *pool)
{
    if (!pool) return;
    for (unsigned i = 0; i < pool->workers; ++i) {
        pool->worker[i].stop = true;
        SDL_SemPost(pool->worker[i].wake);
    }
    for (unsigned i = 0; i < pool->workers; ++i) {
        qa_job_worker *worker = pool->worker + i;
        SDL_WaitThread(worker->thread, NULL);
        SDL_DestroySemaphore(worker->wake);
        SDL_DestroySemaphore(worker->done);
    }
    free(pool);
}
static void jobs_restore(const fenv_t *environment, int exceptions)
{
    (void)fesetenv(environment);
    exceptions &= ~fetestexcept(FE_ALL_EXCEPT);
    if (exceptions) (void)feraiseexcept(exceptions);
}
static void jobs_inline(size_t count, qa_job_fn function, void *context)
{
    fenv_t environment;
    bool saved = fegetenv(&environment) == 0;
    for (size_t i = 0; i < count; ++i) function(context, i);
    if (saved) jobs_restore(&environment, fetestexcept(FE_ALL_EXCEPT));
}
void qa_jobs_dispatch(qa_jobs *pool, unsigned participants, size_t count,
    qa_job_fn function, void *context)
{
    if (!count) return;
    if (!pool || !pool->workers || participants == 1) {
        jobs_inline(count, function, context);
        return;
    }
    if (atomic_exchange_explicit(&pool->active, true, memory_order_acquire)) {
        jobs_inline(count, function, context);
        return;
    }
    if (fegetenv(&pool->environment) != 0) {
        jobs_inline(count, function, context);
        atomic_store_explicit(&pool->active, false, memory_order_release);
        return;
    }
    unsigned workers = pool->workers;
    if (participants && participants - 1 < workers) workers = participants - 1;
    if (count - 1 < workers) workers = (unsigned)(count - 1);
    pool->count = count;
    pool->function = function;
    pool->context = context;
    atomic_store_explicit(&pool->next, 0, memory_order_relaxed);
    for (unsigned i = 0; i < workers; ++i) SDL_SemPost(pool->worker[i].wake);
    jobs_claim(pool);
    int exceptions = fetestexcept(FE_ALL_EXCEPT);
    for (unsigned i = 0; i < workers; ++i) {
        SDL_SemWait(pool->worker[i].done);
        exceptions |= pool->worker[i].exceptions;
    }
    jobs_restore(&pool->environment, exceptions);
    pool->count = 0;
    pool->function = NULL;
    pool->context = NULL;
    atomic_store_explicit(&pool->active, false, memory_order_release);
}

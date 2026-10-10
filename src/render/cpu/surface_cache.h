#ifndef QA_CPU_SURFACE_CACHE_H
#define QA_CPU_SURFACE_CACHE_H

#include "qa/render_cpu.h"
enum { CPU_SURFACE_MIPS = 32 };

typedef struct cpu_surface_mip {
  const uint8_t *pixels;
  uint32_t width, height;
  size_t stride;
  unsigned mip;
} cpu_surface_mip;

/* Views stay immutable through end, including across subsequent preparations.
 * An unsupported surface or a full pinned batch returns false without an error;
 * the caller may flush the batch and retry, or use the triangle path. */
bool cpu_surface_cache_prepare(qa_cpu_renderer *, const qa_scene_draw *,
                               unsigned mip, cpu_surface_mip *);
bool cpu_surface_cache_init(qa_cpu_renderer *, qa_error *);
void cpu_surface_cache_begin(qa_cpu_renderer *);
void cpu_surface_cache_end(qa_cpu_renderer *);
void cpu_surface_cache_destroy(qa_cpu_renderer *);

#endif

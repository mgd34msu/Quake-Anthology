/* Port of src/render/cpu/row-parallel.ts. Node workers become pthreads sharing
 * the framebuffer. The setup buffer keeps the TS 129-wide Float64 layout.
 * GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_ROW_PARALLEL_H
#define QA_RENDER_CPU_ROW_PARALLEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/render/cpu/lighting.h"
#include "qa/render/cpu/triangle_kernel.h"
#define QA_ROW_SETUP_STRIDE 129
/* TS RowSharedFramebuffer: shared buffers become plain pointers. */
typedef struct qa_row_shared_framebuffer { double width, height; uint8_t *color; double *depth; uint32_t *stencil; } qa_row_shared_framebuffer;
typedef struct qa_row_registries { void *ctx; double (*texture_id)(void *ctx, const qa_bound_texture *texture); double (*lighting_id)(void *ctx, const qa_batch_lighting *parameters); double (*depth_id)(void *ctx, const qa_depth_image_level *depth); } qa_row_registries;
void qa_encode_row_setup(double *data, size_t data_count, double offset, const qa_triangle_setup *setup, qa_row_registries registries);
typedef struct qa_row_decode_registries { void *ctx; qa_bound_texture (*texture)(void *ctx, double id); qa_batch_lighting (*lighting)(void *ctx, double id); const qa_depth_image_level *(*depth)(void *ctx, double id); } qa_row_decode_registries;
/* Decoded fog/lighting are written to caller storage. */
typedef struct qa_row_decode_storage { qa_batch_fog fog; qa_cpu_triangle_lighting lighting; } qa_row_decode_storage;
qa_triangle_setup qa_decode_row_setup(const double *data, size_t data_count, double offset, qa_row_decode_registries registries, qa_row_decode_storage *storage);
typedef struct qa_row_texture_registration { double id; qa_bound_texture texture; } qa_row_texture_registration;
typedef struct qa_row_lighting_registration { double id; qa_batch_lighting parameters; } qa_row_lighting_registration;
typedef struct qa_row_depth_registration { double id; qa_depth_image_level depth; } qa_row_depth_registration;
typedef struct qa_row_job_message { const char *kind; double count, band_rows; double *job_buffer; size_t job_count; const qa_row_texture_registration *textures; size_t texture_count; const qa_row_lighting_registration *lightings; size_t lighting_count; const qa_row_depth_registration *depths; size_t depth_count; } qa_row_job_message;
typedef struct qa_row_parallel_pool qa_row_parallel_pool;
struct qa_row_parallel_pool { void *impl; double jobs_run; double worker_count;
  /* TS tryCreate's `failed` callback: why the workers could not start (NULL when they started
   * or none were configured), so a silent fallback to serial drawing stays diagnosable. */
  const char *failure; };
/* TS tryCreate: returns false (null) when QTS_CPU_WORKERS or the CPU count selects
 * no workers, or when the threads cannot start. */
bool qa_row_parallel_pool_try_create(qa_row_parallel_pool *pool, qa_row_shared_framebuffer shared);
double qa_row_parallel_pool_worker_count(const qa_row_parallel_pool *pool);
qa_row_registries qa_row_parallel_pool_registries(qa_row_parallel_pool *pool);
/* Synchronous: setups and everything they reference must outlive the call, and
 * nothing else may touch the shared framebuffer until it returns. */
void qa_row_parallel_pool_run(qa_row_parallel_pool *pool, const qa_triangle_setup *setups, size_t setup_count);
void qa_row_parallel_pool_close(qa_row_parallel_pool *pool);
#endif

/* Port of src/render/worker-runtime.ts. Everything here runs on the render thread
 * except the wire types, which both sides share. Throws on that thread reach the
 * transport as a terminal failure through qa_range_error_catch. GPL-2.0-or-later. */
#ifndef QA_RENDER_WORKER_RUNTIME_H
#define QA_RENDER_WORKER_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/platform/sdl_render_context.h"
#include "qa/render/gl/renderer.h"
#include "qa/render/worker_protocol.h"
#include "qa/render/worker_transport.h"
/* TS initialization {backend:"cpu",width,height,gamma} | {backend:"gl",transfer,gamma}. */
typedef struct qa_render_worker_init {
  const char *backend; /* static "cpu"|"gl" */
  double width, height, gamma;
  qa_sdl_render_context_transfer transfer;
} qa_render_worker_init;
/* TS SdlWorkerRenderContext as the runtime uses it. */
typedef struct qa_render_worker_context {
  void *ctx;
  qa_gl_render_context render;
  void (*park)(void *ctx);
  void (*resume)(void *ctx);
  void (*set_swap_interval)(void *ctx, int32_t interval);
  int32_t (*swap_interval)(void *ctx);
  void (*release)(void *ctx);
} qa_render_worker_context;
/* Initialize ctx. NULL means SdlWorkerRenderContext.adopt; a test may supply its own. */
typedef struct qa_render_worker_platform {
  void *ctx;
  bool (*adopt)(void *ctx, const qa_sdl_render_context_transfer *transfer, qa_render_worker_context *out);
} qa_render_worker_platform;
/* TS createRenderWorkerRuntime: payload is a qa_render_worker_init, ctx a platform or NULL. */
bool qa_create_render_worker_runtime(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure);

/* --- dispatch payloads --- */
typedef struct qa_worker_gl_config { double max_texture_size, texture_units, color_bits, depth_bits; bool stereo_enabled; } qa_worker_gl_config;
typedef enum qa_worker_op {
  QA_WORKER_OP_EXECUTE, QA_WORKER_OP_IMAGE, QA_WORKER_OP_BEGIN_VIEW, QA_WORKER_OP_IMMEDIATE, QA_WORKER_OP_PREPARE,
  QA_WORKER_OP_PREPARED, QA_WORKER_OP_OPACITY, QA_WORKER_OP_DRAW_BUFFER, QA_WORKER_OP_OVERDRAW, QA_WORKER_OP_STENCIL,
  QA_WORKER_OP_DEPTH, QA_WORKER_OP_SHOW_IMAGE, QA_WORKER_OP_CLEAR_COLOR, QA_WORKER_OP_GAMMA, QA_WORKER_OP_RESIZE,
  QA_WORKER_OP_FINISH, QA_WORKER_OP_PIXELS, QA_WORKER_OP_DESCRIPTION, QA_WORKER_OP_SWAP_INTERVAL, QA_WORKER_OP_PARK, QA_WORKER_OP_RESUME
} qa_worker_op;
typedef enum qa_worker_phase { QA_WORKER_PHASE_BEGIN, QA_WORKER_PHASE_TEXTURE, QA_WORKER_PHASE_DRAW, QA_WORKER_PHASE_CLEANUP } qa_worker_phase;
typedef struct qa_worker_wire_request {
  qa_worker_op kind;
  qa_wire_command *commands; size_t command_count; bool final;
  qa_wire_image_operation operation;
  qa_wire_view view;
  qa_wire_operation *immediate;
  qa_wire_batch *batch;
  double id; qa_worker_phase phase; double unit; qa_wire_binding texture;
  double opacity, callback;
  const char *buffer; bool flag;
  double length, x, y;
  qa_wire_image image; qa_rect rect;
  double gamma, width, height, interval;
} qa_worker_wire_request;
/* TS WorkerDescription on the wire. */
typedef struct qa_worker_wire_description {
  bool is_gl; double width, height, stencil_bits, swap_interval;
  bool has_driver; char vendor[256], renderer[256], version[256], shading_language[256];
  bool has_gl_config; qa_worker_gl_config gl_config;
} qa_worker_wire_description;
/* Reply of "pixels": this header, then length bytes. */
typedef struct qa_worker_wire_pixels { double width, height; size_t length; } qa_worker_wire_pixels;

/* --- host requests the runtime makes --- */
typedef enum qa_worker_host_kind { QA_WORKER_HOST_IMAGE_APPLIED, QA_WORKER_HOST_DYNAMIC_IMAGE, QA_WORKER_HOST_PENDING_IMAGES, QA_WORKER_HOST_OPACITY_CALLBACK, QA_WORKER_HOST_SWAP } qa_worker_host_kind;
/* Swap: this header, capture_count doubles, then image_length bytes. */
typedef struct qa_worker_host_request {
  qa_worker_host_kind kind;
  double token, callback;
  bool has_image; double width, height; size_t image_length, capture_count;
} qa_worker_host_request;
#endif

/* Port of src/render/worker-runtime.ts. GPL-2.0-or-later. */
#include "qa/render/worker_runtime.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/formats/images/mip.h"
#include "qa/platform/gl.h"
#include "qa/render/cpu/rasterizer.h"
#include "qa/render/execution.h"
#include "qa/render/image_journal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct prepared_entry { double id; qa_prepared_backend_draw draw; } prepared_entry;
typedef struct rimpl {
  qa_rwt_worker *worker;
  qa_identity_owner identity; qa_renderer_resource_owner owner;
  bool has_context; qa_render_worker_context context;
  bool is_gl; qa_software_renderer cpu; qa_gl_renderer gl;
  qa_renderer_backend backend;
  qa_render_executor executor;
  qa_render_image_journal journal;
  qa_wire_decoder decoder;
  prepared_entry *prepared; size_t prepared_count, prepared_cap;
  bool closed, overdraw, parked;
  double gamma, swap_interval;
  bool has_thrown; qa_rwt_failure thrown; /* the transport failure a raise is carrying */
  qa_worker_wire_description initial;
} rimpl;

/* TS request(): a transport failure throws; the failure itself crosses via has_thrown. */
static void request_host(rimpl *p, const void *bytes, size_t size, qa_rwt_value *out) {
  qa_rwt_value payload; payload.bytes = bytes; payload.size = size; payload.owned = NULL;
  qa_rwt_failure failure; memset(&failure, 0, sizeof failure);
  qa_rwt_value reply; memset(&reply, 0, sizeof reply);
  if (!qa_rwt_worker_request(p->worker, payload, &reply, &failure)) {
    p->thrown = failure; p->has_thrown = true;
    qa_range_error("%s", failure.message[0] != 0 ? failure.message : "Renderer host callback failed");
  }
  if (out != NULL) *out = reply; else qa_rwt_value_release(&reply);
}
static qa_renderer_image reached_dynamic(void *ctx, double token, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  /* TS ignores apply here: the frontend applies through a nested "image" dispatch. */
  (void)apply; (void)apply_ctx;
  rimpl *p = (rimpl *)ctx;
  qa_worker_host_request request; memset(&request, 0, sizeof request);
  request.kind = QA_WORKER_HOST_DYNAMIC_IMAGE; request.token = token;
  qa_rwt_value reply; request_host(p, &request, sizeof request, &reply);
  if (reply.size != sizeof(qa_wire_image)) { qa_rwt_value_release(&reply); qa_range_error("Invalid renderer wire record"); }
  qa_wire_image wire; memcpy(&wire, reply.bytes, sizeof wire);
  qa_rwt_value_release(&reply);
  return qa_wire_decoder_image(&p->decoder, wire);
}
static void refresh_backend(rimpl *p) {
  /* TS backend.width/height are getters; GL follows the drawable. */
  if (p->is_gl) { p->backend.width = qa_gl_renderer_width(&p->gl); p->backend.height = qa_gl_renderer_height(&p->gl); }
  p->executor.backend = p->backend;
}
static void *pixels_blob(rimpl *p, size_t *out_size, double *out_width, double *out_height) {
  qa_worker_wire_pixels header; memset(&header, 0, sizeof header);
  const uint8_t *bytes; uint8_t *gl_pixels = NULL; qa_image_level level; memset(&level, 0, sizeof level);
  if (p->is_gl) {
    size_t length = 0;
    gl_pixels = qa_gl_renderer_read_pixels(&p->gl, &length);
    if (gl_pixels == NULL) qa_range_error("Render worker allocation failed");
    header.width = qa_gl_renderer_width(&p->gl); header.height = qa_gl_renderer_height(&p->gl); header.length = length; bytes = gl_pixels;
  } else {
    if (!qa_software_renderer_read_rgba(&p->cpu, &level)) qa_range_error("Render worker allocation failed");
    header.width = level.width; header.height = level.height; header.length = level.pixels.length; bytes = level.pixels.bytes;
  }
  size_t size = sizeof header + header.length;
  unsigned char *blob = (unsigned char *)malloc(size);
  if (blob == NULL) qa_range_error("Render worker allocation failed");
  memcpy(blob, &header, sizeof header);
  memcpy(blob + sizeof header, bytes, header.length);
  free(gl_pixels);
  if (!p->is_gl) qa_image_level_destroy(&level);
  *out_size = size; *out_width = header.width; *out_height = header.height;
  return blob;
}
static void svc_image(void *ctx, qa_image_resource_operation op) {
  rimpl *p = (rimpl *)ctx;
  qa_render_image_journal_record(&p->journal, op);
  qa_worker_host_request request; memset(&request, 0, sizeof request);
  request.kind = QA_WORKER_HOST_IMAGE_APPLIED; request.token = qa_wire_decoder_acknowledgment(&p->decoder, &op);
  request_host(p, &request, sizeof request, NULL);
}
static void request_swap(rimpl *p, const double *captures, size_t count) {
  size_t pixels_size = 0; double width = 0, height = 0;
  unsigned char *pixels = (unsigned char *)pixels_blob(p, &pixels_size, &width, &height);
  size_t image_length = pixels_size - sizeof(qa_worker_wire_pixels);
  qa_worker_host_request request; memset(&request, 0, sizeof request);
  request.kind = QA_WORKER_HOST_SWAP; request.has_image = true; request.width = width; request.height = height;
  request.image_length = image_length; request.capture_count = count;
  size_t size = sizeof request + count * sizeof(double) + image_length;
  unsigned char *blob = (unsigned char *)malloc(size);
  if (blob == NULL) { free(pixels); qa_range_error("Render worker allocation failed"); }
  memcpy(blob, &request, sizeof request);
  if (count != 0) memcpy(blob + sizeof request, captures, count * sizeof(double));
  memcpy(blob + sizeof request + count * sizeof(double), pixels + sizeof(qa_worker_wire_pixels), image_length);
  free(pixels);
  qa_rwt_value payload; payload.bytes = blob; payload.size = size; payload.owned = NULL;
  qa_rwt_failure failure; memset(&failure, 0, sizeof failure);
  qa_rwt_value reply; memset(&reply, 0, sizeof reply);
  bool ok = qa_rwt_worker_request(p->worker, payload, &reply, &failure);
  free(blob);
  if (!ok) { p->thrown = failure; p->has_thrown = true; qa_range_error("%s", failure.message[0] != 0 ? failure.message : "Renderer host callback failed"); }
  qa_rwt_value_release(&reply);
}
static void svc_swap(void *ctx, const double *captures, size_t count) {
  rimpl *p = (rimpl *)ctx;
  if (!p->is_gl) {
    qa_software_renderer_finish(&p->cpu);
    request_swap(p, captures, count);
  } else {
    if (count != 0) request_swap(p, captures, count);
    qa_gl_renderer_present(&p->gl);
  }
}
static qa_worker_wire_description describe(rimpl *p) {
  qa_worker_wire_description d; memset(&d, 0, sizeof d);
  d.is_gl = p->is_gl; d.swap_interval = p->swap_interval;
  if (p->is_gl) {
    d.width = qa_gl_renderer_width(&p->gl); d.height = qa_gl_renderer_height(&p->gl); d.stencil_bits = p->gl.stencil_bits;
    d.has_driver = true;
    (void)snprintf(d.vendor, sizeof d.vendor, "%s", p->gl.driver.vendor != NULL ? p->gl.driver.vendor : "");
    (void)snprintf(d.renderer, sizeof d.renderer, "%s", p->gl.driver.renderer != NULL ? p->gl.driver.renderer : "");
    (void)snprintf(d.version, sizeof d.version, "%s", p->gl.driver.version != NULL ? p->gl.driver.version : "");
    (void)snprintf(d.shading_language, sizeof d.shading_language, "%s", p->gl.driver.shading_language != NULL ? p->gl.driver.shading_language : "");
    d.has_gl_config = true;
    d.gl_config = (qa_worker_gl_config){p->gl.max_texture_size, p->gl.texture_units, p->gl.color_bits, p->gl.depth_bits, p->gl.stereo_enabled};
  } else { d.width = p->cpu.width; d.height = p->cpu.height; d.stencil_bits = p->cpu.stencil_bits; }
  return d;
}
static void reply_copy(qa_rwt_value *out, const void *bytes, size_t size) {
  void *owned = malloc(size ? size : 1);
  if (owned == NULL) qa_range_error("Render worker allocation failed");
  memcpy(owned, bytes, size);
  out->bytes = owned; out->size = size; out->owned = owned;
}
static void reply_description(rimpl *p, qa_rwt_value *out) { qa_worker_wire_description d = describe(p); reply_copy(out, &d, sizeof d); }

/* The TS Iterable leaves: decode each operation and batch only when reached. */
typedef struct opacity_scope { rimpl *p; const qa_wire_operation *operation; } opacity_scope;
static void draw_batches(rimpl *p, const qa_wire_operation *operation) {
  for (size_t i = 0; i < operation->batch_count; i++) qa_render_executor_draw(&p->executor, qa_wire_decoder_batch(&p->decoder, &operation->batches[i]));
}
static void opacity_draw(void *ctx) { opacity_scope *s = (opacity_scope *)ctx; draw_batches(s->p, s->operation); }
static void run_operations(rimpl *p, const qa_wire_operation *operations, size_t count) {
  for (size_t i = 0; i < count; i++) {
    qa_execution_operation op = qa_wire_decoder_operation(&p->decoder, &operations[i]);
    if (op.kind == QA_RENDER_OP_DRAW) draw_batches(p, &operations[i]);
    else if (op.kind == QA_RENDER_OP_OBJECT_OPACITY) {
      opacity_scope scope = {p, &operations[i]};
      p->backend.with_object_opacity(p->backend.ctx, op.opacity, opacity_draw, &scope);
    } else p->backend.draw_immediate(p->backend.ctx, op);
  }
}
static void run_command(rimpl *p, const qa_wire_command *wire) {
  qa_execution_command command = qa_wire_decoder_command(&p->decoder, wire);
  refresh_backend(p);
  if (command.kind == QA_EXEC_COMMAND_VIEW) {
    run_operations(p, wire->view.before_view, wire->view.before_view_count);
    p->backend.begin_view(p->backend.ctx, qa_wire_decoder_view_state(&p->decoder, &wire->view));
    run_operations(p, wire->view.operations, wire->view.operation_count);
  } else qa_render_executor_execute(&p->executor, command);
}
static void journal_apply(void *ctx, qa_image_resource_operation op) { qa_software_renderer_apply_image_resource((qa_software_renderer *)ctx, op); }
static void resize_cpu(rimpl *p, double width, double height) {
  if (p->is_gl || (width == p->cpu.width && height == p->cpu.height)) return;
  if (p->prepared_count != 0) qa_range_error("Cannot resize with prepared worker geometry");
  qa_software_renderer next; memset(&next, 0, sizeof next);
  if (!qa_software_renderer_init_default(&next, width, height, p->owner)) qa_range_error("Render worker allocation failed");
  qa_software_renderer_set_output_gamma(&next, p->gamma);
  qa_software_renderer_set_overdraw_measurement(&next, p->overdraw);
  qa_journal_backend journal = {&next, journal_apply};
  qa_render_image_journal_replay(&p->journal, journal);
  qa_software_renderer_close(&p->cpu);
  p->cpu = next;
  p->backend = qa_software_renderer_backend(&p->cpu);
  qa_render_executor_replace_backend(&p->executor, p->backend);
}
static double dimension(double value) {
  if (!isfinite(value)) qa_range_error("Invalid render worker number");
  if (value != floor(value) || value < 0 || value > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Invalid render worker integer");
  if (value == 0) qa_range_error("Empty render worker dimension");
  return value;
}
static double worker_integer(double value) {
  if (!isfinite(value)) qa_range_error("Invalid render worker number");
  if (value != floor(value) || value < 0 || value > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Invalid render worker integer");
  return value;
}
static void set_gamma(rimpl *p, double gamma) {
  if (p->is_gl) qa_gl_renderer_set_output_gamma(&p->gl, gamma);
  else qa_software_renderer_set_output_gamma(&p->cpu, gamma);
}
static prepared_entry *find_prepared(rimpl *p, double id) {
  for (size_t i = 0; i < p->prepared_count; i++) if (p->prepared[i].id == id) return &p->prepared[i];
  return NULL;
}
typedef struct host_opacity { rimpl *p; double callback; } host_opacity;
static void opacity_callback(void *ctx) {
  host_opacity *s = (host_opacity *)ctx;
  qa_worker_host_request request; memset(&request, 0, sizeof request);
  request.kind = QA_WORKER_HOST_OPACITY_CALLBACK; request.callback = s->callback;
  request_host(s->p, &request, sizeof request, NULL);
}
typedef struct dispatch_scope { rimpl *p; const qa_worker_wire_request *input; qa_rwt_value *out; } dispatch_scope;
static void dispatch_body(void *ctx) {
  dispatch_scope *s = (dispatch_scope *)ctx; rimpl *p = s->p; const qa_worker_wire_request *in = s->input; qa_rwt_value *out = s->out;
  if (p->closed) qa_range_error("Render worker is closed");
  if (in->kind == QA_WORKER_OP_PARK) { if (p->has_context) p->context.park(p->context.ctx); p->parked = true; return; }
  if (in->kind == QA_WORKER_OP_RESUME) { if (p->has_context) p->context.resume(p->context.ctx); p->parked = false; return; }
  if (p->parked) qa_range_error("Render worker is parked");
  refresh_backend(p);
  switch (in->kind) {
  case QA_WORKER_OP_EXECUTE:
    for (size_t i = 0; i < in->command_count; i++) run_command(p, &in->commands[i]);
    if (in->final) {
      qa_worker_host_request request; memset(&request, 0, sizeof request); request.kind = QA_WORKER_HOST_PENDING_IMAGES;
      qa_rwt_value reply; request_host(p, &request, sizeof request, &reply);
      size_t count = reply.size / sizeof(qa_wire_image_operation);
      for (size_t i = 0; i < count; i++) {
        qa_wire_image_operation wire; memcpy(&wire, (const unsigned char *)reply.bytes + i * sizeof wire, sizeof wire);
        qa_render_executor_image(&p->executor, qa_wire_decoder_image_operation(&p->decoder, wire));
      }
      qa_rwt_value_release(&reply);
      if (p->prepared_count == 0) qa_wire_decoder_clear_sources(&p->decoder);
    }
    reply_description(p, out);
    return;
  case QA_WORKER_OP_IMAGE: qa_render_executor_image(&p->executor, qa_wire_decoder_image_operation(&p->decoder, in->operation)); return;
  case QA_WORKER_OP_BEGIN_VIEW: p->backend.begin_view(p->backend.ctx, qa_wire_decoder_view_state(&p->decoder, &in->view)); return;
  case QA_WORKER_OP_IMMEDIATE: {
    qa_execution_operation op = qa_wire_decoder_operation(&p->decoder, in->immediate);
    if (op.kind == QA_RENDER_OP_DRAW || op.kind == QA_RENDER_OP_OBJECT_OPACITY) qa_range_error("Non-immediate render operation");
    p->backend.draw_immediate(p->backend.ctx, op);
    return;
  }
  case QA_WORKER_OP_PREPARE: {
    double id = worker_integer(in->id);
    if (find_prepared(p, id) != NULL) qa_range_error("Duplicate prepared worker draw");
    qa_prepared_backend_draw draw = p->backend.prepare_geometry(p->backend.ctx, qa_wire_decoder_batch(&p->decoder, in->batch));
    if (p->prepared_count == p->prepared_cap) {
      size_t cap = p->prepared_cap ? p->prepared_cap * 2 : 8;
      prepared_entry *grown = (prepared_entry *)realloc(p->prepared, cap * sizeof *grown);
      if (grown == NULL) qa_range_error("Render worker allocation failed");
      p->prepared = grown; p->prepared_cap = cap;
    }
    p->prepared[p->prepared_count].id = id; p->prepared[p->prepared_count].draw = draw; p->prepared_count++;
    return;
  }
  case QA_WORKER_OP_PREPARED: {
    prepared_entry *entry = find_prepared(p, worker_integer(in->id));
    if (entry == NULL) qa_range_error("Unknown prepared worker draw");
    /* By value: a nested dispatch reached through a dynamic texture may grow the table. */
    qa_prepared_backend_draw draw = entry->draw; double id = entry->id;
    switch (in->phase) {
    case QA_WORKER_PHASE_BEGIN: draw.begin(draw.ctx); return;
    case QA_WORKER_PHASE_TEXTURE: draw.apply_texture(draw.ctx, worker_integer(in->unit), qa_wire_decoder_binding(&p->decoder, in->texture)); return;
    case QA_WORKER_PHASE_DRAW: draw.draw(draw.ctx); return;
    case QA_WORKER_PHASE_CLEANUP:
      draw.cleanup(draw.ctx);
      entry = find_prepared(p, id);
      if (entry != NULL) { size_t i = (size_t)(entry - p->prepared); memmove(entry, entry + 1, (p->prepared_count - i - 1) * sizeof *entry); p->prepared_count--; }
      return;
    }
    qa_range_error("Unknown prepared worker phase");
  }
  case QA_WORKER_OP_OPACITY: {
    if (!isfinite(in->opacity)) qa_range_error("Invalid render worker number");
    host_opacity scope = {p, in->callback};
    p->backend.with_object_opacity(p->backend.ctx, in->opacity, opacity_callback, &scope);
    return;
  }
  case QA_WORKER_OP_DRAW_BUFFER: {
    const char *b = in->buffer;
    if (b == NULL || (strcmp(b, "front") && strcmp(b, "back") && strcmp(b, "back-left") && strcmp(b, "back-right"))) qa_range_error("Invalid worker draw buffer");
    p->backend.select_draw_buffer(p->backend.ctx, b, in->flag);
    return;
  }
  case QA_WORKER_OP_OVERDRAW: p->backend.set_overdraw_measurement(p->backend.ctx, in->flag); p->overdraw = in->flag; return;
  case QA_WORKER_OP_STENCIL: {
    size_t length = (size_t)worker_integer(in->length);
    uint8_t *destination = (uint8_t *)calloc(length ? length : 1, 1);
    if (destination == NULL) qa_range_error("Render worker allocation failed");
    p->backend.read_stencil_overdraw(p->backend.ctx, destination, length);
    out->bytes = destination; out->size = length; out->owned = destination;
    return;
  }
  case QA_WORKER_OP_DEPTH: {
    if (!isfinite(in->x) || !isfinite(in->y)) qa_range_error("Invalid render worker number");
    double depth = p->backend.read_depth_pixel(p->backend.ctx, in->x, in->y);
    reply_copy(out, &depth, sizeof depth);
    return;
  }
  case QA_WORKER_OP_SHOW_IMAGE:
    if (!isfinite(in->rect.x) || !isfinite(in->rect.y) || !isfinite(in->rect.width) || !isfinite(in->rect.height)) qa_range_error("Invalid render worker number");
    p->backend.draw_show_image(p->backend.ctx, qa_wire_decoder_image(&p->decoder, in->image), in->rect, in->flag);
    return;
  case QA_WORKER_OP_CLEAR_COLOR: p->backend.clear_color_buffer(p->backend.ctx); return;
  case QA_WORKER_OP_GAMMA:
    if (!isfinite(in->gamma)) qa_range_error("Invalid render worker number");
    set_gamma(p, in->gamma); p->gamma = in->gamma;
    return;
  case QA_WORKER_OP_RESIZE: resize_cpu(p, dimension(in->width), dimension(in->height)); reply_description(p, out); return;
  case QA_WORKER_OP_FINISH: p->backend.finish(p->backend.ctx); return;
  case QA_WORKER_OP_PIXELS: {
    size_t size = 0; double width = 0, height = 0;
    void *blob = pixels_blob(p, &size, &width, &height);
    out->bytes = blob; out->size = size; out->owned = blob;
    return;
  }
  case QA_WORKER_OP_DESCRIPTION: reply_description(p, out); return;
  case QA_WORKER_OP_SWAP_INTERVAL: {
    double value = in->interval;
    if (value != -1 && value != 0 && value != 1) qa_range_error("Invalid render worker swap interval");
    if (p->has_context) p->context.set_swap_interval(p->context.ctx, (int32_t)value);
    p->swap_interval = p->has_context ? value : 0;
    reply_copy(out, &p->swap_interval, sizeof p->swap_interval);
    return;
  }
  case QA_WORKER_OP_PARK: case QA_WORKER_OP_RESUME: return;
  }
  qa_range_error("Unknown render worker operation: %d", (int)in->kind);
}
/* A throw on this thread: the carried transport failure keeps its host token. */
static void to_failure(rimpl *p, const char *message, qa_rwt_failure *failure) {
  if (p != NULL && p->has_thrown) { *failure = p->thrown; p->has_thrown = false; }
  else qa_rwt_failure_set(failure, "Error", message);
}
static bool rt_dispatch(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_failure *failure) {
  rimpl *p = (rimpl *)ctx;
  if (payload.size != sizeof(qa_worker_wire_request)) { qa_rwt_failure_set(failure, "TypeError", "Invalid render worker request"); return false; }
  qa_worker_wire_request input; memcpy(&input, payload.bytes, sizeof input);
  dispatch_scope scope = {p, &input, out};
  char message[1024];
  if (qa_range_error_catch(dispatch_body, &scope, message, sizeof message)) return true;
  qa_rwt_value_release(out);
  to_failure(p, message, failure);
  return false;
}
/* TS close: collects failures, and stays open for a retry until the backend closed. */
typedef void (*close_step)(void *ctx);
static bool close_try(close_step step, void *ctx, char *first, size_t capacity, bool *failed) {
  char message[1024];
  if (qa_range_error_catch(step, ctx, message, sizeof message)) return true;
  if (!*failed) { (void)snprintf(first, capacity, "Render worker close failed: %.900s", message); *failed = true; }
  return false;
}
static void step_resume(void *ctx) { rimpl *p = (rimpl *)ctx; p->context.resume(p->context.ctx); }
static void step_cleanup(void *ctx) { prepared_entry *e = (prepared_entry *)ctx; e->draw.cleanup(e->draw.ctx); }
static void step_backend(void *ctx) {
  rimpl *p = (rimpl *)ctx;
  if (p->is_gl) qa_gl_renderer_close(&p->gl); else qa_software_renderer_close(&p->cpu);
}
static void step_release(void *ctx) { rimpl *p = (rimpl *)ctx; if (p->has_context) p->context.release(p->context.ctx); }
static bool rt_close(void *ctx, qa_rwt_failure *failure) {
  rimpl *p = (rimpl *)ctx;
  if (p->closed) { free(p); return true; } /* retry after a close that failed late */
  char first[1024]; first[0] = 0; bool failed = false;
  if (p->parked && p->has_context) { if (close_try(step_resume, p, first, sizeof first, &failed)) p->parked = false; }
  for (size_t i = 0; i < p->prepared_count;) {
    if (close_try(step_cleanup, &p->prepared[i], first, sizeof first, &failed)) {
      memmove(&p->prepared[i], &p->prepared[i + 1], (p->prepared_count - i - 1) * sizeof *p->prepared); p->prepared_count--;
    } else i++;
  }
  if (close_try(step_backend, p, first, sizeof first, &failed) && close_try(step_release, p, first, sizeof first, &failed)) {
    p->closed = true;
    if (p->is_gl) qa_gl_renderer_destroy(&p->gl);
    qa_render_image_journal_destroy(&p->journal);
    qa_wire_decoder_destroy(&p->decoder);
    free(p->prepared);
    qa_identity_owner_destroy(&p->identity);
  }
  if (failed) { qa_rwt_failure_set(failure, "AggregateError", first); return false; }
  free(p); /* only reachable with p->closed set: the transport drops the runtime on true */
  return true;
}

/* --- createRenderWorkerRuntime --- */
static void sdl_park(void *ctx) { qa_sdl_worker_render_context_park((qa_sdl_worker_render_context *)ctx); }
static void sdl_resume(void *ctx) { qa_sdl_worker_render_context_resume((qa_sdl_worker_render_context *)ctx); }
static void sdl_set_interval(void *ctx, int32_t interval) { qa_sdl_worker_render_context_set_swap_interval((qa_sdl_worker_render_context *)ctx, interval); }
static int32_t sdl_interval(void *ctx) { return qa_sdl_worker_render_context_swap_interval((qa_sdl_worker_render_context *)ctx); }
static void sdl_release(void *ctx) { qa_sdl_worker_render_context *c = (qa_sdl_worker_render_context *)ctx; qa_sdl_worker_render_context_release(c); free(c); }
static void sdl_make_current(void *ctx) { qa_sdl_worker_render_context_make_current((qa_sdl_worker_render_context *)ctx); }
static void sdl_drawable(void *ctx, double *width, double *height) {
  int32_t w = 0, h = 0; qa_sdl_worker_render_context_drawable_size((qa_sdl_worker_render_context *)ctx, &w, &h);
  *width = w; *height = h;
}
static void sdl_rendering(void *ctx, bool enabled) { qa_sdl_worker_render_context_set_rendering_enabled((qa_sdl_worker_render_context *)ctx, enabled); }
static void sdl_swap(void *ctx) { qa_sdl_worker_render_context_swap((qa_sdl_worker_render_context *)ctx); }
static bool sdl_adopt(const qa_sdl_render_context_transfer *transfer, qa_render_worker_context *out) {
  qa_sdl_worker_render_context *c = (qa_sdl_worker_render_context *)calloc(1, sizeof *c);
  if (c == NULL) return false;
  if (!qa_sdl_worker_render_context_adopt(transfer, c)) { free(c); return false; }
  memset(out, 0, sizeof *out);
  out->ctx = c; out->park = sdl_park; out->resume = sdl_resume; out->set_swap_interval = sdl_set_interval; out->swap_interval = sdl_interval; out->release = sdl_release;
  out->render.ctx = c; out->render.gl_context = qa_gl_context_from_worker(c);
  out->render.make_current = sdl_make_current; out->render.drawable_size = sdl_drawable; out->render.set_rendering_enabled = sdl_rendering; out->render.swap = sdl_swap;
  return true;
}
typedef struct create_scope { rimpl *p; const qa_render_worker_init *init; const qa_render_worker_platform *platform; bool backend_open; } create_scope;
static void create_backend(void *ctx) {
  create_scope *s = (create_scope *)ctx; rimpl *p = s->p; const qa_render_worker_init *init = s->init;
  if (!isfinite(init->gamma)) qa_range_error("Invalid render worker number");
  if (init->backend != NULL && !strcmp(init->backend, "cpu")) {
    if (!qa_software_renderer_init_default(&p->cpu, dimension(init->width), dimension(init->height), p->owner)) qa_range_error("Render worker allocation failed");
    s->backend_open = true;
    p->backend = qa_software_renderer_backend(&p->cpu);
  } else if (init->backend != NULL && !strcmp(init->backend, "gl")) {
    bool adopted = s->platform != NULL ? s->platform->adopt(s->platform->ctx, &init->transfer, &p->context) : sdl_adopt(&init->transfer, &p->context);
    if (!adopted) qa_range_error("Render worker could not adopt its context");
    p->has_context = true; p->is_gl = true;
    if (!qa_gl_renderer_init(&p->gl, p->context.render, p->owner)) qa_range_error("Worker GL initialization failed");
    s->backend_open = true;
    p->backend = qa_gl_renderer_backend(&p->gl);
  } else qa_range_error("Unknown render worker backend");
  set_gamma(p, p->gamma);
  if (p->has_context) p->swap_interval = p->context.swap_interval(p->context.ctx);
}
static void create_cleanup(void *ctx) {
  create_scope *s = (create_scope *)ctx; rimpl *p = s->p;
  if (s->backend_open) step_backend(p);
  if (p->is_gl && s->backend_open) qa_gl_renderer_destroy(&p->gl);
  step_release(p);
}
bool qa_create_render_worker_runtime(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure) {
  if (payload.size != sizeof(qa_render_worker_init)) { qa_rwt_failure_set(failure, "TypeError", "Invalid render worker request"); return false; }
  qa_render_worker_init init; memcpy(&init, payload.bytes, sizeof init);
  rimpl *p = (rimpl *)calloc(1, sizeof *p);
  if (p == NULL) { qa_rwt_failure_set(failure, "Error", "Render worker allocation failed"); return false; }
  if (!qa_identity_owner_create(&p->identity, "render-worker")) { free(p); qa_rwt_failure_set(failure, "Error", "Render worker allocation failed"); return false; }
  p->worker = worker; p->gamma = init.gamma;
  p->owner.identity = p; p->owner.session = p->identity.session; p->owner.generation = 0;
  create_scope scope = {p, &init, (const qa_render_worker_platform *)ctx, false};
  char message[1024];
  if (!qa_range_error_catch(create_backend, &scope, message, sizeof message)) {
    /* TS tears down what opened; a teardown failure joins the startup failure. */
    char cleanup[1024];
    if (qa_range_error_catch(create_cleanup, &scope, cleanup, sizeof cleanup)) qa_rwt_failure_set(failure, "Error", message);
    else { char both[1024]; (void)snprintf(both, sizeof both, "Worker initialization and close failed: %.400s; %.400s", message, cleanup); qa_rwt_failure_set(failure, "AggregateError", both); }
    qa_identity_owner_destroy(&p->identity); free(p);
    return false;
  }
  if (!qa_render_image_journal_init(&p->journal)) qa_range_error("Render worker allocation failed");
  /* TS paletteSource: the receiver's stand-in provenance for indexed palettes. */
  qa_resolved_resource_reference palette; memset(&palette, 0, sizeof palette);
  palette.id = "resource:render-worker-palette"; palette.requested_path = "palette"; palette.byte_length = 768;
  palette.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  if (!qa_wire_decoder_init(&p->decoder, p->owner, reached_dynamic, p, palette)) qa_range_error("Render worker allocation failed");
  qa_execution_services services; services.ctx = p; services.image_applied = svc_image; services.swap = svc_swap;
  qa_render_executor_init(&p->executor, p->backend, services);
  p->initial = describe(p);
  memset(out, 0, sizeof *out);
  out->ctx = p; out->dispatch = rt_dispatch; out->close = rt_close;
  out->description.bytes = &p->initial; out->description.size = sizeof p->initial; out->description.owned = NULL;
  return true;
}

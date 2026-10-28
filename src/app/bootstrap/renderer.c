/* Port of src/app/bootstrap/renderer.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/renderer.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/render/execution.h"
/* FrameCaptures */
typedef struct capture_entry { double id; qa_app_frame_capture_request request; } capture_entry;
typedef struct captures {
  double next_id;
  double *armed; size_t armed_count, armed_cap;
  capture_entry *pending; size_t pending_count, pending_cap;
  const char *failure;
} captures;
static void captures_init(captures *c) { memset(c, 0, sizeof *c); c->next_id = 1; }
static double *captures_take(captures *c, size_t *out_count) {
  double *armed = c->armed; *out_count = c->armed_count;
  c->armed = NULL; c->armed_count = 0; c->armed_cap = 0;
  return armed;
}
static void captures_complete(captures *c, const qa_image_level *image, const double *ids, size_t count) {
  if (count != 0 && image == NULL) qa_range_error("Renderer capture has no pixels");
  if (image == NULL) return;
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < c->pending_count; j++) {
      if (c->pending[j].id != ids[i]) continue;
      capture_entry entry = c->pending[j];
      memmove(&c->pending[j], &c->pending[j + 1], (c->pending_count - j - 1) * sizeof *c->pending);
      c->pending_count--;
      qa_image_level copy = *image;
      uint8_t *pixels = malloc(image->pixels.length ? image->pixels.length : 1);
      if (!pixels) qa_range_error("Out of memory");
      memcpy(pixels, image->pixels.bytes, image->pixels.length);
      copy.pixels.bytes = pixels;
      entry.request.resolve(entry.request.ctx, copy);
      break;
    }
  }
}
static void captures_fail(captures *c, const char *reason) {
  if (c->failure == NULL) c->failure = reason;
  for (size_t i = 0; i < c->pending_count; i++) c->pending[i].request.reject(c->pending[i].request.ctx, c->failure);
  c->pending_count = 0;
  free(c->armed); c->armed = NULL; c->armed_count = 0; c->armed_cap = 0;
}
static void captures_request(captures *c, qa_app_frame_capture_request request) {
  if (c->failure != NULL) { request.reject(request.ctx, c->failure); return; }
  if (request.aborted) { request.reject(request.ctx, request.abort_reason ? request.abort_reason : "Frame capture aborted"); return; }
  double id = c->next_id++;
  if (c->pending_count == c->pending_cap) { c->pending_cap = c->pending_cap ? c->pending_cap * 2 : 8; c->pending = realloc(c->pending, c->pending_cap * sizeof *c->pending); if (!c->pending) qa_range_error("Out of memory"); }
  c->pending[c->pending_count++] = (capture_entry){id, request};
  if (c->armed_count == c->armed_cap) { c->armed_cap = c->armed_cap ? c->armed_cap * 2 : 8; c->armed = realloc(c->armed, c->armed_cap * sizeof *c->armed); if (!c->armed) qa_range_error("Out of memory"); }
  c->armed[c->armed_count++] = id;
}
struct qa_app_native_renderer {
  qa_app_native_renderer_host host;
  qa_app_native_backend_bundle current;
  qa_renderer_resource_owner owner;
  qa_scene_image_registry *images;
  bool owns_images;
  qa_render_image_journal journal;
  bool journal_live;
  captures caps;
  qa_render_executor executor;
  qa_vec4 color;
  double gamma;
  double interval;
  bool closed, preparing;
  qa_app_prepared_renderer_restart *pending_restart;
  /* Retirements retained until close: previous bundles (host-owned ctx + close fns). */
  qa_app_native_backend_bundle *retired; size_t retired_count, retired_cap;
};
struct qa_app_prepared_renderer_restart {
  qa_app_native_renderer *renderer;
  qa_app_native_backend_bundle candidate;
  qa_app_native_backend_bundle previous;
  qa_render_image_journal journal;
  int phase; /* 0 prepared, 1 published, 2 discarded */
  bool retired_done;
};
static void journal_record_cb(void *ctx, qa_image_resource_operation operation) {
  qa_app_native_renderer *r = ctx;
  qa_render_image_journal_record(&r->journal, operation);
}
static void renderer_swap(qa_app_native_renderer *r, const double *ids, size_t count);
static void swap_cb(void *ctx, const double *captures_, size_t capture_count) {
  renderer_swap((qa_app_native_renderer *)ctx, captures_, capture_count);
}
static bool owner_same(qa_renderer_resource_owner a, qa_renderer_resource_owner b) {
  return a.identity == b.identity && a.session.token == b.session.token && a.generation == b.generation;
}
qa_app_native_renderer *qa_app_native_renderer_open(qa_app_native_renderer_host host,
    const char *renderer_kind, double width, double height, bool hidden, double gamma, bool render_worker,
    qa_renderer_resource_owner owner, qa_scene_image_registry *images) {
  qa_app_native_renderer *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->host = host; r->owner = owner; r->gamma = gamma; r->interval = 1;
  r->color = (qa_vec4){1, 1, 1, 1};
  captures_init(&r->caps);
  if (images != NULL) {
    if (!owner_same(images->owner, owner)) { free(r); qa_range_error("Renderer image registry belongs to another owner"); }
    r->images = images;
  } else {
    r->images = calloc(1, sizeof *r->images);
    if (!r->images) { free(r); return NULL; }
    qa_scene_image_registry_init(r->images, owner, NULL);
    r->owns_images = true;
  }
  if (!qa_render_image_journal_init(&r->journal)) { if (r->owns_images) { qa_scene_image_registry_destroy(r->images); free(r->images); } free(r); return NULL; }
  r->journal_live = true;
  qa_app_native_backend_bundle bundle; memset(&bundle, 0, sizeof bundle);
  if (!host.open_bundle(host.ctx, renderer_kind, width, height, hidden, render_worker, owner, gamma, &bundle)) {
    qa_render_image_journal_destroy(&r->journal);
    if (r->owns_images) { qa_scene_image_registry_destroy(r->images); free(r->images); }
    free(r);
    return NULL;
  }
  r->current = bundle;
  if (strcmp(renderer_kind, "gl") == 0 && r->current.window) qa_sdl_window_set_swap_interval(r->current.window, 1);
  qa_execution_services services = {r, journal_record_cb, swap_cb};
  qa_render_executor_init(&r->executor, r->current.backend, services);
  return r;
}
void qa_app_native_renderer_destroy(qa_app_native_renderer *r) {
  if (!r) return;
  qa_app_native_renderer_close(r);
  free(r->caps.armed); free(r->caps.pending); free(r->retired);
  if (r->journal_live) qa_render_image_journal_destroy(&r->journal);
  free(r);
}
qa_sdl_window *qa_app_native_renderer_window(qa_app_native_renderer *r) { return r->current.window; }
qa_renderer_backend qa_app_native_renderer_backend(qa_app_native_renderer *r) { return r->current.backend; }
qa_renderer_resource_owner qa_app_native_renderer_owner(const qa_app_native_renderer *r) { return r->owner; }
qa_scene_image_registry *qa_app_native_renderer_images(qa_app_native_renderer *r) { return r->images; }
bool qa_app_native_renderer_render_worker(const qa_app_native_renderer *r) { return r->current.is_worker; }
const qa_app_gl_driver *qa_app_native_renderer_driver(const qa_app_native_renderer *r) {
  return (r->current.is_worker || r->current.is_gl) ? r->current.driver : NULL;
}
const qa_app_gl_config *qa_app_native_renderer_gl_config(const qa_app_native_renderer *r) {
  return (r->current.is_worker || r->current.is_gl) ? r->current.gl_config : NULL;
}
static const char *window_backend_name(qa_app_native_renderer *r) {
  if (r->current.window == NULL) return r->current.is_gl ? "gl" : "cpu";
  return qa_sdl_window_backend(r->current.window) == QA_SDL_BACKEND_CPU ? "cpu" : "gl";
}
double qa_app_native_renderer_swap_interval(qa_app_native_renderer *r) {
  if (r->current.is_worker) return r->current.worker_swap_interval;
  if (strcmp(window_backend_name(r), "gl") != 0) return 0;
  int32_t interval = qa_sdl_window_swap_interval(r->current.window);
  if (interval != -1 && interval != 0 && interval != 1) qa_range_error("SDL returned an unsupported swap interval");
  return interval;
}
static void writable(qa_app_native_renderer *r) {
  if (r->closed) qa_range_error("Native renderer is closed");
  if (r->preparing || r->pending_restart != NULL) qa_range_error("Native renderer restart is prepared");
}
void qa_app_native_renderer_set_swap_interval(qa_app_native_renderer *r, double interval) {
  writable(r);
  if (r->current.is_worker) { if (r->current.set_swap_interval) r->current.set_swap_interval(r->current.ctx, interval); }
  else if (strcmp(window_backend_name(r), "gl") == 0) qa_sdl_window_set_swap_interval(r->current.window, (int32_t)interval);
  r->interval = interval;
}
void qa_app_native_renderer_mutate_window(qa_app_native_renderer *r, void (*operation)(void *ctx), void *ctx) {
  writable(r);
  if (r->host.with_parked_contexts) r->host.with_parked_contexts(r->host.ctx, operation, ctx);
  else operation(ctx);
}
double qa_app_native_renderer_output_gamma(const qa_app_native_renderer *r) { return r->gamma; }
void qa_app_native_renderer_synchronize(qa_app_native_renderer *r) {
  if (r->current.is_worker && r->current.synchronize) r->current.synchronize(r->current.ctx);
}
void qa_app_native_renderer_set_output_gamma(qa_app_native_renderer *r, double gamma) {
  writable(r);
  if (r->current.set_output_gamma) r->current.set_output_gamma(r->current.ctx, gamma);
  r->gamma = gamma;
}
static void apply_image(qa_app_native_renderer *r, qa_image_resource_operation operation) {
  if (r->current.is_worker) { if (r->current.apply_image_resource) r->current.apply_image_resource(r->current.ctx, operation); }
  else qa_render_executor_image(&r->executor, operation);
}
static void dispose_bundle(qa_app_native_renderer *r, qa_app_native_backend_bundle *bundle) {
  (void)r;
  if (bundle->window && bundle->is_gl && !bundle->is_worker) qa_sdl_window_make_current(bundle->window);
  if (bundle->close_backend) bundle->close_backend(bundle->ctx);
  if (bundle->close_window) bundle->close_window(bundle->ctx);
  else if (bundle->window) qa_sdl_window_close(bundle->window);
}
qa_app_prepared_renderer_restart *qa_app_native_renderer_prepare_restart(qa_app_native_renderer *r, const char *kind, bool has_worker, bool worker) {
  writable(r);
  qa_app_native_renderer_synchronize(r);
  size_t op_count = 0;
  qa_image_resource_operation *ops = qa_scene_image_registry_drain_pending(r->images, &op_count);
  for (size_t i = 0; i < op_count; i++) apply_image(r, ops[i]);
  free(ops);
  if (!has_worker) worker = r->current.is_worker;
  qa_sdl_window_presentation presentation = qa_sdl_window_capture_presentation(r->current.window);
  qa_sdl_size dimensions = qa_sdl_window_drawable_size(r->current.window);
  if (strcmp(window_backend_name(r), "gl") == 0) r->interval = qa_app_native_renderer_swap_interval(r);
  r->preparing = true;
  qa_app_prepared_renderer_restart *p = calloc(1, sizeof *p);
  if (!p) { r->preparing = false; return NULL; }
  p->renderer = r;
  if (!qa_render_image_journal_init(&p->journal)) { r->preparing = false; free(p); return NULL; }
  qa_app_native_backend_bundle candidate; memset(&candidate, 0, sizeof candidate);
  bool opened = r->host.open_bundle(r->host.ctx, kind, presentation.size.width, presentation.size.height, true, worker, r->owner, r->gamma, &candidate);
  if (!opened) { qa_render_image_journal_destroy(&p->journal); r->preparing = false; free(p); return NULL; }
  p->candidate = candidate;
  if (strcmp(kind, "gl") == 0 && candidate.window) qa_sdl_window_set_swap_interval(candidate.window, (int32_t)r->interval);
  qa_journal_backend jb = {candidate.ctx, candidate.apply_image_resource};
  if (candidate.apply_image_resource) qa_render_image_journal_replay(&r->journal, jb);
  if (!candidate.is_worker) {
    /* Serial backend: p->journal starts empty for post-publish operations;
     * the candidate already received the replay above. */
  } else {
    if (candidate.execute_set_color) candidate.execute_set_color(candidate.ctx, r->color);
    if (candidate.synchronize) candidate.synchronize(candidate.ctx);
  }
  if (candidate.window && (qa_sdl_window_width(candidate.window) != dimensions.width || qa_sdl_window_height(candidate.window) != dimensions.height)) {
    dispose_bundle(r, &p->candidate);
    qa_render_image_journal_destroy(&p->journal);
    r->preparing = false; free(p);
    qa_range_error("Prepared renderer drawable dimensions changed");
  }
  if (r->closed) {
    dispose_bundle(r, &p->candidate);
    qa_render_image_journal_destroy(&p->journal);
    r->preparing = false; free(p);
    qa_range_error("Renderer closed during restart preparation");
  }
  p->previous = r->current;
  r->preparing = false;
  r->pending_restart = p;
  return p;
}
qa_sdl_window *qa_app_prepared_renderer_restart_window(qa_app_prepared_renderer_restart *p) { return p->candidate.window; }
qa_renderer_backend qa_app_prepared_renderer_restart_backend(qa_app_prepared_renderer_restart *p) { return p->candidate.backend; }
bool qa_app_prepared_renderer_restart_published(const qa_app_prepared_renderer_restart *p) { return p->phase == 1; }
void qa_app_prepared_renderer_restart_publish(qa_app_prepared_renderer_restart *p) {
  qa_app_native_renderer *r = p->renderer;
  if (p->phase != 0 || r->pending_restart != p || r->closed) qa_range_error("Renderer restart is no longer prepared");
  qa_sdl_window_presentation presentation = qa_sdl_window_capture_presentation(p->previous.window);
  qa_sdl_window_restore_presentation(p->candidate.window, &presentation);
  if (p->candidate.is_gl && !p->candidate.is_worker) qa_sdl_window_make_current(p->candidate.window);
  qa_sdl_window_set_visible(p->previous.window, false);
  r->current = p->candidate;
  if (r->journal_live) qa_render_image_journal_destroy(&r->journal);
  r->journal = p->journal;
  r->journal_live = true;
  memset(&p->journal, 0, sizeof p->journal);
  qa_render_executor_replace_backend(&r->executor, p->candidate.backend);
  qa_execution_command set_color; memset(&set_color, 0, sizeof set_color);
  set_color.kind = QA_EXEC_COMMAND_SET_COLOR; set_color.color = r->color;
  qa_render_executor_execute(&r->executor, set_color);
  p->phase = 1;
  r->pending_restart = NULL;
  if (r->retired_count == r->retired_cap) { r->retired_cap = r->retired_cap ? r->retired_cap * 2 : 4; r->retired = realloc(r->retired, r->retired_cap * sizeof *r->retired); if (!r->retired) qa_range_error("Out of memory"); }
  r->retired[r->retired_count++] = p->previous;
}
void qa_app_prepared_renderer_restart_retire(qa_app_prepared_renderer_restart *p) {
  if (p->retired_done) return;
  dispose_bundle(p->renderer, &p->previous);
  p->retired_done = true;
  qa_app_native_renderer *r = p->renderer;
  for (size_t i = 0; i < r->retired_count; i++) {
    if (r->retired[i].ctx == p->previous.ctx && r->retired[i].window == p->previous.window) {
      memmove(&r->retired[i], &r->retired[i + 1], (r->retired_count - i - 1) * sizeof *r->retired);
      r->retired_count--;
      break;
    }
  }
}
void qa_app_prepared_renderer_restart_discard(qa_app_prepared_renderer_restart *p) {
  if (p->phase != 0) return;
  dispose_bundle(p->renderer, &p->candidate);
  qa_render_image_journal_destroy(&p->journal);
  p->phase = 2;
  if (p->renderer->pending_restart == p) p->renderer->pending_restart = NULL;
}
void qa_app_renderer_diagnostics_free(qa_app_renderer_diagnostics *d) {
  free(d->display_modes); free(d->images); memset(d, 0, sizeof *d);
}
static const char *encoding_name(qa_render_image_kind kind) {
  return kind == QA_RENDER_IMAGE_INDEXED8 ? "indexed8" : kind == QA_RENDER_IMAGE_RGBA8 ? "rgba8" : "depth32f";
}
qa_app_renderer_diagnostics qa_app_native_renderer_diagnostics(qa_app_native_renderer *r) {
  if (r->closed) qa_range_error("Native renderer is closed");
  qa_app_native_renderer_synchronize(r);
  qa_app_renderer_diagnostics d; memset(&d, 0, sizeof d);
  d.backend = window_backend_name(r);
  d.width = r->current.backend.width;
  d.height = r->current.backend.height;
  d.driver = qa_app_native_renderer_driver(r);
  d.display_modes = qa_sdl_window_display_modes(r->current.window, &d.display_mode_count);
  size_t capacity = 64;
  for (;;) {
    qa_journal_image_description *rows = calloc(capacity, sizeof *rows);
    if (!rows) qa_range_error("Out of memory");
    size_t total = qa_render_image_journal_describe(&r->journal, rows, capacity);
    if (total <= capacity) {
      d.images = calloc(total ? total : 1, sizeof *d.images);
      if (!d.images) qa_range_error("Out of memory");
      for (size_t i = 0; i < total; i++) {
        d.images[i].ordinal = rows[i].ordinal; d.images[i].name = rows[i].name;
        d.images[i].width = rows[i].width; d.images[i].height = rows[i].height;
        d.images[i].encoding = encoding_name(rows[i].encoding); d.images[i].mip_levels = rows[i].mip_levels;
      }
      d.image_count = total;
      free(rows);
      break;
    }
    free(rows);
    capacity = total;
  }
  return d;
}
static void renderer_resize(qa_app_native_renderer *r) {
  if (r->current.is_gl && !r->current.is_worker) return;
  qa_sdl_size size = qa_sdl_window_drawable_size(r->current.window);
  if (r->current.is_worker) { if (r->current.resize) r->current.resize(r->current.ctx, size.width, size.height); return; }
  if (size.width == r->current.backend.width && size.height == r->current.backend.height) return;
  /* CPU replacement backend construction is host work. */
  if (r->current.resize) r->current.resize(r->current.ctx, size.width, size.height);
}
static void renderer_swap(qa_app_native_renderer *r, const double *ids, size_t count) {
  if (r->current.is_worker) qa_range_error("Worker swaps execute on their owning thread");
  if (r->current.is_cpu && r->current.finish) r->current.finish(r->current.ctx);
  if (count != 0) {
    qa_image_level image; memset(&image, 0, sizeof image);
    image.width = r->current.backend.width; image.height = r->current.backend.height;
    uint8_t *owned = NULL;
    if (r->current.is_cpu) {
      size_t len = 0; const uint8_t *px = r->current.pixels ? r->current.pixels(r->current.ctx, &len) : NULL;
      image.pixels.bytes = px; image.pixels.length = len;
    } else {
      size_t len = 0; owned = r->current.read_pixels ? r->current.read_pixels(r->current.ctx, &len) : NULL;
      image.pixels.bytes = owned; image.pixels.length = len;
    }
    captures_complete(&r->caps, &image, ids, count);
    free(owned);
  }
  if (r->current.is_cpu) {
    size_t len = 0; const uint8_t *px = r->current.pixels ? r->current.pixels(r->current.ctx, &len) : NULL;
    if (r->current.present_cpu) r->current.present_cpu(r->current.ctx, px, len);
    else qa_sdl_window_present(r->current.window, px, len);
  } else if (r->current.present) r->current.present(r->current.ctx);
}
static qa_execution_command to_execution(qa_render_command command, const double *captures_, size_t capture_count, qa_execution_view *view_storage) {
  qa_execution_command out; memset(&out, 0, sizeof out);
  out.kind = (qa_execution_command_kind)command.kind;
  out.buffer = command.buffer; out.clear = command.clear; out.operation = command.operation;
  out.color = command.color; out.rect = command.rect; out.uv = command.uv; out.image = command.image;
  view_storage->state = command.view.state;
  view_storage->before_view = command.view.before_view; view_storage->before_view_count = command.view.before_view_count;
  view_storage->operations = command.view.operations; view_storage->operation_count = command.view.operation_count;
  out.view = *view_storage;
  out.captures = captures_; out.capture_count = capture_count;
  return out;
}
void qa_app_native_renderer_execute(qa_app_native_renderer *r, qa_render_frame frame) {
  writable(r);
  if (!owner_same(frame.owner, r->owner)) qa_range_error("Frame belongs to another renderer lifetime");
  renderer_resize(r);
  if (r->current.is_worker) {
    bool has_swap = false;
    for (size_t i = 0; i < frame.command_count; i++) if (frame.commands[i].kind == QA_RENDER_COMMAND_SWAP_BUFFERS) has_swap = true;
    size_t cap_count = 0;
    double *cap_ids = has_swap ? captures_take(&r->caps, &cap_count) : NULL;
    size_t op_count = 0;
    qa_image_resource_operation *ops = qa_scene_image_registry_drain_pending(r->images, &op_count);
    if (r->current.execute_commands) r->current.execute_commands(r->current.ctx, frame.commands, frame.command_count, cap_ids, cap_count);
    for (size_t i = 0; i < op_count; i++) apply_image(r, ops[i]);
    free(ops);
    free(cap_ids);
    for (size_t i = 0; i < frame.command_count; i++)
      if (frame.commands[i].kind == QA_RENDER_COMMAND_SET_COLOR) r->color = frame.commands[i].color;
  } else {
    for (size_t i = 0; i < frame.command_count; i++) {
      qa_render_command command = frame.commands[i];
      if (command.kind == QA_RENDER_COMMAND_SET_COLOR) r->color = command.color;
      qa_execution_view view_storage;
      if (command.kind == QA_RENDER_COMMAND_SWAP_BUFFERS) {
        size_t cap_count = 0;
        double *cap_ids = captures_take(&r->caps, &cap_count);
        qa_render_executor_execute(&r->executor, to_execution(command, cap_ids, cap_count, &view_storage));
        free(cap_ids);
      } else {
        qa_render_executor_execute(&r->executor, to_execution(command, NULL, 0, &view_storage));
      }
    }
    size_t op_count = 0;
    qa_image_resource_operation *ops = qa_scene_image_registry_drain_pending(r->images, &op_count);
    for (size_t i = 0; i < op_count; i++) apply_image(r, ops[i]);
    free(ops);
  }
}
uint8_t *qa_app_native_renderer_read_pixels(qa_app_native_renderer *r, size_t *out_length) {
  if (strcmp(window_backend_name(r), "cpu") != 0)
    qa_range_error("GL captures must be requested before presentation with captureNextFrame()");
  if (r->current.is_worker) {
    if (r->current.read_pixels == NULL) qa_range_error("CPU renderer backend is unavailable");
    return r->current.read_pixels(r->current.ctx, out_length);
  }
  if (!r->current.is_cpu) qa_range_error("CPU renderer backend is unavailable");
  if (r->current.finish) r->current.finish(r->current.ctx);
  size_t len = 0; const uint8_t *px = r->current.pixels ? r->current.pixels(r->current.ctx, &len) : NULL;
  uint8_t *copy = malloc(len ? len : 1);
  if (!copy) qa_range_error("Out of memory");
  if (px) memcpy(copy, px, len);
  *out_length = len;
  return copy;
}
void qa_app_native_renderer_capture_next_frame(qa_app_native_renderer *r, qa_app_frame_capture_request request) {
  if (r->closed) { request.reject(request.ctx, "Native renderer is closed"); return; }
  captures_request(&r->caps, request);
}
void qa_app_native_renderer_close(qa_app_native_renderer *r) {
  if (r->closed) return;
  r->closed = true;
  if (r->pending_restart) qa_app_prepared_renderer_restart_discard(r->pending_restart);
  captures_fail(&r->caps, "Renderer closed before the requested frame was presented");
  dispose_bundle(r, &r->current);
  /* images.close() + drain */
  size_t op_count = 0;
  qa_image_resource_operation *ops = qa_scene_image_registry_drain_pending(r->images, &op_count);
  free(ops);
  if (r->owns_images) { qa_scene_image_registry_destroy(r->images); free(r->images); r->images = NULL; r->owns_images = false; }
  qa_render_image_journal_clear(&r->journal);
  for (size_t i = 0; i < r->retired_count; i++) dispose_bundle(r, &r->retired[i]);
  r->retired_count = 0;
}

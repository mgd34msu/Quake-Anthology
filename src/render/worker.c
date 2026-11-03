/* Port of src/render/worker.ts. GPL-2.0-or-later. */
#include "qa/render/worker.h"
#include "qa/foundation/support.h"
#include "qa/render/worker_entry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS 8 MiB packet split. TS wireBytes estimates a JS object graph and C the snapshot
 * bytes; only where packets split depends on it. */
#define PACKET_LIMIT (8u * 1024u * 1024u)
#define DEFAULT_TIMEOUT 30000.0

typedef struct wprep { qa_worker_renderer *r; double id; qa_wire_arena *arena; struct wprep *next, *prev; } wprep;
typedef struct wcallback { double id; void (*draw)(void *); void *ctx; } wcallback;
typedef struct wimpl {
  qa_worker_renderer *self;
  qa_wire_encoder encoder;
  qa_rwt_transport *transport;
  qa_worker_window window;
  qa_scene_image_registry *images; qa_render_image_journal *journal;
  qa_worker_swap_fn swap; void *swap_ctx;
  qa_worker_failed_fn failed; void *failed_ctx;
  bool has_metadata; qa_worker_wire_description metadata;
  qa_wire_image_operation *suffix; size_t suffix_count, suffix_cap;
  int callback_depth;
  double next_draw, next_callback;
  wcallback *callbacks; size_t callback_count, callback_cap;
  wprep *draws;
  qa_wire_arena *inflight; /* backs the issued packet until the next barrier */
  bool parked, closed, registered;
  char last_error[1024];
  struct wimpl *next_context;
} wimpl;
static wimpl *W(const qa_worker_renderer *r) { return (wimpl *)r->impl; }
/* TS static contexts Set; frontend-thread state, as SDL window changes are. */
static wimpl *contexts;
static void contexts_remove(wimpl *p) {
  for (wimpl **link = &contexts; *link != NULL; link = &(*link)->next_context) if (*link == p) { *link = p->next_context; p->next_context = NULL; break; }
  p->registered = false;
}

/* --- description --- */
static double desc_integer(double v) { if (v != floor(v) || v < 0 || v > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Invalid render worker integer"); return v; }
static qa_worker_wire_description decode_description(const void *bytes, size_t size) {
  if (bytes == NULL || size != sizeof(qa_worker_wire_description)) qa_range_error("Invalid render worker response");
  qa_worker_wire_description d; memcpy(&d, bytes, sizeof d);
  if (d.swap_interval != -1 && d.swap_interval != 0 && d.swap_interval != 1) qa_range_error("Invalid render worker swap interval");
  (void)desc_integer(d.width); (void)desc_integer(d.height); (void)desc_integer(d.stencil_bits);
  if (d.has_gl_config) { (void)desc_integer(d.gl_config.max_texture_size); (void)desc_integer(d.gl_config.texture_units); (void)desc_integer(d.gl_config.color_bits); (void)desc_integer(d.gl_config.depth_bits); }
  return d;
}
static void set_metadata(wimpl *p, const void *bytes, size_t size) {
  p->metadata = decode_description(bytes, size); p->has_metadata = true;
  p->self->width = p->metadata.width; p->self->height = p->metadata.height; p->self->stencil_bits = p->metadata.stencil_bits;
}
qa_worker_description qa_worker_renderer_description(qa_worker_renderer *r, qa_gl_driver_info *driver, qa_worker_gl_config *config) {
  wimpl *p = W(r);
  if (p == NULL || !p->has_metadata) qa_range_error("Render worker is not initialized");
  qa_worker_description d; memset(&d, 0, sizeof d);
  d.backend = p->metadata.is_gl ? "gl" : "cpu"; d.width = p->metadata.width; d.height = p->metadata.height;
  d.stencil_bits = p->metadata.stencil_bits; d.swap_interval = p->metadata.swap_interval;
  if (p->metadata.has_driver && driver != NULL) {
    driver->vendor = p->metadata.vendor; driver->renderer = p->metadata.renderer; driver->version = p->metadata.version; driver->shading_language = p->metadata.shading_language;
    d.driver = driver;
  }
  if (p->metadata.has_gl_config && config != NULL) { *config = p->metadata.gl_config; d.gl_config = config; }
  return d;
}

/* --- connection --- */
static qa_rwt_transport *connection(wimpl *p) {
  if (p == NULL || p->closed || p->transport == NULL) qa_range_error("Render worker is not open");
  return p->transport;
}
static void raise_error(const qa_rwt_error *error) { qa_range_error("%s", error != NULL ? error->message : "Render worker failed"); }
static void request_init(qa_worker_wire_request *request, qa_worker_op kind) { memset(request, 0, sizeof *request); request->kind = kind; }
static qa_rwt_value request_value(const qa_worker_wire_request *request) { qa_rwt_value v; v.bytes = request; v.size = sizeof *request; v.owned = NULL; return v; }
/* TS private call(): a callback's own operation may run at its suspended parent. */
static qa_rwt_value call(wimpl *p, const qa_worker_wire_request *request) {
  if (p != NULL && p->parked) qa_range_error("Render context is parked for an SDL window change");
  qa_rwt_transport *t = connection(p);
  qa_rwt_value out; memset(&out, 0, sizeof out); qa_rwt_error *error = NULL;
  if (!qa_rwt_callback_call(t, request_value(request), &out, &error)) raise_error(error);
  return out;
}
static void call_void(wimpl *p, const qa_worker_wire_request *request) { qa_rwt_value out = call(p, request); qa_rwt_value_release(&out); }
static void release_inflight(wimpl *p) { qa_wire_arena_destroy(p->inflight); p->inflight = NULL; }
void qa_worker_renderer_synchronize(qa_worker_renderer *r) {
  wimpl *p = W(r);
  qa_rwt_transport *t = connection(p);
  qa_rwt_value out; memset(&out, 0, sizeof out); qa_rwt_error *error = NULL;
  if (!qa_rwt_synchronize(t, &out, &error)) raise_error(error);
  qa_rwt_value_release(&out);
  release_inflight(p);
}
void qa_worker_renderer_pump(qa_worker_renderer *r) { qa_rwt_pump(connection(W(r))); }

/* --- host requests --- */
static void suffix_push(wimpl *p, qa_wire_image_operation op) {
  if (p->suffix_count == p->suffix_cap) {
    size_t cap = p->suffix_cap ? p->suffix_cap * 2 : 16;
    qa_wire_image_operation *grown = (qa_wire_image_operation *)realloc(p->suffix, cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Render worker allocation failed");
    p->suffix = grown; p->suffix_cap = cap;
  }
  p->suffix[p->suffix_count++] = op;
}
static void worker_apply_image(qa_worker_renderer *r, qa_image_resource_operation op);
static void apply_forward(void *ctx, qa_image_resource_operation op) { worker_apply_image((qa_worker_renderer *)ctx, op); }
typedef struct host_scope { wimpl *p; const unsigned char *bytes; size_t size; qa_rwt_value *out; } host_scope;
static void dispatch_host(void *ctx) {
  host_scope *s = (host_scope *)ctx; wimpl *p = s->p;
  if (s->size < sizeof(qa_worker_host_request)) qa_range_error("Invalid render worker response");
  qa_worker_host_request request; memcpy(&request, s->bytes, sizeof request);
  switch (request.kind) {
  case QA_WORKER_HOST_IMAGE_APPLIED: {
    qa_image_resource_operation op = qa_wire_encoder_acknowledge(&p->encoder, request.token);
    if (p->journal != NULL) qa_render_image_journal_record(p->journal, op);
    return;
  }
  case QA_WORKER_HOST_DYNAMIC_IMAGE: {
    qa_dynamic_image_source source = qa_wire_encoder_source(&p->encoder, desc_integer(request.token));
    qa_wire_image wire = qa_wire_encoder_image(&p->encoder, source.resolve(source.ctx, apply_forward, p->self));
    qa_wire_image *copy = (qa_wire_image *)malloc(sizeof *copy);
    if (copy == NULL) qa_range_error("Render worker allocation failed");
    *copy = wire;
    s->out->bytes = copy; s->out->size = sizeof *copy; s->out->owned = copy;
    return;
  }
  case QA_WORKER_HOST_PENDING_IMAGES:
    s->out->bytes = p->suffix; s->out->size = p->suffix_count * sizeof *p->suffix; s->out->owned = p->suffix;
    p->suffix = NULL; p->suffix_count = 0; p->suffix_cap = 0;
    return;
  case QA_WORKER_HOST_OPACITY_CALLBACK:
    for (size_t i = 0; i < p->callback_count; i++) if (p->callbacks[i].id == request.callback) { wcallback callback = p->callbacks[i]; callback.draw(callback.ctx); return; }
    qa_range_error("Unknown render worker opacity callback");
  case QA_WORKER_HOST_SWAP: {
    size_t captures_bytes = request.capture_count * sizeof(double);
    if (s->size != sizeof request + captures_bytes + request.image_length) qa_range_error("Invalid render worker response");
    double *captures = NULL;
    if (request.capture_count != 0) {
      captures = (double *)malloc(captures_bytes);
      if (captures == NULL) qa_range_error("Render worker allocation failed");
      memcpy(captures, s->bytes + sizeof request, captures_bytes);
      for (size_t i = 0; i < request.capture_count; i++) (void)desc_integer(captures[i]);
    }
    qa_image_level level; memset(&level, 0, sizeof level);
    if (request.has_image) {
      level.width = request.width; level.height = request.height;
      level.pixels.bytes = s->bytes + sizeof request + captures_bytes; level.pixels.length = request.image_length;
      level = qa_decode_wire_image_level(level);
    }
    p->swap(p->swap_ctx, request.has_image ? &level : NULL, captures, request.capture_count);
    free(captures);
    return;
  }
  }
  qa_range_error("Unknown render worker host request");
}
typedef struct host_run { wimpl *p; host_scope *scope; bool ok; char *message; size_t capacity; } host_run;
/* TS try/finally around dispatchHost, with the throw kept for the transport. */
static void host_execute(void *ctx) {
  host_run *run = (host_run *)ctx;
  run->p->callback_depth++;
  run->ok = qa_range_error_catch(dispatch_host, run->scope, run->message, run->capacity);
  run->p->callback_depth--;
}
static void collect_operations(void *ctx, qa_image_resource_operation *operations, size_t count) {
  wimpl *p = (wimpl *)ctx;
  for (size_t i = 0; i < count; i++) suffix_push(p, qa_wire_encoder_image_operation(&p->encoder, operations[i]));
  free(operations);
}
static bool host_request(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error) {
  wimpl *p = (wimpl *)ctx;
  host_scope scope = {p, (const unsigned char *)payload.bytes, payload.size, out};
  char message[1024]; message[0] = 0;
  host_run run = {p, &scope, false, message, sizeof message};
  if (p->callback_depth == 0 && p->images != NULL) qa_scene_image_registry_isolate(p->images, host_execute, &run, collect_operations, p);
  else host_execute(&run);
  if (!run.ok) { *error = qa_rwt_error_new(p->transport, "Error", message); return false; }
  return true;
}
static bool host_completed(void *ctx, qa_rwt_value payload, qa_rwt_error **error) {
  wimpl *p = (wimpl *)ctx;
  if (payload.size != sizeof(qa_worker_wire_description)) { *error = qa_rwt_error_new(p->transport, "TypeError", "Invalid render worker response"); return false; }
  set_metadata(p, payload.bytes, payload.size);
  return true;
}
static void host_failed(void *ctx, qa_rwt_error *error) {
  wimpl *p = (wimpl *)ctx;
  (void)snprintf(p->last_error, sizeof p->last_error, "%s", error->message);
  if (p->failed != NULL) p->failed(p->failed_ctx, error->message);
}

/* --- open --- */
static void park_forget(wimpl *p);
static void free_impl(wimpl *p, qa_worker_renderer *r) {
  park_forget(p);
  qa_wire_encoder_destroy(&p->encoder);
  free(p->suffix); free(p->callbacks);
  for (wprep *d = p->draws; d != NULL;) { wprep *next = d->next; qa_wire_arena_destroy(d->arena); free(d); d = next; }
  qa_wire_arena_destroy(p->inflight);
  free(p); r->impl = NULL;
}
typedef struct restore_scope { qa_worker_window window; } restore_scope;
static void restore_body(void *ctx) { restore_scope *s = (restore_scope *)ctx; s->window.restore_render_context(s->window.ctx); }
bool qa_worker_renderer_open(qa_worker_renderer *r, qa_worker_window window, qa_renderer_resource_owner owner, double gamma,
    qa_scene_image_registry *images, qa_render_image_journal *journal, qa_worker_swap_fn swap, void *swap_ctx,
    qa_worker_failed_fn failed, void *failed_ctx, const qa_render_worker_platform *platform) {
  memset(r, 0, sizeof *r);
  wimpl *p = (wimpl *)calloc(1, sizeof *p);
  if (p == NULL) return false;
  r->impl = p; r->owner = owner; p->self = r;
  if (!qa_wire_encoder_init(&p->encoder, owner)) { free(p); r->impl = NULL; return false; }
  p->window = window; p->images = images; p->journal = journal; p->swap = swap; p->swap_ctx = swap_ctx; p->failed = failed; p->failed_ctx = failed_ctx;
  p->next_draw = 1; p->next_callback = 1;
  qa_render_worker_init init; memset(&init, 0, sizeof init);
  init.gamma = gamma;
  if (!window.is_gl) { init.backend = "cpu"; window.drawable_size(window.ctx, &init.width, &init.height); }
  else { init.backend = "gl"; init.transfer = window.detach_render_context(window.ctx); }
  qa_rwt_host host; memset(&host, 0, sizeof host);
  host.ctx = p; host.request = host_request; host.completed = host_completed; host.failed = host_failed;
  qa_rwt_value value; value.bytes = &init; value.size = sizeof init; value.owned = NULL;
  qa_rwt_error *error = NULL;
  bool ok = qa_rwt_open(&p->transport, qa_render_worker_entry, (void *)platform, value, host, DEFAULT_TIMEOUT, &error);
  if (ok) {
    qa_rwt_value description = qa_rwt_description(p->transport);
    set_metadata(p, description.bytes, description.size);
    if (window.is_gl) { p->next_context = contexts; contexts = p; p->registered = true; }
    return true;
  }
  (void)snprintf(p->last_error, sizeof p->last_error, "%s", error != NULL ? error->message : "Render worker failed to start");
  qa_rwt_destroy(p->transport); p->transport = NULL;
  if (window.is_gl) {
    restore_scope scope = {window};
    char cleanup[1024];
    if (!qa_range_error_catch(restore_body, &scope, cleanup, sizeof cleanup)) {
      /* TS AggregateError: the worker kept the context. */
      char startup[1024]; (void)snprintf(startup, sizeof startup, "%s", p->last_error);
      free_impl(p, r);
      qa_range_error("Render worker startup failed while retaining its context: %.400s; %.400s", startup, cleanup);
    }
  }
  free_impl(p, r);
  return false;
}
void qa_worker_renderer_bind_images(qa_worker_renderer *r, qa_scene_image_registry *images, qa_render_image_journal *journal) { wimpl *p = W(r); p->images = images; p->journal = journal; }

/* --- withParkedContexts --- */
typedef struct park_scope { void (*operation)(void *); void *ctx; wimpl **parked; size_t *count; struct park_scope *previous; } park_scope;
/* Scopes in progress: a renderer destroyed inside one leaves it (TS keeps the closed
 * object alive and skips it). */
static park_scope *park_scopes;
static void park_forget(wimpl *p) {
  for (park_scope *s = park_scopes; s != NULL; s = s->previous)
    for (size_t i = 0; i < *s->count; i++) if (s->parked[i] == p) s->parked[i] = NULL;
}
static void park_body(void *ctx) {
  park_scope *s = (park_scope *)ctx;
  for (wimpl *p = contexts; p != NULL; p = p->next_context) {
    if (p->parked) continue;
    if (p->callback_depth != 0) qa_range_error("Cannot change SDL windows during a render callback");
    qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_PARK);
    qa_rwt_value out; memset(&out, 0, sizeof out); qa_rwt_error *error = NULL;
    if (!qa_rwt_call(connection(p), request_value(&request), &out, &error)) raise_error(error);
    qa_rwt_value_release(&out);
    p->parked = true; s->parked[(*s->count)++] = p;
  }
  s->operation(s->ctx);
}
void qa_worker_renderer_with_parked_contexts(void (*operation)(void *ctx), void *ctx) {
  size_t total = 0, count = 0;
  for (wimpl *p = contexts; p != NULL; p = p->next_context) total++;
  wimpl **parked = (wimpl **)calloc(total ? total : 1, sizeof *parked);
  if (parked == NULL) qa_range_error("Render worker allocation failed");
  park_scope scope = {operation, ctx, parked, &count, park_scopes};
  park_scopes = &scope;
  char message[1024]; message[0] = 0;
  bool ok = qa_range_error_catch(park_body, &scope, message, sizeof message);
  park_scopes = scope.previous;
  char resume_failure[1024]; resume_failure[0] = 0;
  for (size_t i = count; i-- > 0;) {
    wimpl *p = parked[i];
    if (p == NULL || p->closed) continue;
    qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_RESUME);
    qa_rwt_value out; memset(&out, 0, sizeof out); qa_rwt_error *error = NULL;
    if (qa_rwt_call(p->transport, request_value(&request), &out, &error)) { qa_rwt_value_release(&out); p->parked = false; }
    else if (resume_failure[0] == 0) (void)snprintf(resume_failure, sizeof resume_failure, "%s", error != NULL ? error->message : "resume failed");
  }
  free(parked);
  if (resume_failure[0] != 0) {
    if (!ok) qa_range_error("SDL window change could not resume its render workers: %.400s; %.400s", message, resume_failure);
    qa_range_error("SDL window change could not resume its render workers: %.800s", resume_failure);
  }
  if (!ok) qa_range_error("%s", message);
}

/* --- execute --- */
/* TS wireBytes over the encoded command, as the snapshot bytes it will occupy. */
static size_t batch_bytes(const qa_draw_batch *b) {
  size_t bytes = 16 + b->indices.count * sizeof(double) + b->vertex_count * sizeof(qa_render_vertex) + b->pair_vertex_count * sizeof(qa_multitexture_vertex);
  bytes += b->lighting.world_position_count * sizeof(qa_vec3) + b->lighting.normal_count * sizeof(qa_vec3);
  bytes += b->lighting.light_count * sizeof(qa_q2_fragment_light) + b->lighting.model_shadow_light_count * sizeof(qa_q2_model_shadow_light);
  return bytes;
}
static size_t operation_bytes(const qa_render_operation *op) {
  size_t bytes = 16;
  switch (op->kind) {
  case QA_RENDER_OP_DRAW: case QA_RENDER_OP_OBJECT_OPACITY:
    for (size_t i = 0; i < op->batch_count; i++) bytes += batch_bytes(&op->batches[i]);
    break;
  case QA_RENDER_OP_DEPTH_ATLAS:
    for (size_t i = 0; i < op->pass_count; i++) for (size_t j = 0; j < op->passes[i].draw_count; j++)
      bytes += op->passes[i].draws[j].position_count * sizeof(double) + op->passes[i].draws[j].indices.count * sizeof(double);
    break;
  case QA_RENDER_OP_SKY_SIDE:
    for (size_t i = 0; i < op->strip_count; i++) bytes += op->strips[i].vertex_count * sizeof(qa_vertex_position_tex_coord);
    break;
  case QA_RENDER_OP_SHADOW_VOLUME: case QA_RENDER_OP_SHADOW_FINISH:
    bytes += op->position_count * sizeof(qa_vec4) + op->indices.count * sizeof(double);
    break;
  case QA_RENDER_OP_Q2_FOG: case QA_RENDER_OP_DEPTH_RANGE: case QA_RENDER_OP_CULL: case QA_RENDER_OP_POLYGON_OFFSET: case QA_RENDER_OP_DISABLE_PORTAL_CLIP: break;
  }
  return bytes;
}
static size_t image_operation_bytes(const qa_image_resource_operation *op) {
  size_t bytes = 16;
  if (op->kind == QA_IMAGE_OP_CREATE) {
    for (size_t i = 0; i < op->content.level_count; i++) bytes += op->content.levels[i].pixels.length;
    for (size_t i = 0; i < op->content.depth_level_count; i++) bytes += op->content.depth_levels[i].pixel_count * sizeof(float);
    bytes += op->content.palette.colors.length;
  } else if (op->kind == QA_IMAGE_OP_UPDATE) {
    if (op->update_level != NULL) bytes += op->update_level->pixels.length;
    if (op->update_depth_level != NULL) bytes += op->update_depth_level->pixel_count * sizeof(float);
  }
  return bytes;
}
static size_t command_bytes(const qa_render_command *c) {
  size_t bytes = 16;
  if (c->kind == QA_RENDER_COMMAND_VIEW) {
    for (size_t i = 0; i < c->view.before_view_count; i++) bytes += operation_bytes(&c->view.before_view[i]);
    for (size_t i = 0; i < c->view.operation_count; i++) bytes += operation_bytes(&c->view.operations[i]);
  } else if (c->kind == QA_RENDER_COMMAND_IMAGE_RESOURCE) bytes += image_operation_bytes(&c->operation);
  return bytes;
}
typedef struct packet { wimpl *p; qa_wire_arena *arena; qa_wire_command *pending; size_t count, cap, bytes; } packet;
static void packet_issue(packet *pk, bool final) {
  wimpl *p = pk->p;
  qa_rwt_transport *t = connection(p);
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_EXECUTE);
  /* The list must live as long as the commands it names. */
  request.commands = (qa_wire_command *)qa_wire_arena_copy(pk->arena, pk->pending, pk->count * sizeof *pk->pending);
  request.command_count = pk->count; request.final = final;
  qa_rwt_error *error = NULL;
  if (!qa_rwt_issue(t, request_value(&request), &error)) {
    /* Both failure paths in issue precede the send, so the worker never saw it. */
    qa_wire_arena_destroy(pk->arena); free(pk->pending);
    raise_error(error);
  }
  /* issue's own barrier retired the previous packet. */
  release_inflight(p);
  p->inflight = pk->arena;
  pk->arena = qa_wire_arena_create(); pk->count = 0; pk->bytes = 0;
}
void qa_worker_renderer_execute(qa_worker_renderer *r, const qa_render_command *commands, size_t command_count,
    const double *captures, size_t capture_count, const qa_image_resource_operation *suffix, size_t suffix_count) {
  wimpl *p = W(r);
  if (p != NULL && p->callback_depth != 0) qa_range_error("Cannot submit a frame during a render callback");
  qa_worker_renderer_synchronize(r);
  if (p->draws == NULL) qa_wire_encoder_clear_sources(&p->encoder);
  for (size_t i = 0; i < suffix_count; i++) suffix_push(p, qa_wire_encoder_image_operation(&p->encoder, suffix[i]));
  const double *armed = captures; size_t armed_count = capture_count;
  packet pk; memset(&pk, 0, sizeof pk); pk.p = p; pk.arena = qa_wire_arena_create();
  pk.cap = command_count ? command_count : 1;
  pk.pending = (qa_wire_command *)malloc(pk.cap * sizeof *pk.pending);
  if (pk.pending == NULL) qa_range_error("Render worker allocation failed");
  for (size_t i = 0; i < command_count; i++) {
    size_t size = command_bytes(&commands[i]);
    if (pk.count != 0 && pk.bytes + size > PACKET_LIMIT) packet_issue(&pk, false);
    bool is_swap = commands[i].kind == QA_RENDER_COMMAND_SWAP_BUFFERS;
    pk.pending[pk.count++] = qa_wire_encoder_command(&p->encoder, pk.arena, commands[i], is_swap ? armed : NULL, is_swap ? armed_count : 0);
    if (is_swap) { armed = NULL; armed_count = 0; }
    pk.bytes += size;
    /* An indivisible oversized command executes alone before recording more data. */
    if (pk.bytes > PACKET_LIMIT) { packet_issue(&pk, false); qa_worker_renderer_synchronize(r); }
  }
  packet_issue(&pk, true);
  qa_wire_arena_destroy(pk.arena); free(pk.pending);
}

/* --- synchronous calls --- */
void qa_worker_renderer_resize(qa_worker_renderer *r, double width, double height) {
  wimpl *p = W(r);
  if (p == NULL || !p->has_metadata) qa_range_error("Render worker is not initialized");
  if (width == p->metadata.width && height == p->metadata.height) return;
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_RESIZE); request.width = width; request.height = height;
  qa_rwt_value out = call(p, &request);
  set_metadata(p, out.bytes, out.size);
  qa_rwt_value_release(&out);
}
void qa_worker_renderer_set_output_gamma(qa_worker_renderer *r, double gamma) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_GAMMA); request.gamma = gamma; call_void(W(r), &request);
}
void qa_worker_renderer_set_swap_interval(qa_worker_renderer *r, double interval) {
  wimpl *p = W(r);
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_SWAP_INTERVAL); request.interval = interval; call_void(p, &request);
  request_init(&request, QA_WORKER_OP_DESCRIPTION);
  qa_rwt_value out = call(p, &request);
  set_metadata(p, out.bytes, out.size);
  qa_rwt_value_release(&out);
}
void qa_worker_renderer_read_pixels(qa_worker_renderer *r, qa_image_level *level) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_PIXELS);
  qa_rwt_value out = call(W(r), &request);
  if (out.size < sizeof(qa_worker_wire_pixels)) { qa_rwt_value_release(&out); qa_range_error("Invalid renderer RGBA readback"); }
  qa_worker_wire_pixels header; memcpy(&header, out.bytes, sizeof header);
  if (out.size != sizeof header + header.length) { qa_rwt_value_release(&out); qa_range_error("Invalid renderer RGBA readback"); }
  uint8_t *pixels = (uint8_t *)malloc(header.length ? header.length : 1);
  if (pixels == NULL) qa_range_error("Render worker allocation failed");
  memcpy(pixels, (const unsigned char *)out.bytes + sizeof header, header.length);
  qa_rwt_value_release(&out);
  memset(level, 0, sizeof *level);
  level->width = header.width; level->height = header.height; level->pixels.bytes = pixels; level->pixels.length = header.length;
  *level = qa_decode_wire_image_level(*level);
}

/* --- RendererBackend --- */
static void worker_apply_image(qa_worker_renderer *r, qa_image_resource_operation op) {
  wimpl *p = W(r);
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_IMAGE);
  request.operation = qa_wire_encoder_image_operation(&p->encoder, op);
  call_void(p, &request);
}
static void be_image(void *ctx, qa_image_resource_operation op) { worker_apply_image((qa_worker_renderer *)ctx, op); }
static void be_buffer(void *ctx, qa_renderer_draw_buffer buffer, bool clear) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_DRAW_BUFFER); request.buffer = buffer; request.flag = clear;
  call_void(W((qa_worker_renderer *)ctx), &request);
}
static void be_overdraw(void *ctx, bool enabled) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_OVERDRAW); request.flag = enabled;
  call_void(W((qa_worker_renderer *)ctx), &request);
}
static void be_stencil(void *ctx, uint8_t *destination, size_t length) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_STENCIL); request.length = (double)length;
  qa_rwt_value out = call(W((qa_worker_renderer *)ctx), &request);
  if (out.size != length) { qa_rwt_value_release(&out); qa_range_error("Invalid worker stencil readback"); }
  if (length != 0) memcpy(destination, out.bytes, length);
  qa_rwt_value_release(&out);
}
static double be_depth(void *ctx, double x, double y) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_DEPTH); request.x = x; request.y = y;
  qa_rwt_value out = call(W((qa_worker_renderer *)ctx), &request);
  if (out.size != sizeof(double)) { qa_rwt_value_release(&out); qa_range_error("Invalid render worker number"); }
  double depth; memcpy(&depth, out.bytes, sizeof depth);
  qa_rwt_value_release(&out);
  return depth;
}
/* The barrier returns before the caller's view storage can change, so it is borrowed. */
static void be_view(void *ctx, qa_render_view_state view) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_BEGIN_VIEW); request.view.state = view;
  call_void(W((qa_worker_renderer *)ctx), &request);
}
typedef struct opacity_scope { wimpl *p; const qa_worker_wire_request *request; } opacity_scope;
static void opacity_body(void *ctx) { opacity_scope *s = (opacity_scope *)ctx; call_void(s->p, s->request); }
static void be_opacity(void *ctx, double opacity, void (*draw)(void *), void *draw_ctx) {
  wimpl *p = W((qa_worker_renderer *)ctx);
  if (p == NULL) qa_range_error("Render worker is not open");
  double id = p->next_callback++;
  if (p->callback_count == p->callback_cap) {
    size_t cap = p->callback_cap ? p->callback_cap * 2 : 4;
    wcallback *grown = (wcallback *)realloc(p->callbacks, cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Render worker allocation failed");
    p->callbacks = grown; p->callback_cap = cap;
  }
  p->callbacks[p->callback_count].id = id; p->callbacks[p->callback_count].draw = draw; p->callbacks[p->callback_count].ctx = draw_ctx; p->callback_count++;
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_OPACITY); request.opacity = opacity; request.callback = id;
  opacity_scope scope = {p, &request};
  char message[1024];
  bool ok = qa_range_error_catch(opacity_body, &scope, message, sizeof message);
  for (size_t i = 0; i < p->callback_count; i++) if (p->callbacks[i].id == id) { /* TS finally */
    memmove(&p->callbacks[i], &p->callbacks[i + 1], (p->callback_count - i - 1) * sizeof *p->callbacks); p->callback_count--; break;
  }
  if (!ok) qa_range_error("%s", message);
}
static void be_immediate(void *ctx, qa_render_operation op) {
  wimpl *p = W((qa_worker_renderer *)ctx);
  if (p == NULL) qa_range_error("Render worker is not open");
  qa_wire_arena *arena = qa_wire_arena_create();
  qa_wire_operation wire = qa_wire_encoder_operation(&p->encoder, arena, op);
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_IMMEDIATE); request.immediate = &wire;
  opacity_scope scope = {p, &request};
  char message[1024];
  bool ok = qa_range_error_catch(opacity_body, &scope, message, sizeof message);
  qa_wire_arena_destroy(arena);
  if (!ok) qa_range_error("%s", message);
}
static void wp_call(wprep *pr, qa_worker_phase phase, double unit, qa_wire_binding *texture) {
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_PREPARED);
  request.id = pr->id; request.phase = phase; request.unit = unit;
  if (texture != NULL) request.texture = *texture;
  call_void(W(pr->r), &request);
}
static void wp_begin(void *ctx) { wp_call((wprep *)ctx, QA_WORKER_PHASE_BEGIN, 0, NULL); }
static void wp_texture(void *ctx, double unit, qa_texture_binding binding) {
  wprep *pr = (wprep *)ctx;
  wimpl *p = W(pr->r);
  if (p == NULL) qa_range_error("Render worker is not open");
  qa_wire_binding wire = qa_wire_encoder_binding(&p->encoder, binding);
  wp_call(pr, QA_WORKER_PHASE_TEXTURE, unit, &wire);
}
static void wp_draw(void *ctx) { wp_call((wprep *)ctx, QA_WORKER_PHASE_DRAW, 0, NULL); }
static void wp_cleanup(void *ctx) {
  wprep *pr = (wprep *)ctx; wimpl *p = W(pr->r);
  wp_call(pr, QA_WORKER_PHASE_CLEANUP, 0, NULL);
  if (pr->prev != NULL) pr->prev->next = pr->next; else p->draws = pr->next;
  if (pr->next != NULL) pr->next->prev = pr->prev;
  qa_wire_arena_destroy(pr->arena); free(pr);
}
static qa_prepared_backend_draw be_prepare(void *ctx, qa_draw_batch batch) {
  qa_worker_renderer *r = (qa_worker_renderer *)ctx; wimpl *p = W(r);
  if (p == NULL) qa_range_error("Render worker is not open");
  wprep *pr = (wprep *)calloc(1, sizeof *pr);
  if (pr == NULL) qa_range_error("Render worker allocation failed");
  pr->r = r; pr->id = p->next_draw++; pr->arena = qa_wire_arena_create();
  /* The prepared draw reads this batch until cleanup. */
  qa_wire_batch encoded = qa_wire_encoder_batch(&p->encoder, pr->arena, batch);
  qa_wire_batch *wire = (qa_wire_batch *)qa_wire_arena_copy(pr->arena, &encoded, sizeof encoded);
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_PREPARE); request.id = pr->id; request.batch = wire;
  opacity_scope scope = {p, &request};
  char message[1024];
  if (!qa_range_error_catch(opacity_body, &scope, message, sizeof message)) {
    qa_wire_arena_destroy(pr->arena); free(pr);
    qa_range_error("%s", message);
  }
  pr->next = p->draws; if (p->draws != NULL) p->draws->prev = pr; p->draws = pr;
  qa_prepared_backend_draw o; memset(&o, 0, sizeof o);
  o.ctx = pr; o.begin = wp_begin; o.apply_texture = wp_texture; o.draw = wp_draw; o.cleanup = wp_cleanup;
  return o;
}
static void be_clear(void *ctx) { qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_CLEAR_COLOR); call_void(W((qa_worker_renderer *)ctx), &request); }
static void be_show(void *ctx, qa_renderer_image image, qa_rect rect, bool proportional) {
  wimpl *p = W((qa_worker_renderer *)ctx);
  if (p == NULL) qa_range_error("Render worker is not open");
  qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_SHOW_IMAGE);
  request.image = qa_wire_encoder_image(&p->encoder, image); request.rect = rect; request.flag = proportional;
  call_void(p, &request);
}
static void be_finish(void *ctx) { qa_worker_wire_request request; request_init(&request, QA_WORKER_OP_FINISH); call_void(W((qa_worker_renderer *)ctx), &request); }
static void be_close(void *ctx) {
  qa_worker_renderer *r = (qa_worker_renderer *)ctx;
  if (!qa_worker_renderer_close(r)) qa_range_error("%s", qa_worker_renderer_last_error(r));
}
qa_renderer_backend qa_worker_renderer_backend(qa_worker_renderer *r) {
  qa_renderer_backend b; memset(&b, 0, sizeof b);
  b.ctx = r; b.owner = r->owner; b.width = r->width; b.height = r->height; b.stencil_bits = r->stencil_bits;
  b.apply_image_resource = be_image; b.select_draw_buffer = be_buffer; b.set_overdraw_measurement = be_overdraw; b.read_stencil_overdraw = be_stencil;
  b.read_depth_pixel = be_depth; b.begin_view = be_view; b.with_object_opacity = be_opacity; b.draw_immediate = be_immediate; b.prepare_geometry = be_prepare;
  b.clear_color_buffer = be_clear; b.draw_show_image = be_show; b.finish = be_finish; b.close = be_close;
  return b;
}

/* --- close --- */
bool qa_worker_renderer_close(qa_worker_renderer *r) {
  wimpl *p = W(r);
  if (p == NULL || p->closed) return true;
  qa_rwt_transport *t = connection(p);
  qa_rwt_error *error = NULL;
  bool ok = qa_rwt_close(t, &error);
  if (!ok) (void)snprintf(p->last_error, sizeof p->last_error, "%s", error != NULL ? error->message : "Render worker close failed");
  if (qa_rwt_retired(t)) {
    p->closed = true;
    qa_wire_encoder_clear(&p->encoder);
    p->callback_count = 0;
    contexts_remove(p);
    release_inflight(p);
    if (p->window.is_gl) p->window.restore_render_context(p->window.ctx);
  }
  return ok;
}
bool qa_worker_renderer_closed(const qa_worker_renderer *r) { wimpl *p = W(r); return p == NULL || p->closed; }
const char *qa_worker_renderer_last_error(const qa_worker_renderer *r) { wimpl *p = W(r); return p != NULL ? p->last_error : ""; }
void qa_worker_renderer_destroy(qa_worker_renderer *r) {
  wimpl *p = W(r);
  if (p == NULL) return;
  if (!p->closed) (void)qa_worker_renderer_close(r);
  bool retired = p->transport != NULL && qa_rwt_retired(p->transport);
  contexts_remove(p);
  qa_rwt_destroy(p->transport); p->transport = NULL;
  /* An unretired worker keeps reading its packets, as TS leaves the context borrowed. */
  if (!retired) { p->inflight = NULL; p->draws = NULL; }
  free_impl(p, r);
}

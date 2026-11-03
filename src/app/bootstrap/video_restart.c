/* Port of src/app/bootstrap/video-restart.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/video_restart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/input/client_commands.h"
struct qa_app_video_restart {
  qa_app_native_renderer *renderer;
  qa_app_video_restart_host host;
  bool has_request;
  char kind[8];
  bool render_worker;
  bool has_source; qa_command_context source;
  bool running, closed;
  qa_command_buffer *commands;
  double completed;
};
qa_app_video_restart *qa_app_video_restart_create(qa_app_native_renderer *renderer, qa_app_video_restart_host host) {
  qa_app_video_restart *v = calloc(1, sizeof *v);
  if (!v) return NULL;
  v->renderer = renderer; v->host = host;
  return v;
}
static void drop_source(qa_app_video_restart *v) {
  if (v->has_source) qa_command_context_release(&v->source);
  v->has_source = false;
}
void qa_app_video_restart_destroy(qa_app_video_restart *v) {
  if (!v) return;
  qa_app_video_restart_close(v);
  drop_source(v);
  free(v);
}
bool qa_app_video_restart_pending(const qa_app_video_restart *v) { return v->has_request || v->running; }
double qa_app_video_restart_generation(const qa_app_video_restart *v) { return v->completed; }
void qa_app_video_restart_request(qa_app_video_restart *v, const char *kind, const qa_command_context *source) {
  if (v->closed) qa_raise("Error", 0, 0, "Video restart owner has retired");
  if (kind == NULL) {
    qa_sdl_window *window = qa_app_native_renderer_window(v->renderer);
    kind = qa_sdl_window_backend(window) == QA_SDL_BACKEND_CPU ? "cpu" : "gl";
  }
  bool worker = false;
  bool render_worker = v->host.render_worker != NULL && v->host.render_worker(v->host.ctx, &worker)
      ? worker : qa_app_native_renderer_render_worker(v->renderer);
  /* The request keeps its command context: a copy that owns its strings. */
  qa_command_context copy;
  if (source != NULL) copy = qa_command_context_copy(source);
  drop_source(v);
  snprintf(v->kind, sizeof v->kind, "%s", kind);
  v->render_worker = render_worker;
  v->has_source = source != NULL;
  if (source != NULL) v->source = copy;
  v->has_request = true;
}
static void vid_restart_handler(void *ctx, qa_command_invocation *command) {
  qa_app_video_restart *v = ctx;
  const qa_command_origin *origin = &command->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    v->host.print(v->host.ctx, "vid_restart is a local client command.\n", &command->source);
    return;
  }
  /* Landed argv excludes the command name (TS command.args). */
  const char *kind = command->argc > 0 ? command->argv[0] : NULL;
  if (command->argc > 1 || (kind != NULL && strcmp(kind, "cpu") != 0 && strcmp(kind, "gl") != 0)) {
    v->host.print(v->host.ctx, "vid_restart [cpu|gl]\n", &command->source);
    return;
  }
  qa_app_video_restart_request(v, kind, &command->source);
}
void qa_app_video_restart_register(qa_app_video_restart *v, qa_command_buffer *commands) {
  if (v->commands != NULL) qa_raise("Error", 0, 0, "Video restart command already registered");
  static const char *const examples[] = {"vid_restart", "vid_restart cpu", "vid_restart gl"};
  qa_command_documentation doc = {"Restart the client renderer while retaining the current game.", "vid_restart [cpu|gl]", examples, 3, NULL, 0};
  if (!qa_command_buffer_register_engine(commands, "vid_restart", vid_restart_handler, v, &doc))
    qa_raise("Error", 0, 0, "Video restart command belongs to another owner");
  v->commands = commands;
}

/* ---- drain ---- */
typedef struct drain_state {
  qa_app_video_restart *v;
  const char *kind;
  bool render_worker;
  const qa_command_context *source;
  qa_app_prepared_renderer_restart *surface;
  bool has_presentation;
  qa_app_prepared_video_presentation presentation;
  bool retire; /* TS retire !== null */
  qa_sdl_window *previous_window;
  bool input_published;
} drain_state;
/* The TS try block. */
static void drain_body(void *ctx) {
  drain_state *s = ctx;
  qa_app_video_restart *v = s->v;
  qa_app_capture *capture = v->host.capture != NULL ? v->host.capture(v->host.ctx) : NULL;
  if (capture != NULL) qa_app_capture_drain(capture);
  s->surface = qa_app_native_renderer_prepare_restart(v->renderer, s->kind, true, s->render_worker);
  if (s->surface == NULL) qa_range_error("Out of memory");
  s->has_presentation = v->host.prepare(v->host.ctx, &s->presentation);
  if (v->closed) qa_raise("Error", 0, 0, "Video restart was cancelled during preparation");
  if (s->has_presentation) s->presentation.validate_publication(s->presentation.ctx);
  v->host.publish_window(v->host.ctx, qa_app_prepared_renderer_restart_window(s->surface));
  s->input_published = true;
  qa_app_prepared_renderer_restart_publish(s->surface);
  s->retire = true;
  if (s->has_presentation) s->presentation.restart(s->presentation.ctx);
  if (v->closed) qa_raise("Error", 0, 0, "Video restart was cancelled after publication");
  v->host.published(v->host.ctx, s->kind);
  qa_app_prepared_renderer_restart_retire(s->surface);
  s->retire = false;
  char text[64];
  snprintf(text, sizeof text, "Renderer restarted (%s).\n", s->kind);
  v->host.print(v->host.ctx, text, s->source);
}
static void publish_previous_window(void *ctx) {
  drain_state *s = ctx;
  s->v->host.publish_window(s->v->host.ctx, s->previous_window);
}
static void discard_surface(void *ctx) {
  drain_state *s = ctx;
  if (s->surface != NULL) qa_app_prepared_renderer_restart_discard(s->surface);
}
static void retire_surface(void *ctx) {
  drain_state *s = ctx;
  if (s->retire) qa_app_prepared_renderer_restart_retire(s->surface);
}
typedef struct failure_list { qa_caught_error first; size_t count; } failure_list;
static void cleanup_step(failure_list *failures, void (*step)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (!qa_try(step, ctx, &caught)) failures->count++;
}
typedef struct failed_call { qa_app_video_restart *v; const qa_caught_error *error; } failed_call;
static void call_failed(void *ctx) {
  failed_call *call = ctx;
  call->v->host.failed(call->v->host.ctx, call->error);
  qa_rethrow(call->error); /* failed(): Promise<never> */
}
typedef struct print_call { qa_app_video_restart *v; const char *message; const qa_command_context *source; } print_call;
static void print_rejected(void *ctx) {
  print_call *call = ctx;
  char text[QA_CAUGHT_MESSAGE_CAPACITY + 64];
  snprintf(text, sizeof text, "Video restart rejected: %s\n", call->message);
  call->v->host.print(call->v->host.ctx, text, call->source);
}
bool qa_app_video_restart_drain(qa_app_video_restart *v) {
  if (v->running) qa_raise("Error", 0, 0, "Video restart is already preparing");
  qa_app_capture *capture = v->host.capture != NULL ? v->host.capture(v->host.ctx) : NULL;
  if (v->closed || !v->has_request || (capture != NULL && qa_app_capture_pending_readback(capture))) return false;
  char kind[8];
  snprintf(kind, sizeof kind, "%s", v->kind);
  bool has_source = v->has_source;
  qa_command_context source;
  if (has_source) source = v->source;
  v->has_source = false; /* the request's context moves to this drain */
  v->has_request = false;
  v->running = true;
  drain_state s;
  memset(&s, 0, sizeof s);
  s.v = v;
  s.kind = kind;
  s.render_worker = v->render_worker;
  s.source = has_source ? &source : NULL;
  s.previous_window = qa_app_native_renderer_window(v->renderer);
  qa_caught_error error;
  bool restarted = qa_try(drain_body, &s, &error);
  bool raise = false;
  qa_caught_error raised;
  if (!restarted) {
    failure_list failures = {error, 1};
    bool published = s.surface != NULL && qa_app_prepared_renderer_restart_published(s.surface);
    if (!published) {
      if (s.input_published) cleanup_step(&failures, publish_previous_window, &s);
      cleanup_step(&failures, discard_surface, &s);
    } else cleanup_step(&failures, retire_surface, &s);
    if (published || failures.count > 1 || strcmp(error.name, "AggregateError") == 0) {
      /* return this.host.failed(new AggregateError(failures, "Video restart publication failed")) */
      qa_caught_error aggregate;
      memset(&aggregate, 0, sizeof aggregate);
      aggregate.name = "AggregateError";
      aggregate.detail = (long long)failures.count;
      snprintf(aggregate.message, sizeof aggregate.message, "Video restart publication failed");
      failed_call call = {v, &aggregate};
      qa_try(call_failed, &call, &raised);
      raise = true;
    } else {
      print_call call = {v, error.message, s.source};
      if (!qa_try(print_rejected, &call, &raised)) raise = true;
    }
  }
  /* finally */
  if (s.has_presentation && s.presentation.release != NULL) s.presentation.release(s.presentation.ctx);
  v->completed++;
  v->running = false;
  v->host.settled(v->host.ctx);
  if (has_source) qa_command_context_release(&source);
  if (raise) qa_rethrow(&raised);
  return restarted;
}
void qa_app_video_restart_close(qa_app_video_restart *v) {
  if (v->closed) return;
  v->closed = true;
  v->has_request = false;
  drop_source(v);
  if (v->commands) {
    qa_command_buffer_unregister(v->commands, "vid_restart", vid_restart_handler, v, true);
    v->commands = NULL;
  }
}

/* ---- prepareVideoGuests ---- */
typedef struct guest_entry {
  qa_app_video_guest_seat seat;
  qa_app_q3_video_reopen *reopen;
} guest_entry;
typedef struct guest_prep {
  qa_app_application_input *input;
  qa_app_native_renderer *renderer;
  guest_entry *guests; size_t guest_count;
  void (*assert_current)(void *ctx); void *ctx;
} guest_prep;
static void guest_release(void *ctx) {
  guest_prep *g = ctx;
  for (size_t i = 0; i < g->guest_count; i++) qa_app_q3_video_reopen_destroy(g->guests[i].reopen);
  free(g->guests);
  free(g);
}
static void guest_validate(void *ctx) {
  guest_prep *g = ctx;
  g->assert_current(g->ctx);
}
/* QvmVideoReopenOptions.viewport: () => seat.viewport(renderer.window); the
 * reopened client owns this closure. */
typedef struct viewport_closure {
  qa_rect (*viewport)(void *ctx, qa_sdl_window *window);
  void *ctx;
  qa_app_native_renderer *renderer;
} viewport_closure;
static qa_rect guest_viewport(void *ctx) {
  viewport_closure *closure = ctx;
  return closure->viewport(closure->ctx, qa_app_native_renderer_window(closure->renderer));
}
static void client_shutdown(void *ctx) { qa_app_application_q3_client_shutdown(ctx); }
static void client_close(void *ctx) { qa_app_application_q3_client_close(ctx); }
typedef struct reopen_call {
  guest_prep *g;
  guest_entry *entry;
  qa_client_command_owner *registration;
  qa_app_application_q3_client *replacement;
} reopen_call;
/* replacement = await reopen({ renderer, viewport, commandRegistration }); seat.publish(replacement); */
static void reopen_body(void *ctx) {
  reopen_call *call = ctx;
  qa_app_native_renderer *renderer = call->g->renderer;
  qa_app_qvm_video_reopen_options overrides;
  memset(&overrides, 0, sizeof overrides);
  overrides.renderer.backend_stencil_bits = qa_app_native_renderer_backend(renderer).stencil_bits;
  overrides.renderer.driver = qa_app_native_renderer_driver(renderer);
  overrides.renderer.gl_config = qa_app_native_renderer_gl_config(renderer);
  viewport_closure *closure = calloc(1, sizeof *closure);
  if (closure == NULL) qa_range_error("Out of memory");
  *closure = (viewport_closure){call->entry->seat.viewport, call->entry->seat.ctx, renderer};
  overrides.ctx = closure;
  overrides.viewport = guest_viewport;
  overrides.release = free; /* the reopened client frees the closure */
  overrides.command_bindings = qa_app_application_input_client_commands(call->g->input);
  overrides.command_registration = call->registration;
  call->replacement = qa_app_q3_video_reopen_open(call->entry->reopen, &overrides);
  call->entry->seat.publish(call->entry->seat.ctx, call->replacement);
}
static void replacement_close(void *ctx) {
  reopen_call *call = ctx;
  if (call->replacement != NULL) qa_app_application_q3_client_close(call->replacement);
}
static void registration_close(void *ctx) {
  reopen_call *call = ctx;
  qa_client_command_owner_close(qa_app_application_input_client_commands(call->g->input), call->registration);
}
static void guest_restart(void *ctx) {
  guest_prep *g = ctx;
  size_t errors = 0;
  qa_caught_error first;
  memset(&first, 0, sizeof first);
  for (size_t i = 0; i < g->guest_count; i++) {
    qa_caught_error caught;
    if (!qa_try(client_shutdown, g->guests[i].seat.client, &caught) && errors++ == 0) first = caught;
    if (!qa_try(client_close, g->guests[i].seat.client, &caught) && errors++ == 0) first = caught;
  }
  if (errors != 0)
    qa_raise("AggregateError", 0, (long long)errors, "Previous guest video shutdown failed (%zu errors; first %s: %s)", errors,
             first.name, first.message);
  for (size_t i = 0; i < g->guest_count; i++) {
    guest_entry *entry = &g->guests[i];
    reopen_call call = {g, entry, NULL, NULL};
    call.registration = qa_app_application_input_client_command_registration(
        g->input, qa_session_seat_id(qa_app_application_q3_client_options_of(entry->seat.client)->local->player.seat), NULL);
    qa_caught_error error;
    if (qa_try(reopen_body, &call, &error)) continue;
    size_t failures = 1;
    qa_caught_error cleanup;
    if (!qa_try(replacement_close, &call, &cleanup)) failures++;
    if (!qa_try(registration_close, &call, &cleanup)) failures++;
    if (failures > 1)
      qa_raise("AggregateError", 0, (long long)failures, "Guest video reopen and cleanup failed (%zu errors; first %s: %s)",
               failures, error.name, error.message);
    qa_rethrow(&error);
  }
}
bool qa_app_prepare_video_guests(qa_app_application_input *input, qa_app_native_renderer *renderer,
                                 const qa_app_video_guest_seat *seats, size_t seat_count,
                                 void (*assert_current)(void *ctx), void *ctx,
                                 qa_app_prepared_video_presentation *out) {
  if (seat_count == 0) return false;
  guest_prep *g = calloc(1, sizeof *g);
  if (g == NULL) qa_range_error("Out of memory");
  g->input = input;
  g->renderer = renderer;
  g->assert_current = assert_current;
  g->ctx = ctx;
  g->guests = calloc(seat_count, sizeof *g->guests);
  if (g->guests == NULL) qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, guest_release, g);
  /* seats.map(seat => ({ seat, reopen: seat.client.captureVideoReopen() })) */
  for (size_t i = 0; i < seat_count; i++) {
    g->guests[i].seat = seats[i];
    g->guests[i].reopen = qa_app_application_q3_client_capture_video_reopen(seats[i].client);
    g->guest_count = i + 1;
  }
  qa_cleanup_pop(&node);
  out->ctx = g;
  out->validate_publication = guest_validate;
  out->restart = guest_restart;
  out->release = guest_release;
  return true;
}

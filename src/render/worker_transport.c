/* Port of src/render/worker-transport.ts. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "qa/render/worker_transport.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/foundation/js.h"

/* --- wire --- */
typedef enum msg_kind { MSG_REQUEST, MSG_REPLY, MSG_COMPLETE, MSG_DISPATCH, MSG_INITIALIZE, MSG_CLOSE } msg_kind;
typedef struct response { bool is_failure; void *payload; size_t size; qa_rwt_failure failure; } response;
typedef struct message {
  msg_kind kind; int64_t sequence; bool has_parent; int64_t parent;
  response response; /* payload of dispatch/initialize too */
  struct message *next;
} message;
typedef struct queue { message *head, *tail; } queue;
typedef enum event_kind { EVENT_ERROR, EVENT_EXIT } event_kind;
typedef struct event { event_kind kind; int code; qa_rwt_failure failure; struct event *next; } event;

/* The MessageChannel plus the two-word Atomics signal. */
typedef struct channel {
  pthread_mutex_t lock; pthread_cond_t wake;
  uint64_t signal0; atomic_int signal1;
  queue to_worker, to_main;
  event *events, *events_tail;
} channel;

static void *xmalloc(size_t n) { void *m = malloc(n ? n : 1); if (m == NULL) qa_range_error("Render thread allocation failed"); return m; }
static void *copy_bytes(const void *bytes, size_t size) { if (bytes == NULL || size == 0) return NULL; void *m = xmalloc(size); memcpy(m, bytes, size); return m; }
static void copy_text(char *destination, size_t capacity, const char *text) { (void)snprintf(destination, capacity, "%s", text); }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6; }

void qa_rwt_value_release(qa_rwt_value *value) { if (value == NULL) return; free(value->owned); value->owned = NULL; value->bytes = NULL; value->size = 0; }
void qa_rwt_failure_set(qa_rwt_failure *failure, const char *name, const char *message) {
  failure->is_host = false; failure->token = 0;
  copy_text(failure->name, sizeof failure->name, name); copy_text(failure->message, sizeof failure->message, message);
}

static void send_message(channel *c, queue *q, msg_kind kind, int64_t sequence, bool has_parent, int64_t parent, const response *r) {
  message *m = (message *)xmalloc(sizeof *m);
  memset(m, 0, sizeof *m);
  m->kind = kind; m->sequence = sequence; m->has_parent = has_parent; m->parent = parent;
  if (r != NULL) { m->response = *r; }
  pthread_mutex_lock(&c->lock);
  if (q->tail) q->tail->next = m; else q->head = m;
  q->tail = m;
  c->signal0++;
  pthread_cond_broadcast(&c->wake);
  pthread_mutex_unlock(&c->lock);
}
static message *receive_message(channel *c, queue *q) {
  pthread_mutex_lock(&c->lock);
  message *m = q->head;
  if (m) { q->head = m->next; if (!q->head) q->tail = NULL; m->next = NULL; }
  pthread_mutex_unlock(&c->lock);
  return m;
}
static uint64_t load_wake(channel *c) { pthread_mutex_lock(&c->lock); uint64_t v = c->signal0; pthread_mutex_unlock(&c->lock); return v; }
/* Atomics.wait(signal, 0, wake, milliseconds). */
static void wait_wake(channel *c, uint64_t wake, double milliseconds) {
  struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
  long long ns = t.tv_nsec + (long long)(milliseconds * 1e6);
  t.tv_sec += (time_t)(ns / 1000000000LL); t.tv_nsec = (long)(ns % 1000000000LL);
  pthread_mutex_lock(&c->lock);
  while (c->signal0 == wake) { if (pthread_cond_timedwait(&c->wake, &c->lock, &t) != 0) break; }
  pthread_mutex_unlock(&c->lock);
}
static void post_event(channel *c, event_kind kind, int code, const qa_rwt_failure *failure) {
  event *e = (event *)xmalloc(sizeof *e);
  memset(e, 0, sizeof *e);
  e->kind = kind; e->code = code; if (failure) e->failure = *failure;
  pthread_mutex_lock(&c->lock);
  if (c->events_tail) c->events_tail->next = e; else c->events = e;
  c->events_tail = e;
  c->signal0++;
  pthread_cond_broadcast(&c->wake);
  pthread_mutex_unlock(&c->lock);
}
/* TS sequence()/decodeMessage: kinds are typed here, so the numbers remain. */
static bool valid_sequence(int64_t value) { return value > 0 && value <= QA_MAX_SAFE_INTEGER; }
static const char *invalid_message(const message *m) {
  if (!valid_sequence(m->sequence)) return "Invalid render thread message sequence";
  if (m->kind == MSG_CLOSE) return NULL;
  if (m->has_parent && !valid_sequence(m->parent)) return "Invalid render thread message sequence";
  if ((m->kind == MSG_REQUEST || m->kind == MSG_REPLY || m->kind == MSG_COMPLETE) && m->response.is_failure
    && m->response.failure.is_host && !valid_sequence(m->response.failure.token)) return "Invalid render thread message sequence";
  return NULL;
}
static void free_message(message *m) { if (m) { free(m->response.payload); free(m); } }

/* --- worker (servePort) --- */
typedef struct cb_frame { int64_t sequence, parent; } cb_frame;
struct qa_rwt_worker {
  channel *channel; double timeout;
  qa_rwt_initialize_fn initialize; void *initialize_ctx;
  bool has_runtime; qa_rwt_runtime runtime;
  bool has_terminal; qa_rwt_failure terminal;
  int64_t next_request;
  int64_t *operations; size_t operation_count, operation_cap;
  cb_frame *callbacks; size_t callback_count, callback_cap;
  bool exiting;
};
static void execute(qa_rwt_worker *w, message *m);
bool qa_rwt_worker_request(qa_rwt_worker *w, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_failure *failure) {
  if (w->operation_count == 0) { qa_rwt_failure_set(failure, "Error", "Worker callback requires an executing operation"); return false; }
  int64_t parent = w->operations[w->operation_count - 1];
  int64_t id = w->next_request++; double deadline = now_ms() + w->timeout;
  if (w->callback_count == w->callback_cap) {
    size_t cap = w->callback_cap ? w->callback_cap * 2 : 8;
    cb_frame *grown = (cb_frame *)realloc(w->callbacks, cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Render thread allocation failed");
    w->callbacks = grown; w->callback_cap = cap;
  }
  w->callbacks[w->callback_count].sequence = id; w->callbacks[w->callback_count].parent = parent; w->callback_count++;
  response r; memset(&r, 0, sizeof r); r.payload = copy_bytes(payload.bytes, payload.size); r.size = payload.size;
  send_message(w->channel, &w->channel->to_main, MSG_REQUEST, id, true, parent, &r);
  bool result = false;
  for (;;) {
    uint64_t wake = load_wake(w->channel);
    message *reply = receive_message(w->channel, &w->channel->to_worker);
    if (reply != NULL) {
      const char *invalid = invalid_message(reply);
      if (invalid) { free_message(reply); qa_rwt_failure_set(failure, "Error", invalid); break; }
      if (reply->kind == MSG_DISPATCH && reply->has_parent && reply->parent == id && w->has_runtime) { execute(w, reply); free_message(reply); continue; }
      if (reply->kind != MSG_REPLY || reply->sequence != id || !reply->has_parent || reply->parent != parent) {
        free_message(reply); qa_rwt_failure_set(failure, "Error", "Unexpected render worker callback reply"); break;
      }
      if (reply->response.is_failure) { *failure = reply->response.failure; free_message(reply); break; }
      out->owned = reply->response.payload; out->bytes = reply->response.payload; out->size = reply->response.size;
      reply->response.payload = NULL; free_message(reply); result = true; break;
    }
    double now = now_ms();
    if (now >= deadline) { qa_rwt_failure_set(failure, "Error", "Render worker host callback timed out"); break; }
    wait_wake(w->channel, wake, qa_js_min(10, qa_js_max(0, deadline - now)));
  }
  w->callback_count--;
  return result;
}
static void execute(qa_rwt_worker *w, message *m) {
  response r; memset(&r, 0, sizeof r);
  if (w->operation_count == w->operation_cap) {
    size_t cap = w->operation_cap ? w->operation_cap * 2 : 8;
    int64_t *grown = (int64_t *)realloc(w->operations, cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Render thread allocation failed");
    w->operations = grown; w->operation_cap = cap;
  }
  w->operations[w->operation_count++] = m->sequence;
  atomic_store(&w->channel->signal1, 1);
  qa_rwt_failure failure; memset(&failure, 0, sizeof failure);
  bool thrown = false;
  qa_rwt_value payload; payload.bytes = m->response.payload; payload.size = m->response.size; payload.owned = NULL;
  bool has_callback = w->callback_count != 0;
  int64_t callback = has_callback ? w->callbacks[w->callback_count - 1].sequence : 0;
  if (m->kind == MSG_CLOSE) {
    if (has_callback) { qa_rwt_failure_set(&failure, "Error", "Cannot close a suspended render callback"); thrown = true; }
    else if (w->has_runtime && w->runtime.close != NULL && !w->runtime.close(w->runtime.ctx, &failure)) thrown = true;
    else w->has_runtime = false;
  } else if (m->kind != MSG_DISPATCH && m->kind != MSG_INITIALIZE) {
    qa_rwt_failure_set(&failure, "Error", "Unexpected render worker command"); thrown = true;
  } else if (m->has_parent != has_callback || (has_callback && m->parent != callback)) {
    qa_rwt_failure_set(&failure, "Error", "Render worker dispatch has an invalid callback parent"); thrown = true;
  } else if (w->has_terminal) {
    r.is_failure = true; r.failure = w->terminal;
  } else if (m->kind == MSG_INITIALIZE && !w->has_runtime && !m->has_parent) {
    qa_rwt_runtime runtime; memset(&runtime, 0, sizeof runtime);
    if (w->initialize(w->initialize_ctx, payload, w, &runtime, &failure)) {
      w->runtime = runtime; w->has_runtime = true;
      r.payload = copy_bytes(runtime.description.bytes, runtime.description.size); r.size = runtime.description.size;
    } else thrown = true;
  } else if (m->kind == MSG_DISPATCH && w->has_runtime) {
    qa_rwt_value out; memset(&out, 0, sizeof out);
    if (w->runtime.dispatch(w->runtime.ctx, payload, &out, &failure)) {
      r.payload = copy_bytes(out.bytes, out.size); r.size = out.size;
      qa_rwt_value_release(&out);
    } else thrown = true;
  } else {
    qa_rwt_failure_set(&failure, "Error", "Unexpected render worker lifecycle command"); thrown = true;
  }
  if (thrown) {
    if (!w->has_terminal) { w->terminal = failure; w->has_terminal = true; }
    free(r.payload); memset(&r, 0, sizeof r);
    r.is_failure = true; r.failure = w->terminal;
  }
  w->operation_count--;
  atomic_store(&w->channel->signal1, w->operation_count != 0 ? 1 : 0);
  bool closing = m->kind == MSG_CLOSE && !r.is_failure;
  send_message(w->channel, &w->channel->to_main, MSG_COMPLETE, m->sequence, false, 0, &r);
  if (closing) w->exiting = true;
}
static void *worker_main(void *arg) {
  qa_rwt_worker *w = (qa_rwt_worker *)arg;
  while (!w->exiting) {
    uint64_t wake = load_wake(w->channel);
    message *m = receive_message(w->channel, &w->channel->to_worker);
    if (m == NULL) { wait_wake(w->channel, wake, 1000); continue; }
    const char *invalid = invalid_message(m);
    if (invalid != NULL) {
      /* An uncaught throw in the port listener reaches worker.on("error"), then exit. */
      qa_rwt_failure f; qa_rwt_failure_set(&f, "Error", invalid);
      free_message(m);
      post_event(w->channel, EVENT_ERROR, 0, &f);
      post_event(w->channel, EVENT_EXIT, 1, NULL);
      return NULL;
    }
    execute(w, m);
    free_message(m);
  }
  post_event(w->channel, EVENT_EXIT, 0, NULL);
  return NULL;
}

/* --- frontend --- */
typedef struct pending {
  int64_t sequence; bool publish, has_response, consumed, armed, outstanding;
  double deadline; response response;
  struct pending *next;
} pending;
typedef struct host_failure { int64_t token; qa_rwt_error *error; } host_failure;
typedef struct callback_frame { int64_t request, parent; pending *pending; } callback_frame;
struct qa_rwt_transport {
  channel *channel; qa_rwt_worker *worker; pthread_t thread; qa_rwt_host host; double timeout;
  qa_rwt_error *errors;
  host_failure *host_failures; size_t host_failure_count, host_failure_cap;
  int64_t next_sequence, next_host_failure;
  pending *all, *current, *close_pending;
  qa_rwt_error *fault, *notified, *observed;
  callback_frame *callbacks; size_t callback_count, callback_cap;
  bool closed, delivering, failure_delivered, thread_joined;
  void *description; size_t description_size;
  int wait_depth;
};

qa_rwt_error *qa_rwt_error_new(qa_rwt_transport *t, const char *name, const char *text) {
  qa_rwt_error *e = (qa_rwt_error *)xmalloc(sizeof *e);
  memset(e, 0, sizeof *e);
  copy_text(e->name, sizeof e->name, name); copy_text(e->message, sizeof e->message, text);
  e->next = t->errors; t->errors = e;
  return e;
}
static qa_rwt_error *fail(qa_rwt_transport *t, qa_rwt_error *error) {
  if (t->notified == NULL) t->notified = error;
  if (t->fault == NULL) t->fault = t->notified;
  if (t->failure_delivered) return t->fault;
  t->failure_delivered = true;
  if (t->host.failed != NULL) t->host.failed(t->host.ctx, t->fault);
  return t->fault;
}
static qa_rwt_error *host_failure_for(qa_rwt_transport *t, int64_t token) {
  for (size_t i = 0; i < t->host_failure_count; i++) if (t->host_failures[i].token == token) return t->host_failures[i].error;
  return NULL;
}
/* restoreFailure, resolving a known host token to its exact thrown error. */
static qa_rwt_error *resolve_failure(qa_rwt_transport *t, const qa_rwt_failure *f) {
  if (f->is_host) {
    qa_rwt_error *known = host_failure_for(t, f->token);
    return known != NULL ? known : qa_rwt_error_new(t, "Error", "Renderer host callback failed");
  }
  return qa_rwt_error_new(t, f->name, f->message);
}
static pending *new_pending(qa_rwt_transport *t, int64_t sequence, bool publish) {
  pending *p = (pending *)xmalloc(sizeof *p);
  memset(p, 0, sizeof *p);
  p->sequence = sequence; p->publish = publish; p->outstanding = true;
  p->armed = true; p->deadline = now_ms() + t->timeout; /* armTimeout */
  p->next = t->all; t->all = p;
  return p;
}
/* C ownership: TS lets the GC reclaim settled records. Sweep at top level only,
 * when no suspended frame can still reference one. */
static void sweep_pending(qa_rwt_transport *t) {
  if (t->wait_depth != 0 || t->callback_count != 0) return;
  pending **link = &t->all;
  while (*link != NULL) {
    pending *p = *link;
    if (p != t->current && p != t->close_pending && !p->outstanding) { *link = p->next; free(p->response.payload); free(p); }
    else link = &p->next;
  }
}
static qa_rwt_error *accept_message(qa_rwt_transport *t, message *m) {
  if (m->kind == MSG_REQUEST) {
    if (m->response.is_failure) return qa_rwt_error_new(t, "Error", "Worker request carried an error response");
    pending *p = t->current;
    if (p == NULL || p->has_response || !m->has_parent || m->parent != p->sequence)
      return qa_rwt_error_new(t, "Error", "Worker callback has no matching parent operation");
    if (t->callback_count == t->callback_cap) {
      size_t cap = t->callback_cap ? t->callback_cap * 2 : 8;
      callback_frame *grown = (callback_frame *)realloc(t->callbacks, cap * sizeof *grown);
      if (grown == NULL) qa_range_error("Render thread allocation failed");
      t->callbacks = grown; t->callback_cap = cap;
    }
    callback_frame *frame = &t->callbacks[t->callback_count++];
    frame->request = m->sequence; frame->parent = p->sequence; frame->pending = p;
    qa_rwt_value payload; payload.bytes = m->response.payload; payload.size = m->response.size; payload.owned = NULL;
    qa_rwt_value out; memset(&out, 0, sizeof out);
    qa_rwt_error *thrown = NULL;
    response r; memset(&r, 0, sizeof r);
    if (t->host.request(t->host.ctx, payload, &out, &thrown)) {
      r.payload = copy_bytes(out.bytes, out.size); r.size = out.size;
      qa_rwt_value_release(&out);
    } else {
      int64_t token = t->next_host_failure++;
      if (t->host_failure_count == t->host_failure_cap) {
        size_t cap = t->host_failure_cap ? t->host_failure_cap * 2 : 8;
        host_failure *grown = (host_failure *)realloc(t->host_failures, cap * sizeof *grown);
        if (grown == NULL) qa_range_error("Render thread allocation failed");
        t->host_failures = grown; t->host_failure_cap = cap;
      }
      t->host_failures[t->host_failure_count].token = token;
      t->host_failures[t->host_failure_count].error = thrown != NULL ? thrown : qa_rwt_error_new(t, "Error", "Renderer host callback failed");
      t->host_failure_count++;
      r.is_failure = true; r.failure.is_host = true; r.failure.token = token;
    }
    t->callback_count--;
    send_message(t->channel, &t->channel->to_worker, MSG_REPLY, m->sequence, true, p->sequence, &r);
    return NULL;
  }
  if (m->kind != MSG_COMPLETE) return qa_rwt_error_new(t, "Error", "Unexpected renderer worker message");
  pending *p = NULL;
  for (pending *c = t->all; c != NULL; c = c->next) if (c->outstanding && c->sequence == m->sequence) { p = c; break; }
  if (p == NULL || m->has_parent || p->has_response)
    return qa_rwt_error_new(t, "Error", "Renderer completed an unowned command buffer");
  p->response = m->response; m->response.payload = NULL;
  p->has_response = true; p->armed = false; p->outstanding = false;
  if (p->response.is_failure) (void)fail(t, resolve_failure(t, &p->response.failure));
  if (p->publish && !p->response.is_failure) {
    qa_rwt_value payload; payload.bytes = p->response.payload; payload.size = p->response.size; payload.owned = NULL;
    qa_rwt_error *thrown = NULL;
    t->delivering = true;
    bool ok = t->host.completed(t->host.ctx, payload, &thrown);
    t->delivering = false;
    if (!ok) return fail(t, thrown != NULL ? thrown : qa_rwt_error_new(t, "Error", "Renderer completion callback failed"));
  }
  return NULL;
}
/* drain(): returns the fault it raised, or NULL. */
static qa_rwt_error *drain(qa_rwt_transport *t) {
  for (;;) {
    message *m = receive_message(t->channel, &t->channel->to_main);
    if (m == NULL) return NULL;
    const char *invalid = invalid_message(m);
    qa_rwt_error *error = invalid != NULL ? qa_rwt_error_new(t, "Error", invalid) : accept_message(t, m);
    free_message(m);
    if (error != NULL) return fail(t, error);
  }
}
static bool wait_pending(qa_rwt_transport *t, qa_rwt_value *out, qa_rwt_error **error) {
  qa_rwt_error *thrown = NULL;
  if (out) memset(out, 0, sizeof *out);
  t->wait_depth++;
  if (t->closed) { thrown = qa_rwt_error_new(t, "Error", "Render thread is closed"); goto failed; }
  {
    pending *p = t->current;
    if (p == NULL) { if (t->fault != NULL) { thrown = t->fault; goto failed; } t->wait_depth--; return true; }
    double deadline = now_ms() + t->timeout;
    while (!p->has_response) {
      uint64_t wake = load_wake(t->channel);
      thrown = drain(t);
      if (thrown != NULL) goto failed;
      if (t->fault != NULL) { thrown = t->fault; goto failed; }
      if (p->has_response) break;
      double now = now_ms();
      if (now >= deadline) { thrown = fail(t, qa_rwt_error_new(t, "Error", "Render thread did not complete its issued buffer")); goto failed; }
      wait_wake(t->channel, wake, qa_js_min(10, qa_js_max(0, deadline - now)));
    }
    t->current = NULL;
    p->consumed = true;
    if (!p->response.is_failure) {
      if (t->fault != NULL) { thrown = t->fault; goto failed; }
      if (out) {
        out->owned = p->response.payload; out->bytes = p->response.payload; out->size = p->response.size;
        p->response.payload = NULL;
      }
      t->wait_depth--;
      sweep_pending(t);
      return true;
    }
    thrown = fail(t, resolve_failure(t, &p->response.failure));
  }
failed:
  if (t->fault != NULL && t->fault == thrown) t->observed = t->fault;
  t->wait_depth--;
  sweep_pending(t);
  if (error) *error = thrown;
  return false;
}
static bool begin_operation(qa_rwt_transport *t, msg_kind kind, qa_rwt_value payload, bool publish, bool has_parent, int64_t parent, qa_rwt_error **error) {
  if (t->closed) { *error = qa_rwt_error_new(t, "Error", "Render thread is closed"); return false; }
  if (t->callback_count != 0 && (!has_parent || parent != t->callbacks[t->callback_count - 1].request)) {
    *error = qa_rwt_error_new(t, "Error", "Reentrant renderer operation during host callback"); return false;
  }
  if (t->fault != NULL) { *error = t->fault; return false; }
  if (t->current != NULL) { *error = qa_rwt_error_new(t, "Error", "Renderer buffer is still owned by the backend"); return false; }
  int64_t sequence = t->next_sequence++;
  t->current = new_pending(t, sequence, publish);
  response r; memset(&r, 0, sizeof r); r.payload = copy_bytes(payload.bytes, payload.size); r.size = payload.size;
  send_message(t->channel, &t->channel->to_worker, kind, sequence, has_parent, parent, &r);
  return true;
}
bool qa_rwt_synchronize(qa_rwt_transport *t, qa_rwt_value *out, qa_rwt_error **error) {
  if (t->callback_count != 0 || t->delivering) {
    if (out) memset(out, 0, sizeof *out);
    if (error) *error = qa_rwt_error_new(t, "Error", "Reentrant renderer synchronization during host callback");
    return false;
  }
  return wait_pending(t, out, error);
}
bool qa_rwt_issue(qa_rwt_transport *t, qa_rwt_value payload, qa_rwt_error **error) {
  qa_rwt_value previous;
  if (!qa_rwt_synchronize(t, &previous, error)) return false;
  qa_rwt_value_release(&previous);
  return begin_operation(t, MSG_DISPATCH, payload, true, false, 0, error);
}
bool qa_rwt_call(qa_rwt_transport *t, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error) {
  qa_rwt_value previous;
  if (out) memset(out, 0, sizeof *out);
  if (!qa_rwt_synchronize(t, &previous, error)) return false;
  qa_rwt_value_release(&previous);
  if (!begin_operation(t, MSG_DISPATCH, payload, false, false, 0, error)) return false;
  return qa_rwt_synchronize(t, out, error);
}
bool qa_rwt_callback_call(qa_rwt_transport *t, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error) {
  if (t->callback_count == 0) return qa_rwt_call(t, payload, out, error);
  if (out) memset(out, 0, sizeof *out);
  callback_frame frame = t->callbacks[t->callback_count - 1];
  if (t->current != frame.pending) { *error = qa_rwt_error_new(t, "Error", "Callback does not own suspended renderer operation"); return false; }
  pending *suspended = t->current;
  t->current = NULL;
  bool ok = begin_operation(t, MSG_DISPATCH, payload, false, true, frame.request, error) && wait_pending(t, out, error);
  t->current = suspended;
  return ok;
}
bool qa_rwt_active(const qa_rwt_transport *t) { return atomic_load(&t->channel->signal1) != 0; }
bool qa_rwt_settled(const qa_rwt_transport *t) { return t->closed || t->current == NULL || t->current->has_response; }
bool qa_rwt_retired(const qa_rwt_transport *t) { return t->closed; }
qa_rwt_value qa_rwt_description(const qa_rwt_transport *t) { qa_rwt_value v; v.bytes = t->description; v.size = t->description_size; v.owned = NULL; return v; }
static void retire(qa_rwt_transport *t) {
  t->closed = true;
  t->current = NULL;
  if (!t->thread_joined) { pthread_join(t->thread, NULL); t->thread_joined = true; } /* worker.terminate after acknowledged close */
  t->host_failure_count = 0;
  for (pending *p = t->all; p != NULL; p = p->next) { p->armed = false; p->outstanding = false; }
}
bool qa_rwt_close(qa_rwt_transport *t, qa_rwt_error **error) {
  if (t->closed) return true;
  if (t->callback_count != 0 || t->delivering) { *error = qa_rwt_error_new(t, "Error", "Reentrant render thread close"); return false; }
  qa_rwt_error *first = t->fault;
  t->fault = NULL;
  qa_rwt_value dropped; qa_rwt_error *sync_error = NULL;
  if (qa_rwt_synchronize(t, &dropped, &sync_error)) qa_rwt_value_release(&dropped);
  else first = sync_error;
  if (t->current != NULL && !t->current->has_response) {
    *error = first != NULL ? first : qa_rwt_error_new(t, "Error", "Renderer cannot retire before its current operation completes");
    return false;
  }
  if (t->close_pending != NULL && t->close_pending->has_response && !t->close_pending->response.is_failure) {
    retire(t);
    if (first != NULL) { *error = first; return false; }
    return true;
  }
  t->fault = NULL;
  pending *p = new_pending(t, t->next_sequence++, false);
  t->current = p; t->close_pending = p;
  send_message(t->channel, &t->channel->to_worker, MSG_CLOSE, p->sequence, false, 0, NULL);
  if (qa_rwt_synchronize(t, &dropped, &sync_error)) qa_rwt_value_release(&dropped);
  else if (first == NULL) first = fail(t, sync_error);
  if (p->has_response && !p->response.is_failure) retire(t);
  if (first != NULL) { *error = first; return false; }
  return true;
}
bool qa_rwt_retire_after_failure(qa_rwt_transport *t, bool *retired, qa_rwt_error **error) {
  *retired = false;
  if (t->fault == NULL || t->observed != t->fault) return true;
  t->fault = NULL;
  t->observed = NULL;
  if (!qa_rwt_close(t, error)) return false;
  *retired = true;
  return true;
}
/* Node event loop: port listener with catch -> fail, worker error/exit, timers. */
void qa_rwt_pump(qa_rwt_transport *t) {
  if (t->callback_count != 0 || t->delivering) return;
  for (;;) {
    pthread_mutex_lock(&t->channel->lock);
    event *e = t->channel->events;
    if (e != NULL) { t->channel->events = e->next; if (t->channel->events == NULL) t->channel->events_tail = NULL; }
    pthread_mutex_unlock(&t->channel->lock);
    if (e == NULL) break;
    if (!t->closed) {
      if (e->kind == EVENT_ERROR) (void)fail(t, resolve_failure(t, &e->failure));
      else if (t->close_pending == NULL || !t->close_pending->has_response || t->close_pending->response.is_failure) {
        char text[128];
        (void)snprintf(text, sizeof text, "Render thread exited before shutdown (%d)", e->code);
        (void)fail(t, qa_rwt_error_new(t, "Error", text));
      }
    }
    free(e);
  }
  if (t->closed) return;
  (void)drain(t);
  double now = now_ms();
  for (pending *p = t->all; p != NULL; p = p->next) {
    if (!p->armed || now < p->deadline) continue;
    p->armed = false;
    if (t->closed || p->has_response) continue;
    (void)drain(t);
    if (!p->has_response) (void)fail(t, qa_rwt_error_new(t, "Error", "Render thread did not complete its issued buffer"));
  }
}
bool qa_rwt_open(qa_rwt_transport **out, qa_rwt_initialize_fn initialize, void *initialize_ctx, qa_rwt_value initialization, qa_rwt_host host, double timeout_milliseconds, qa_rwt_error **error) {
  *out = NULL;
  if (!isfinite(timeout_milliseconds) || timeout_milliseconds <= 0) qa_range_error("Invalid render thread timeout");
  qa_rwt_transport *t = (qa_rwt_transport *)calloc(1, sizeof *t);
  channel *c = (channel *)calloc(1, sizeof *c);
  qa_rwt_worker *w = (qa_rwt_worker *)calloc(1, sizeof *w);
  if (t == NULL || c == NULL || w == NULL) qa_range_error("Render thread allocation failed");
  pthread_mutex_init(&c->lock, NULL); pthread_cond_init(&c->wake, NULL); atomic_init(&c->signal1, 0);
  t->channel = c; t->worker = w; t->host = host; t->timeout = timeout_milliseconds;
  t->next_sequence = 1; t->next_host_failure = 1;
  w->channel = c; w->timeout = timeout_milliseconds; w->initialize = initialize; w->initialize_ctx = initialize_ctx; w->next_request = 1;
  *out = t;
  if (pthread_create(&t->thread, NULL, worker_main, w) != 0) {
    t->thread_joined = true; t->closed = true;
    *error = qa_rwt_error_new(t, "Error", "Render thread could not start");
    return false;
  }
  qa_rwt_error *thrown = NULL; qa_rwt_value description; memset(&description, 0, sizeof description);
  bool ok = begin_operation(t, MSG_INITIALIZE, initialization, false, false, 0, &thrown);
  if (ok) {
    /* Yield during startup so loader errors are delivered without waiting for the timeout. */
    double deadline = now_ms() + timeout_milliseconds;
    while (t->current != NULL && !t->current->has_response && t->fault == NULL && now_ms() < deadline) {
      qa_rwt_error *drained = drain(t);
      if (drained != NULL) { thrown = drained; ok = false; break; }
      qa_rwt_pump(t);
      if (t->current != NULL && !t->current->has_response) { struct timespec ms = {0, 1000000}; nanosleep(&ms, NULL); }
    }
  }
  if (ok) ok = qa_rwt_synchronize(t, &description, &thrown);
  if (ok) { t->description = description.owned; t->description_size = description.size; return true; }
  qa_rwt_error *problem = fail(t, thrown);
  qa_rwt_error *ignored = NULL;
  (void)qa_rwt_close(t, &ignored); /* Preserve startup failure; an unacknowledged context remains borrowed. */
  *error = problem;
  return false;
}
void qa_rwt_destroy(qa_rwt_transport *t) {
  if (t == NULL) return;
  if (!t->thread_joined) pthread_detach(t->thread); /* channel and worker stay with the running thread */
  else {
    message *m;
    while ((m = receive_message(t->channel, &t->channel->to_worker)) != NULL) free_message(m);
    while ((m = receive_message(t->channel, &t->channel->to_main)) != NULL) free_message(m);
    for (event *e = t->channel->events; e != NULL;) { event *next = e->next; free(e); e = next; }
    pthread_mutex_destroy(&t->channel->lock); pthread_cond_destroy(&t->channel->wake);
    free(t->channel);
    free(t->worker->operations); free(t->worker->callbacks); free(t->worker);
  }
  for (pending *p = t->all; p != NULL;) { pending *next = p->next; free(p->response.payload); free(p); p = next; }
  for (qa_rwt_error *e = t->errors; e != NULL;) { qa_rwt_error *next = e->next; free(e); e = next; }
  free(t->host_failures); free(t->callbacks); free(t->description); free(t);
}

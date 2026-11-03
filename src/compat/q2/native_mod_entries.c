/* Port of src/compat/q2/native-mod-entries.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_entries.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_throw.h"
typedef struct { bool has_stack; uint64_t stack; } entry_frame;
static void observe_cb(void *ctx) { qa_compat_q2_native_mod_entry_binding *b=ctx; if (b->has_frame) b->frame_stack=b->host.read_rsp(b->host.ctx); }
void qa_compat_q2_bind_native_mod_entry(qa_compat_q2_native_mod_entry_binding *out, qa_compat_q2_native_mod_entry_host host, qa_guest_address address, qa_callback_id id, qa_compat_q2_guest_call_signature signature) {
  out->host=host; out->address=address; out->signature=signature; out->has_frame=false; out->closed=false;
  if (host.observe_entry) host.observe_entry(host.ctx, address, out, observe_cb);
  (void)id;
}
qa_guest_call_result qa_compat_q2_native_mod_entry_original(qa_compat_q2_native_mod_entry_binding *b, const qa_guest_call_value *v, size_t n) {
  b->has_frame=true; qa_guest_call_result r=b->host.invoke(b->host.ctx,b->address,&b->signature,v,n); b->has_frame=false; return r;
}
void qa_compat_q2_native_mod_entry_close(qa_compat_q2_native_mod_entry_binding *b) {
  if (b->closed) return;
  b->closed=true; if (b->host.unbind) b->host.unbind(b->host.ctx); if (b->host.unobserve) b->host.unobserve(b->host.ctx);
}

/* --- TS bindNativeModEntry over the memory/runner mirrors. --- */
struct qa_compat_q2_native_entry {
  qa_compat_q2_memory *memory; qa_compat_q2_runner *runner;
  void *invoke_ctx; qa_compat_q2_native_entry_invoke_fn invoke;
  qa_guest_address address; char *id; qa_compat_q2_guest_call_signature signature;
  void *ctx; qa_compat_q2_native_entry_execute_fn execute;
  void *accepts_ctx; bool (*accepts)(void *);
  entry_frame *frames; size_t frame_count, frame_capacity;
  uint64_t observe, bound;
};
static uint64_t entry_stack(const qa_compat_q2_native_entry *b) {
  return b->runner->read_register(b->runner->ctx, "rsp", b->memory->pointer_bytes == 4 ? 32 : 64);
}
static void entry_observed(void *v) {
  qa_compat_q2_native_entry *b = v;
  if (b->frame_count != 0 && !b->frames[b->frame_count - 1].has_stack) {
    b->frames[b->frame_count - 1].has_stack = true;
    b->frames[b->frame_count - 1].stack = entry_stack(b);
  }
}
static bool entry_accepts(void *v) {
  qa_compat_q2_native_entry *b = v;
  /* TS frames.at(-1)?.stack !== stack(): a frame still at null differs too. */
  if (b->frame_count != 0 && b->frames[b->frame_count - 1].has_stack && b->frames[b->frame_count - 1].stack == entry_stack(b)) return false;
  return b->accepts == NULL || b->accepts(b->accepts_ctx);
}
static qa_guest_call_result entry_invoked(void *v, const qa_guest_call_context *context, const qa_guest_call_value *values, size_t count) {
  qa_compat_q2_native_entry *b = v; (void)context;
  return b->execute(b->ctx, values, count, b);
}
qa_compat_q2_native_entry *qa_compat_q2_native_entry_bind(
    qa_compat_q2_memory *memory, qa_compat_q2_runner *runner, void *invoke_ctx, qa_compat_q2_native_entry_invoke_fn invoke,
    qa_guest_address address, const char *id, qa_compat_q2_guest_call_signature signature,
    void *ctx, qa_compat_q2_native_entry_execute_fn execute, void *accepts_ctx, bool (*accepts)(void *)) {
  qa_compat_q2_native_entry *b = calloc(1, sizeof *b);
  size_t length = strlen(id) + 1;
  char *owned = malloc(length);
  if (b == NULL || owned == NULL) qa_range_error("Native entry binding out of memory");
  memcpy(owned, id, length);
  b->memory = memory; b->runner = runner; b->invoke_ctx = invoke_ctx; b->invoke = invoke;
  b->address = address; b->id = owned; b->signature = signature;
  b->ctx = ctx; b->execute = execute; b->accepts_ctx = accepts_ctx; b->accepts = accepts;
  b->observe = runner->observe_entry(runner->ctx, address, b, entry_observed);
  b->bound = runner->bind_entry(runner->ctx, address, owned, signature, b, entry_invoked, b, entry_accepts);
  return b;
}
typedef struct { qa_compat_q2_native_entry *b; const qa_guest_call_value *values; size_t count; qa_guest_call_result result; } original_call;
static void original_body(void *v) {
  original_call *c = v;
  c->result = c->b->invoke(c->b->invoke_ctx, c->b->address, c->b->signature, c->values, c->count);
}
qa_guest_call_result qa_compat_q2_native_entry_call_original(qa_compat_q2_native_entry *b, const qa_guest_call_value *values, size_t count) {
  if (b->frame_count == b->frame_capacity) {
    size_t capacity = b->frame_capacity ? b->frame_capacity * 2 : 4;
    entry_frame *grown = realloc(b->frames, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native entry binding out of memory");
    b->frames = grown; b->frame_capacity = capacity;
  }
  b->frames[b->frame_count++] = (entry_frame){false, 0};
  size_t depth = b->frame_count;
  original_call call = {b, values, count, {0}};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(original_body, &call, &thrown);
  b->frame_count = depth - 1;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return call.result;
}
void qa_compat_q2_native_entry_close(qa_compat_q2_native_entry *b) {
  if (b == NULL) return;
  b->runner->remove_entry(b->runner->ctx, b->bound);
  b->runner->remove_entry(b->runner->ctx, b->observe);
  free(b->id); free(b->frames); free(b);
}

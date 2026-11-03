/* C checks for the synchronous cases of tests/compat/qvm/function-hooks.test.ts:
 * original regions, invocation hooks, observers, resolvers, cancellation,
 * branches, counters and source evaluations, over both QVM semantics.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
  do {                                                                              \
    checks++;                                                                       \
    if (!(cond)) {                                                                  \
      failures++;                                                                   \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                               \
  } while (0)

#define SENTINEL 0x5eed1234u
typedef struct op { int32_t opcode; bool has; int32_t operand; } op;
#define O(x) {QA_QVM_OP_##x, false, 0}
#define OA(x, v) {QA_QVM_OP_##x, true, (v)}

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = 0;
  for (int i = 0; i < 4; i++) bits |= (uint32_t)bytes[offset + (size_t)i] << (8 * i);
  int32_t out;
  memcpy(&out, &bits, 4);
  return out;
}

static size_t build(uint8_t *out, const op *ops, size_t count, int32_t bss) {
  size_t length = 0;
  uint8_t *code = out + 32;
  for (size_t i = 0; i < count; i++) {
    code[length++] = (uint8_t)ops[i].opcode;
    if (ops[i].has) {
      if (ops[i].opcode == QA_QVM_OP_ARG) code[length++] = (uint8_t)ops[i].operand;
      else { put_i32(code, length, ops[i].operand); length += 4; }
    }
  }
  int32_t header[8] = {0x12721444, (int32_t)count, 32, (int32_t)length, 32 + (int32_t)length, 0, 0, bss};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  return 32 + length;
}

static bool throws_with(void (*fn)(void *), void *ctx, const char *needle) {
  char message[512];
  if (qa_range_error_catch(fn, ctx, message, sizeof message)) return false;
  if (needle != NULL && strstr(message, needle) == NULL) {
    fprintf(stderr, "  unexpected message: %s (wanted %s)\n", message, needle);
    return false;
  }
  return true;
}

static const double zero_args[10] = {0};
static const char *const semantics_names[2] = {"interpreted", "compiled"};

typedef struct vm { qa_qvm_image image; qa_qvm_interpreter *interp; } vm;

static void vm_open(vm *out, const op *ops, size_t count, int32_t bss, qa_qvm_interpreter_system_call system_call,
                    const char *semantics) {
  static uint8_t bytes[65536];
  size_t length = build(bytes, ops, count, bss);
  CHECK(qa_qvm_parse(bytes, length, "<qvm>", &out->image));
  out->interp = qa_qvm_interpreter_create(&out->image, system_call, NULL, NULL, semantics, NULL);
  CHECK(out->interp != NULL);
}
static void vm_close(vm *v) { qa_qvm_interpreter_destroy(v->interp); qa_qvm_image_destroy(&v->image); }
static size_t vm_length(vm *v) { size_t n = 0; (void)qa_qvm_interpreter_get_memory(v->interp, &n); return n; }
static uint8_t *vm_bytes(vm *v) { return qa_qvm_interpreter_get_memory(v->interp, NULL); }
static bool vm_idle(vm *v) {
  return !qa_qvm_interpreter_get_is_active(v->interp) && qa_qvm_interpreter_get_stack_pointer(v->interp) == (double)vm_length(v);
}

typedef struct invoke_job { qa_qvm_interpreter *interp; const double *args; size_t count; double index; double result; } invoke_job;
static void invoke_body(void *ctx) {
  invoke_job *job = ctx;
  job->result = qa_qvm_interpreter_invoke(job->interp, job->args, job->count, job->index, NULL);
}
static double invoke0(vm *v) { return qa_qvm_interpreter_invoke(v->interp, zero_args, 10, 0, NULL); }
static bool invoke_throws(vm *v, const char *needle) {
  invoke_job job = {v->interp, zero_args, 10, 0, 0};
  return throws_with(invoke_body, &job, needle);
}

static double unexpected_trap(void *ctx, qa_qvm_syscall *call) { (void)ctx; (void)call; qa_range_error("Unexpected syscall"); }
static double zero_trap(void *ctx, qa_qvm_syscall *call) { (void)ctx; (void)call; return 0; }
static const qa_qvm_interpreter_system_call no_trap = {NULL, unexpected_trap};

/* ---- original regions ---- */

static const op region_source[] = {
    OA(ENTER, 32), OA(LOCAL, 24), OA(CONST, 7), O(STORE4),
    OA(LOCAL, 24), OA(LOCAL, 24), O(LOAD4), OA(CONST, 3), O(ADD), O(STORE4),
    OA(LOCAL, 24), O(LOAD4), OA(LEAVE, 32),
};

typedef struct region_ctx {
  uint32_t magic;
  int depth, completions, controls, mode;
  double entry, join;
  qa_qvm_cancellation_scope scope;
  bool completed;
  bool caught;
} region_ctx;

static const char *region_run(void *ctx, qa_qvm_region_control *control) {
  region_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  r->controls++;
  CHECK(control->local_word(control->ctx, 24) == 7);
  if (r->depth == 1) {
    CHECK(control->invoke(control->ctx, zero_args, 10, true, 0, NULL) == 10);
    CHECK(control->local_word(control->ctx, 24) == 7);
    return "skip";
  }
  return "execute";
}
static void region_completed(void *ctx, qa_qvm_region_control *control) {
  region_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  r->completions++;
  CHECK(control->local_word(control->ctx, 24) == 10);
}
static double region_hook(void *ctx, qa_qvm_function_call *call) {
  region_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  r->depth++;
  qa_qvm_region_binding binding = {4, 10, r, region_run, region_completed};
  call->regions(call->ctx, &binding, 1);
  double result = strcmp(call->execution, "asynchronous") == 0 ? call->proceed_async(call->ctx) : call->proceed(call->ctx);
  r->depth--;
  return result;
}

static const char *skip_run(void *ctx, qa_qvm_region_control *control) { (void)ctx; (void)control; return "skip"; }
static double bounded_region_hook(void *ctx, qa_qvm_function_call *call) {
  region_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  qa_qvm_region_binding binding = {r->entry, r->join, r, skip_run, NULL};
  call->regions(call->ctx, &binding, 1);
  return call->proceed(call->ctx);
}

typedef struct local_job { qa_qvm_region_control *control; double offset; } local_job;
static void local_body(void *ctx) {
  local_job *job = ctx;
  (void)job->control->local_word(job->control->ctx, job->offset);
}
static const char *sticky_run(void *ctx, qa_qvm_region_control *control) {
  region_ctx *r = ctx;
  local_job job = {control, 32};
  r->caught = throws_with(local_body, &job, "local is outside");
  return "execute";
}
static double sticky_hook(void *ctx, qa_qvm_function_call *call) {
  region_ctx *r = ctx;
  qa_qvm_region_binding binding = {4, 10, r, sticky_run, NULL};
  call->regions(call->ctx, &binding, 1);
  return call->proceed(call->ctx);
}

static const char *cancel_run(void *ctx, qa_qvm_region_control *control) {
  region_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  control->cancel_function(control->ctx, r->scope);
  return "execute";
}
static void cancel_completed(void *ctx, qa_qvm_region_control *control) {
  (void)control;
  ((region_ctx *)ctx)->completed = true;
}
static double region_cancel_hook(void *ctx, qa_qvm_function_call *call) {
  region_ctx *r = ctx;
  r->scope = call->cancellation_scope(call->ctx);
  qa_qvm_region_binding binding = {4, 10, r, cancel_run, cancel_completed};
  call->regions(call->ctx, &binding, 1);
  return call->proceed(call->ctx);
}

static double evaluate_hook(void *ctx, qa_qvm_function_call *call) {
  region_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  int64_t inputs[1] = {24};
  double values[1] = {19};
  qa_qvm_region_evaluation region = {4, 10, inputs, r->mode == 0 ? 1u : 0u, true, 24};
  return call->evaluate_region(call->ctx, region, values, r->mode == 0 ? 1 : 0);
}

typedef struct read_only_job { qa_qvm_interpreter *interp; qa_qvm_read_only_evaluation *evaluation; } read_only_job;
static void read_only_body(void *ctx) {
  read_only_job *job = ctx;
  (void)qa_qvm_interpreter_invoke(job->interp, zero_args, 10, 0, job->evaluation);
}

static void test_regions(void) {
  for (int s = 0; s < 2; s++) {
    for (int asynchronous = 0; asynchronous < 2; asynchronous++) {
      vm v;
      vm_open(&v, region_source, 13, 4096, no_trap, semantics_names[s]);
      region_ctx r;
      memset(&r, 0, sizeof r);
      r.magic = SENTINEL;
      (void)qa_qvm_interpreter_bind_invocation(v.interp, 0, (qa_qvm_function_hook){&r, region_hook});
      double result = asynchronous ? qa_qvm_interpreter_invoke_async(v.interp, zero_args, 10, 0) : invoke0(&v);
      CHECK(result == 7);
      CHECK(r.completions == 1 && r.controls == 2);
      CHECK(vm_idle(&v));
      vm_close(&v);
    }
  }
  const double bounds[4][2] = {{6, 10}, {4, 9}, {4, 13}, {0, 10}};
  for (int i = 0; i < 4; i++) {
    vm v;
    vm_open(&v, region_source, 13, 4096, no_trap, NULL);
    region_ctx r;
    memset(&r, 0, sizeof r);
    r.magic = SENTINEL;
    r.entry = bounds[i][0];
    r.join = bounds[i][1];
    (void)qa_qvm_interpreter_bind_invocation(v.interp, 0, (qa_qvm_function_hook){&r, bounded_region_hook});
    CHECK(invoke_throws(&v, "QVM region"));
    CHECK(vm_idle(&v));
    vm_close(&v);
  }
  {
    vm v;
    vm_open(&v, region_source, 13, 4096, no_trap, NULL);
    region_ctx r;
    memset(&r, 0, sizeof r);
    r.magic = SENTINEL;
    (void)qa_qvm_interpreter_bind_invocation(v.interp, 0, (qa_qvm_function_hook){&r, sticky_hook});
    CHECK(invoke_throws(&v, "local is outside"));
    CHECK(r.caught);
    CHECK(vm_idle(&v));
    vm_close(&v);
  }
  /* original region cancellation preserves the caller and skips its source continuation */
  {
    vm v;
    vm_open(&v, region_source, 13, 4096, no_trap, NULL);
    region_ctx r;
    memset(&r, 0, sizeof r);
    r.magic = SENTINEL;
    (void)qa_qvm_interpreter_bind_invocation(v.interp, 0, (qa_qvm_function_hook){&r, region_cancel_hook});
    CHECK(invoke0(&v) == 0);
    CHECK(!r.completed);
    CHECK(vm_idle(&v));
    vm_close(&v);
  }
  /* standalone original region consumes only explicit live-ins */
  {
    vm v;
    vm_open(&v, region_source, 13, 4096, no_trap, NULL);
    region_ctx r;
    memset(&r, 0, sizeof r);
    r.magic = SENTINEL;
    qa_qvm_unbind remove = qa_qvm_interpreter_bind_invocation(v.interp, 0, (qa_qvm_function_hook){&r, evaluate_hook});
    CHECK(invoke0(&v) == 22);
    CHECK(vm_idle(&v));
    remove.call(remove.ctx);
    region_ctx empty = r;
    empty.mode = 1;
    (void)qa_qvm_interpreter_bind_invocation(v.interp, 0, (qa_qvm_function_hook){&empty, evaluate_hook});
    CHECK(invoke_throws(&v, "undeclared source local 24"));
    CHECK(vm_idle(&v));
    vm_close(&v);
  }
  /* read-only original regions reject writes outside their own frame */
  {
    const op mutating[] = {OA(ENTER, 32), OA(CONST, 64), OA(CONST, 12), O(STORE4), OA(CONST, 0), OA(LEAVE, 32)};
    vm v;
    vm_open(&v, mutating, 6, 4096, no_trap, NULL);
    qa_qvm_read_only_evaluation evaluation;
    memset(&evaluation, 0, sizeof evaluation);
    evaluation.region = (qa_qvm_region_evaluation){1, 4, NULL, 0, false, 0};
    read_only_job job = {v.interp, &evaluation};
    CHECK(throws_with(read_only_body, &job, "cannot write outside"));
    CHECK(get_i32(vm_bytes(&v), 64) == 0);
    vm_close(&v);
  }
}


/* ---- live callback resolver, observers and explicit hooks ---- */

typedef struct resolver_ctx {
  uint32_t magic;
  int resolved, observed;
  uint8_t *memory;
} resolver_ctx;

static double plus_one_hook(void *ctx, qa_qvm_function_call *call) {
  CHECK(((resolver_ctx *)ctx)->magic == SENTINEL);
  return call->proceed(call->ctx) + 1;
}
static double hundred_hook(void *ctx, qa_qvm_function_call *call) { (void)ctx; (void)call; return 100; }
static bool resolve_hook(void *ctx, double entry, double pointer, const qa_qvmx_view *words, qa_qvm_function_hook *out) {
  resolver_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  CHECK(words->length >= 4);
  r->resolved++;
  if (pointer == 64 && get_i32(r->memory, 64) == entry) {
    *out = (qa_qvm_function_hook){r, plus_one_hook};
    return true;
  }
  return false;
}
static void count_observer(void *ctx, qa_qvm_function_observation *observation) {
  (void)observation;
  resolver_ctx *r = ctx;
  CHECK(r->magic == SENTINEL);
  r->observed++;
}

static void test_resolver(void) {
  const op ops[] = {
      OA(ENTER, 16), OA(CONST, 64), OA(CONST, 19), O(STORE4),
      OA(CONST, 64), OA(ARG, 8), OA(CONST, 64), O(LOAD4), O(CALL),
      OA(CONST, 64), OA(CONST, 22), O(STORE4),
      OA(CONST, 64), OA(ARG, 8), OA(CONST, 64), O(LOAD4), O(CALL),
      O(ADD), OA(LEAVE, 16),
      OA(ENTER, 0), OA(CONST, 7), OA(LEAVE, 0),
      OA(ENTER, 0), OA(CONST, 11), OA(LEAVE, 0),
  };
  vm v;
  vm_open(&v, ops, 25, 4096, no_trap, "compiled");
  resolver_ctx r = {SENTINEL, 0, 0, vm_bytes(&v)};
  qa_qvm_unbind remove = qa_qvm_interpreter_bind_function_resolver(v.interp, (qa_qvm_function_resolver){&r, resolve_hook});
  (void)qa_qvm_interpreter_observe_function(v.interp, 19, (qa_qvm_function_observer){&r, count_observer});
  (void)qa_qvm_interpreter_observe_function(v.interp, 22, (qa_qvm_function_observer){&r, count_observer});
  CHECK(invoke0(&v) == 20);
  CHECK(r.resolved == 2 && r.observed == 2);
  qa_qvm_unbind explicit_hook = qa_qvm_interpreter_bind_function(v.interp, 19, (qa_qvm_function_hook){&r, hundred_hook});
  CHECK(invoke0(&v) == 112);
  CHECK(r.resolved == 3 && r.observed == 4);
  double args[10] = {64};
  CHECK(qa_qvm_interpreter_invoke(v.interp, args, 10, 19, NULL) == 7);
  CHECK(r.resolved == 3 && r.observed == 4);
  explicit_hook.call(explicit_hook.ctx);
  remove.call(remove.ctx);
  remove.call(remove.ctx);
  CHECK(invoke0(&v) == 18);
  CHECK(r.resolved == 3 && r.observed == 6);
  vm_close(&v);
}

/* ---- source call hooks ---- */

/* The caller keeps an operand and passes a pointer to its local alongside a value. */
static const op hook_source[] = {
    OA(ENTER, 32), OA(LOCAL, 24), OA(CONST, 7), O(STORE4),
    OA(CONST, 5), OA(LOCAL, 24), OA(ARG, 8), OA(CONST, 9), OA(ARG, 12),
    OA(CONST, 16), O(CALL), O(ADD),
    OA(LOCAL, 24), O(LOAD4), O(ADD), OA(LEAVE, 32),
    OA(ENTER, 16), OA(LOCAL, 24), O(LOAD4), O(LOAD4),
    OA(LOCAL, 28), O(LOAD4), O(ADD),
    OA(CONST, 27), O(CALL), O(ADD), OA(LEAVE, 16),
    OA(ENTER, 8), OA(CONST, 3), OA(LEAVE, 8),
};
#define HOOK_SOURCE_COUNT 30

static const op cancellable_source[] = {
    OA(ENTER, 16), OA(CONST, 5), OA(CONST, 9), O(CALL), O(ADD),
    OA(CONST, 21), O(CALL), O(ADD), OA(LEAVE, 16),
    OA(ENTER, 16), OA(CONST, 100), OA(CONST, 15), O(CALL), O(ADD), OA(LEAVE, 16),
    OA(ENTER, 16), OA(CONST, 200), OA(CONST, -1), O(CALL), O(ADD), OA(LEAVE, 16),
    OA(ENTER, 0), OA(CONST, 7), OA(LEAVE, 0),
};
#define CANCELLABLE_COUNT 24

typedef struct hook_ctx {
  uint32_t magic;
  int mode, entered, entry, target;
  bool reentry, cleanup, caught;
  char order[16][16];
  int order_count;
  qa_qvm_cancellation_scope scope;
  bool has_scope;
  struct hook_ctx *other;
  qa_qvm_function_call *ancestor;
  vm *peer;
  double seen[8];
  int seen_count;
} hook_ctx;

static void push_order(hook_ctx *h, const char *text) {
  if (h->order_count < 16) snprintf(h->order[h->order_count++], 16, "%s", text);
}

static double cancel_trap(void *ctx, qa_qvm_syscall *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  if (h->mode == 1) qa_range_error("source failed");
  CHECK(h->has_scope);
  call->cancel_function(call->ctx, h->scope);
  return 0;
}

static double direct_cancel_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  h->entered++;
  h->scope = call->cancellation_scope(call->ctx);
  h->has_scope = true;
  return call->proceed(call->ctx);
}

static void test_direct_cancellation(void) {
  for (int s = 0; s < 2; s++)
    for (int asynchronous = 0; asynchronous < 2; asynchronous++)
      for (int direct = 0; direct < 2; direct++) {
        hook_ctx h;
        memset(&h, 0, sizeof h);
        h.magic = SENTINEL;
        vm v;
        vm_open(&v, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){&h, cancel_trap}, semantics_names[s]);
        (void)qa_qvm_interpreter_bind_invocation(v.interp, 9, (qa_qvm_function_hook){&h, direct_cancel_hook});
        double entry = direct ? 9 : 0;
        double result = asynchronous ? qa_qvm_interpreter_invoke_async(v.interp, zero_args, 10, entry)
                                     : qa_qvm_interpreter_invoke(v.interp, zero_args, 10, entry, NULL);
        CHECK(result == (direct ? 0 : 12));
        CHECK(h.entered == 1);
        CHECK(qa_qvm_interpreter_invoke(v.interp, zero_args, 10, 21, NULL) == 7);
        CHECK(vm_idle(&v));
        vm_close(&v);
      }
}

/* try { proceed or reenter } catch { 999 } finally { leave } */
typedef struct nested_job { qa_qvm_function_call *call; hook_ctx *h; double result; } nested_job;
static void nested_body(void *ctx) {
  nested_job *job = ctx;
  if (job->h->reentry && job->h->entry == 15)
    job->result = job->call->invoke(job->call->ctx, zero_args, 10, true, 15, NULL);
  else job->result = job->call->proceed(job->call->ctx);
}
static double exact_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *self = ctx;
  hook_ctx *h = self->other;
  CHECK(self->magic == SENTINEL && h->magic == SENTINEL);
  if (self->entry == h->target) {
    h->scope = call->cancellation_scope(call->ctx);
    h->has_scope = true;
  }
  char text[16];
  snprintf(text, sizeof text, "enter:%d", self->entry);
  push_order(h, text);
  h->entry = self->entry;
  nested_job job = {call, h, 0};
  char message[512];
  double result = qa_range_error_catch(nested_body, &job, message, sizeof message) ? job.result : 999;
  snprintf(text, sizeof text, "leave:%d", self->entry);
  push_order(h, text);
  return result;
}
static void sibling_observer(void *ctx, qa_qvm_function_observation *observation) {
  (void)observation;
  push_order(ctx, "sibling");
}

static void test_exact_cancellation(void) {
  for (int s = 0; s < 2; s++)
    for (int t = 0; t < 2; t++)
      for (int reentry = 0; reentry < 2; reentry++) {
        hook_ctx h;
        memset(&h, 0, sizeof h);
        h.magic = SENTINEL;
        h.target = t == 0 ? 9 : 15;
        h.reentry = reentry;
        hook_ctx nine = {.magic = SENTINEL, .entry = 9, .other = &h};
        hook_ctx fifteen = {.magic = SENTINEL, .entry = 15, .other = &h};
        vm v;
        vm_open(&v, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){&h, cancel_trap}, semantics_names[s]);
        (void)qa_qvm_interpreter_bind_function(v.interp, 9, (qa_qvm_function_hook){&nine, exact_hook});
        (void)qa_qvm_interpreter_bind_function(v.interp, 15, (qa_qvm_function_hook){&fifteen, exact_hook});
        (void)qa_qvm_interpreter_observe_function(v.interp, 21, (qa_qvm_function_observer){&h, sibling_observer});
        CHECK(invoke0(&v) == (h.target == 9 ? 12 : 112));
        CHECK(h.order_count == 5 && strcmp(h.order[0], "enter:9") == 0 && strcmp(h.order[1], "enter:15") == 0 &&
              strcmp(h.order[2], "leave:15") == 0 && strcmp(h.order[3], "leave:9") == 0 && strcmp(h.order[4], "sibling") == 0);
        CHECK(vm_idle(&v));
        vm_close(&v);
      }
}

/* cancellation cannot hide source faults or cleanup failures */
typedef struct proceed_job { qa_qvm_function_call *call; double result; } proceed_job;
static void proceed_body(void *ctx) {
  proceed_job *job = ctx;
  job->result = job->call->proceed(job->call->ctx);
}
static double catching_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  h->scope = call->cancellation_scope(call->ctx);
  h->has_scope = true;
  proceed_job job = {call, 0};
  char message[512];
  return qa_range_error_catch(proceed_body, &job, message, sizeof message) ? job.result : 999;
}
static double cleanup_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  proceed_job job = {call, 0};
  char message[512];
  bool ok = qa_range_error_catch(proceed_body, &job, message, sizeof message);
  if (h->cleanup) qa_range_error("cleanup failed");
  if (!ok) qa_range_error("%s", message);
  return job.result;
}

static void test_faults(void) {
  for (int cleanup = 0; cleanup < 2; cleanup++) {
    hook_ctx h;
    memset(&h, 0, sizeof h);
    h.magic = SENTINEL;
    h.mode = cleanup ? 0 : 1;
    h.cleanup = cleanup;
    vm v;
    vm_open(&v, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){&h, cancel_trap}, "compiled");
    (void)qa_qvm_interpreter_bind_function(v.interp, 9, (qa_qvm_function_hook){&h, catching_hook});
    (void)qa_qvm_interpreter_bind_function(v.interp, 15, (qa_qvm_function_hook){&h, cleanup_hook});
    CHECK(invoke_throws(&v, cleanup ? "cleanup failed" : "source failed"));
    CHECK(vm_idle(&v));
    vm_close(&v);
  }
}

/* a cancellation capability opens before proceeding and expires with its exact call */
typedef struct cancel_job { qa_qvm_function_call *call; qa_qvm_cancellation_scope scope; } cancel_job;
static void cancel_body(void *ctx) {
  cancel_job *job = ctx;
  job->call->cancel_function(job->call->ctx, job->scope);
}
static void open_scope_body(void *ctx) {
  cancel_job *job = ctx;
  job->scope = job->call->cancellation_scope(job->call->ctx);
}
static double capability_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  char message[512];
  if (h->mode == 0) {
    h->scope = call->cancellation_scope(call->ctx);
    h->has_scope = true;
    cancel_job job = {call, h->scope};
    return qa_range_error_catch(cancel_body, &job, message, sizeof message) ? 0 : 999;
  }
  if (h->mode == 1) {
    cancel_job job = {call, h->scope};
    return qa_range_error_catch(cancel_body, &job, message, sizeof message) ? 0 : 999;
  }
  if (h->mode == 2) {
    double result = call->proceed(call->ctx);
    cancel_job job = {call, {NULL}};
    (void)qa_range_error_catch(open_scope_body, &job, message, sizeof message);
    return result;
  }
  qa_qvm_cancellation_scope scope = call->cancellation_scope(call->ctx);
  cancel_job job = {call, scope};
  (void)qa_range_error_catch(cancel_body, &job, message, sizeof message);
  return qa_range_error_catch(cancel_body, &job, message, sizeof message) ? 0 : 999;
}

static double foreign_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  cancel_job job = {call, h->other->scope};
  char message[512];
  return qa_range_error_catch(cancel_body, &job, message, sizeof message) ? 0 : 999;
}
static double first_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  h->scope = call->cancellation_scope(call->ctx);
  hook_ctx foreign = {.magic = SENTINEL, .other = h};
  qa_qvm_unbind remove = qa_qvm_interpreter_bind_function(h->peer->interp, 9, (qa_qvm_function_hook){&foreign, foreign_hook});
  h->caught = invoke_throws(h->peer, "another interpreter");
  remove.call(remove.ctx);
  CHECK(vm_idle(h->peer));
  return call->proceed(call->ctx);
}

static double ancestor_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  h->ancestor = call;
  (void)call->cancellation_scope(call->ctx);
  proceed_job job = {call, 0};
  char message[512];
  return qa_range_error_catch(proceed_body, &job, message, sizeof message) ? job.result : 999;
}
static double descendant_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  qa_qvm_cancellation_scope scope = call->cancellation_scope(call->ctx);
  cancel_job job = {h->ancestor, scope};
  char message[512];
  return qa_range_error_catch(cancel_body, &job, message, sizeof message) ? 0 : 999;
}

static void test_capabilities(void) {
  hook_ctx h;
  memset(&h, 0, sizeof h);
  h.magic = SENTINEL;
  vm v;
  vm_open(&v, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){NULL, zero_trap}, "compiled");
  qa_qvm_unbind remove = qa_qvm_interpreter_bind_function(v.interp, 9, (qa_qvm_function_hook){&h, capability_hook});
  CHECK(invoke0(&v) == 12);
  remove.call(remove.ctx);
  h.mode = 1;
  qa_qvm_unbind stale = qa_qvm_interpreter_bind_function(v.interp, 9, (qa_qvm_function_hook){&h, capability_hook});
  CHECK(invoke_throws(&v, "has expired"));
  stale.call(stale.ctx);
  hook_ctx late = h;
  late.mode = 2;
  qa_qvm_unbind late_remove = qa_qvm_interpreter_bind_function(v.interp, 9, (qa_qvm_function_hook){&late, capability_hook});
  CHECK(invoke_throws(&v, "before proceeding"));
  late_remove.call(late_remove.ctx);
  hook_ctx reused = h;
  reused.mode = 3;
  (void)qa_qvm_interpreter_bind_function(v.interp, 9, (qa_qvm_function_hook){&reused, capability_hook});
  CHECK(invoke_throws(&v, "already been used"));
  CHECK(vm_idle(&v));
  vm_close(&v);

  /* foreign capabilities fail ordinarily even when their hook catches */
  vm first, second;
  vm_open(&first, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){NULL, zero_trap}, "compiled");
  vm_open(&second, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){NULL, zero_trap}, "compiled");
  hook_ctx owner;
  memset(&owner, 0, sizeof owner);
  owner.magic = SENTINEL;
  owner.peer = &second;
  (void)qa_qvm_interpreter_bind_function(first.interp, 9, (qa_qvm_function_hook){&owner, first_hook});
  CHECK(invoke0(&first) == 312);
  CHECK(owner.caught);
  CHECK(vm_idle(&first) && vm_idle(&second));
  vm_close(&first);
  vm_close(&second);

  /* an ancestor callback cannot cancel its descendant through an inactive call object */
  vm a;
  vm_open(&a, cancellable_source, CANCELLABLE_COUNT, 4096, (qa_qvm_interpreter_system_call){NULL, zero_trap}, "compiled");
  hook_ctx chain;
  memset(&chain, 0, sizeof chain);
  chain.magic = SENTINEL;
  (void)qa_qvm_interpreter_bind_function(a.interp, 9, (qa_qvm_function_hook){&chain, ancestor_hook});
  (void)qa_qvm_interpreter_bind_function(a.interp, 15, (qa_qvm_function_hook){&chain, descendant_hook});
  CHECK(invoke_throws(&a, "active syscall or function hook"));
  CHECK(vm_idle(&a));
  vm_close(&a);
}

/* hooks preserve caller operands, local pointers, argument writes and nested guest calls */
static double inner_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  push_order(h, "inner");
  return call->proceed(call->ctx) + 1;
}
static double outer_hook(void *ctx, qa_qvm_function_call *call) {
  hook_ctx *h = ctx;
  CHECK(h->magic == SENTINEL);
  push_order(h, "before");
  CHECK(call->instruction_index == 16);
  int32_t address = qa_qvmx_view_i32(&call->words, 0);
  CHECK(get_i32(call->memory, (size_t)address) == 7);
  put_i32(call->memory, (size_t)address, 11);
  qa_qvmx_view_set_i32(&call->words, 4, 13);
  CHECK(call->invoke(call->ctx, zero_args, 10, true, 27, NULL) == 3);
  double value = call->proceed(call->ctx);
  push_order(h, "after");
  return value * 2;
}

static void test_operands(void) {
  hook_ctx h;
  memset(&h, 0, sizeof h);
  h.magic = SENTINEL;
  vm v;
  vm_open(&v, hook_source, HOOK_SOURCE_COUNT, 4096, no_trap, NULL);
  CHECK(invoke0(&v) == 31);
  qa_qvm_unbind inner = qa_qvm_interpreter_bind_function(v.interp, 27, (qa_qvm_function_hook){&h, inner_hook});
  qa_qvm_unbind outer = qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){&h, outer_hook});
  CHECK(invoke0(&v) == 72);
  CHECK(h.order_count == 3 && strcmp(h.order[0], "before") == 0 && strcmp(h.order[1], "inner") == 0 &&
        strcmp(h.order[2], "after") == 0);
  CHECK(vm_idle(&v));
  outer.call(outer.ctx);
  inner.call(inner.ctx);
  CHECK(invoke0(&v) == 31);
  vm_close(&v);
}

/* independent entry observers coexist with replacement and original execution */
typedef struct observe_ctx { uint32_t magic; double seen[8]; int count; bool argument_rejected; } observe_ctx;
typedef struct argument_job { qa_qvm_function_observation *observation; double index; } argument_job;
static void argument_body(void *ctx) {
  argument_job *job = ctx;
  (void)job->observation->argument(job->observation->ctx, job->index);
}
static void first_observer(void *ctx, qa_qvm_function_observation *observation) {
  observe_ctx *o = ctx;
  CHECK(o->magic == SENTINEL);
  o->seen[o->count++] = observation->argument(observation->ctx, 1);
}
static void second_observer(void *ctx, qa_qvm_function_observation *observation) {
  observe_ctx *o = ctx;
  CHECK(o->magic == SENTINEL);
  o->seen[o->count++] = observation->invoke(observation->ctx, zero_args, 10, true, 27, NULL);
}
static double plus_two_hook(void *ctx, qa_qvm_function_call *call) { (void)ctx; return call->proceed(call->ctx) + 2; }

static void test_observers(void) {
  observe_ctx o;
  memset(&o, 0, sizeof o);
  o.magic = SENTINEL;
  vm v;
  vm_open(&v, hook_source, HOOK_SOURCE_COUNT, 4096, no_trap, "compiled");
  qa_qvm_unbind first = qa_qvm_interpreter_observe_function(v.interp, 16, (qa_qvm_function_observer){&o, first_observer});
  qa_qvm_unbind second = qa_qvm_interpreter_observe_function(v.interp, 16, (qa_qvm_function_observer){&o, second_observer});
  qa_qvm_unbind hook = qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){&o, plus_two_hook});
  CHECK(invoke0(&v) == 33);
  CHECK(o.count == 2 && o.seen[0] == 9 && o.seen[1] == 3);
  hook.call(hook.ctx);
  first.call(first.ctx);
  first.call(first.ctx);
  o.count = 0;
  CHECK(qa_qvm_interpreter_invoke_async(v.interp, zero_args, 10, 0) == 31);
  CHECK(o.count == 1 && o.seen[0] == 3);
  second.call(second.ctx);
  CHECK(invoke0(&v) == 31);
  CHECK(vm_idle(&v));
  vm_close(&v);
}

/* replacement returns a signed source word and removal cannot remove a newer binding */
static double minus_ten_hook(void *ctx, qa_qvm_function_call *call) { (void)ctx; (void)call; return -10; }
static double unsigned_hook(void *ctx, qa_qvm_function_call *call) { (void)ctx; (void)call; return 2147483648.0; }
typedef struct bind_job { qa_qvm_interpreter *interp; double index; } bind_job;
static void bind_body(void *ctx) {
  bind_job *job = ctx;
  (void)qa_qvm_interpreter_bind_function(job->interp, job->index, (qa_qvm_function_hook){NULL, minus_ten_hook});
}

static void test_replacement(void) {
  vm v;
  vm_open(&v, hook_source, HOOK_SOURCE_COUNT, 4096, no_trap, NULL);
  qa_qvm_unbind remove = qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){NULL, minus_ten_hook});
  CHECK(invoke0(&v) == 2);
  bind_job again = {v.interp, 16};
  CHECK(throws_with(bind_body, &again, "already has a hook"));
  remove.call(remove.ctx);
  qa_qvm_unbind replacement = qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){NULL, minus_ten_hook});
  remove.call(remove.ctx);
  CHECK(invoke0(&v) == 2);
  replacement.call(replacement.ctx);
  qa_qvm_unbind invalid = qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){NULL, unsigned_hook});
  CHECK(invoke_throws(&v, "signed 32-bit integer"));
  CHECK(vm_idle(&v));
  invalid.call(invalid.ctx);
  CHECK(invoke0(&v) == 31);
  bind_job not_entry = {v.interp, 17};
  CHECK(throws_with(bind_body, &not_entry, "function entry"));
  bind_job outside = {v.interp, 1000};
  CHECK(throws_with(bind_body, &outside, "invalid QVM instruction index"));
  vm_close(&v);
}

/* original bodies can call host services which synchronously reenter the guest */
static double reenter_trap(void *ctx, qa_qvm_syscall *call) {
  CHECK(*(uint32_t *)ctx == SENTINEL);
  return call->invoke(call->ctx, zero_args, 10, true, 8, NULL);
}
static double times_three_hook(void *ctx, qa_qvm_function_call *call) { (void)ctx; return call->proceed(call->ctx) * 3; }
static double async_in_sync_hook(void *ctx, qa_qvm_function_call *call) { (void)ctx; return call->proceed_async(call->ctx); }
static double twice_hook(void *ctx, qa_qvm_function_call *call) {
  (void)ctx;
  (void)call->proceed(call->ctx);
  return call->proceed(call->ctx);
}
static double failing_trap(void *ctx, qa_qvm_syscall *call) { (void)ctx; (void)call; qa_range_error("original failed"); }
static double swallow_hook(void *ctx, qa_qvm_function_call *call) {
  (void)ctx;
  proceed_job job = {call, 0};
  char message[512];
  return qa_range_error_catch(proceed_body, &job, message, sizeof message) ? job.result : 9;
}

static void test_sync_rules(void) {
  const op nested_ops[] = {OA(ENTER, 16), OA(CONST, 4), O(CALL), OA(LEAVE, 16),
                           OA(ENTER, 16), OA(CONST, -1), O(CALL), OA(LEAVE, 16),
                           OA(ENTER, 0), OA(CONST, 7), OA(LEAVE, 0)};
  uint32_t magic = SENTINEL;
  vm nested;
  vm_open(&nested, nested_ops, 11, 4096, (qa_qvm_interpreter_system_call){&magic, reenter_trap}, NULL);
  (void)qa_qvm_interpreter_bind_function(nested.interp, 4, (qa_qvm_function_hook){NULL, times_three_hook});
  CHECK(invoke0(&nested) == 21);
  CHECK(vm_idle(&nested));
  vm_close(&nested);

  vm v;
  vm_open(&v, hook_source, HOOK_SOURCE_COUNT, 4096, no_trap, NULL);
  qa_qvm_unbind remove = qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){NULL, async_in_sync_hook});
  CHECK(invoke_throws(&v, "asynchronous syscall"));
  remove.call(remove.ctx);
  (void)qa_qvm_interpreter_bind_function(v.interp, 16, (qa_qvm_function_hook){NULL, twice_hook});
  CHECK(invoke_throws(&v, "only run once"));
  CHECK(vm_idle(&v));
  vm_close(&v);

  const op trapping[] = {OA(ENTER, 16), OA(CONST, 4), O(CALL), OA(LEAVE, 16),
                         OA(ENTER, 16), OA(CONST, -1), O(CALL), OA(CONST, 2), O(ADD), OA(LEAVE, 16)};
  vm failing;
  vm_open(&failing, trapping, 10, 4096, (qa_qvm_interpreter_system_call){NULL, failing_trap}, NULL);
  (void)qa_qvm_interpreter_bind_function(failing.interp, 4, (qa_qvm_function_hook){NULL, swallow_hook});
  CHECK(invoke_throws(&failing, "original failed"));
  CHECK(vm_idle(&failing));
  vm_close(&failing);
}

/* ---- branches ---- */

static const op conditional_source[] = {
    OA(ENTER, 16), OA(CONST, 6), O(CALL), OA(CONST, 20), O(ADD), OA(LEAVE, 16),
    OA(ENTER, 16), OA(CONST, 15), O(CALL), OA(CONST, 1), OA(EQ, 13),
    OA(CONST, 3), OA(LEAVE, 16), OA(CONST, 7), OA(LEAVE, 16),
    OA(ENTER, 16), OA(CONST, 64), OA(CONST, 64), O(LOAD4), OA(CONST, 1), O(ADD), O(STORE4),
    OA(CONST, -1), O(CALL), O(POP), OA(CONST, 1), OA(LEAVE, 16),
};
#define CONDITIONAL_COUNT 27

typedef struct branch_ctx {
  uint32_t magic;
  bool recurse, cancel;
  double nested;
  int depth, observed_count;
  int observed_depth[4];
  bool observed_taken[4];
  qa_qvm_cancellation_scope scope;
  const char *request;
} branch_ctx;

typedef struct branch_binding_ctx { branch_ctx *b; int depth; } branch_binding_ctx;

static double branch_trap(void *ctx, qa_qvm_syscall *call) {
  branch_ctx *b = ctx;
  CHECK(b->magic == SENTINEL);
  if (!b->recurse) return 0;
  b->recurse = false;
  b->nested = call->invoke(call->ctx, zero_args, 10, true, 6, NULL);
  return 0;
}
static bool depth_decide(void *ctx, bool taken, qa_qvm_syscall *control) {
  (void)control;
  branch_binding_ctx *d = ctx;
  branch_ctx *b = d->b;
  CHECK(b->magic == SENTINEL);
  b->observed_depth[b->observed_count] = d->depth;
  b->observed_taken[b->observed_count++] = taken;
  return d->depth > 1;
}
static double branch_hook(void *ctx, qa_qvm_function_call *call) {
  branch_ctx *b = ctx;
  branch_binding_ctx own = {b, ++b->depth};
  qa_qvm_branch_binding binding = {10, &own, depth_decide};
  call->branches(call->ctx, &binding, 1);
  proceed_job job = {call, 0};
  char message[512];
  bool ok = qa_range_error_catch(proceed_body, &job, message, sizeof message);
  b->depth--;
  if (!ok) qa_range_error("%s", message);
  return job.result;
}

typedef struct control_cancel_job { qa_qvm_syscall *control; qa_qvm_cancellation_scope scope; } control_cancel_job;
static void control_cancel_body(void *ctx) {
  control_cancel_job *job = ctx;
  job->control->cancel_function(job->control->ctx, job->scope);
}
static bool cancel_decide(void *ctx, bool taken, qa_qvm_syscall *control) {
  (void)taken;
  branch_ctx *b = ctx;
  if (!b->cancel) qa_range_error("unowned decision failure");
  control_cancel_job job = {control, b->scope};
  char message[512];
  (void)qa_range_error_catch(control_cancel_body, &job, message, sizeof message);
  return false;
}
static double cancel_branch_hook(void *ctx, qa_qvm_function_call *call) {
  branch_ctx *b = ctx;
  b->scope = call->cancellation_scope(call->ctx);
  qa_qvm_branch_binding binding = {10, b, cancel_decide};
  call->branches(call->ctx, &binding, 1);
  return call->proceed(call->ctx);
}

static bool taken_decide(void *ctx, bool taken, qa_qvm_syscall *control) { (void)ctx; (void)control; return taken; }
static double registration_hook(void *ctx, qa_qvm_function_call *call) {
  branch_ctx *b = ctx;
  if (strcmp(b->request, "late") == 0) (void)call->proceed(call->ctx);
  if (strcmp(b->request, "twice") == 0) call->branches(call->ctx, NULL, 0);
  double index = strcmp(b->request, "outside") == 0 ? 15 : strcmp(b->request, "opcode") == 0 ? 9 : 10;
  qa_qvm_branch_binding bindings[2] = {{index, b, taken_decide}, {index, b, taken_decide}};
  call->branches(call->ctx, bindings, strcmp(b->request, "duplicate") == 0 ? 2 : 1);
  return 0;
}

static void test_branches(void) {
  for (int s = 0; s < 2; s++) {
    for (int asynchronous = 0; asynchronous < 2; asynchronous++) {
      branch_ctx b;
      memset(&b, 0, sizeof b);
      b.magic = SENTINEL;
      b.recurse = true;
      vm v;
      vm_open(&v, conditional_source, CONDITIONAL_COUNT, 4096, (qa_qvm_interpreter_system_call){&b, branch_trap}, semantics_names[s]);
      qa_qvm_unbind remove = qa_qvm_interpreter_bind_invocation(v.interp, 6, (qa_qvm_function_hook){&b, branch_hook});
      double result = asynchronous ? qa_qvm_interpreter_invoke_async(v.interp, zero_args, 10, 0) : invoke0(&v);
      CHECK(result == 23);
      CHECK(b.nested == 7);
      CHECK(b.observed_count == 2 && b.observed_depth[0] == 2 && b.observed_taken[0] &&
            b.observed_depth[1] == 1 && b.observed_taken[1]);
      CHECK(get_i32(vm_bytes(&v), 64) == 2);
      remove.call(remove.ctx);
      CHECK(invoke0(&v) == 27);
      CHECK(b.observed_count == 2);
      CHECK(vm_idle(&v));
      vm_close(&v);
    }
    for (int cancel = 0; cancel < 2; cancel++) {
      branch_ctx b;
      memset(&b, 0, sizeof b);
      b.magic = SENTINEL;
      b.cancel = cancel;
      vm v;
      vm_open(&v, conditional_source, CONDITIONAL_COUNT, 4096, (qa_qvm_interpreter_system_call){NULL, zero_trap}, semantics_names[s]);
      (void)qa_qvm_interpreter_bind_invocation(v.interp, 6, (qa_qvm_function_hook){&b, cancel_branch_hook});
      if (cancel) CHECK(invoke0(&v) == 20);
      else CHECK(invoke_throws(&v, "unowned decision failure"));
      CHECK(get_i32(vm_bytes(&v), 64) == 1);
      CHECK(!qa_qvm_interpreter_get_is_active(v.interp));
      vm_close(&v);
    }
  }
  const char *requests[] = {"outside", "opcode", "duplicate", "twice", "late"};
  for (int i = 0; i < 5; i++) {
    branch_ctx b;
    memset(&b, 0, sizeof b);
    b.magic = SENTINEL;
    b.request = requests[i];
    vm v;
    vm_open(&v, conditional_source, CONDITIONAL_COUNT, 4096, (qa_qvm_interpreter_system_call){NULL, zero_trap}, NULL);
    (void)qa_qvm_interpreter_bind_invocation(v.interp, 6, (qa_qvm_function_hook){&b, registration_hook});
    CHECK(invoke_throws(&v, "QVM branch"));
    CHECK(vm_idle(&v));
    vm_close(&v);
  }
}

/* ---- counters and source evaluations ---- */

typedef struct counter_ctx { qa_qvm_interpreter *interp; const qa_qvm_read_only_evaluation *evaluation; } counter_ctx;
static double counter_execute(void *ctx) {
  counter_ctx *c = ctx;
  return qa_qvm_interpreter_invoke(c->interp, zero_args, 10, 0, c->evaluation);
}
typedef struct counter_job { qa_qvm_interpreter *interp; const qa_qvm_evaluation_stack *stack; double result; bool region; } counter_job;
static void counter_body(void *ctx) {
  counter_job *job = ctx;
  if (job->region) {
    qa_qvm_read_only_evaluation evaluation;
    memset(&evaluation, 0, sizeof evaluation);
    evaluation.region = (qa_qvm_region_evaluation){1, 4, NULL, 0, true, 24};
    job->result = qa_qvm_interpreter_invoke(job->interp, zero_args, 10, 0, &evaluation);
    return;
  }
  counter_ctx c = {job->interp, NULL};
  double functions[1] = {0};
  job->result = qa_qvm_interpreter_evaluate_counter(job->interp, 64, 0, functions, 1, counter_execute, &c, job->stack);
}

static void test_counters(void) {
  const op counter_ops[] = {OA(ENTER, 16), OA(CONST, 64), OA(CONST, 64), O(LOAD4),
                            OA(CONST, 5), O(ADD), O(STORE4), OA(CONST, 0), OA(LEAVE, 16)};
  vm v;
  vm_open(&v, counter_ops, 9, 2049, no_trap, NULL);
  put_i32(vm_bytes(&v), 64, 13);
  counter_job job = {v.interp, NULL, 0, false};
  counter_body(&job);
  CHECK(job.result == 5);
  CHECK(get_i32(vm_bytes(&v), 64) == 13);
  CHECK(invoke0(&v) == 0 && get_i32(vm_bytes(&v), 64) == 18);
  CHECK(vm_idle(&v));
  vm_close(&v);
  const int32_t addresses[2] = {88, 65};
  const int32_t opcodes[2] = {QA_QVM_OP_STORE4, QA_QVM_OP_STORE1};
  for (int i = 0; i < 2; i++) {
    const op ops[] = {OA(ENTER, 16), OA(CONST, addresses[i]), OA(CONST, 7), {opcodes[i], false, 0}, OA(CONST, 0), OA(LEAVE, 16)};
    vm w;
    vm_open(&w, ops, 6, 2049, no_trap, NULL);
    counter_job write = {w.interp, NULL, 0, false};
    CHECK(throws_with(counter_body, &write, "unrelated source write"));
    CHECK(vm_bytes(&w)[addresses[i]] == 0);
    CHECK(vm_idle(&w));
    (void)invoke0(&w);
    CHECK(vm_bytes(&w)[addresses[i]] == 7);
    vm_close(&w);
  }
  /* source evaluations reject frames entering data and accept a declared original stack */
  const op deep[] = {OA(ENTER, 2048), OA(LOCAL, 24), OA(CONST, 7), O(STORE4), OA(CONST, 0), OA(LEAVE, 2048)};
  for (int region = 0; region < 2; region++) {
    vm d;
    vm_open(&d, deep, 6, 2049, no_trap, NULL);
    static uint8_t before[2052];
    memcpy(before, vm_bytes(&d), 2052);
    counter_job run = {d.interp, NULL, 0, region};
    CHECK(throws_with(counter_body, &run, "overlap source data"));
    CHECK(memcmp(before, vm_bytes(&d), 2052) == 0);
    CHECK(vm_idle(&d));
    vm_close(&d);
  }
  const op shallow[] = {OA(ENTER, 16), OA(CONST, 64), OA(CONST, 9), O(STORE4), OA(CONST, 0), OA(LEAVE, 16)};
  vm s;
  vm_open(&s, shallow, 6, 4096, no_trap, NULL);
  static uint8_t before[4096];
  memcpy(before, vm_bytes(&s), vm_length(&s));
  counter_job plain = {s.interp, NULL, 0, false};
  CHECK(throws_with(counter_body, &plain, "overlap source data"));
  CHECK(memcmp(before, vm_bytes(&s), vm_length(&s)) == 0);
  qa_qvm_evaluation_stack stack = {2048, 4096};
  counter_job declared = {s.interp, &stack, 0, false};
  counter_body(&declared);
  CHECK(declared.result == 9);
  CHECK(vm_bytes(&s)[64] == 0);
  vm_close(&s);
}

/* ---- private calls use the original OP_ARG extent ---- */

typedef struct extent_ctx { uint32_t magic; int observed; double target; bool outside; } extent_ctx;
static void extent_observer(void *ctx, qa_qvm_function_observation *observation) {
  extent_ctx *e = ctx;
  CHECK(e->magic == SENTINEL);
  e->observed++;
  CHECK(observation->argument(observation->ctx, 61) == 62);
  argument_job job = {observation, 62};
  e->outside = throws_with(argument_body, &job, "outside source call");
}
typedef struct word_job { qa_qvmx_view *view; int64_t offset; } word_job;
static void word_body(void *ctx) {
  word_job *job = ctx;
  (void)qa_qvmx_view_i32(job->view, job->offset);
}
static double extent_hook(void *ctx, qa_qvm_function_call *call) {
  extent_ctx *e = ctx;
  CHECK(call->words.length == 248);
  word_job job = {&call->words, 248};
  CHECK(throws_with(word_body, &job, NULL));
  double args[62];
  for (int i = 0; i < 62; i++) args[i] = 2;
  CHECK(call->invoke(call->ctx, args, 62, true, e->target, NULL) == 4);
  CHECK(qa_qvmx_view_i32(&call->words, 61 * 4) == 62);
  return call->proceed(call->ctx);
}
static void small_observer(void *ctx, qa_qvm_function_observation *observation) {
  argument_job job = {observation, 2};
  ((extent_ctx *)ctx)->outside = throws_with(argument_body, &job, "outside source call");
}
static double small_hook(void *ctx, qa_qvm_function_call *call) {
  (void)ctx;
  CHECK(call->words.length == 8);
  return call->proceed(call->ctx);
}

static void test_argument_extent(void) {
  for (int s = 0; s < 2; s++) {
    static op ops[256];
    size_t count = 0;
    ops[count++] = (op)OA(ENTER, 256);
    for (int i = 0; i < 62; i++) {
      ops[count++] = (op)OA(CONST, i + 1);
      ops[count++] = (op)OA(ARG, 8 + i * 4);
    }
    int32_t target = (int32_t)count + 3;
    ops[count++] = (op)OA(CONST, target);
    ops[count++] = (op)O(CALL);
    ops[count++] = (op)OA(LEAVE, 256);
    ops[count++] = (op)OA(ENTER, 8);
    ops[count++] = (op)OA(LOCAL, 16);
    ops[count++] = (op)O(LOAD4);
    ops[count++] = (op)OA(LOCAL, 260);
    ops[count++] = (op)O(LOAD4);
    ops[count++] = (op)O(ADD);
    ops[count++] = (op)OA(LEAVE, 8);
    vm v;
    vm_open(&v, ops, count, 4096, no_trap, semantics_names[s]);
    extent_ctx e = {SENTINEL, 0, target, false};
    (void)qa_qvm_interpreter_observe_function(v.interp, target, (qa_qvm_function_observer){&e, extent_observer});
    (void)qa_qvm_interpreter_bind_function(v.interp, target, (qa_qvm_function_hook){&e, extent_hook});
    CHECK(invoke0(&v) == 63);
    CHECK(e.observed == 1 && e.outside);
    double args[63];
    for (int i = 0; i < 63; i++) args[i] = i + 1;
    CHECK(qa_qvm_interpreter_invoke_async(v.interp, args, 62, target) == 63);
    invoke_job too_many = {v.interp, args, 63, target, 0};
    CHECK(throws_with(invoke_body, &too_many, "OP_ARG"));
    invoke_job vm_main = {v.interp, args, 11, 0, 0};
    CHECK(throws_with(invoke_body, &vm_main, "vmMain"));
    CHECK(vm_idle(&v));
    vm_close(&v);

    const op small_ops[] = {OA(ENTER, 16), OA(CONST, 5), O(CALL), OA(LEAVE, 16),
                            O(IGNORE), OA(ENTER, 8), OA(CONST, 1), OA(LEAVE, 8)};
    vm small;
    vm_open(&small, small_ops, 8, 4096, no_trap, semantics_names[s]);
    extent_ctx tiny = {SENTINEL, 0, 5, false};
    (void)qa_qvm_interpreter_observe_function(small.interp, 5, (qa_qvm_function_observer){&tiny, small_observer});
    (void)qa_qvm_interpreter_bind_function(small.interp, 5, (qa_qvm_function_hook){&tiny, small_hook});
    CHECK(invoke0(&small) == 1);
    CHECK(tiny.outside);
    vm_close(&small);
  }
}

/* ---- QvmModule bindings ---- */

typedef struct module_fixture {
  uint8_t bytes[4096];
  size_t length;
  char digest[96];
  qa_resolved_qvm_artifact artifact;
  qa_qvm_module module;
} module_fixture;

static void module_open(module_fixture *f, const op *ops, size_t count, int32_t bss, const char *id) {
  f->length = build(f->bytes, ops, count, bss);
  char *digest = qa_digest_bytes(f->bytes, f->length);
  snprintf(f->digest, sizeof f->digest, "%s", digest);
  free(digest);
  qa_qvm_resolve_artifact_options options;
  memset(&options, 0, sizeof options);
  options.module = (qa_module_identity){id, "vm/qagame.qvm", f->digest, "test"};
  options.role = "qagame";
  options.bytes = f->bytes;
  options.length = f->length;
  CHECK(qa_qvm_resolve_artifact(options, &f->artifact));
  qa_qvm_module_options module_options;
  memset(&module_options, 0, sizeof module_options);
  module_options.artifact = &f->artifact;
  module_options.host = qa_qvm_reject_syscall_host;
  qa_qvm_module_init(&f->module, &module_options);
}
static void module_close(module_fixture *f) {
  qa_qvm_module_destroy(&f->module);
  qa_resolved_qvm_artifact_destroy(&f->artifact);
}
static qa_guest_callback_reference reference_of(module_fixture *f, double index) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = f->artifact.module;
  reference.instruction_index = index;
  return reference;
}

typedef struct module_hook_ctx { uint32_t magic; module_fixture *f; int entered; } module_hook_ctx;
static int32_t entered_hook(void *ctx, qa_qvm_module_function_call *call) {
  module_hook_ctx *m = ctx;
  CHECK(m->magic == SENTINEL);
  m->entered++;
  return call->proceed(call->call_ctx) + 1;
}
static int32_t nested_module_hook(void *ctx, qa_qvm_module_function_call *call) {
  module_hook_ctx *m = ctx;
  CHECK(m->magic == SENTINEL);
  return qa_qvm_module_call(&m->f->module, NULL, 0, 27) + call->proceed(call->call_ctx);
}
static int32_t read_only_module_hook(void *ctx, qa_qvm_module_function_call *call) {
  module_hook_ctx *m = ctx;
  m->entered++;
  qa_qvm_region_evaluation region = {1, 5, NULL, 0, true, 24};
  CHECK(qa_qvm_module_evaluate_region(&m->f->module, NULL, 0, 0, &region, NULL, 0, NULL) == 77);
  return call->proceed(call->call_ctx);
}
static int32_t counter_module_hook(void *ctx, qa_qvm_module_function_call *call) {
  module_hook_ctx *m = ctx;
  m->entered++;
  int64_t functions[1] = {0};
  CHECK(qa_qvm_module_evaluate_counter(&m->f->module, NULL, 0, 0, 64, functions, 1, NULL) == 5);
  return call->proceed(call->call_ctx);
}
typedef struct module_bind_job { module_fixture *f; qa_guest_callback_reference reference; } module_bind_job;
static void module_bind_body(void *ctx) {
  module_bind_job *job = ctx;
  (void)qa_qvm_module_bind_function(&job->f->module, &job->reference, entered_hook, NULL);
}
static void count_store(void *ctx, const qa_qvm_committed_write *event) { (void)event; ++*(int *)ctx; }

static void test_modules(void) {
  /* invocation bindings intercept direct module entry and source calls exactly once */
  {
    static module_fixture f;
    module_open(&f, hook_source, HOOK_SOURCE_COUNT, 4096, "q3:invocation-test");
    module_hook_ctx m = {SENTINEL, &f, 0};
    qa_guest_callback_reference reference = reference_of(&f, 16);
    size_t remove = qa_qvm_module_bind_invocation(&f.module, &reference, entered_hook, &m);
    module_bind_job again = {&f, reference};
    CHECK(throws_with(module_bind_body, &again, "already has a hook"));
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == 32);
    qa_qvm_writable_view view;
    qa_qvm_memory_view(f.module.memory, 64, 4, 0, &view);
    qa_qvm_writable_view_set_int32(&view, 0, 7, true);
    int32_t words[2] = {64, 9};
    CHECK(qa_qvm_module_call(&f.module, words, 2, 16) == 20);
    CHECK(m.entered == 2);
    qa_qvm_module_unbind(&f.module, remove);
    qa_qvm_module_unbind(&f.module, remove);
    CHECK(qa_qvm_module_call(&f.module, words, 2, 16) == 19);
    CHECK(m.entered == 2);
    qa_qvm_module_retire(&f.module);
    module_close(&f);
  }
  /* module binding checks artifact identity, retains host hooks through restart */
  {
    static module_fixture f;
    module_open(&f, hook_source, HOOK_SOURCE_COUNT, 4096, "test:function-hooks");
    module_hook_ctx m = {SENTINEL, &f, 0};
    qa_guest_callback_reference other = reference_of(&f, 16);
    other.module.revision = "other";
    module_bind_job wrong = {&f, other};
    CHECK(throws_with(module_bind_body, &wrong, "different module artifact"));
    qa_guest_callback_reference reference = reference_of(&f, 16);
    size_t remove = qa_qvm_module_bind_function(&f.module, &reference, nested_module_hook, &m);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == 34);
    qa_qvm_interpreter *interp = qa_qvm_module_get_interpreter(&f.module);
    size_t length = 0;
    uint8_t *memory = qa_qvm_interpreter_get_memory(interp, &length);
    static uint8_t copy[4096];
    memcpy(copy, memory, length);
    qa_qvm_interpreter_restore_data(interp, copy, length);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == 34);
    qa_qvm_module_restart(&f.module, f.bytes, f.length);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == 34);
    qa_qvm_module_unbind(&f.module, remove);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == 31);
    qa_qvm_module_retire(&f.module);
    module_bind_job retired = {&f, reference};
    CHECK(throws_with(module_bind_body, &retired, "retired"));
    module_close(&f);
  }
  /* read-only original regions use live memory without replacing existing entry ownership */
  {
    const op ops[] = {OA(ENTER, 32), OA(LOCAL, 24), OA(CONST, 64), O(LOAD4), O(STORE4),
                      OA(LOCAL, 24), O(LOAD4), OA(LEAVE, 32)};
    static module_fixture f;
    module_open(&f, ops, 8, 2049, "test:read-only");
    module_hook_ctx m = {SENTINEL, &f, 0};
    qa_guest_callback_reference reference = reference_of(&f, 0);
    (void)qa_qvm_module_bind_invocation(&f.module, &reference, read_only_module_hook, &m);
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(f.module.memory, 64, 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, 31, true);
    qa_qvm_region_evaluation region = {1, 5, NULL, 0, true, 24};
    CHECK(qa_qvm_module_evaluate_region(&f.module, NULL, 0, 0, &region, NULL, 0, NULL) == 31);
    CHECK(m.entered == 0);
    qa_qvm_writable_view_set_int32(&view, 0, 77, true);
    CHECK(qa_qvm_module_call(&f.module, NULL, 0, 0) == 77);
    CHECK(m.entered == 1);
    CHECK(qa_qvm_writable_view_get_int32(&view, 0, true) == 77);
    size_t length = 0;
    (void)qa_qvm_interpreter_get_memory(qa_qvm_module_get_interpreter(&f.module), &length);
    CHECK(qa_qvm_interpreter_get_stack_pointer(qa_qvm_module_get_interpreter(&f.module)) == (double)length);
    qa_qvm_module_retire(&f.module);
    module_close(&f);
  }
  /* source counter queries isolate their word, preserve hooks and reject unrelated stores */
  {
    const op ops[] = {OA(ENTER, 16), OA(CONST, 64), OA(CONST, 64), O(LOAD4),
                      OA(CONST, 5), O(ADD), O(STORE4), OA(CONST, 0), OA(LEAVE, 16)};
    static module_fixture f;
    module_open(&f, ops, 9, 2049, "test:counter");
    module_hook_ctx m = {SENTINEL, &f, 0};
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(f.module.memory, 64, 4, &view);
    qa_qvm_writable_view_set_int32(&view, 0, 13, true);
    int stores = 0;
    qa_qvm_write_range range = {64, 4};
    (void)qa_qvm_memory_observe_writes(f.module.memory, &range, 1, count_store, &stores, NULL, NULL);
    qa_guest_callback_reference reference = reference_of(&f, 0);
    (void)qa_qvm_module_bind_invocation(&f.module, &reference, counter_module_hook, &m);
    int64_t functions[1] = {0};
    CHECK(qa_qvm_module_evaluate_counter(&f.module, NULL, 0, 0, 64, functions, 1, NULL) == 5);
    CHECK(m.entered == 0 && stores == 0);
    CHECK(qa_qvm_writable_view_get_int32(&view, 0, true) == 13);
    (void)qa_qvm_module_call(&f.module, NULL, 0, 0);
    CHECK(m.entered == 1 && stores == 1);
    CHECK(qa_qvm_writable_view_get_int32(&view, 0, true) == 18);
    qa_qvm_module_retire(&f.module);
    module_close(&f);
  }
}

int main(void) {
  test_regions();
  test_resolver();
  test_direct_cancellation();
  test_exact_cancellation();
  test_faults();
  test_capabilities();
  test_operands();
  test_observers();
  test_replacement();
  test_sync_rules();
  test_branches();
  test_counters();
  test_argument_extent();
  test_modules();
  printf("%d checks, %d failures\n", checks, failures);
  fflush(stdout);
  return failures == 0 ? 0 : 1;
}

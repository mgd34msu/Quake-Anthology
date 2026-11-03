/* See include/qa/compat/qvm/module.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/module.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/guest_memory.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

const qa_numeric_profile qa_qvm_numeric_profile = {
    "q3:qvm-binary32",
    {QA_ARITHMETIC_BINARY32, NULL, 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
    QA_FLOAT_TO_INT_QVM_INDEFINITE};

/* Landed QvmUiExport.UI_GETAPIVERSION (TS abi.ts). */
enum { UI_GETAPIVERSION = QA_QVM_UI_EXPORT_UI_GETAPIVERSION };

qa_q3_api_identity qa_qvm_api(qa_qvm_role role, qa_qvm_abi_profile profile) {
  bool modern = profile == NULL || strcmp(profile, "q3-modern") == 0;
  qa_q3_api_identity result;
  if (strcmp(role, "qagame") == 0) {
    result.kind = QA_Q3_API_QAGAME;
    result.version = modern ? 8 : 7;
  } else if (strcmp(role, "cgame") == 0) {
    result.kind = QA_Q3_API_CGAME;
    result.version = modern ? 4 : 3;
  } else {
    result.kind = QA_Q3_API_UI;
    result.version = modern ? 6 : 4;
  }
  return result;
}

qa_qvm_arguments qa_qvm_make_arguments(const int32_t *words, size_t count) {
  if (count > 10) qa_range_error("QVM entry accepts at most ten argument words");
  qa_qvm_arguments result;
  for (size_t i = 0; i < 10; i++) result.words[i] = i < count ? words[i] : 0;
  return result;
}

static bool same_module(qa_module_identity left, qa_module_identity right) {
  return strcmp(left.id, right.id) == 0 && strcmp(left.digest, right.digest) == 0 &&
         strcmp(left.artifact_path, right.artifact_path) == 0 &&
         strcmp(left.revision, right.revision) == 0;
}

qa_qvm_abi_profile qa_qvm_module_get_abi_profile(const qa_qvm_module *module) {
  return module->options.artifact->has_abi_profile ? module->options.artifact->abi_profile : "q3-modern";
}

const qa_execution_profile *qa_qvm_module_get_profile(const qa_qvm_module *module) {
  return &module->profile;
}

static void command_arguments(void *ctx, const char *const **out, size_t *out_count, bool *out_has) {
  qa_qvm_module *module = ctx;
  *out = module->command_arguments;
  *out_count = module->command_argument_count;
  *out_has = module->has_command_arguments;
}

static void assert_owner(qa_qvm_module *module) {
  qa_qvm_memory_assert_live(module->memory);
  if (module->retired)
    qa_range_error("%s QVM module has been retired", module->options.artifact->role);
}

static void live(qa_qvm_module *module) {
  qa_qvm_memory_assert_not_publishing(module->memory);
  assert_owner(module);
}


/* ----- TS try/finally around currentEntry ----- */

typedef struct entry_scope_job {
  void (*body)(void *ctx);
  void *ctx;
} entry_scope_job;

static void entry_scope_body(void *ctx) {
  entry_scope_job *job = ctx;
  job->body(job->ctx);
}

/* Runs body with `entry` current (unchanged when NULL) and restores the
 * previous entry on return or throw. */
static void with_entry(qa_qvm_module *module, const qa_qvm_module_syscall *entry,
                       void (*body)(void *ctx), void *ctx) {
  bool previous_has = module->has_current_entry;
  qa_qvm_module_syscall previous = module->current_entry;
  if (entry != NULL) {
    module->current_entry = *entry;
    module->has_current_entry = true;
  }
  entry_scope_job job = {body, ctx};
  char message[512];
  bool ok = qa_range_error_catch(entry_scope_body, &job, message, sizeof message);
  module->current_entry = previous;
  module->has_current_entry = previous_has;
  if (!ok) qa_range_error("%s", message);
}

/* Runs body and frees `owned` afterwards whether or not it throws. */
static void with_cleanup(void (*body)(void *ctx), void *ctx, void *owned) {
  char message[512];
  bool ok = qa_range_error_catch(body, ctx, message, sizeof message);
  free(owned);
  if (!ok) qa_range_error("%s", message);
}

/* ----- the module-built interpreter (TS constructor) ----- */

typedef struct rt_hook {
  qa_qvm_function_hook_fn fn;
  void *ctx;
  struct rt_state *rt;
} rt_hook;

typedef struct rt_observer {
  qa_qvm_function_observer_fn fn;
  void *ctx;
  struct rt_state *rt;
} rt_observer;

typedef struct rt_resolver {
  qa_qvm_function_resolver_fn fn;
  void *ctx;
  struct rt_state *rt;
} rt_resolver;

typedef struct rt_binding {
  size_t id;
  qa_qvm_unbind unbind;
} rt_binding;

typedef struct rt_state {
  qa_qvm_interpreter_runtime table;
  qa_qvm_module *module;
  qa_qvm_interpreter *interp;
  qa_qvm_guest_memory guest;
  qa_guest_memory guest_table;
  rt_binding *bindings;
  size_t binding_count, binding_capacity;
  size_t next_id;
  void **owned; /* hook/observer/resolver adapters: a running call may outlive its removal */
  size_t owned_count, owned_capacity;
} rt_state;

static void rt_own(rt_state *rt, void *pointer) {
  if (rt->owned_count == rt->owned_capacity) {
    size_t capacity = rt->owned_capacity == 0 ? 16 : rt->owned_capacity * 2;
    void **grown = realloc(rt->owned, capacity * sizeof *grown);
    if (grown == NULL) {
      free(pointer);
      qa_range_error("QVM memory allocation failed");
    }
    rt->owned = grown;
    rt->owned_capacity = capacity;
  }
  rt->owned[rt->owned_count++] = pointer;
}

static void *rt_alloc(rt_state *rt, size_t size) {
  void *pointer = calloc(1, size);
  if (pointer == NULL) qa_range_error("QVM memory allocation failed");
  rt_own(rt, pointer);
  return pointer;
}

/* B call_ctx over an interpreter callback scope. */
typedef struct rt_call {
  void *ctx;
  double (*invoke)(void *ctx, const double *args, size_t arg_count, bool has_index,
                   double instruction_index, const qa_qvm_read_only_evaluation *evaluation);
  double (*invoke_async_validated)(void *ctx, const double *args, size_t arg_count,
                                   double instruction_index, void (*validate)(void *ctx),
                                   void *validate_ctx);
  void (*cancel)(void *ctx, qa_qvm_cancellation_scope scope);
  qa_qvm_function_call *call;
  void *branch_adapters;
  void *region_adapters;
} rt_call;

#define RT_WORDS 256

static void rt_words(const int32_t *args, size_t arg_count, int64_t instruction_index, double *out) {
  if (arg_count > RT_WORDS)
    qa_range_error(instruction_index == 0 ? "QVM vmMain requires ten public argument words"
                                          : "QVM private call exceeds OP_ARG argument capacity");
  for (size_t i = 0; i < arg_count; i++) out[i] = args[i];
}

static int32_t rt_invoke(void *ctx, const int32_t *args, size_t arg_count, int64_t instruction_index,
                         const qa_qvm_read_only_evaluation *evaluation) {
  rt_call *call = ctx;
  double words[RT_WORDS];
  rt_words(args, arg_count, instruction_index, words);
  return (int32_t)call->invoke(call->ctx, words, arg_count, true, (double)instruction_index, evaluation);
}

static int32_t rt_invoke_async(void *ctx, const int32_t *args, size_t arg_count, int64_t instruction_index,
                               qa_qvm_validate_fn validate, void *validate_ctx) {
  rt_call *call = ctx;
  double words[RT_WORDS];
  rt_words(args, arg_count, instruction_index, words);
  return (int32_t)call->invoke_async_validated(call->ctx, words, arg_count, (double)instruction_index,
                                               validate, validate_ctx);
}

static void rt_cancel(void *ctx, qa_qvm_cancellation_scope scope) {
  rt_call *call = ctx;
  call->cancel(call->ctx, scope);
}

static qa_qvm_module_syscall rt_entry(rt_state *rt, rt_call *call, const qa_qvmx_view *words) {
  qa_qvm_module_syscall entry;
  memset(&entry, 0, sizeof entry);
  size_t length = 0;
  entry.words = words != NULL ? words->bytes : NULL;
  entry.words_length = words != NULL ? words->length : 0;
  entry.memory = qa_qvm_interpreter_get_memory(rt->interp, &length);
  entry.memory_length = length;
  entry.guest = rt->module->memory;
  entry.call_ctx = call;
  entry.invoke = rt_invoke;
  entry.invoke_async = rt_invoke_async;
  entry.cancel_function = rt_cancel;
  return entry;
}

static bool rt_decide(void *ctx, bool original_taken, qa_qvm_syscall *control) {
  const qa_qvm_module_branch_binding *binding = ctx;
  return binding->decide(binding->ctx, original_taken, control->ctx, control->cancel_function);
}

typedef struct rt_bind_job {
  qa_qvm_function_call *call;
  const void *bindings;
  size_t count;
} rt_bind_job;

static void rt_bind_branches_body(void *ctx) {
  rt_bind_job *job = ctx;
  job->call->branches(job->call->ctx, job->bindings, job->count);
}

static void rt_branches(void *ctx, const qa_qvm_module_branch_binding *bindings, size_t count) {
  rt_call *call = ctx;
  /* The decide adapters live for this call; the interpreter copies the list. */
  qa_qvm_module_branch_binding *adapters = malloc((count == 0 ? 1 : count) * sizeof *adapters);
  qa_qvm_branch_binding *converted = malloc((count == 0 ? 1 : count) * sizeof *converted);
  if (adapters == NULL || converted == NULL) {
    free(adapters);
    free(converted);
    qa_range_error("QVM memory allocation failed");
  }
  free(call->branch_adapters);
  call->branch_adapters = adapters;
  for (size_t i = 0; i < count; i++) {
    adapters[i] = bindings[i];
    converted[i].instruction_index = (double)bindings[i].instruction_index;
    converted[i].ctx = &adapters[i];
    converted[i].decide = rt_decide;
  }
  rt_bind_job job = {call->call, converted, count};
  with_cleanup(rt_bind_branches_body, &job, converted);
}

static const char *rt_region_run(void *ctx, qa_qvm_region_control *control) {
  const qa_qvm_module_region_binding *binding = ctx;
  return binding->run(binding->ctx, control);
}

static void rt_region_completed(void *ctx, qa_qvm_region_control *control) {
  const qa_qvm_module_region_binding *binding = ctx;
  binding->completed(binding->ctx, control);
}

static void rt_bind_regions_body(void *ctx) {
  rt_bind_job *job = ctx;
  job->call->regions(job->call->ctx, job->bindings, job->count);
}

static void rt_regions(void *ctx, const qa_qvm_module_region_binding *bindings, size_t count) {
  rt_call *call = ctx;
  qa_qvm_module_region_binding *adapters = malloc((count == 0 ? 1 : count) * sizeof *adapters);
  qa_qvm_region_binding *converted = malloc((count == 0 ? 1 : count) * sizeof *converted);
  if (adapters == NULL || converted == NULL) {
    free(adapters);
    free(converted);
    qa_range_error("QVM memory allocation failed");
  }
  free(call->region_adapters);
  call->region_adapters = adapters;
  for (size_t i = 0; i < count; i++) {
    adapters[i] = bindings[i];
    converted[i].entry = (double)bindings[i].entry;
    converted[i].join = (double)bindings[i].join;
    converted[i].ctx = &adapters[i];
    converted[i].run = rt_region_run;
    converted[i].completed = bindings[i].completed != NULL ? rt_region_completed : NULL;
  }
  rt_bind_job job = {call->call, converted, count};
  with_cleanup(rt_bind_regions_body, &job, converted);
}

typedef struct rt_region_job {
  qa_qvm_function_call *call;
  const qa_qvm_region_evaluation *region;
  const double *inputs;
  size_t count;
  int32_t result;
} rt_region_job;

static void rt_evaluate_region_body(void *ctx) {
  rt_region_job *job = ctx;
  job->result = (int32_t)job->call->evaluate_region(job->call->ctx, *job->region, job->inputs, job->count);
}

static int32_t rt_evaluate_region(void *ctx, const qa_qvm_region_evaluation *region,
                                  const int64_t *inputs, size_t input_count) {
  rt_call *call = ctx;
  double *words = malloc((input_count == 0 ? 1 : input_count) * sizeof *words);
  if (words == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < input_count; i++) words[i] = (double)inputs[i];
  rt_region_job job = {call->call, region, words, input_count, 0};
  with_cleanup(rt_evaluate_region_body, &job, words);
  return job.result;
}

static qa_qvm_cancellation_scope rt_cancellation_scope(void *ctx) {
  rt_call *call = ctx;
  return call->call->cancellation_scope(call->call->ctx);
}

static void rt_effect(void *ctx, qa_qvm_perform_fn perform, void *perform_ctx) {
  rt_call *call = ctx;
  call->call->effect(call->call->ctx, perform, perform_ctx);
}

static int32_t rt_proceed(void *ctx) {
  rt_call *call = ctx;
  return (int32_t)call->call->proceed(call->call->ctx);
}

static int32_t rt_proceed_async(void *ctx) {
  rt_call *call = ctx;
  return (int32_t)call->call->proceed_async(call->call->ctx);
}

typedef struct rt_hook_job {
  rt_hook *hook;
  qa_qvm_module_function_call *call;
  int32_t result;
} rt_hook_job;

static void rt_hook_body(void *ctx) {
  rt_hook_job *job = ctx;
  job->result = job->hook->fn(job->hook->ctx, job->call);
}

static double rt_hook_call(void *ctx, qa_qvm_function_call *a) {
  rt_hook *hook = ctx;
  rt_call call = {a->ctx, a->invoke, a->invoke_async_validated, a->cancel_function, a, NULL, NULL};
  qa_qvm_module_function_call b;
  memset(&b, 0, sizeof b);
  b.instruction_index = (int64_t)a->instruction_index;
  b.has_caller_instruction = a->has_caller_instruction;
  b.caller_instruction = (int64_t)a->caller_instruction;
  b.execution = a->execution;
  b.words = a->words.bytes;
  b.words_length = a->words.length;
  b.words_offset = a->words.bytes - a->memory;
  b.memory = a->memory;
  b.memory_length = a->memory_length;
  b.guest = hook->rt->module->memory;
  b.call_ctx = &call;
  b.invoke = rt_invoke;
  b.invoke_async = rt_invoke_async;
  b.cancel_function = rt_cancel;
  b.cancellation_scope = rt_cancellation_scope;
  b.branches = rt_branches;
  b.regions = rt_regions;
  b.evaluate_region = rt_evaluate_region;
  b.effect = rt_effect;
  b.proceed = rt_proceed;
  b.proceed_async = rt_proceed_async;
  rt_hook_job job = {hook, &b, 0};
  char message[512];
  bool ok = qa_range_error_catch(rt_hook_body, &job, message, sizeof message);
  free(call.branch_adapters);
  free(call.region_adapters);
  if (!ok) qa_range_error("%s", message);
  return job.result;
}

static void rt_observe(void *ctx, qa_qvm_function_observation *observation) {
  rt_observer *observer = ctx;
  rt_call call = {observation->ctx, observation->invoke, observation->invoke_async_validated,
                  observation->cancel_function, NULL, NULL, NULL};
  qa_qvm_module_function_call b;
  memset(&b, 0, sizeof b);
  size_t length = 0;
  const uint8_t *memory = qa_qvm_interpreter_get_memory(observer->rt->interp, &length);
  b.instruction_index = (int64_t)observation->instruction_index;
  b.execution = "synchronous";
  b.words = observation->words.bytes;
  b.words_length = observation->words.length;
  b.words_offset = observation->words.bytes - memory;
  b.memory = memory;
  b.memory_length = length;
  b.guest = observer->rt->module->memory;
  b.call_ctx = &call;
  b.invoke = rt_invoke;
  b.invoke_async = rt_invoke_async;
  b.cancel_function = rt_cancel;
  observer->fn(observer->ctx, &b, 0);
}

static bool rt_resolve(void *ctx, double instruction_index, double first_argument,
                       const qa_qvmx_view *words, qa_qvm_function_hook *out_hook) {
  rt_resolver *resolver = ctx;
  qa_qvm_function_hook_fn fn = NULL;
  void *fn_ctx = NULL;
  if (!resolver->fn(resolver->ctx, (int64_t)instruction_index, (int32_t)first_argument, words->bytes,
                    words->length, &fn, &fn_ctx))
    return false;
  rt_hook *hook = rt_alloc(resolver->rt, sizeof *hook);
  hook->fn = fn;
  hook->ctx = fn_ctx;
  hook->rt = resolver->rt;
  out_hook->ctx = hook;
  out_hook->call = rt_hook_call;
  return true;
}

static size_t rt_record(rt_state *rt, qa_qvm_unbind unbind) {
  if (rt->binding_count == rt->binding_capacity) {
    size_t capacity = rt->binding_capacity == 0 ? 16 : rt->binding_capacity * 2;
    rt_binding *grown = realloc(rt->bindings, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    rt->bindings = grown;
    rt->binding_capacity = capacity;
  }
  size_t id = ++rt->next_id;
  rt->bindings[rt->binding_count++] = (rt_binding){id, unbind};
  return id;
}

static bool rt_is_active(void *ctx) { return qa_qvm_interpreter_get_is_active(((rt_state *)ctx)->interp); }

static int64_t rt_stack_pointer(void *ctx) {
  return (int64_t)qa_qvm_interpreter_get_stack_pointer(((rt_state *)ctx)->interp);
}

static int32_t rt_table_invoke(void *ctx, const int32_t *args, size_t arg_count, int64_t instruction_index,
                               const qa_qvm_read_only_evaluation *evaluation) {
  rt_state *rt = ctx;
  double words[RT_WORDS];
  rt_words(args, arg_count, instruction_index, words);
  return (int32_t)qa_qvm_interpreter_invoke(rt->interp, words, arg_count, (double)instruction_index, evaluation);
}

static int32_t rt_table_invoke_async(void *ctx, const int32_t *args, size_t arg_count, int64_t instruction_index,
                                     qa_qvm_validate_fn validate, void *validate_ctx) {
  rt_state *rt = ctx;
  double words[RT_WORDS];
  rt_words(args, arg_count, instruction_index, words);
  return (int32_t)qa_qvm_interpreter_invoke_async_validated(rt->interp, words, arg_count,
                                                            (double)instruction_index, validate, validate_ctx);
}

typedef struct rt_counter_run {
  int32_t (*run)(void *ctx);
  void *ctx;
} rt_counter_run;

static double rt_counter_execute(void *ctx) {
  rt_counter_run *run = ctx;
  return run->run(run->ctx);
}

typedef struct rt_counter_job {
  rt_state *rt;
  int64_t address;
  const double *functions;
  size_t count;
  rt_counter_run run;
  const qa_qvm_evaluation_stack *stack;
  int32_t result;
} rt_counter_job;

static void rt_counter_body(void *ctx) {
  rt_counter_job *job = ctx;
  job->result = (int32_t)qa_qvm_interpreter_evaluate_counter(job->rt->interp, (double)job->address, 0,
                                                             job->functions, job->count, rt_counter_execute,
                                                             &job->run, job->stack);
}

static int32_t rt_evaluate_counter(void *ctx, int64_t address, const int64_t *functions, size_t function_count,
                                   int32_t (*run)(void *run_ctx), void *run_ctx,
                                   const qa_qvm_evaluation_stack *stack) {
  rt_state *rt = ctx;
  double *entries = malloc((function_count == 0 ? 1 : function_count) * sizeof *entries);
  if (entries == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < function_count; i++) entries[i] = (double)functions[i];
  rt_counter_job job = {rt, address, entries, function_count, {run, run_ctx}, stack, 0};
  with_cleanup(rt_counter_body, &job, entries);
  return job.result;
}

static size_t rt_bind(void *ctx, int64_t instruction_index, qa_qvm_function_hook_fn fn, void *hook_ctx,
                      bool invocation) {
  rt_state *rt = ctx;
  rt_hook *hook = rt_alloc(rt, sizeof *hook);
  hook->fn = fn;
  hook->ctx = hook_ctx;
  hook->rt = rt;
  qa_qvm_function_hook a = {hook, rt_hook_call};
  qa_qvm_unbind unbind = invocation
      ? qa_qvm_interpreter_bind_invocation(rt->interp, (double)instruction_index, a)
      : qa_qvm_interpreter_bind_function(rt->interp, (double)instruction_index, a);
  return rt_record(rt, unbind);
}

static size_t rt_bind_function(void *ctx, int64_t instruction_index, qa_qvm_function_hook_fn fn, void *hook_ctx) {
  return rt_bind(ctx, instruction_index, fn, hook_ctx, false);
}

static size_t rt_bind_invocation(void *ctx, int64_t instruction_index, qa_qvm_function_hook_fn fn, void *hook_ctx) {
  return rt_bind(ctx, instruction_index, fn, hook_ctx, true);
}

static size_t rt_bind_function_resolver(void *ctx, qa_qvm_function_resolver_fn resolve, void *resolve_ctx) {
  rt_state *rt = ctx;
  rt_resolver *resolver = rt_alloc(rt, sizeof *resolver);
  resolver->fn = resolve;
  resolver->ctx = resolve_ctx;
  resolver->rt = rt;
  qa_qvm_function_resolver a = {resolver, rt_resolve};
  return rt_record(rt, qa_qvm_interpreter_bind_function_resolver(rt->interp, a));
}

static size_t rt_observe_function(void *ctx, int64_t instruction_index, qa_qvm_function_observer_fn observe,
                                  void *observe_ctx) {
  rt_state *rt = ctx;
  rt_observer *observer = rt_alloc(rt, sizeof *observer);
  observer->fn = observe;
  observer->ctx = observe_ctx;
  observer->rt = rt;
  qa_qvm_function_observer a = {observer, rt_observe};
  return rt_record(rt, qa_qvm_interpreter_observe_function(rt->interp, (double)instruction_index, a));
}

static void rt_unbind(void *ctx, size_t id) {
  rt_state *rt = ctx;
  for (size_t i = 0; i < rt->binding_count; i++) {
    if (rt->bindings[i].id == id) {
      qa_qvm_unbind unbind = rt->bindings[i].unbind;
      rt->bindings[i] = rt->bindings[--rt->binding_count];
      unbind.call(unbind.ctx);
      return;
    }
  }
}

typedef struct rt_restart_job {
  qa_qvm_interpreter *interp;
  qa_qvm_data_image *image;
} rt_restart_job;

static void rt_restart_body(void *ctx) {
  rt_restart_job *job = ctx;
  qa_qvm_interpreter_restart(job->interp, job->image);
}

static void rt_restart_bytes(void *ctx, const uint8_t *bytes, size_t length, const char *artifact_path) {
  rt_state *rt = ctx;
  qa_qvm_data_image image;
  memset(&image, 0, sizeof image);
  if (!qa_qvm_parse_restart(bytes, length, artifact_path, &image)) qa_range_error("QVM memory allocation failed");
  /* The image is owned here; a refused restart still releases it. */
  rt_restart_job job = {rt->interp, &image};
  char message[512];
  bool ok = qa_range_error_catch(rt_restart_body, &job, message, sizeof message);
  qa_qvm_data_image_destroy(&image);
  if (!ok) qa_range_error("%s", message);
}

static void rt_restore_data(void *ctx, const uint8_t *data, size_t length) {
  qa_qvm_interpreter_restore_data(((rt_state *)ctx)->interp, data, length);
}

typedef struct rt_system_job {
  qa_qvm_module *module;
  qa_qvm_module_syscall entry;
  int32_t result;
} rt_system_job;

static void rt_system_call_body(void *ctx) {
  rt_system_job *job = ctx;
  job->result = qa_qvm_system_call_invoke(&job->module->source_system_call, &job->entry);
}

/* TS: the interpreter's system call makes its scope the current entry. */
static double rt_system_call(void *ctx, qa_qvm_syscall *a) {
  qa_qvm_module *module = ctx;
  rt_state *rt = module->owned;
  rt_call call = {a->ctx, a->invoke, a->invoke_async_validated, a->cancel_function, NULL, NULL, NULL};
  rt_system_job job = {module, rt_entry(rt, &call, &a->words), 0};
  with_entry(module, &job.entry, rt_system_call_body, &job);
  return job.result;
}

/* TS store effect: the effect scope is the current entry while it runs. */
static void rt_store_effect(void *ctx, qa_qvm_syscall *scope, void (*perform)(void *perform_ctx),
                            void *perform_ctx) {
  qa_qvm_module *module = ctx;
  rt_state *rt = module->owned;
  rt_call call = {scope->ctx, scope->invoke, scope->invoke_async_validated, scope->cancel_function, NULL, NULL, NULL};
  qa_qvm_module_syscall entry = rt_entry(rt, &call, NULL);
  with_entry(module, &entry, perform, perform_ctx);
}

static void rt_create(qa_qvm_module *module) {
  const qa_qvm_module_options *options = &module->options;
  if (options->artifact->is_typescript) qa_range_error("QVM module requires a bytecode artifact");
  rt_state *rt = calloc(1, sizeof *rt);
  if (rt == NULL) qa_range_error("QVM memory allocation failed");
  module->owned = rt;
  rt->module = module;
  qa_qvm_interpreter_system_call system_call = {module, rt_system_call};
  qa_qvm_store_effect store_effect = {module, rt_store_effect};
  rt->interp = qa_qvm_interpreter_create(&options->artifact->image, system_call, options->allocation,
                                         options->registration, options->semantics ? options->semantics : "compiled", &store_effect);
  if (rt->interp == NULL) qa_range_error("QVM memory allocation failed");
  qa_qvm_interpreter_runtime *table = &rt->table;
  table->ctx = rt;
  table->address_space = qa_qvm_interpreter_get_guest(rt->interp);
  table->is_active = rt_is_active;
  table->stack_pointer = rt_stack_pointer;
  table->invoke = rt_table_invoke;
  table->invoke_async = rt_table_invoke_async;
  table->evaluate_counter = rt_evaluate_counter;
  table->bind_function = rt_bind_function;
  table->bind_invocation = rt_bind_invocation;
  table->bind_function_resolver = rt_bind_function_resolver;
  table->observe_function = rt_observe_function;
  table->unbind = rt_unbind;
  table->restart_bytes = rt_restart_bytes;
  table->restore_data = rt_restore_data;
  qa_qvm_guest_memory_init(&rt->guest, options->artifact->module,
                           qa_qvm_interpreter_get_address_space(rt->interp));
  rt->guest_table = qa_qvm_guest_memory_as_guest_memory(&rt->guest);
  module->options.interpreter = table;
  module->options.guest_memory = &rt->guest_table;
}

static void rt_destroy(qa_qvm_module *module) {
  rt_state *rt = module->owned;
  if (rt == NULL) return;
  qa_qvm_interpreter_destroy(rt->interp);
  qa_qvm_guest_memory_destroy(&rt->guest);
  for (size_t i = 0; i < rt->owned_count; i++) free(rt->owned[i]);
  free(rt->owned);
  free(rt->bindings);
  free(rt);
  module->owned = NULL;
}

struct qa_qvm_interpreter *qa_qvm_module_get_interpreter(qa_qvm_module *module) {
  rt_state *rt = module->owned;
  return rt != NULL ? rt->interp : NULL;
}

int32_t qa_qvm_module_region_invoke(void *control_ctx, const int32_t *args, size_t arg_count,
                                    int64_t instruction_index) {
  qa_qvm_region_control *control = control_ctx;
  double words[RT_WORDS];
  rt_words(args, arg_count, instruction_index, words);
  return (int32_t)control->invoke(control->ctx, words, arg_count, true, (double)instruction_index, NULL);
}

void qa_qvm_module_region_cancel(void *control_ctx, qa_qvm_cancellation_scope scope) {
  qa_qvm_region_control *control = control_ctx;
  control->cancel_function(control->ctx, scope);
}

int32_t qa_qvm_module_region_local_word(void *control_ctx, int64_t offset) {
  qa_qvm_region_control *control = control_ctx;
  return (int32_t)control->local_word(control->ctx, (double)offset);
}

static void init_common(qa_qvm_module *module, const qa_qvm_module_options *options) {
  memset(module, 0, sizeof *module);
  module->options = *options;
  const qa_resolved_qvm_artifact *artifact = options->artifact;
  memset(&module->profile, 0, sizeof module->profile);
  module->profile.kind = QA_EXECUTION_QVM;
  module->profile.module = artifact->module;
  module->profile.api.kind = QA_GAME_API_Q3;
  module->profile.api.as.q3 = qa_qvm_api(artifact->role, qa_qvm_module_get_abi_profile(module));
  module->profile.magic = 0x12721444;
  module->profile.numeric = qa_qvm_numeric_profile;
  qa_qvm_create_system_call(&module->source_system_call, artifact->role, options->host,
                            options->host_ctx, command_arguments, module,
                            qa_qvm_module_get_abi_profile(module));
  if (options->interpreter == NULL) rt_create(module);
  module->interpreter = module->options.interpreter;
  module->memory = module->interpreter->address_space;
  module->guest_memory = module->options.guest_memory;
}

static void validate_ui_version(qa_qvm_module *module, int32_t version) {
  char message[128];
  if (strcmp(qa_qvm_module_get_abi_profile(module), "q3-modern") != 0 && version != 4) {
    snprintf(message, sizeof message, "Legacy User Interface is version %d, expected 4", (int)version);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  if (version != 4 && version != 6) {
    snprintf(message, sizeof message, "User Interface is version %d, expected 6", (int)version);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  module->profile.api.as.q3.kind = QA_Q3_API_UI;
  module->profile.api.as.q3.version = version;
}

static void ui_version_body(void *ctx) {
  qa_qvm_module *module = ctx;
  int32_t words[] = {UI_GETAPIVERSION};
  validate_ui_version(module, qa_qvm_module_call(module, words, 1, 0));
}

void qa_qvm_module_init(qa_qvm_module *module, const qa_qvm_module_options *options) {
  init_common(module, options);
  if (strcmp(options->artifact->role, "ui") == 0) {
    /* TS retires, then rethrows. */
    char message[512];
    if (!qa_range_error_catch(ui_version_body, module, message, sizeof message)) {
      qa_qvm_module_retire(module);
      qa_range_error("%s", message);
    }
  }
}

void qa_qvm_module_init_deferred(qa_qvm_module *module, const qa_qvm_module_options *options) {
  init_common(module, options);
}

void qa_qvm_module_create(qa_qvm_module *module, const qa_qvm_module_options *options,
                          qa_qvm_validate_fn validate, void *validate_ctx) {
  init_common(module, options);
  if (strcmp(options->artifact->role, "ui") == 0) {
    int32_t words[] = {UI_GETAPIVERSION};
    validate_ui_version(module, qa_qvm_module_call_async(module, words, 1, 0, validate, validate_ctx));
  } else if (validate != NULL) {
    validate(validate_ctx);
  }
}

static void free_wrapper(void *ctx);

void qa_qvm_module_destroy(qa_qvm_module *module) {
  typedef struct { size_t id; void *ctx; } wrapper_entry;
  wrapper_entry *wrappers = module->wrappers;
  for (size_t i = 0; i < module->wrapper_count; i++) free_wrapper(wrappers[i].ctx);
  free(wrappers);
  rt_destroy(module);
  memset(module, 0, sizeof *module);
}

int32_t qa_qvm_module_system_call(qa_qvm_module *module, const qa_qvm_module_syscall *call) {
  rt_system_job job = {module, *call, 0};
  with_entry(module, call, rt_system_call_body, &job);
  return job.result;
}

void qa_qvm_module_cancel_function(qa_qvm_module *module, qa_qvm_cancellation_scope scope) {
  if (!module->has_current_entry)
    qa_range_error("QVM cancellation requires the current source invocation");
  module->current_entry.cancel_function(module->current_entry.call_ctx, scope);
  qa_range_error("QVM cancellation scope returned without unwinding");
}

typedef struct wrapper {
  bool resolver_state; /* a resolver_state owning cached hook wrappers */
  qa_qvm_module *module;
  qa_qvm_function_hook_fn hook;
  void *hook_ctx;
  qa_qvm_function_observer_fn observe;
  void *observe_ctx;
  qa_qvm_function_resolver_fn resolve;
  void *resolve_ctx;
} wrapper;

typedef struct wrapper_entry {
  size_t id;
  void *ctx;
} wrapper_entry;

static void register_wrapper(qa_qvm_module *module, size_t id, void *ctx) {
  if (module->wrapper_count == module->wrapper_capacity) {
    module->wrapper_capacity = module->wrapper_capacity == 0 ? 8 : module->wrapper_capacity * 2;
    module->wrappers = realloc(module->wrappers, module->wrapper_capacity * sizeof(wrapper_entry));
    if (module->wrappers == NULL) qa_range_error("QVM memory allocation failed");
  }
  wrapper_entry *wrappers = module->wrappers;
  wrappers[module->wrapper_count++] = (wrapper_entry){id, ctx};
}

static qa_qvm_module_syscall entry_of(qa_qvm_module_function_call *call) {
  qa_qvm_module_syscall entry;
  entry.words = call->words;
  entry.words_length = call->words_length;
  entry.memory = call->memory;
  entry.memory_length = call->memory_length;
  entry.guest = call->guest;
  entry.call_ctx = call->call_ctx;
  entry.invoke = call->invoke;
  entry.invoke_async = call->invoke_async;
  entry.cancel_function = call->cancel_function;
  return entry;
}

typedef struct wrapped_job {
  wrapper *wrap;
  qa_qvm_module_function_call *call;
  int32_t result;
} wrapped_job;

static void wrapped_hook_body(void *ctx) {
  wrapped_job *job = ctx;
  job->result = job->wrap->hook(job->wrap->hook_ctx, job->call);
}

static int32_t wrapped_hook(void *ctx, qa_qvm_module_function_call *call) {
  wrapper *wrap = ctx;
  qa_qvm_module *module = wrap->module;
  live(module);
  qa_qvm_module_syscall entry = entry_of(call);
  wrapped_job job = {wrap, call, 0};
  with_entry(module, &entry, wrapped_hook_body, &job);
  return job.result;
}

typedef struct bind_job {
  qa_qvm_module *module;
  const qa_guest_callback_reference *reference;
  void *wrap;
  bool invocation;
  size_t id;
} bind_job;

static int32_t wrapped_hook(void *ctx, qa_qvm_module_function_call *call);

static void bind_body(void *ctx) {
  bind_job *job = ctx;
  qa_qvm_interpreter_runtime *runtime = job->module->interpreter;
  int64_t index = (int64_t)job->reference->instruction_index;
  job->id = job->invocation ? runtime->bind_invocation(runtime->ctx, index, wrapped_hook, job->wrap)
                            : runtime->bind_function(runtime->ctx, index, wrapped_hook, job->wrap);
}

static size_t bind_entry(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                         qa_qvm_function_hook_fn hook, void *hook_ctx, bool invocation) {
  live(module);
  if (!same_module(reference->module, module->profile.module))
    qa_range_error("QVM function hook belongs to a different module artifact");
  wrapper *wrap = malloc(sizeof *wrap);
  if (wrap == NULL) qa_range_error("QVM memory allocation failed");
  memset(wrap, 0, sizeof *wrap);
  wrap->module = module;
  wrap->hook = hook;
  wrap->hook_ctx = hook_ctx;
  /* A refused binding releases its wrapper before rethrowing. */
  bind_job job = {module, reference, wrap, invocation, 0};
  char message[512];
  if (!qa_range_error_catch(bind_body, &job, message, sizeof message)) {
    free(wrap);
    qa_range_error("%s", message);
  }
  register_wrapper(module, job.id, wrap);
  return job.id;
}

size_t qa_qvm_module_bind_function(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                   qa_qvm_function_hook_fn hook, void *hook_ctx) {
  return bind_entry(module, reference, hook, hook_ctx, false);
}

size_t qa_qvm_module_bind_invocation(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                     qa_qvm_function_hook_fn hook, void *hook_ctx) {
  return bind_entry(module, reference, hook, hook_ctx, true);
}

typedef struct resolver_cache {
  qa_qvm_function_hook_fn hook;
  void *hook_ctx;
  wrapper *wrap;
} resolver_cache;

typedef struct resolver_state {
  wrapper base;
  resolver_cache *cache;
  size_t cache_count;
  size_t cache_capacity;
} resolver_state;

static bool wrapped_resolver(void *ctx, int64_t instruction_index, int32_t first_argument,
                             const uint8_t *words, size_t words_length,
                             qa_qvm_function_hook_fn *out_hook, void **out_hook_ctx) {
  resolver_state *state = ctx;
  qa_qvm_function_hook_fn hook = NULL;
  void *hook_ctx = NULL;
  if (!state->base.resolve(state->base.resolve_ctx, instruction_index, first_argument, words, words_length, &hook, &hook_ctx))
    return false;
  for (size_t i = 0; i < state->cache_count; i++) {
    if (state->cache[i].hook == hook && state->cache[i].hook_ctx == hook_ctx) {
      *out_hook = wrapped_hook;
      *out_hook_ctx = state->cache[i].wrap;
      return true;
    }
  }
  wrapper *wrap = malloc(sizeof *wrap);
  if (wrap == NULL) qa_range_error("QVM memory allocation failed");
  memset(wrap, 0, sizeof *wrap);
  wrap->module = state->base.module;
  wrap->hook = hook;
  wrap->hook_ctx = hook_ctx;
  if (state->cache_count == state->cache_capacity) {
    state->cache_capacity = state->cache_capacity == 0 ? 8 : state->cache_capacity * 2;
    state->cache = realloc(state->cache, state->cache_capacity * sizeof *state->cache);
    if (state->cache == NULL) qa_range_error("QVM memory allocation failed");
  }
  state->cache[state->cache_count++] = (resolver_cache){hook, hook_ctx, wrap};
  *out_hook = wrapped_hook;
  *out_hook_ctx = wrap;
  return true;
}

size_t qa_qvm_module_bind_function_resolver(qa_qvm_module *module, qa_qvm_function_resolver_fn resolve, void *resolve_ctx) {
  live(module);
  resolver_state *state = malloc(sizeof *state);
  if (state == NULL) qa_range_error("QVM memory allocation failed");
  memset(state, 0, sizeof *state);
  state->base.resolver_state = true;
  state->base.module = module;
  state->base.resolve = resolve;
  state->base.resolve_ctx = resolve_ctx;
  size_t id = module->interpreter->bind_function_resolver(module->interpreter->ctx, wrapped_resolver, state);
  register_wrapper(module, id, state);
  return id;
}

typedef struct observe_job {
  wrapper *wrap;
  qa_qvm_module_function_call *call;
  int64_t base;
} observe_job;

static void wrapped_observe_body(void *ctx) {
  observe_job *job = ctx;
  job->wrap->observe(job->wrap->observe_ctx, job->call, job->base);
}

static void wrapped_observe(void *ctx, qa_qvm_module_function_call *call, int64_t base) {
  wrapper *wrap = ctx;
  qa_qvm_module *module = wrap->module;
  live(module);
  qa_qvm_module_syscall entry = entry_of(call);
  observe_job job = {wrap, call, base};
  with_entry(module, &entry, wrapped_observe_body, &job);
}

size_t qa_qvm_module_observe_function(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                      qa_qvm_function_observer_fn observe, void *observe_ctx) {
  assert_owner(module);
  if (!same_module(reference->module, module->profile.module))
    qa_range_error("QVM function observer belongs to a different module artifact");
  wrapper *wrap = malloc(sizeof *wrap);
  if (wrap == NULL) qa_range_error("QVM memory allocation failed");
  memset(wrap, 0, sizeof *wrap);
  wrap->module = module;
  wrap->observe = observe;
  wrap->observe_ctx = observe_ctx;
  size_t id = module->interpreter->observe_function(module->interpreter->ctx,
                                                     (int64_t)reference->instruction_index, wrapped_observe, wrap);
  register_wrapper(module, id, wrap);
  return id;
}

static void free_wrapper(void *ctx) {
  wrapper *wrap = ctx;
  if (wrap != NULL && wrap->resolver_state) {
    resolver_state *state = ctx;
    for (size_t i = 0; i < state->cache_count; i++) free(state->cache[i].wrap);
    free(state->cache);
  }
  free(wrap);
}

void qa_qvm_module_unbind(qa_qvm_module *module, size_t id) {
  module->interpreter->unbind(module->interpreter->ctx, id);
  wrapper_entry *wrappers = module->wrappers;
  for (size_t i = 0; i < module->wrapper_count; i++) {
    if (wrappers[i].id == id) {
      free_wrapper(wrappers[i].ctx);
      memmove(&wrappers[i], &wrappers[i + 1], (module->wrapper_count - i - 1) * sizeof *wrappers);
      module->wrapper_count--;
      return;
    }
  }
}

int32_t qa_qvm_module_call(qa_qvm_module *module, const int32_t *words, size_t count, int64_t instruction_index) {
  live(module);
  if (module->options.registration != NULL) qa_qvm_vm_registration_called(module->options.registration);
  qa_qvm_arguments arguments;
  const int32_t *args = words;
  size_t arg_count = count;
  if (instruction_index == 0 || count <= 10) {
    arguments = qa_qvm_make_arguments(words, count);
    args = arguments.words;
    arg_count = 10;
  }
  if (!module->has_current_entry)
    return module->interpreter->invoke(module->interpreter->ctx, args, arg_count, instruction_index, NULL);
  return module->current_entry.invoke(module->current_entry.call_ctx, args, arg_count, instruction_index, NULL);
}

typedef struct validate_current {
  qa_qvm_module *module;
  qa_qvm_validate_fn validate;
  void *validate_ctx;
} validate_current;

static void run_validate_current(void *ctx) {
  validate_current *state = ctx;
  live(state->module);
  if (state->validate != NULL) state->validate(state->validate_ctx);
}

int32_t qa_qvm_module_call_async(qa_qvm_module *module, const int32_t *words, size_t count,
                                 int64_t instruction_index, qa_qvm_validate_fn validate, void *validate_ctx) {
  live(module);
  if (module->options.registration != NULL) qa_qvm_vm_registration_called(module->options.registration);
  qa_qvm_arguments arguments;
  const int32_t *args = words;
  size_t arg_count = count;
  if (instruction_index == 0 || count <= 10) {
    arguments = qa_qvm_make_arguments(words, count);
    args = arguments.words;
    arg_count = 10;
  }
  validate_current state = {module, validate, validate_ctx};
  run_validate_current(&state);
  int32_t result = !module->has_current_entry
      ? module->interpreter->invoke_async(module->interpreter->ctx, args, arg_count, instruction_index, run_validate_current, &state)
      : module->current_entry.invoke_async(module->current_entry.call_ctx, args, arg_count, instruction_index, run_validate_current, &state);
  run_validate_current(&state);
  return result;
}

typedef struct command_job {
  qa_qvm_module *module;
  const int32_t *words;
  size_t count;
  bool asynchronous;
  qa_qvm_validate_fn validate;
  void *validate_ctx;
  int32_t result;
} command_job;

static void command_body(void *ctx) {
  command_job *job = ctx;
  job->result = job->asynchronous
      ? qa_qvm_module_call_async(job->module, job->words, job->count, 0, job->validate, job->validate_ctx)
      : qa_qvm_module_call(job->module, job->words, job->count, 0);
}

/* Command arguments belong to this invocation and are restored by `finally`. */
static int32_t with_command_arguments(qa_qvm_module *module, const char *const *arguments,
                                      size_t argument_count, command_job *job) {
  bool previous_has = module->has_command_arguments;
  const char *const *previous = module->command_arguments;
  size_t previous_count = module->command_argument_count;
  module->has_command_arguments = true;
  module->command_arguments = arguments;
  module->command_argument_count = argument_count;
  char message[512];
  bool ok = qa_range_error_catch(command_body, job, message, sizeof message);
  module->has_command_arguments = previous_has;
  module->command_arguments = previous;
  module->command_argument_count = previous_count;
  if (!ok) qa_range_error("%s", message);
  return job->result;
}

int32_t qa_qvm_module_command_async(qa_qvm_module *module, const int32_t *words, size_t count,
                                    const char *const *arguments, size_t argument_count,
                                    qa_qvm_validate_fn validate, void *validate_ctx) {
  if (module->interpreter->is_active(module->interpreter->ctx) && !module->has_current_entry)
    qa_range_error("QVM is already active; command arguments belong to the current invocation");
  command_job job = {module, words, count, true, validate, validate_ctx, 0};
  return with_command_arguments(module, arguments, argument_count, &job);
}

int32_t qa_qvm_module_command(qa_qvm_module *module, const int32_t *words, size_t count,
                              const char *const *arguments, size_t argument_count) {
  command_job job = {module, words, count, false, NULL, NULL, 0};
  return with_command_arguments(module, arguments, argument_count, &job);
}

typedef struct source_callback_job {
  qa_qvm_module *module;
  qa_qvm_module_function_call *call;
  uint8_t *words;
  size_t words_length;
  int32_t result;
} source_callback_job;

static void run_source_callback(void *ctx) {
  source_callback_job *job = ctx;
  qa_qvm_module *module = job->module;
  live(module);
  qa_qvm_module_syscall syscall;
  syscall.words = job->words;
  syscall.words_length = job->words_length;
  syscall.memory = job->call->memory;
  syscall.memory_length = job->call->memory_length;
  syscall.guest = job->call->guest;
  syscall.call_ctx = job->call->call_ctx;
  syscall.invoke = job->call->invoke;
  syscall.invoke_async = job->call->invoke_async;
  syscall.cancel_function = job->call->cancel_function;
  /* TS sets currentEntry and calls sourceSystemCall directly. */
  rt_system_job inner = {module, syscall, 0};
  with_entry(module, &syscall, rt_system_call_body, &inner);
  job->result = inner.result;
}

typedef struct source_callback_effect {
  qa_qvm_module_function_call *call;
  source_callback_job *job;
} source_callback_effect;

static void source_callback_effect_body(void *ctx) {
  source_callback_effect *effect = ctx;
  effect->call->effect(effect->call->call_ctx, run_source_callback, effect->job);
}

int32_t qa_qvm_module_invoke_source_callback(qa_qvm_module *module, qa_qvm_module_function_call *call,
                                             int32_t pointer, const int32_t *args, size_t arg_count) {
  live(module);
  /* TS validates pointer/args are int32 words ("Invalid original QVM
   * callback words"); the C signature carries int32_t, so the check
   * holds by construction. */
  if (pointer >= 0) {
    return strcmp(call->execution, "synchronous") == 0
        ? call->invoke(call->call_ctx, args, arg_count, pointer, NULL)
        : call->invoke_async(call->call_ctx, args, arg_count, pointer, NULL, NULL);
  }
  size_t words_length = (arg_count + 1) * 4;
  uint8_t *words = malloc(words_length);
  if (words == NULL) qa_range_error("QVM memory allocation failed");
  int32_t trap = (int32_t)(0xffffffffu - (uint32_t)pointer);
  memcpy(words, &trap, 4);
  for (size_t i = 0; i < arg_count; i++) memcpy(words + (i + 1) * 4, &args[i], 4);
  source_callback_job job = {module, call, words, words_length, 0};
  /* The words buffer is released whether or not the effect throws. */
  source_callback_effect effect = {call, &job};
  with_cleanup(source_callback_effect_body, &effect, words);
  return job.result;
}

int32_t qa_qvm_module_evaluate_region(qa_qvm_module *module, const int32_t *words, size_t count,
                                      int64_t instruction_index, const qa_qvm_region_evaluation *region,
                                      const int64_t *inputs, size_t input_count,
                                      const qa_qvm_evaluation_stack *stack) {
  live(module);
  qa_qvm_arguments arguments;
  const int32_t *args = words;
  size_t arg_count = count;
  if (instruction_index == 0 || count <= 10) {
    arguments = qa_qvm_make_arguments(words, count);
    args = arguments.words;
    arg_count = 10;
  }
  qa_qvm_read_only_evaluation evaluation;
  memset(&evaluation, 0, sizeof evaluation);
  evaluation.region = *region;
  /* The landed evaluation carries TS number inputs as doubles. */
  double *input_words = malloc((input_count == 0 ? 1 : input_count) * sizeof *input_words);
  if (input_words == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < input_count; i++) input_words[i] = (double)inputs[i];
  evaluation.inputs = input_words;
  evaluation.input_count = input_count;
  if (stack != NULL) {
    evaluation.has_stack = true;
    evaluation.stack = *stack;
  }
  int32_t result;
  if (!module->has_current_entry)
    result = module->interpreter->invoke(module->interpreter->ctx, args, arg_count, instruction_index, &evaluation);
  else
    result = module->current_entry.invoke(module->current_entry.call_ctx, args, arg_count, instruction_index, &evaluation);
  free(input_words);
  return result;
}

typedef struct counter_job {
  qa_qvm_module *module;
  const int32_t *words;
  size_t count;
  int64_t instruction_index;
} counter_job;

static int32_t run_counter(void *ctx) {
  counter_job *job = ctx;
  return qa_qvm_module_call(job->module, job->words, job->count, job->instruction_index);
}

int32_t qa_qvm_module_evaluate_counter(qa_qvm_module *module, const int32_t *words, size_t count,
                                       int64_t instruction_index, int64_t address,
                                       const int64_t *functions, size_t function_count,
                                       const qa_qvm_evaluation_stack *stack) {
  live(module);
  bool found = false;
  for (size_t i = 0; i < function_count; i++) if (functions[i] == instruction_index) found = true;
  if (!found) qa_range_error("Counter operation lacks its original entry");
  counter_job job = {module, words, count, instruction_index};
  return module->interpreter->evaluate_counter(module->interpreter->ctx, address, functions,
                                               function_count, run_counter, &job, stack);
}

qa_guest_call_result qa_qvm_module_invoke(qa_qvm_module *module, const qa_guest_call_context *context,
                                          const qa_guest_call_value *arguments, size_t argument_count) {
  const qa_guest_callback_reference *callback = &context->callback;
  if (callback->kind != QA_GUEST_CALLBACK_QVM || !same_module(callback->module, module->profile.module) ||
      !same_module(context->module, module->profile.module))
    qa_range_error("QVM callback belongs to a different execution owner");
  int32_t *words = malloc((argument_count == 0 ? 1 : argument_count) * sizeof *words);
  if (words == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < argument_count; i++) {
    const qa_guest_call_value *value = &arguments[i];
    switch (value->kind) {
      case QA_GUEST_VALUE_INT32: words[i] = qa_int32(value->number_value); break;
      case QA_GUEST_VALUE_UINT32: {
        if (!isfinite(value->number_value) || trunc(value->number_value) != value->number_value ||
            value->number_value < 0 || value->number_value > 4294967295.0) {
          free(words);
          qa_range_error("Invalid QVM unsigned word");
        }
        words[i] = (int32_t)(uint32_t)value->number_value;
        break;
      }
      case QA_GUEST_VALUE_FLOAT32: words[i] = (int32_t)qa_float32_to_bits(value->number_value); break;
      case QA_GUEST_VALUE_POINTER: {
        if (!value->pointer_value.has_value) {
          words[i] = 0;
          break;
        }
        (void)module->guest_memory->borrow(module->guest_memory->ctx, value->pointer_value.value, 0);
        int64_t offset = value->pointer_value.value.byte_offset;
        words[i] = (int32_t)(offset == 0 ? (int64_t)module->memory->length : offset);
        break;
      }
      case QA_GUEST_VALUE_INT64:
        free(words);
        qa_range_error("QVM callback requires ABI lowering for int64");
      case QA_GUEST_VALUE_UINT64:
        free(words);
        qa_range_error("QVM callback requires ABI lowering for uint64");
      case QA_GUEST_VALUE_FLOAT64:
        free(words);
        qa_range_error("QVM callback requires ABI lowering for float64");
      case QA_GUEST_VALUE_AGGREGATE:
        free(words);
        qa_range_error("QVM callback requires ABI lowering for aggregate");
      default:
        free(words);
        qa_range_error("QVM callback requires ABI lowering for void");
    }
  }
  int32_t value = qa_qvm_module_call(module, words, argument_count, (int64_t)callback->instruction_index);
  free(words);
  qa_guest_call_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_GUEST_VALUE_INT32;
  result.number_value = value;
  return result;
}

void qa_qvm_module_restart(qa_qvm_module *module, const uint8_t *bytes, size_t length) {
  live(module);
  module->interpreter->restart_bytes(module->interpreter->ctx, bytes, length,
                                     module->profile.module.artifact_path);
}

void qa_qvm_module_checkpoint(qa_qvm_module *module, qa_guest_checkpoint *out) {
  live(module);
  if (module->interpreter->is_active(module->interpreter->ctx))
    qa_range_error("QVM checkpoints require a completed source call");
  const qa_qvm_host_state *state = module->options.host_state;
  if (state == NULL) qa_range_error("QVM host has not bound checkpoint services");
  qa_qvm_host_checkpoint host = state->checkpoint(state->ctx);
  if (!same_module(host.state.module, module->profile.module))
    qa_range_error("QVM host checkpoint belongs to another module");
  memset(out, 0, sizeof *out);
  out->kind = QA_GUEST_CHECKPOINT_QVM;
  out->module = module->profile.module;
  out->has_abi_profile = true;
  out->abi_profile = qa_qvm_module_get_abi_profile(module);
  out->q3_api = module->profile.api.as.q3;
  uint8_t *data = malloc(module->memory->length == 0 ? 1 : module->memory->length);
  if (data == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(data, module->memory->bytes, module->memory->length);
  out->data.bytes = data;
  out->data.length = module->memory->length;
  out->instruction_index = 0;
  out->program_stack = (double)module->interpreter->stack_pointer(module->interpreter->ctx);
  out->random = host.random;
  out->random_count = host.random_count;
  out->callbacks = host.callbacks;
  out->callback_count = host.callback_count;
  out->host_state = host.state;
  uint8_t *host_bytes = malloc(host.state.bytes.length == 0 ? 1 : host.state.bytes.length);
  if (host_bytes == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(host_bytes, host.state.bytes.bytes, host.state.bytes.length);
  out->host_state.bytes.bytes = host_bytes;
}

void qa_qvm_module_restore(qa_qvm_module *module, const qa_guest_checkpoint *checkpoint) {
  live(module);
  if (module->interpreter->is_active(module->interpreter->ctx))
    qa_range_error("Cannot restore an active QVM");
  const char *checkpoint_profile = checkpoint->has_abi_profile ? checkpoint->abi_profile : "q3-modern";
  if (checkpoint->kind != QA_GUEST_CHECKPOINT_QVM ||
      strcmp(checkpoint_profile, qa_qvm_module_get_abi_profile(module)) != 0 ||
      !same_module(checkpoint->module, module->profile.module) ||
      checkpoint->q3_api.kind != module->profile.api.as.q3.kind ||
      checkpoint->q3_api.version != module->profile.api.as.q3.version ||
      !same_module(checkpoint->host_state.module, module->profile.module))
    qa_range_error("QVM checkpoint artifact or API mismatch");
  if (checkpoint->instruction_index != 0 || checkpoint->operand_stack.count != 0 ||
      checkpoint->program_stack != (double)module->memory->length)
    qa_range_error("QVM checkpoint contains a suspended execution stack");
  const qa_qvm_host_state *state = module->options.host_state;
  if (state == NULL) qa_range_error("QVM host has not bound checkpoint services");
  module->interpreter->restore_data(module->interpreter->ctx, checkpoint->data.bytes, checkpoint->data.length);
  qa_qvm_host_checkpoint host = {checkpoint->host_state, checkpoint->random, checkpoint->random_count,
                                 checkpoint->callbacks, checkpoint->callback_count};
  state->restore(state->ctx, &host);
}

void qa_qvm_module_retire(qa_qvm_module *module) {
  qa_qvm_memory_close(module->memory);
  module->retired = true;
  if (module->options.registration != NULL) qa_qvm_vm_registration_free(module->options.registration);
}

const char *qa_qvm_semantics_from_cvar(qa_cvar_registry *cvars, qa_qvm_role role) {
  const char *name = strcmp(role, "qagame") == 0 ? "vm_game" : strcmp(role, "cgame") == 0 ? "vm_cgame" : "vm_ui";
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_register(cvars, name, "2", 1, &snapshot)) return "compiled";
  const bool interpreted = snapshot.integer_value == 1;
  qa_cvar_read_free(&snapshot);
  return interpreted ? "interpreted" : "compiled";
}

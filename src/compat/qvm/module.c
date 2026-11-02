/* See include/qa/compat/qvm/module.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/module.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

const qa_numeric_profile qa_qvm_numeric_profile = {
    "q3:qvm-interpreted",
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
  module->interpreter = options->interpreter;
  module->memory = options->interpreter->address_space;
  module->guest_memory = options->guest_memory;
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

void qa_qvm_module_init(qa_qvm_module *module, const qa_qvm_module_options *options) {
  init_common(module, options);
  if (strcmp(options->artifact->role, "ui") == 0) {
    int32_t words[] = {UI_GETAPIVERSION};
    /* TS retires then rethrows on failure; C errors abort. */
    validate_ui_version(module, qa_qvm_module_call(module, words, 1, 0));
  }
}

void qa_qvm_module_init_deferred(qa_qvm_module *module, const qa_qvm_module_options *options) {
  init_common(module, options);
}

void qa_qvm_module_create(qa_qvm_module *module, const qa_qvm_module_options *options,
                          qa_qvmy_validate_fn validate, void *validate_ctx) {
  init_common(module, options);
  if (strcmp(options->artifact->role, "ui") == 0) {
    int32_t words[] = {UI_GETAPIVERSION};
    validate_ui_version(module, qa_qvm_module_call_async(module, words, 1, 0, validate, validate_ctx));
  } else if (validate != NULL) {
    validate(validate_ctx);
  }
}

void qa_qvm_module_destroy(qa_qvm_module *module) {
  typedef struct { size_t id; void *ctx; } wrapper_entry;
  wrapper_entry *wrappers = module->wrappers;
  for (size_t i = 0; i < module->wrapper_count; i++) free(wrappers[i].ctx);
  free(wrappers);
  memset(module, 0, sizeof *module);
}

int32_t qa_qvm_module_system_call(qa_qvm_module *module, const qa_qvmy_syscall *call) {
  bool previous_has = module->has_current_entry;
  qa_qvmy_syscall previous = module->current_entry;
  module->current_entry = *call;
  module->has_current_entry = true;
  int32_t result = qa_qvm_system_call_invoke(&module->source_system_call, call);
  module->current_entry = previous;
  module->has_current_entry = previous_has;
  return result;
}

void qa_qvm_module_cancel_function(qa_qvm_module *module, qa_qvm_cancellation_scope scope) {
  if (!module->has_current_entry)
    qa_range_error("QVM cancellation requires the current source invocation");
  module->current_entry.cancel_function(module->current_entry.call_ctx, scope);
  qa_range_error("QVM cancellation requires the current source invocation");
}

typedef struct wrapper {
  qa_qvm_module *module;
  qa_qvmy_function_hook_fn hook;
  void *hook_ctx;
  qa_qvmy_function_observer_fn observe;
  void *observe_ctx;
  qa_qvmy_function_resolver_fn resolve;
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

static qa_qvmy_syscall entry_of(qa_qvmy_function_call *call) {
  qa_qvmy_syscall entry;
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

static int32_t wrapped_hook(void *ctx, qa_qvmy_function_call *call) {
  wrapper *wrap = ctx;
  qa_qvm_module *module = wrap->module;
  live(module);
  bool previous_has = module->has_current_entry;
  qa_qvmy_syscall previous = module->current_entry;
  module->current_entry = entry_of(call);
  module->has_current_entry = true;
  int32_t result = wrap->hook(wrap->hook_ctx, call);
  module->current_entry = previous;
  module->has_current_entry = previous_has;
  return result;
}

static size_t bind_entry(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                         qa_qvmy_function_hook_fn hook, void *hook_ctx, bool invocation) {
  live(module);
  if (!same_module(reference->module, module->profile.module))
    qa_range_error("QVM function hook belongs to a different module artifact");
  wrapper *wrap = malloc(sizeof *wrap);
  if (wrap == NULL) qa_range_error("QVM memory allocation failed");
  memset(wrap, 0, sizeof *wrap);
  wrap->module = module;
  wrap->hook = hook;
  wrap->hook_ctx = hook_ctx;
  size_t id = invocation
      ? module->interpreter->bind_invocation(module->interpreter->ctx, (int64_t)reference->instruction_index, wrapped_hook, wrap)
      : module->interpreter->bind_function(module->interpreter->ctx, (int64_t)reference->instruction_index, wrapped_hook, wrap);
  register_wrapper(module, id, wrap);
  return id;
}

size_t qa_qvm_module_bind_function(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                   qa_qvmy_function_hook_fn hook, void *hook_ctx) {
  return bind_entry(module, reference, hook, hook_ctx, false);
}

size_t qa_qvm_module_bind_invocation(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                     qa_qvmy_function_hook_fn hook, void *hook_ctx) {
  return bind_entry(module, reference, hook, hook_ctx, true);
}

typedef struct resolver_cache {
  qa_qvmy_function_hook_fn hook;
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
                             qa_qvmy_function_hook_fn *out_hook, void **out_hook_ctx) {
  resolver_state *state = ctx;
  qa_qvmy_function_hook_fn hook = NULL;
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

size_t qa_qvm_module_bind_function_resolver(qa_qvm_module *module, qa_qvmy_function_resolver_fn resolve, void *resolve_ctx) {
  live(module);
  resolver_state *state = malloc(sizeof *state);
  if (state == NULL) qa_range_error("QVM memory allocation failed");
  memset(state, 0, sizeof *state);
  state->base.module = module;
  state->base.resolve = resolve;
  state->base.resolve_ctx = resolve_ctx;
  size_t id = module->interpreter->bind_function_resolver(module->interpreter->ctx, wrapped_resolver, state);
  register_wrapper(module, id, state);
  return id;
}

static void wrapped_observe(void *ctx, qa_qvmy_function_call *call, int64_t base) {
  wrapper *wrap = ctx;
  qa_qvm_module *module = wrap->module;
  live(module);
  bool previous_has = module->has_current_entry;
  qa_qvmy_syscall previous = module->current_entry;
  module->current_entry = entry_of(call);
  module->has_current_entry = true;
  wrap->observe(wrap->observe_ctx, call, base);
  module->current_entry = previous;
  module->has_current_entry = previous_has;
}

size_t qa_qvm_module_observe_function(qa_qvm_module *module, const qa_guest_callback_reference *reference,
                                      qa_qvmy_function_observer_fn observe, void *observe_ctx) {
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

void qa_qvm_module_unbind(qa_qvm_module *module, size_t id) {
  module->interpreter->unbind(module->interpreter->ctx, id);
  wrapper_entry *wrappers = module->wrappers;
  for (size_t i = 0; i < module->wrapper_count; i++) {
    if (wrappers[i].id == id) {
      free(wrappers[i].ctx);
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
  qa_qvmy_validate_fn validate;
  void *validate_ctx;
} validate_current;

static void run_validate_current(void *ctx) {
  validate_current *state = ctx;
  live(state->module);
  if (state->validate != NULL) state->validate(state->validate_ctx);
}

int32_t qa_qvm_module_call_async(qa_qvm_module *module, const int32_t *words, size_t count,
                                 int64_t instruction_index, qa_qvmy_validate_fn validate, void *validate_ctx) {
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

int32_t qa_qvm_module_command_async(qa_qvm_module *module, const int32_t *words, size_t count,
                                    const char *const *arguments, size_t argument_count,
                                    qa_qvmy_validate_fn validate, void *validate_ctx) {
  if (module->interpreter->is_active(module->interpreter->ctx) && !module->has_current_entry)
    qa_range_error("QVM is already active; command arguments belong to the current invocation");
  bool previous_has = module->has_command_arguments;
  const char *const *previous = module->command_arguments;
  size_t previous_count = module->command_argument_count;
  module->has_command_arguments = true;
  module->command_arguments = arguments;
  module->command_argument_count = argument_count;
  int32_t result = qa_qvm_module_call_async(module, words, count, 0, validate, validate_ctx);
  module->has_command_arguments = previous_has;
  module->command_arguments = previous;
  module->command_argument_count = previous_count;
  return result;
}

int32_t qa_qvm_module_command(qa_qvm_module *module, const int32_t *words, size_t count,
                              const char *const *arguments, size_t argument_count) {
  bool previous_has = module->has_command_arguments;
  const char *const *previous = module->command_arguments;
  size_t previous_count = module->command_argument_count;
  module->has_command_arguments = true;
  module->command_arguments = arguments;
  module->command_argument_count = argument_count;
  int32_t result = qa_qvm_module_call(module, words, count, 0);
  module->has_command_arguments = previous_has;
  module->command_arguments = previous;
  module->command_argument_count = previous_count;
  return result;
}

typedef struct source_callback_job {
  qa_qvm_module *module;
  qa_qvmy_function_call *call;
  uint8_t *words;
  size_t words_length;
  int32_t result;
} source_callback_job;

static void run_source_callback(void *ctx) {
  source_callback_job *job = ctx;
  qa_qvm_module *module = job->module;
  live(module);
  qa_qvmy_syscall syscall;
  syscall.words = job->words;
  syscall.words_length = job->words_length;
  syscall.memory = job->call->memory;
  syscall.memory_length = job->call->memory_length;
  syscall.guest = job->call->guest;
  syscall.call_ctx = job->call->call_ctx;
  syscall.invoke = job->call->invoke;
  syscall.invoke_async = job->call->invoke_async;
  syscall.cancel_function = job->call->cancel_function;
  job->result = qa_qvm_module_system_call(module, &syscall);
}

int32_t qa_qvm_module_invoke_source_callback(qa_qvm_module *module, qa_qvmy_function_call *call,
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
  call->effect(call->call_ctx, run_source_callback, &job);
  free(words);
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

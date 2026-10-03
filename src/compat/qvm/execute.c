#include "internal.h"
#include "qa/qvm_save.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define QVM_OPERANDS 256u
#define QVM_ARGUMENTS 62u
#define NO_INSTRUCTION UINT32_MAX

typedef struct operands {
    int32_t words[QVM_OPERANDS];
    bool initialized[QVM_OPERANDS];
    uint32_t depth;
} operands;
typedef struct source_call source_call;
typedef struct execution_frame execution_frame;
typedef struct host_scope host_scope;
typedef struct binding binding;
typedef struct function_entry {
    uint32_t instruction;
    binding *hook, *observers;
} function_entry;
struct binding {
    binding *next, *next_observer;
    qa_qvm_binding id;
    uint32_t instruction;
    enum { BIND_FUNCTION, BIND_OBSERVER, BIND_RESOLVER } kind;
    union { qa_qvm_function_hook hook; qa_qvm_function_observer observer; qa_qvm_function_resolver resolver; } fn;
    void *context;
    bool active, host_invocations;
};
typedef struct source_region {
    qa_qvm_region_binding binding;
    enum { REGION_READY, REGION_RUNNING, REGION_COMPLETE } state;
} source_region;
struct source_call {
    source_call *parent;
    operands *operands;
    uint32_t stack, instruction, caller, depth, argument_base;
    int32_t return_pc;
    size_t argument_count;
    bool active, proceeded, branches_bound, regions_bound, cancellation_used;
    qa_qvm_branch_binding *branches;
    size_t branch_count;
    source_region *regions;
    size_t region_count;
};
struct execution_frame {
    execution_frame *parent;
    source_call *scope;
    operands *operands;
    uint32_t stack;
    uint32_t floor;
};
typedef enum host_kind { HOST_FUNCTION, HOST_OBSERVER, HOST_SYSCALL, HOST_BRANCH, HOST_REGION, HOST_EFFECT } host_kind;
struct host_scope {
    host_scope *parent;
    execution_frame *frame;
    source_call *owner;
    qa_qvm_call call;
    host_kind kind;
};
typedef struct execution {
    execution_frame *active;
    host_scope *host;
    source_call *cancelled;
    binding *bindings, *resolver;
    function_entry **entries;
    size_t entry_count, entry_capacity;
    uint32_t program_stack;
    uint64_t next_binding, instructions, breaks;
    bool failed;
    qa_error failure;
    struct counter_evaluation *counter;
    qa_qvm_word_projection *projections;
    uint32_t scratch_floor;
    size_t scratch_depth;
} execution;
struct qa_qvm_word_projection {
    qa_qvm_word_projection *previous;
    qa_qvm *vm;
    qa_qvm_source_word *saved;
    size_t count;
    bool closing, observed;
};
typedef struct return_address { uint32_t stack; int32_t pc; } return_address;
typedef struct counter_evaluation {
    uint32_t floor, top;
    qa_qvm_source_word *words;
    size_t word_count;
    const uint32_t *functions;
    uint32_t *ends;
    size_t count;
    uint32_t remaining;
} counter_evaluation;
static atomic_uint_fast64_t next_call_token = 1;

static bool execute(qa_qvm *, uint32_t, uint32_t, operands *, source_call *,
                    const qa_qvm_region_evaluation *, const int32_t *, int32_t *, bool *, qa_error *);
static bool intercept(qa_qvm *, execution_frame *, uint32_t, int32_t, uint32_t,
                      uint32_t, size_t, qa_qvm_function_hook, void *, binding *, int32_t *, bool *, qa_error *);
static bool qualify(const qa_qvm_image *, uint32_t, uint32_t, uint32_t,
                    const qa_qvm_region_evaluation *, bool, qa_error *);

static bool error_at(qa_error *error, size_t offset, const char *message)
{
    qa_qvm_error(error, QA_ERROR_ARGUMENT, offset, message);
    return false;
}

static execution *state(const qa_qvm *vm) { return vm == NULL ? NULL : vm->execution; }
static void latch(qa_qvm *vm, const qa_error *error)
{
    execution *exec = state(vm);
    if (exec == NULL || exec->active == NULL || exec->failed) return;
    exec->failed = true;
    if (error != NULL && error->code != QA_OK) exec->failure = *error;
    else qa_error_set(&exec->failure, QA_ERROR_ARGUMENT, 0, "QVM host callback failed");
}
static bool healthy(qa_qvm *vm, qa_error *error)
{
    execution *exec = state(vm);
    if (exec->failed) { if (error != NULL) *error = exec->failure; return false; }
    return exec->cancelled == NULL;
}
static int32_t signed_bits(uint32_t value) { int32_t result; memcpy(&result, &value, 4); return result; }
static float word_float(int32_t value) { float result; memcpy(&result, &value, 4); return result; }
static int32_t float_word(float value) { int32_t result; memcpy(&result, &value, 4); return result; }

static bool counter_access(qa_qvm *vm, uint32_t address, size_t size, bool writing,
                            int32_t *word, bool *virtual_word, qa_error *error)
{
    counter_evaluation *counter = state(vm)->counter;
    *virtual_word = false;
    if (counter == NULL) return true;
    for (size_t i = 0; i < counter->word_count; ++i) {
        qa_qvm_source_word *value = counter->words + i;
        if (address < (uint64_t)value->offset + 4 && (uint64_t)address + size > value->offset) {
            if (address != value->offset || size != 4)
                return error_at(error, address, "QVM isolated counter requires a whole word access");
            if (writing) value->value = *word; else *word = value->value;
            *virtual_word = true; return true;
        }
    }
    uint32_t floor = counter->floor;
    if (state(vm)->active != NULL && state(vm)->active->stack > floor) floor = state(vm)->active->stack;
    if (writing && (address < floor || (uint64_t)address + size > counter->top))
        return error_at(error, address, "QVM counter evaluation attempted an unrelated write");
    return true;
}

static bool read_word(qa_qvm *vm, uint32_t address, int32_t *out, qa_error *error)
{
    if (!qa_qvm_raw_range(vm, address, 4, error)) return false;
    bool virtual_word;
    if (!counter_access(vm, address, 4, false, out, &virtual_word, error)) return false;
    if (virtual_word) return true;
    *out = qa_load_i32le(vm->data + address);
    return true;
}
static bool write_word(qa_qvm *vm, uint32_t address, int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    if (!qa_qvm_raw_range(vm, address, 4, error)) return false;
    bool virtual_word;
    if (!counter_access(vm, address, 4, true, &value, &virtual_word, error)) return false;
    if (virtual_word) return true;
    if (state(vm)->counter != NULL) { memcpy(vm->data + address, bytes, 4); return true; }
    return qa_qvm_write(vm, address, (qa_bytes){bytes, 4}, error);
}
static bool stack_address(qa_qvm *vm, int64_t value, uint32_t *out, qa_error *error)
{
    if (value < 0 || (uint64_t)value > vm->data_size || (value & 3) != 0)
        return error_at(error, 0, "QVM program stack is outside memory or misaligned");
    *out = (uint32_t)value;
    return true;
}
static bool push(operands *stack, int32_t word, bool initialized, qa_error *error)
{
    if (stack->depth == QVM_OPERANDS - 1) return error_at(error, 0, "QVM operand stack overflow");
    ++stack->depth;
    /* PUSH reserves the existing cell, exactly as the source operand stack. */
    if (initialized) { stack->words[stack->depth] = word; stack->initialized[stack->depth] = true; }
    return true;
}
static bool peek(const operands *stack, int32_t *word, qa_error *error)
{
    if (!stack->initialized[stack->depth]) return error_at(error, 0, "QVM reads an uninitialized operand");
    *word = stack->words[stack->depth]; return true;
}
static bool pop(operands *stack, int32_t *word, qa_error *error)
{
    if (stack->depth == 0) return error_at(error, 0, "QVM operand stack underflow");
    if (!peek(stack, word, error)) return false;
    --stack->depth; return true;
}
static void set_top(operands *stack, int32_t word)
{ stack->words[stack->depth] = word; stack->initialized[stack->depth] = true; }

/* Ordinary sequential decode reuses the current compact instruction. Only a
 * changed PC needs a search. Operand tails remain zero in the source PC space. */
static bool code_word(const qa_qvm_image *image, int32_t pc, size_t *hint, int32_t *out, qa_error *error)
{
    if (pc < 0 || (uint32_t)pc >= image->code_length) return error_at(error, 0, "QVM program counter exceeds code");
    size_t index = *hint;
    if (index >= image->instruction_count || image->instructions[index].byte_offset > (uint32_t)pc
        || (index + 1 < image->instruction_count && image->instructions[index + 1].byte_offset <= (uint32_t)pc)) {
        if (index + 1 < image->instruction_count && image->instructions[index + 1].byte_offset == (uint32_t)pc) ++index;
        else {
            size_t low = 0, high = image->instruction_count;
            while (low < high) {
                size_t middle = low + (high - low) / 2;
                if (image->instructions[middle].byte_offset <= (uint32_t)pc) low = middle + 1;
                else high = middle;
            }
            index = low - 1;
        }
    }
    *hint = index;
    const qa_qvm_instruction *instruction = &image->instructions[index];
    uint32_t relative = (uint32_t)pc - instruction->byte_offset;
    *out = 0;
    if (relative == 0) *out = instruction->opcode;
    else if (relative == 1 && instruction->operand_width != 0) {
        *out = instruction->operand;
        if (instruction->opcode >= QA_QVM_EQ && instruction->opcode <= QA_QVM_GEF)
            *out = (int32_t)image->instructions[instruction->operand].byte_offset;
    }
    return true;
}
static bool target_pc(const qa_qvm_image *image, int32_t instruction, int32_t *pc, qa_error *error)
{
    if (instruction < 0 || (uint32_t)instruction >= image->instruction_count)
        return error_at(error, 0, "QVM instruction target is outside code");
    *pc = (int32_t)image->instructions[instruction].byte_offset; return true;
}
static bool function(const qa_qvm_image *image, uint32_t instruction, qa_error *error)
{
    return (instruction < image->instruction_count && image->instructions[instruction].opcode == QA_QVM_ENTER)
        || error_at(error, instruction, "QVM operation requires an original function entry");
}
static uint32_t function_end(const qa_qvm_image *image, uint32_t instruction)
{
    uint32_t end = instruction + 1;
    while (end < image->instruction_count && image->instructions[end].opcode != QA_QVM_ENTER) ++end;
    return end;
}

static function_entry *entry(execution *exec, uint32_t instruction)
{
    size_t low = 0, high = exec->entry_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (exec->entries[middle]->instruction < instruction) low = middle + 1; else high = middle;
    }
    return low < exec->entry_count && exec->entries[low]->instruction == instruction ? exec->entries[low] : NULL;
}
static function_entry *ensure_entry(execution *exec, uint32_t instruction, qa_error *error)
{
    function_entry *found = entry(exec, instruction);
    if (found != NULL) return found;
    if (exec->entry_count == exec->entry_capacity) {
        size_t capacity = exec->entry_capacity == 0 ? 16 : exec->entry_capacity * 2;
        if (capacity < exec->entry_capacity || capacity > SIZE_MAX / sizeof(*exec->entries)) {
            qa_qvm_error(error, QA_ERROR_MEMORY, 0, "QVM function binding capacity exhausted"); return NULL;
        }
        function_entry **resized = realloc(exec->entries, capacity * sizeof(*resized));
        if (resized == NULL) { qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM function index"); return NULL; }
        exec->entries = resized; exec->entry_capacity = capacity;
    }
    found = calloc(1, sizeof(*found));
    if (found == NULL) { qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM function entry"); return NULL; }
    found->instruction = instruction;
    size_t index = 0;
    while (index < exec->entry_count && exec->entries[index]->instruction < instruction) ++index;
    memmove(exec->entries + index + 1, exec->entries + index, (exec->entry_count - index) * sizeof(*exec->entries));
    exec->entries[index] = found; ++exec->entry_count;
    return found;
}
static void collect_bindings(execution *exec)
{
    if (exec->active != NULL) return;
    for (size_t i = 0; i < exec->entry_count; ++i) {
        function_entry *target = exec->entries[i];
        if (target->hook != NULL && !target->hook->active) target->hook = NULL;
        binding **link = &target->observers;
        while (*link != NULL) { if (!(*link)->active) *link = (*link)->next_observer; else link = &(*link)->next_observer; }
    }
    if (exec->resolver != NULL && !exec->resolver->active) exec->resolver = NULL;
    binding **link = &exec->bindings;
    while (*link != NULL) {
        binding *current = *link;
        if (current->active) link = &current->next;
        else { *link = current->next; free(current); }
    }
}

bool qa_qvm_execution_create(qa_qvm *vm, qa_error *error)
{
    execution *exec = calloc(1, sizeof(*exec));
    if (exec == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM execution state");
    exec->program_stack = (uint32_t)vm->data_size; vm->execution = exec; return true;
}
void qa_qvm_execution_reset(qa_qvm *vm)
{
    execution *exec = state(vm);
    exec->program_stack = (uint32_t)vm->data_size;
    exec->cancelled = NULL; exec->failed = false; exec->failure = (qa_error){0};
    collect_bindings(exec);
}
void qa_qvm_execution_checkpoint(const qa_qvm *vm, uint64_t values[3])
{
    const execution *exec = state(vm);
    values[0] = exec->next_binding;
    values[1] = exec->breaks;
    values[2] = exec->instructions;
}
bool qa_qvm_execution_checkpoint_ready(const qa_qvm *vm, const uint64_t values[3],
                                         bool candidate, qa_error *error)
{
    const execution *exec = state(vm);
    if (exec->active || exec->host || exec->counter || exec->projections || exec->scratch_depth ||
        exec->program_stack != vm->data_size)
        return error_at(error, 0, "QVM checkpoint requires an idle original execution stack");
    if (candidate && (exec->instructions || exec->breaks || exec->failed ||
        values[0] < exec->next_binding))
        return error_at(error, 0, "QVM candidate has executed or exceeds its saved binding generation");
    return true;
}
void qa_qvm_execution_restore(qa_qvm *vm, const uint64_t values[3], bool candidate)
{
    execution *exec = state(vm);
    if (candidate || values[0] > exec->next_binding) exec->next_binding = values[0];
    exec->breaks = values[1];
    exec->instructions = values[2];
}
static bool checkpoint_callbacks(const qa_qvm *vm, const qa_qvm_saved_function *expected,
    size_t count, const qa_qvm_saved_resolver *resolver, bool watches, qa_error *error)
{
    if (!qa_qvm_live(vm,error)) return false;
    if ((count && !expected) || (!watches && vm->watches) || vm->lifecycle_depth || vm->write_delivery_depth)
        return error_at(error,0,"QVM callback inventory has an unqualified write or lifecycle owner");
    uint64_t counters[3];
    qa_qvm_execution_checkpoint(vm,counters);
    if (!qa_qvm_execution_checkpoint_ready(vm,counters,false,error)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!expected[i].binding || !expected[i].hook)
            return error_at(error,0,"QVM source callback descriptor is absent");
        for (size_t j = 0; j < i; ++j)
            if (expected[i].binding == expected[j].binding)
                return error_at(error,0,"QVM source callback descriptor is duplicated");
    }
    if (resolver) {
        if (!resolver->binding || !resolver->resolver || count == SIZE_MAX)
            return error_at(error,0,"QVM dynamic resolver descriptor is absent or exceeds its inventory");
        for (size_t i = 0; i < count; ++i)
            if (expected[i].binding == resolver->binding)
                return error_at(error,0,"QVM resolver identity duplicates a source function");
    }
    size_t actual = 0;
    for (const binding *value = state(vm)->bindings; value; value = value->next) {
        if (value->kind == BIND_RESOLVER) {
            if (!value->active || !resolver || value != state(vm)->resolver ||
                value->instruction != NO_INSTRUCTION || value->host_invocations ||
                value->id != resolver->binding || value->fn.resolver != resolver->resolver ||
                value->context != resolver->context)
                return error_at(error,0,"QVM installed resolver differs from its exact source owner");
            ++actual;
            continue;
        }
        size_t i = 0;
        while (i < count && expected[i].binding != value->id) ++i;
        if (!value->active || value->kind != BIND_FUNCTION || i == count ||
            value->instruction != expected[i].instruction || value->fn.hook != expected[i].hook ||
            value->context != expected[i].context || value->host_invocations != expected[i].host_invocations)
            return error_at(error,0,"QVM installed callback differs from its exact source owner");
        ++actual;
    }
    return actual == count + (resolver != NULL) || error_at(error,0,"QVM source callback inventory is incomplete");
}
bool qa_qvm_checkpoint_callbacks(const qa_qvm *vm, const qa_qvm_saved_function *expected,
    size_t count, const qa_qvm_saved_resolver *resolver, qa_error *error)
{ return checkpoint_callbacks(vm,expected,count,resolver,false,error); }
bool qa_qvm_checkpoint_inventory(const qa_qvm *vm, const qa_qvm_saved_function *expected,
    size_t count, const qa_qvm_saved_resolver *resolver,
    const qa_qvm_saved_write_watch *watches, size_t watch_count, qa_error *error)
{
    return qa_qvm_memory_checkpoint_watches(vm,watches,watch_count,error) &&
        checkpoint_callbacks(vm,expected,count,resolver,true,error);
}
bool qa_qvm_checkpoint_functions(const qa_qvm *vm, const qa_qvm_saved_function *expected,
    size_t count, qa_error *error)
{ return qa_qvm_checkpoint_callbacks(vm, expected, count, NULL, error); }
static bool restore_callbacks(qa_qvm *vm, uint64_t generation,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, const qa_qvm_saved_resolver *resolver, qa_qvm_binding saved_resolver,
    bool watches, qa_error *error)
{
    if (count && !saved) return error_at(error, 0, "QVM candidate saved binding inventory is absent");
    if (!checkpoint_callbacks(vm, constructed, count, resolver, watches, error)) return false;
    size_t total = count + (resolver != NULL);
    execution *exec = state(vm);
    if (exec->next_binding != total || (!watches && vm->next_watch != 1) || vm->write_sequence ||
        exec->instructions || exec->breaks || exec->failed || generation < total)
        return error_at(error, 0, "QVM binding reconstruction requires untouched constructor identities");
    if ((resolver && (!saved_resolver || saved_resolver > generation)) || (!resolver && saved_resolver))
        return error_at(error, 0, "QVM saved resolver differs from its actual constructor owner");
    for (size_t i = 0; i < count; ++i) {
        if (!saved[i] || saved[i] > generation)
            return error_at(error, i, "QVM saved binding leaves its source generation");
        if (resolver && saved[i] == saved_resolver)
            return error_at(error, i, "QVM saved function duplicates its resolver identity");
        for (size_t j = 0; j < i; ++j)
            if (saved[i] == saved[j])
                return error_at(error, i, "QVM saved binding identity is duplicated");
    }
    /* All lookups use constructor identities before changing any binding. */
    for (binding *value = exec->bindings; value; value = value->next) {
        if (value->kind == BIND_RESOLVER) { value->id = saved_resolver; continue; }
        size_t i = 0;
        while (constructed[i].binding != value->id) ++i;
        value->id = saved[i];
    }
    exec->next_binding = generation;
    return true;
}
bool qa_qvm_execution_restore_callbacks(qa_qvm *vm, uint64_t generation,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, const qa_qvm_saved_resolver *resolver, qa_qvm_binding saved_resolver,
    qa_error *error)
{ return restore_callbacks(vm,generation,constructed,saved,count,resolver,saved_resolver,false,error); }
bool qa_qvm_execution_restore_inventory(qa_qvm *vm, uint64_t generation,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, const qa_qvm_saved_resolver *resolver, qa_qvm_binding saved_resolver,
    qa_error *error)
{ return restore_callbacks(vm,generation,constructed,saved,count,resolver,saved_resolver,true,error); }
bool qa_qvm_execution_restore_bindings(qa_qvm *vm, uint64_t generation,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, qa_error *error)
{ return qa_qvm_execution_restore_callbacks(vm, generation, constructed, saved, count, NULL, 0, error); }
void qa_qvm_execution_destroy(qa_qvm *vm)
{
    execution *exec = state(vm); if (exec == NULL) return;
    for (size_t i = 0; i < exec->entry_count; ++i) free(exec->entries[i]);
    binding *current = exec->bindings;
    while (current != NULL) { binding *next = current->next; free(current); current = next; }
    free(exec->entries); free(exec); vm->execution = NULL;
}
bool qa_qvm_execution_active(const qa_qvm *vm)
{
    const execution *exec = state(vm);
    return exec && (exec->active || exec->projections || exec->scratch_depth);
}
bool qa_qvm_execution_reentry(const qa_qvm *vm, qa_error *error)
{
    if (!qa_qvm_mutable(vm, error)) return false;
    if (vm->lifecycle_depth != 0) return error_at(error, 0, "QVM execution is unavailable during checkpoint or restore");
    execution *exec = state(vm);
    return (exec != NULL && (exec->active == NULL || (exec->host != NULL && exec->host->frame == exec->active)))
        || error_at(error, 0, "QVM recursive entry requires the current host callback");
}
static host_scope *find_scope(const qa_qvm_call *call)
{
    execution *exec = call == NULL ? NULL : state(call->vm);
    if (exec == NULL) return NULL;
    for (host_scope *scope = exec->host; scope != NULL; scope = scope->parent)
        if (scope->call.token == call->token && scope->call.argument_base == call->argument_base
            && scope->call.argument_count == call->argument_count && scope->call.instruction == call->instruction
            && scope->call.caller_instruction == call->caller_instruction) return scope;
    return NULL;
}
bool qa_qvm_execution_token(const qa_qvm_call *call, qa_error *error)
{
    host_scope *scope = find_scope(call);
    if (scope == NULL || state(call->vm)->host != scope || state(call->vm)->active != scope->frame)
        return error_at(error, 0, "QVM call token is expired or suspended");
    return qa_qvm_mutable(call->vm, error);
}
static bool open_host(qa_qvm *vm, host_scope *host, execution_frame *frame,
                      source_call *owner, host_kind kind, uint32_t instruction,
                      uint32_t caller, uint32_t base, size_t count, qa_error *error)
{
    execution *exec = state(vm);
    uint_fast64_t token = atomic_load_explicit(&next_call_token, memory_order_relaxed);
    for (;;) {
        if (token == UINT64_MAX) return error_at(error, 0, "QVM call token space exhausted");
        if (atomic_compare_exchange_weak_explicit(&next_call_token, &token, token + 1,
                                                  memory_order_relaxed, memory_order_relaxed)) break;
    }
    *host = (host_scope){exec->host, frame, owner,
        {vm, (uint64_t)token, instruction, caller, base, count}, kind};
    if (error != NULL) *error = (qa_error){0};
    exec->host = host; return true;
}
static bool close_host(qa_qvm *vm, host_scope *host, bool ok, qa_error *error)
{
    execution *exec = state(vm);
    if (!ok && (exec->cancelled == NULL || (error != NULL && error->code != QA_OK))) latch(vm, error);
    exec->host = host->parent;
    return healthy(vm, error);
}
bool qa_qvm_execution_effect(qa_qvm *vm, qa_qvm_effect_fn perform, void *context, qa_error *error)
{
    if (perform == NULL || !qa_qvm_mutable(vm, error)) return false;
    execution *exec = state(vm);
    if (exec->active == NULL) return perform(context, error);
    uint32_t saved = exec->program_stack;
    if (!stack_address(vm, (int64_t)exec->active->stack - 4, &exec->program_stack, error)) return false;
    source_call *owner = exec->host != NULL && exec->host->frame == exec->active ? exec->host->owner : exec->active->scope;
    host_scope host;
    if (!open_host(vm, &host, exec->active, owner, HOST_EFFECT,
                   NO_INSTRUCTION, NO_INSTRUCTION, 0, 0, error)) { exec->program_stack = saved; return false; }
    bool ok = perform(context, error);
    ok = close_host(vm, &host, ok, error);
    exec->program_stack = saved; return ok;
}

static binding *new_binding(qa_qvm *vm, uint32_t instruction, qa_error *error)
{
    execution *exec = state(vm);
    if (exec->next_binding == UINT64_MAX) { error_at(error, 0, "QVM binding identities exhausted"); return NULL; }
    binding *result = calloc(1, sizeof(*result));
    if (result == NULL) { qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM binding"); return NULL; }
    result->id = ++exec->next_binding; result->instruction = instruction; result->active = true;
    result->next = exec->bindings; exec->bindings = result;
    return result;
}
bool qa_qvm_bind_function(qa_qvm *vm, uint32_t instruction, bool host_invocations,
                          qa_qvm_function_hook hook, void *context, qa_qvm_binding *out, qa_error *error)
{
    if (!qa_qvm_mutable(vm, error) || !function(vm->image, instruction, error)) return false;
    if (hook == NULL || out == NULL) return error_at(error, instruction, "QVM function hook or output is missing");
    execution *exec = state(vm);
    function_entry *target = ensure_entry(exec, instruction, error);
    if (target == NULL) return false;
    if (target->hook != NULL && target->hook->active) return error_at(error, instruction, "QVM function already has a hook");
    binding *value = new_binding(vm, instruction, error);
    if (value == NULL) return false;
    value->kind = BIND_FUNCTION; value->fn.hook = hook; value->context = context; value->host_invocations = host_invocations;
    target->hook = value; *out = value->id; return true;
}
bool qa_qvm_observe_function(qa_qvm *vm, uint32_t instruction, qa_qvm_function_observer observe,
                             void *context, qa_qvm_binding *out, qa_error *error)
{
    if (!qa_qvm_live(vm, error) || !function(vm->image, instruction, error)) return false;
    if (vm->candidate_inventory)
        return error_at(error, instruction, "Candidate callback inventory already sealed");
    if (observe == NULL || out == NULL) return error_at(error, instruction, "QVM function observer or output is missing");
    function_entry *target = ensure_entry(state(vm), instruction, error);
    if (target == NULL) return false;
    binding *value = new_binding(vm, instruction, error);
    if (value == NULL) return false;
    value->kind = BIND_OBSERVER; value->fn.observer = observe; value->context = context;
    binding **tail = &target->observers;
    while (*tail != NULL) tail = &(*tail)->next_observer;
    *tail = value; *out = value->id; return true;
}
bool qa_qvm_bind_resolver(qa_qvm *vm, qa_qvm_function_resolver resolver, void *context,
                         qa_qvm_binding *out, qa_error *error)
{
    if (!qa_qvm_mutable(vm, error)) return false;
    if (resolver == NULL || out == NULL) return error_at(error, 0, "QVM resolver or output is missing");
    execution *exec = state(vm);
    if (exec->resolver != NULL && exec->resolver->active) return error_at(error, 0, "QVM resolver is already bound");
    binding *value = new_binding(vm, NO_INSTRUCTION, error);
    if (value == NULL) return false;
    value->kind = BIND_RESOLVER; value->fn.resolver = resolver; value->context = context;
    exec->resolver = value; *out = value->id; return true;
}
bool qa_qvm_unbind(qa_qvm *vm, qa_qvm_binding id, qa_error *error)
{
    if (!qa_qvm_live(vm, error)) return false;
    execution *exec = state(vm);
    for (binding *current = exec->bindings; current != NULL; current = current->next)
        if (current->id == id && current->active) {
            if (vm->candidate_inventory) vm->candidate_inventory_invalid=true;
            current->active = false; collect_bindings(exec); return true;
        }
    return qa_qvm_error(error, QA_ERROR_NOT_FOUND, 0, "QVM binding does not exist");
}

bool qa_qvm_cancel(const qa_qvm_call *call, qa_error *error)
{
    host_scope *target = find_scope(call);
    if (target == NULL || target->kind != HOST_FUNCTION || target->owner == NULL || !target->owner->active
        || !qa_qvm_execution_reentry(call->vm, error)) return error_at(error, 0, "QVM cancellation scope is absent or expired");
    execution *exec = state(call->vm);
    source_call *current = exec->host->owner;
    while (current != NULL && current != target->owner) current = current->parent;
    if (current == NULL || exec->cancelled != NULL || target->owner->cancellation_used)
        return error_at(error, 0, "QVM cancellation requires an unused ancestor scope");
    target->owner->cancellation_used = true;
    exec->cancelled = target->owner;
    return true;
}
bool qa_qvm_call_cancelled(const qa_qvm_call *call, bool *out, qa_error *error)
{
    if (!out) return error_at(error, 0, "QVM cancellation observation requires its destination");
    if (!qa_qvm_execution_token(call, error)) return false;
    *out = state(call->vm)->cancelled != NULL;
    return true;
}
bool qa_qvm_local_word(const qa_qvm_call *call, uint32_t offset, int32_t *out, qa_error *error)
{
    if (!qa_qvm_execution_token(call, error)) return false;
    host_scope *host = state(call->vm)->host;
    if (host->kind != HOST_REGION || host->owner == NULL || out == NULL)
        return error_at(error, offset, "QVM local access requires a region callback");
    int32_t size = call->vm->image->instructions[host->owner->instruction].operand;
    if (offset < 8 || (offset & 3) != 0 || size < 4 || offset > (uint32_t)size - 4)
        return error_at(error, offset, "QVM region local exceeds its original frame");
    return read_word(call->vm, host->frame->stack + offset, out, error);
}
bool qa_qvm_proceed(const qa_qvm_call *call, int32_t *out, qa_error *error)
{
    qa_error local_error = {0};
    if (error == NULL) error = &local_error;
    if (!qa_qvm_execution_token(call, error)) return false;
    execution *exec = state(call->vm); host_scope *host = exec->host;
    source_call *source = host->owner;
    if (host->kind != HOST_FUNCTION || source == NULL || source->proceeded || out == NULL) {
        bool ok = error_at(error, 0, "QVM original continuation must run once from its function hook"); latch(call->vm, error); return ok;
    }
    if (!healthy(call->vm, error)) { if (exec->cancelled != NULL && !exec->failed) { *out = 0; return true; } return false; }
    source->proceeded = true;
    int32_t result;
    bool ok = execute(call->vm, source->instruction, source->stack, source->operands, source, NULL, NULL, &result, NULL, error);
    if (!ok && exec->cancelled != NULL && !exec->failed) { *out = 0; return true; }
    if (!ok) { latch(call->vm, error); return false; }
    *out = result; return true;
}

bool qa_qvm_bind_branches(const qa_qvm_call *call, const qa_qvm_branch_binding *bindings,
                         size_t count, qa_error *error)
{
    if (!qa_qvm_execution_token(call, error)) return false;
    host_scope *host = state(call->vm)->host; source_call *source = host->owner;
    if (host->kind != HOST_FUNCTION || source == NULL || source->proceeded || source->branches_bound
        || (count != 0 && bindings == NULL) || count > SIZE_MAX / sizeof(*bindings))
        return error_at(error, 0, "QVM branches must bind once before proceeding");
    uint32_t end = function_end(call->vm->image, source->instruction);
    for (size_t i = 0; i < count; ++i) {
        uint32_t index = bindings[i].instruction;
        if (index <= source->instruction || index >= end || bindings[i].decide == NULL
            || call->vm->image->instructions[index].opcode < QA_QVM_EQ || call->vm->image->instructions[index].opcode > QA_QVM_GEF)
            return error_at(error, index, "QVM binding is not a conditional in the owning function");
        for (size_t j = 0; j < i; ++j)
            if (bindings[j].instruction == index) return error_at(error, index, "Duplicate QVM branch binding");
    }
    qa_qvm_branch_binding *copy = count == 0 ? NULL : malloc(count * sizeof(*copy));
    if (count != 0 && copy == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM branch bindings");
    if (count != 0) memcpy(copy, bindings, count * sizeof(*copy));
    source->branches = copy; source->branch_count = count; source->branches_bound = true; return true;
}
static int region_order(const void *a, const void *b)
{
    const source_region *left = a, *right = b;
    return left->binding.entry < right->binding.entry ? -1 : left->binding.entry > right->binding.entry ? 1 : 0;
}
bool qa_qvm_bind_regions(const qa_qvm_call *call, const qa_qvm_region_binding *bindings,
                        size_t count, qa_error *error)
{
    if (!qa_qvm_execution_token(call, error)) return false;
    host_scope *host = state(call->vm)->host; source_call *source = host->owner;
    if (host->kind != HOST_FUNCTION || source == NULL || source->proceeded || source->regions_bound
        || (count != 0 && bindings == NULL) || count > SIZE_MAX / sizeof(source_region))
        return error_at(error, 0, "QVM regions must bind once before proceeding");
    source_region *copy = count == 0 ? NULL : calloc(count, sizeof(*copy));
    if (count != 0 && copy == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM region bindings");
    for (size_t i = 0; i < count; ++i) {
        if (bindings[i].enter == NULL) { free(copy); return error_at(error, 0, "QVM region entry callback is missing"); }
        if (!qualify(call->vm->image, source->instruction, bindings[i].entry, bindings[i].join,
                     NULL, call->vm->options.semantics == QA_QVM_INTERPRETED, error)) { free(copy); return false; }
        copy[i].binding = bindings[i];
    }
    if (count > 1) qsort(copy, count, sizeof(*copy), region_order);
    for (size_t i = 1; i < count; ++i)
        if (copy[i].binding.entry < copy[i - 1].binding.join) { free(copy); return error_at(error, 0, "QVM bound regions overlap"); }
    source->regions = copy; source->region_count = count; source->regions_bound = true; return true;
}

typedef enum proof_kind { PROOF_UNKNOWN, PROOF_LOCAL, PROOF_DERIVED, PROOF_CONSTANT } proof_kind;
typedef struct proof_word { proof_kind kind; int64_t value; } proof_word;
typedef struct proof_path {
    proof_word words[QVM_OPERANDS];
    uint32_t depth;
    uint8_t *initialized;
} proof_path;
static bool local(proof_word word) { return word.kind == PROOF_LOCAL || word.kind == PROOF_DERIVED; }
static bool known_local(const uint8_t *initialized, int64_t offset, uint32_t frame)
{ return offset >= 0 && offset < frame && initialized[(uint32_t)offset] != 0; }
static bool proof_edge(proof_path **paths, uint32_t from, uint32_t target, uint32_t entry_pc,
                        uint32_t join, const proof_path *source, uint32_t locals, qa_error *error)
{
    if (target <= from || target > join) return error_at(error, from, "QVM region edge escapes or runs backward");
    proof_path **slot = &paths[target - entry_pc];
    if (*slot == NULL) {
        *slot = malloc(sizeof(**slot));
        if (*slot == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, from, "Allocating QVM region proof");
        **slot = *source; (*slot)->initialized = NULL;
        if (locals != 0) {
            (*slot)->initialized = malloc(locals);
            if ((*slot)->initialized == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, from, "Allocating QVM local proof");
            memcpy((*slot)->initialized, source->initialized, locals);
        }
        return true;
    }
    proof_path *previous = *slot;
    if (previous->depth != source->depth) return error_at(error, target, "QVM region paths disagree on operand depth");
    for (uint32_t i = 0; i < source->depth; ++i) {
        proof_word a = previous->words[i], b = source->words[i];
        if (a.kind == b.kind && a.value == b.value && (a.kind == PROOF_LOCAL || a.kind == PROOF_CONSTANT)) continue;
        previous->words[i] = (proof_word){local(a) || local(b) ? PROOF_DERIVED : PROOF_UNKNOWN, 0};
    }
    for (uint32_t i = 0; i < locals; ++i) previous->initialized[i] &= source->initialized[i];
    return true;
}
static bool valid_local(uint32_t offset, uint32_t frame)
{ return offset >= 8 && (offset & 3) == 0 && frame >= 4 && offset <= frame - 4; }
static bool qualify(const qa_qvm_image *image, uint32_t owner, uint32_t entry_pc, uint32_t join,
                    const qa_qvm_region_evaluation *evaluation, bool interpreted, qa_error *error)
{
    if (image == NULL) return error_at(error, owner, "Missing QVM region image");
    if (!function(image, owner, error)) return false;
    int32_t frame_size = image->instructions[owner].operand;
    uint32_t end = function_end(image, owner);
    if (frame_size < 0 || (uint32_t)frame_size > image->memory_size || (frame_size & 3) != 0
        || entry_pc <= owner || join <= entry_pc || join >= end)
        return error_at(error, owner, "QVM region exceeds its aligned owning frame");
    uint32_t frame = (uint32_t)frame_size;
    if (evaluation != NULL) {
        if ((evaluation->input_count != 0 && evaluation->inputs == NULL)
            || evaluation->input_count > frame / 4 || evaluation->result_offset < -1
            || (evaluation->result_offset >= 0 && !valid_local((uint32_t)evaluation->result_offset, frame)))
            return error_at(error, owner, "Invalid QVM region live-ins or result");
        for (size_t i = 0; i < evaluation->input_count; ++i) {
            if (!valid_local(evaluation->inputs[i], frame)) return error_at(error, owner, "QVM live-in exceeds its frame");
            for (size_t j = 0; j < i; ++j)
                if (evaluation->inputs[i] == evaluation->inputs[j]) return error_at(error, owner, "Duplicate QVM region live-in");
        }
    }
    size_t count = (size_t)join - entry_pc + 1;
    if (count > SIZE_MAX / sizeof(proof_path *)) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "QVM region proof is too large");
    proof_path **paths = calloc(count, sizeof(*paths));
    if (paths == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM region proof paths");
    paths[0] = calloc(1, sizeof(*paths[0]));
    bool ok = paths[0] != NULL, joined = false;
    uint32_t locals = evaluation == NULL ? 0 : frame;
    if (ok && locals != 0) { paths[0]->initialized = calloc(locals, 1); ok = paths[0]->initialized != NULL; }
    if (!ok) qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM initial proof state");
    if (ok && evaluation != NULL)
        for (size_t i = 0; i < evaluation->input_count; ++i) paths[0]->initialized[evaluation->inputs[i]] = 1;
    for (uint32_t pc = entry_pc; ok && pc <= join; ++pc) {
        proof_path *path = paths[pc - entry_pc];
        if (path == NULL) continue;
        if (pc == join) {
            if (path->depth != 0) { ok = error_at(error, pc, "QVM region join retains operands"); break; }
            if (evaluation != NULL && evaluation->result_offset >= 0
                && !known_local(path->initialized, evaluation->result_offset, frame)) {
                ok = error_at(error, pc, "QVM region result is not initialized on every path"); break;
            }
            joined = true; continue;
        }
        qa_qvm_instruction instruction = image->instructions[pc];
        uint8_t op = instruction.opcode;
        bool binary = (op >= QA_QVM_ADD && op <= QA_QVM_RSHU && op != QA_QVM_BCOM)
            || (op >= QA_QVM_ADDF && op <= QA_QVM_MULF);
        bool branch = op >= QA_QVM_EQ && op <= QA_QVM_GEF;
        bool store = op >= QA_QVM_STORE1 && op <= QA_QVM_STORE4;
        bool load = op >= QA_QVM_LOAD1 && op <= QA_QVM_LOAD4;
        uint32_t required = binary || branch || store || op == QA_QVM_BLOCK_COPY ? 2
            : load || op == QA_QVM_POP || op == QA_QVM_ARG || op == QA_QVM_JUMP || op == QA_QVM_CALL
                || (op >= QA_QVM_SEX8 && op <= QA_QVM_NEGI) || op == QA_QVM_BCOM || op == QA_QVM_NEGF
                || op == QA_QVM_CVIF || op == QA_QVM_CVFI ? 1 : 0;
        if (interpreted && op == QA_QVM_BCOM) required = 2;
        if (path->depth < required) { ok = error_at(error, pc, "QVM region consumes outside operands"); break; }
        if (evaluation != NULL && evaluation->read_only
            && (op == QA_QVM_CALL || op == QA_QVM_ARG || op == QA_QVM_BLOCK_COPY || op == QA_QVM_BREAK)) {
            ok = error_at(error, pc, "Read-only QVM region has a call or external effect"); break;
        }
        proof_word left = {PROOF_UNKNOWN, 0}, right = left;
        if (required > 0) right = path->words[--path->depth];
        if (required > 1) left = path->words[--path->depth];
        proof_word result = {PROOF_UNKNOWN, 0}; bool produces = false;
        if (op == QA_QVM_CONST || op == QA_QVM_LOCAL || op == QA_QVM_PUSH) {
            produces = true;
            if (op == QA_QVM_CONST) result = (proof_word){PROOF_CONSTANT, instruction.operand};
            if (op == QA_QVM_LOCAL) {
                if (evaluation != NULL && (instruction.operand < 8 || (instruction.operand & 3) != 0
                    || (uint64_t)(uint32_t)instruction.operand + 4 > (uint64_t)frame + 48)) {
                    ok = error_at(error, pc, "QVM region local exceeds frame and argument reservation"); break;
                }
                result = (proof_word){PROOF_LOCAL, instruction.operand};
            }
        } else if (load) {
            if (evaluation != NULL && (right.kind == PROOF_DERIVED || (right.kind == PROOF_LOCAL
                && right.value < frame && !known_local(path->initialized, right.value, frame)))) {
                ok = error_at(error, pc, "QVM region reads an undeclared or unresolved local"); break;
            }
            produces = true;
        } else if (store) {
            if (evaluation != NULL) {
                if (local(right)) { ok = error_at(error, pc, "QVM region lets a local pointer escape"); break; }
                if (evaluation->read_only && (left.kind != PROOF_LOCAL || left.value < 8 || left.value + 4 > frame)) {
                    ok = error_at(error, pc, "Read-only QVM region writes outside its own frame"); break;
                }
                if (left.kind == PROOF_LOCAL && op == QA_QVM_STORE4 && valid_local((uint32_t)left.value, frame))
                    path->initialized[(uint32_t)left.value] = 1;
            }
        } else if (op == QA_QVM_BLOCK_COPY) {
            if (instruction.operand < 0) { ok = error_at(error, pc, "QVM region copy has a negative size"); break; }
            if (evaluation != NULL) {
                if (right.kind == PROOF_DERIVED) { ok = error_at(error, pc, "QVM region copies an unresolved local"); break; }
                if (right.kind == PROOF_LOCAL)
                    for (int64_t n = 0; n < instruction.operand; n += 4)
                        if (!known_local(path->initialized, right.value + n, frame)) { ok = error_at(error, pc, "QVM region copies undeclared locals"); break; }
                if (left.kind == PROOF_LOCAL)
                    for (int64_t n = 0; n + 4 <= instruction.operand; n += 4)
                        if (left.value + n >= 0 && left.value + n + 4 <= frame)
                            path->initialized[(uint32_t)(left.value + n)] = 1;
            }
        } else if (op == QA_QVM_ARG) {
            if (evaluation != NULL && instruction.operand >= 0 && (uint32_t)instruction.operand < frame)
                path->initialized[(uint32_t)instruction.operand] = 1;
        } else if (interpreted && op == QA_QVM_BCOM) {
            /* The pinned interpreter replaces the preceding cell, retaining top. */
            path->words[path->depth++] = (proof_word){local(right) ? PROOF_DERIVED : PROOF_UNKNOWN, 0};
            result = right; produces = true;
        } else if (binary) {
            produces = true;
            if ((op == QA_QVM_ADD || op == QA_QVM_SUB) && left.kind == PROOF_LOCAL && right.kind == PROOF_CONSTANT)
                result = (proof_word){PROOF_LOCAL, left.value + (op == QA_QVM_ADD ? right.value : -right.value)};
            else if (local(left) || local(right)) result.kind = PROOF_DERIVED;
        } else if (op == QA_QVM_CALL || (required == 1 && op != QA_QVM_POP && op != QA_QVM_JUMP)) {
            produces = true; if (op != QA_QVM_CALL && local(right)) result.kind = PROOF_DERIVED;
        } else if (!branch && op != QA_QVM_JUMP && op != QA_QVM_POP && op != QA_QVM_IGNORE && op != QA_QVM_BREAK) {
            ok = error_at(error, pc, "QVM region contains a frame or unsupported instruction"); break;
        }
        if (produces) {
            if (path->depth == QVM_OPERANDS - 1) { ok = error_at(error, pc, "QVM region operand proof overflows"); break; }
            path->words[path->depth++] = result;
        }
        if (!ok) break;
        if (op == QA_QVM_JUMP) {
            if (pc == 0 || image->instructions[pc - 1].opcode != QA_QVM_CONST || right.kind != PROOF_CONSTANT) {
                ok = error_at(error, pc, "QVM region has an indirect jump"); break;
            }
            ok = right.value >= 0 && right.value <= UINT32_MAX
                ? proof_edge(paths, pc, (uint32_t)right.value, entry_pc, join, path, locals, error)
                : error_at(error, pc, "QVM region jump target is invalid");
        } else {
            ok = proof_edge(paths, pc, pc + 1, entry_pc, join, path, locals, error);
            if (ok && branch) ok = proof_edge(paths, pc, (uint32_t)instruction.operand, entry_pc, join, path, locals, error);
        }
        free(path->initialized); free(path); paths[pc - entry_pc] = NULL;
    }
    if (ok && !joined) ok = error_at(error, entry_pc, "QVM region has no reachable join");
    for (uint32_t pc = owner + 1; ok && pc < end; ++pc) {
        if (pc >= entry_pc && pc < join) continue;
        qa_qvm_instruction instruction = image->instructions[pc];
        int32_t target = -1;
        if (instruction.opcode >= QA_QVM_EQ && instruction.opcode <= QA_QVM_GEF) target = instruction.operand;
        else if (instruction.opcode == QA_QVM_JUMP && image->instructions[pc - 1].opcode == QA_QVM_CONST)
            target = image->instructions[pc - 1].operand;
        if (target > (int32_t)entry_pc && target < (int32_t)join) ok = error_at(error, pc, "QVM region has an incoming interior edge");
    }
    for (size_t i = 0; i < count; ++i) if (paths[i] != NULL) { free(paths[i]->initialized); free(paths[i]); }
    free(paths); return ok;
}
bool qa_qvm_qualify_region(const qa_qvm_image *image, uint32_t owner,
                          const qa_qvm_region_evaluation *evaluation, qa_error *error)
{
    if (evaluation == NULL) return error_at(error, owner, "Missing QVM region evaluation");
    return qualify(image, owner, evaluation->entry, evaluation->join, evaluation, false, error);
}
bool qa_qvm_qualify_source_region(const qa_qvm_image *image, uint32_t owner,
                                  uint32_t entry, uint32_t join, qa_error *error)
{
    return qualify(image, owner, entry, join, NULL, false, error);
}

static bool arithmetic(uint8_t op, int32_t left, int32_t right, int32_t *out, qa_error *error)
{
    uint32_t a = (uint32_t)left, b = (uint32_t)right;
    switch (op) {
    case QA_QVM_ADD: *out = signed_bits(a + b); break;
    case QA_QVM_SUB: *out = signed_bits(a - b); break;
    case QA_QVM_MULI: case QA_QVM_MULU: *out = signed_bits(a * b); break;
    case QA_QVM_DIVI: case QA_QVM_MODI:
        if (right == 0 || (left == INT32_MIN && right == -1)) return error_at(error, 0, "QVM signed division is undefined");
        *out = op == QA_QVM_DIVI ? left / right : left % right; break;
    case QA_QVM_DIVU: case QA_QVM_MODU:
        if (b == 0) return error_at(error, 0, "QVM unsigned division by zero");
        *out = signed_bits(op == QA_QVM_DIVU ? a / b : a % b); break;
    case QA_QVM_BAND: *out = signed_bits(a & b); break;
    case QA_QVM_BOR: *out = signed_bits(a | b); break;
    case QA_QVM_BXOR: *out = signed_bits(a ^ b); break;
    case QA_QVM_LSH: case QA_QVM_RSHI: case QA_QVM_RSHU:
        if (b > 31) return error_at(error, 0, "QVM shift count is outside 0..31");
        if (op == QA_QVM_LSH) *out = signed_bits(a << b);
        else if (op == QA_QVM_RSHI && left < 0 && b != 0) *out = signed_bits((a >> b) | (UINT32_MAX << (32 - b)));
        else *out = signed_bits(a >> b);
        break;
    case QA_QVM_ADDF: *out = float_word(word_float(left) + word_float(right)); break;
    case QA_QVM_SUBF: *out = float_word(word_float(left) - word_float(right)); break;
    case QA_QVM_MULF: *out = float_word(word_float(left) * word_float(right)); break;
    case QA_QVM_DIVF: *out = float_word(word_float(left) / word_float(right)); break;
    default: return error_at(error, 0, "Invalid QVM binary instruction");
    }
    return true;
}
static bool branch_taken(uint8_t op, int32_t left, int32_t right)
{
    switch (op) {
    case QA_QVM_EQ: return left == right;
    case QA_QVM_NE: return left != right;
    case QA_QVM_LTI: return left < right;
    case QA_QVM_LEI: return left <= right;
    case QA_QVM_GTI: return left > right;
    case QA_QVM_GEI: return left >= right;
    case QA_QVM_LTU: return (uint32_t)left < (uint32_t)right;
    case QA_QVM_LEU: return (uint32_t)left <= (uint32_t)right;
    case QA_QVM_GTU: return (uint32_t)left > (uint32_t)right;
    case QA_QVM_GEU: return (uint32_t)left >= (uint32_t)right;
    case QA_QVM_EQF: return word_float(left) == word_float(right);
    case QA_QVM_NEF: return word_float(left) != word_float(right);
    case QA_QVM_LTF: return word_float(left) < word_float(right);
    case QA_QVM_LEF: return word_float(left) <= word_float(right);
    case QA_QVM_GTF: return word_float(left) > word_float(right);
    case QA_QVM_GEF: return word_float(left) >= word_float(right);
    default: return false;
    }
}
static bool reserve_return(return_address **returns, size_t *count, size_t *capacity,
                            return_address *inline_storage, uint32_t stack, int32_t pc, qa_error *error)
{
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 16 : *capacity * 2;
        if (next < *capacity || next > SIZE_MAX / sizeof(**returns)) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "QVM return stack capacity exhausted");
        return_address *resized = *returns == inline_storage ? malloc(next * sizeof(*resized)) : realloc(*returns, next * sizeof(*resized));
        if (resized == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM return stack");
        if (*returns == inline_storage) memcpy(resized, inline_storage, *count * sizeof(*resized));
        *returns = resized; *capacity = next;
    }
    (*returns)[(*count)++] = (return_address){stack, pc}; return true;
}
static bool source_arguments(qa_qvm *vm, uint32_t caller, size_t *out, qa_error *error)
{
    if (caller >= vm->image->instruction_count) return error_at(error, caller, "QVM call has no original instruction");
    for (uint32_t i = caller + 1; i != 0;) {
        const qa_qvm_instruction *instruction = &vm->image->instructions[--i];
        if (instruction->opcode != QA_QVM_ENTER) continue;
        if (instruction->operand < 8 || (instruction->operand & 3) != 0) return error_at(error, caller, "QVM call has no aligned caller frame");
        size_t words = ((uint32_t)instruction->operand - 8) / 4;
        *out = words < QVM_ARGUMENTS ? words : QVM_ARGUMENTS; return true;
    }
    return error_at(error, caller, "QVM call has no caller function");
}
static bool region_callback(qa_qvm *vm, execution_frame *frame, source_call *owner,
                            source_region *region, bool completed, bool *skip, qa_error *error)
{
    if (frame->operands->depth != owner->depth) return error_at(error, 0, "QVM region lost its operand boundary");
    execution *exec = state(vm); uint32_t saved = exec->program_stack;
    if (!stack_address(vm, (int64_t)frame->stack - 4, &exec->program_stack, error)) return false;
    host_scope host;
    if (!open_host(vm, &host, frame, owner, HOST_REGION, owner->instruction, owner->caller,
                   owner->argument_base, owner->argument_count, error)) { exec->program_stack = saved; return false; }
    bool ok = completed ? region->binding.completed == NULL || region->binding.completed(region->binding.context, &host.call, error)
        : region->binding.enter(region->binding.context, &host.call, skip, error);
    ok = close_host(vm, &host, ok, error); exec->program_stack = saved;
    return ok;
}
static bool intercept(qa_qvm *vm, execution_frame *frame, uint32_t stack, int32_t return_pc,
                      uint32_t instruction, uint32_t caller, size_t argument_count,
                      qa_qvm_function_hook hook, void *context, binding *observers,
                      int32_t *out, bool *started, qa_error *error)
{
    if (!function(vm->image, instruction, error) || !qa_qvm_raw_range(vm, stack + 8, argument_count * 4, error)) return false;
    execution *exec = state(vm);
    source_call source = {.parent = frame->scope, .operands = frame->operands, .stack = stack,
        .instruction = instruction, .caller = caller, .depth = frame->operands->depth,
        .argument_base = stack + 8, .return_pc = return_pc, .argument_count = argument_count, .active = true};
    uint64_t last_binding = exec->next_binding;
    bool ok = true;
    for (binding *current = observers; ok && current != NULL && current->id <= last_binding; current = current->next_observer) {
        if (!current->active) continue;
        host_scope host;
        if (!open_host(vm, &host, frame, &source, HOST_OBSERVER, instruction, caller, stack + 8, argument_count, error)) { ok = false; break; }
        ok = current->fn.observer(current->context, &host.call, error);
        ok = close_host(vm, &host, ok, error);
    }
    int32_t result = 0;
    if (ok) {
        host_scope host;
        ok = open_host(vm, &host, frame, &source, HOST_FUNCTION, instruction, caller, stack + 8, argument_count, error);
        if (ok) {
            if (started) *started = true;
            ok = hook == NULL ? qa_qvm_proceed(&host.call, &result, error) : hook(context, &host.call, &result, error);
            ok = close_host(vm, &host, ok, error);
        }
    }
    if (exec->cancelled == &source && !exec->failed) {
        while (source.operands->depth > source.depth) source.operands->initialized[source.operands->depth--] = false;
        if (source.operands->depth < source.depth) ok = error_at(error, 0, "QVM cancellation lost caller operands");
        else {
            exec->cancelled = NULL;
            ok = write_word(vm, source.stack, source.return_pc, error);
            result = 0;
        }
    }
    source.active = false;
    if (exec->cancelled == &source) exec->cancelled = NULL;
    free(source.branches); free(source.regions);
    if (ok) *out = result;
    return ok;
}

static bool execute(qa_qvm *vm, uint32_t instruction, uint32_t entry_stack, operands *stack,
                    source_call *source, const qa_qvm_region_evaluation *evaluation,
                    const int32_t *inputs, int32_t *out, bool *started, qa_error *error)
{
    execution *exec = state(vm);
    execution_frame frame = {exec->active, source != NULL ? source : exec->host == NULL ? NULL : exec->host->owner,
        stack, entry_stack, exec->active == NULL ? 0 : exec->active->floor};
    uint32_t previous_stack = exec->program_stack;
    exec->active = &frame;
    int32_t pc;
    size_t hint = instruction;
    bool ok = target_pc(vm->image, (int32_t)instruction, &pc, error), finished = false;
    int32_t result = 0;
    return_address inline_returns[32];
    return_address *returns = inline_returns; size_t return_count = 0, return_capacity = 32;
    bool compiled = vm->options.semantics == QA_QVM_COMPILED_SEMANTICS;
    if (ok && compiled) ok = reserve_return(&returns, &return_count, &return_capacity, inline_returns, entry_stack, source == NULL ? -1 : source->return_pc, error);
    bool evaluation_started = false;
    uint32_t initial_depth = source == NULL ? 0 : source->depth;
    while (ok && !finished) {
        if (!healthy(vm, error)) { ok = false; break; }
        if (vm->options.instruction_limit != 0 && exec->instructions >= vm->options.instruction_limit) {
            ok = error_at(error, (size_t)pc, "QVM instruction budget exhausted"); break;
        }
        ++exec->instructions;
        counter_evaluation *counter = exec->counter;
        if (counter != NULL) {
            bool allowed = false;
            for (size_t i = 0; i < counter->count; ++i)
                if (pc >= (int32_t)vm->image->instructions[counter->functions[i]].byte_offset
                    && pc < (int32_t)counter->ends[i]) { allowed = true; break; }
            if (counter->remaining == 0 || !allowed) { ok = error_at(error, (size_t)pc, "QVM counter escaped its admitted code or budget"); break; }
            --counter->remaining;
        }
        if (evaluation != NULL && evaluation_started
            && frame.stack == entry_stack - (uint32_t)vm->image->instructions[instruction].operand
            && pc == (int32_t)vm->image->instructions[evaluation->join].byte_offset) {
            if (stack->depth != initial_depth) { ok = error_at(error, (size_t)pc, "QVM evaluation lost its operand boundary"); break; }
            ok = evaluation->result_offset < 0 || read_word(vm, frame.stack + (uint32_t)evaluation->result_offset, &result, error);
            finished = ok; break;
        }
        source_call *owner = counter == NULL && (evaluation == NULL || !evaluation->read_only) ? frame.scope : NULL;
        if (owner != NULL && owner->active && owner->regions != NULL
            && frame.stack == owner->stack - (uint32_t)vm->image->instructions[owner->instruction].operand) {
            bool redirected = false;
            for (size_t i = 0; ok && i < owner->region_count; ++i) {
                source_region *region = &owner->regions[i];
                if (pc == (int32_t)vm->image->instructions[region->binding.join].byte_offset && region->state == REGION_RUNNING) {
                    region->state = REGION_COMPLETE;
                    ok = region_callback(vm, &frame, owner, region, true, NULL, error);
                }
                if (ok && pc == (int32_t)vm->image->instructions[region->binding.entry].byte_offset) {
                    if (region->state != REGION_READY) { ok = error_at(error, (size_t)pc, "QVM region can execute once per invocation"); break; }
                    bool skip = false; region->state = REGION_RUNNING;
                    ok = region_callback(vm, &frame, owner, region, false, &skip, error);
                    if (ok && skip) { region->state = REGION_COMPLETE; pc = (int32_t)vm->image->instructions[region->binding.join].byte_offset; redirected = true; break; }
                }
            }
            if (!ok) break;
            if (redirected) continue;
        }
        int32_t opcode, operand = 0, left = 0, right = 0;
        int32_t opcode_pc = pc;
        if (!code_word(vm->image, pc++, &hint, &opcode, error)) { ok = false; break; }
        uint32_t original = vm->image->instructions[hint].byte_offset == (uint32_t)opcode_pc ? (uint32_t)hint : NO_INSTRUCTION;
        if (vm->options.debug && vm->data_size >= UINT32_C(0x20000) && frame.stack <= vm->data_size - UINT32_C(0x20000)) {
            ok = error_at(error, frame.stack, "QVM debug program stack overflow"); break;
        }
        qa_qvm_trace_event trace = {original, (uint32_t)opcode_pc, frame.stack, stack->depth, (uint8_t)opcode};
        if (vm->options.trace != NULL) {
            ++vm->publication_depth; ok = vm->options.trace(vm->options.context, vm, &trace, error); --vm->publication_depth;
            if (!ok) break;
        }
        if (started) *started = true;
        switch (opcode) {
        case QA_QVM_UNDEF: case QA_QVM_IGNORE:
            if (vm->options.debug) ok = error_at(error, (size_t)opcode_pc, "Bad QVM debug instruction");
            break;
        case QA_QVM_BREAK:
            if (counter != NULL) { ok = error_at(error, (size_t)opcode_pc, "QVM counter cannot break"); break; }
            ++exec->breaks;
            if (vm->options.breakpoint != NULL) {
                ++vm->publication_depth; ok = vm->options.breakpoint(vm->options.context, vm, &trace, error); --vm->publication_depth;
            }
            break;
        case QA_QVM_CONST: case QA_QVM_LOCAL:
            if (!code_word(vm->image, pc, &hint, &operand, error)) { ok = false; break; }
            pc += 4;
            ok = push(stack, opcode == QA_QVM_CONST ? operand : signed_bits(frame.stack + (uint32_t)operand), true, error); break;
        case QA_QVM_PUSH: ok = push(stack, 0, false, error); break;
        case QA_QVM_POP:
            if (stack->depth == 0) ok = error_at(error, (size_t)opcode_pc, "QVM operand stack underflow");
            else --stack->depth;
            break;
        case QA_QVM_ENTER:
            if (!code_word(vm->image, pc, &hint, &operand, error)) { ok = false; break; }
            pc += 4;
            ok = stack_address(vm, (int64_t)frame.stack - operand, &frame.stack, error);
            if (ok && frame.stack < frame.floor) ok = error_at(error, frame.stack, "QVM evaluation stack overlaps source data");
            if (ok && vm->options.debug) ok = write_word(vm, frame.stack + 4, signed_bits(frame.stack + (uint32_t)operand), error);
            if (ok && evaluation != NULL && !evaluation_started && original == instruction) {
                evaluation_started = true;
                for (size_t i = 0; ok && i < evaluation->input_count; ++i)
                    ok = write_word(vm, frame.stack + evaluation->inputs[i], inputs[i], error);
                pc = (int32_t)vm->image->instructions[evaluation->entry].byte_offset;
            }
            break;
        case QA_QVM_LEAVE: {
            if (!code_word(vm->image, pc, &hint, &operand, error)
                || !stack_address(vm, (int64_t)frame.stack + operand, &frame.stack, error)) { ok = false; break; }
            int32_t target;
            if (compiled) {
                if (return_count == 0 || returns[return_count - 1].stack != frame.stack) { ok = error_at(error, (size_t)opcode_pc, "QVM function returned with an invalid program stack"); break; }
                target = returns[--return_count].pc;
            } else if (!read_word(vm, frame.stack, &target, error)) { ok = false; break; }
            if (source != NULL && frame.stack == source->stack && (!compiled || return_count == 0)) {
                if (target != source->return_pc || stack->depth != source->depth + 1) { ok = error_at(error, (size_t)opcode_pc, "QVM function returned with invalid caller operands"); break; }
                ok = pop(stack, &result, error); finished = ok;
            } else if (target == -1) {
                if (stack->depth != 1) { ok = error_at(error, (size_t)opcode_pc, "QVM invocation returned with invalid operands"); break; }
                ok = peek(stack, &result, error); finished = ok;
            } else pc = target;
            break;
        }
        case QA_QVM_CALL: {
            int32_t return_pc = pc;
            if (!write_word(vm, frame.stack, return_pc, error) || !pop(stack, &operand, error)) { ok = false; break; }
            if (operand >= 0) {
                int32_t target;
                if (!target_pc(vm->image, operand, &target, error)) { ok = false; break; }
                if (counter != NULL) {
                    bool allowed = false;
                    for (size_t i = 0; i < counter->count; ++i) if (counter->functions[i] == (uint32_t)operand) { allowed = true; break; }
                    if (!allowed) { ok = error_at(error, (size_t)opcode_pc, "QVM counter called an undeclared function"); break; }
                }
                function_entry *registered = counter == NULL ? entry(exec, (uint32_t)operand) : NULL;
                binding *bound = registered != NULL && registered->hook != NULL && registered->hook->active ? registered->hook : NULL;
                binding *observers = registered == NULL ? NULL : registered->observers;
                qa_qvm_function_hook hook = bound == NULL ? NULL : bound->fn.hook;
                void *context = bound == NULL ? NULL : bound->context;
                size_t count = 0;
                if (hook != NULL || observers != NULL || (counter == NULL && exec->resolver != NULL && exec->resolver->active)) {
                    if (!source_arguments(vm, original, &count, error) || !qa_qvm_raw_range(vm, frame.stack + 8, count * 4, error)) { ok = false; break; }
                    if (hook == NULL && counter == NULL && exec->resolver != NULL && exec->resolver->active) {
                        uint32_t saved = exec->program_stack;
                        if (!stack_address(vm, (int64_t)frame.stack - 4, &exec->program_stack, error)) { ok = false; break; }
                        host_scope host;
                        if (!open_host(vm, &host, &frame, frame.scope, HOST_OBSERVER, (uint32_t)operand, original, frame.stack + 8, count, error)) { exec->program_stack = saved; ok = false; break; }
                        hook = exec->resolver->fn.resolver(exec->resolver->context, &host.call, &context);
                        ok = close_host(vm, &host, true, error); exec->program_stack = saved;
                        if (!ok) break;
                    }
                }
                if (hook == NULL && observers == NULL) {
                    if (compiled && !reserve_return(&returns, &return_count, &return_capacity, inline_returns, frame.stack, return_pc, error)) { ok = false; break; }
                    pc = target; hint = (uint32_t)operand;
                } else {
                    uint32_t saved = exec->program_stack;
                    ok = stack_address(vm, (int64_t)frame.stack - 4, &exec->program_stack, error);
                    if (ok) ok = intercept(vm, &frame, frame.stack, return_pc, (uint32_t)operand, original, count, hook, context, observers, &right, NULL, error);
                    exec->program_stack = saved;
                    if (ok) ok = push(stack, right, true, error);
                    if (ok && !compiled) ok = read_word(vm, frame.stack, &pc, error);
                }
            } else {
                if (counter != NULL) { ok = error_at(error, (size_t)opcode_pc, "QVM counter cannot call an engine service"); break; }
                uint32_t saved = exec->program_stack;
                int32_t saved_chain = 0;
                if (vm->options.debug && !read_word(vm, frame.stack + 4, &saved_chain, error)) { ok = false; break; }
                ok = stack_address(vm, (int64_t)frame.stack - 4, &exec->program_stack, error);
                if (ok) ok = write_word(vm, frame.stack + 4, signed_bits(~(uint32_t)operand), error);
                host_scope host;
                if (ok) ok = qa_qvm_raw_range(vm, frame.stack + 8, 0, error)
                    && open_host(vm, &host, &frame, frame.scope, HOST_SYSCALL, NO_INSTRUCTION, original,
                                 frame.stack + 8, (vm->data_size - frame.stack - 8) / 4, error);
                if (ok) {
                    ok = qa_qvm_dispatch(vm, &host.call, signed_bits(~(uint32_t)operand), &right, error);
                    ok = close_host(vm, &host, ok, error);
                }
                exec->program_stack = saved;
                if (ok && vm->options.debug) ok = write_word(vm, frame.stack + 4, saved_chain, error);
                if (ok) ok = push(stack, right, true, error);
                if (ok && !compiled) ok = read_word(vm, frame.stack, &pc, error);
            }
            break;
        }
        case QA_QVM_JUMP:
            ok = pop(stack, &operand, error) && target_pc(vm->image, operand, &pc, error);
            if (ok) hint = (uint32_t)operand;
            break;
        case QA_QVM_LOAD1: case QA_QVM_LOAD2: case QA_QVM_LOAD4: {
            if (!peek(stack, &operand, error)) { ok = false; break; }
            uint32_t address = (uint32_t)operand & vm->data_mask;
            size_t bytes = opcode == QA_QVM_LOAD1 ? 1 : opcode == QA_QVM_LOAD2 ? 2 : 4;
            if (vm->options.debug && bytes == 4 && ((uint32_t)operand & 3) != 0) { ok = error_at(error, address, "QVM debug LOAD4 is misaligned"); break; }
            ok = qa_qvm_raw_range(vm, address, bytes, error);
            bool virtual_word = false;
            if (ok) ok = counter_access(vm, address, bytes, false, &right, &virtual_word, error);
            if (ok) set_top(stack, virtual_word ? right : bytes == 1 ? vm->data[address] : bytes == 2 ? qa_load_u16le(vm->data + address) : qa_load_i32le(vm->data + address));
            break;
        }
        case QA_QVM_STORE1: case QA_QVM_STORE2: case QA_QVM_STORE4: {
            if (!pop(stack, &right, error) || !pop(stack, &left, error)) { ok = false; break; }
            size_t bytes = opcode == QA_QVM_STORE1 ? 1 : opcode == QA_QVM_STORE2 ? 2 : 4;
            uint32_t address = (uint32_t)left & (vm->data_mask & ~((uint32_t)bytes - 1));
            uint8_t value[4]; qa_store_u32le(value, (uint32_t)right);
            bool virtual_word = false;
            ok = qa_qvm_raw_range(vm, address, bytes, error) && counter_access(vm, address, bytes, true, &right, &virtual_word, error);
            if (ok && !virtual_word) {
                if (counter == NULL) ok = qa_qvm_write(vm, address, (qa_bytes){value, bytes}, error);
                else memcpy(vm->data + address, value, bytes);
            }
            break;
        }
        case QA_QVM_ARG:
            if (!code_word(vm->image, pc++, &hint, &operand, error) || !pop(stack, &right, error)) { ok = false; break; }
            if (operand < 0 || (uint64_t)frame.stack + (uint32_t)operand > UINT32_MAX) { ok = error_at(error, (size_t)opcode_pc, "QVM argument address overflow"); break; }
            ok = write_word(vm, frame.stack + (uint32_t)operand, right, error); break;
        case QA_QVM_BLOCK_COPY:
            if (counter != NULL) { ok = error_at(error, (size_t)opcode_pc, "QVM counter cannot copy source memory"); break; }
            if (!pop(stack, &right, error) || !pop(stack, &left, error) || !code_word(vm->image, pc, &hint, &operand, error)) { ok = false; break; }
            pc += 4;
            if (compiled) {
                if (operand < 0 || left < 0 || right < 0 || (uint64_t)(uint32_t)left + (uint32_t)operand > vm->data_mask
                    || (uint64_t)(uint32_t)right + (uint32_t)operand > vm->data_mask) { ok = error_at(error, (size_t)opcode_pc, "QVM compiled block copy exceeds memory"); break; }
                ok = qa_qvm_copy(vm, (uint32_t)left, (uint32_t)right, (uint32_t)operand, error);
            } else {
                uint32_t from = (uint32_t)right & vm->data_mask, to = (uint32_t)left & vm->data_mask;
                int64_t source_count = ((from + (uint32_t)operand) & vm->data_mask) - (int64_t)from;
                int64_t copied = ((to + (uint32_t)source_count) & vm->data_mask) - (int64_t)to;
                if (((uint32_t)copied | from | to) & 3) { ok = error_at(error, (size_t)opcode_pc, "QVM interpreted block copy is not word aligned"); break; }
                for (int64_t n = copied - 4; ok && n >= 0; n -= 4)
                    ok = read_word(vm, from + (uint32_t)n, &right, error) && write_word(vm, to + (uint32_t)n, right, error);
            }
            break;
        case QA_QVM_BCOM:
            if (!peek(stack, &operand, error)) { ok = false; break; }
            if (compiled) set_top(stack, ~operand);
            else if (stack->depth == 0) ok = error_at(error, (size_t)opcode_pc, "QVM interpreted complement underflows");
            else { stack->words[stack->depth - 1] = ~operand; stack->initialized[stack->depth - 1] = true; }
            break;
        case QA_QVM_SEX8: case QA_QVM_SEX16: case QA_QVM_NEGI: case QA_QVM_NEGF: case QA_QVM_CVIF: case QA_QVM_CVFI:
            if (!peek(stack, &operand, error)) { ok = false; break; }
            if (opcode == QA_QVM_SEX8) right = (operand & 0x80) != 0 ? (operand & 255) - 256 : operand & 255;
            else if (opcode == QA_QVM_SEX16) right = (operand & 0x8000) != 0 ? (operand & 65535) - 65536 : operand & 65535;
            else if (opcode == QA_QVM_NEGI) right = signed_bits(0u - (uint32_t)operand);
            else if (opcode == QA_QVM_NEGF) right = float_word(-word_float(operand));
            else if (opcode == QA_QVM_CVIF) right = float_word((float)operand);
            else { float value = word_float(operand); right = value >= -2147483648.0f && value < 2147483648.0f ? (int32_t)value : INT32_MIN; }
            set_top(stack, right); break;
        default:
            if ((opcode >= QA_QVM_ADD && opcode <= QA_QVM_RSHU) || (opcode >= QA_QVM_ADDF && opcode <= QA_QVM_MULF)) {
                ok = pop(stack, &right, error) && peek(stack, &left, error) && arithmetic((uint8_t)opcode, left, right, &operand, error);
                if (ok) set_top(stack, operand);
            } else if (opcode >= QA_QVM_EQ && opcode <= QA_QVM_GEF) {
                if (!pop(stack, &right, error) || !pop(stack, &left, error) || !code_word(vm->image, pc, &hint, &operand, error)) { ok = false; break; }
                bool taken = branch_taken((uint8_t)opcode, left, right);
                if (owner != NULL)
                    for (size_t i = 0; ok && i < owner->branch_count; ++i) {
                        qa_qvm_branch_binding branch = owner->branches[i];
                        if (branch.instruction != original) continue;
                        uint32_t saved = exec->program_stack;
                        ok = stack_address(vm, (int64_t)frame.stack - 4, &exec->program_stack, error);
                        host_scope host;
                        if (ok) ok = open_host(vm, &host, &frame, owner, HOST_BRANCH, owner->instruction, owner->caller,
                                              owner->argument_base, owner->argument_count, error);
                        if (ok) { ok = branch.decide(branch.context, &host.call, taken, &taken, error); ok = close_host(vm, &host, ok, error); }
                        exec->program_stack = saved; break;
                    }
                pc = taken ? operand : pc + 4;
                if (taken && original != NO_INSTRUCTION) hint = (uint32_t)vm->image->instructions[original].operand;
            } else if (vm->options.debug) ok = error_at(error, (size_t)opcode_pc, "Bad QVM debug instruction");
            /* Release interpreted execution admits unknown words in sparse tails. */
            break;
        }
    }
    if (returns != inline_returns) free(returns);
    exec->active = frame.parent; exec->program_stack = previous_stack;
    if (ok && finished) *out = result;
    return ok && finished;
}

static bool invoke(qa_qvm *vm, uint32_t instruction, const int32_t *words, size_t count,
                    const qa_qvm_region_evaluation *evaluation, const int32_t *inputs,
                    uint32_t floor, int32_t *out, bool *started, qa_error *error)
{
    qa_error local_error = {0};
    if (error == NULL) error = &local_error;
    if (!qa_qvm_execution_reentry(vm, error)) return false;
    execution *exec = state(vm);
    if (out == NULL || (count != 0 && words == NULL) || count > (instruction == 0 ? 10u : QVM_ARGUMENTS)
        || instruction >= vm->image->instruction_count || (instruction != 0 && !function(vm->image, instruction, error))) {
        error_at(error, instruction, "QVM invocation needs an entry, output and bounded arguments"); latch(vm, error); return false;
    }
    bool root = exec->active == NULL;
    if (root) { exec->failed = false; exec->failure = (qa_error){0}; exec->instructions = 0; }
    if (!healthy(vm, error)) { if (!exec->failed && exec->cancelled != NULL) { *out = 0; return true; } return false; }
    int32_t arguments[QVM_ARGUMENTS] = {0};
    if (count != 0) memcpy(arguments, words, count * sizeof(*arguments));
    size_t reserved = count < 10 ? 10 : count;
    uint32_t base;
    if (!stack_address(vm, (int64_t)exec->program_stack - 8 - (int64_t)reserved * 4, &base, error)
        || base < floor) { error_at(error, 0, "QVM argument frame exceeds its stack reservation"); latch(vm, error); return false; }
    operands stack = {0};
    execution_frame setup = {exec->active, exec->host == NULL ? NULL : exec->host->owner, &stack, base, floor};
    uint32_t previous_stack = exec->program_stack;
    exec->active = &setup;
    bool ok = write_word(vm, base, -1, error) && write_word(vm, base + 4, 0, error);
    for (size_t i = 0; ok && i < reserved; ++i) ok = write_word(vm, base + 8 + (uint32_t)i * 4, arguments[i], error);
    int32_t result = 0;
    function_entry *registered = evaluation == NULL && exec->counter == NULL ? entry(exec, instruction) : NULL;
    binding *bound = registered == NULL ? NULL : registered->hook;
    if (ok && bound != NULL && bound->active && bound->host_invocations) {
        ok = stack_address(vm, (int64_t)base - 4, &exec->program_stack, error);
        if (ok) {
            ok = intercept(vm, &setup, base, -1, instruction, NO_INSTRUCTION, reserved, bound->fn.hook, bound->context, NULL, &result, started, error);
        }
    } else if (ok) {
        ok = execute(vm, instruction, base, &stack, NULL, evaluation, inputs, &result, started, error);
    }
    if (!ok && exec->cancelled != NULL && !exec->failed) { ok = true; result = 0; }
    if (!ok) latch(vm, error);
    if (exec->failed) { if (error != NULL) *error = exec->failure; ok = false; }
    exec->program_stack = previous_stack; exec->active = setup.parent;
    if (root) collect_bindings(exec);
    if (ok) *out = result;
    return ok;
}
bool qa_qvm_invoke(qa_qvm *vm, uint32_t instruction, const int32_t *words, size_t count, int32_t *out, qa_error *error)
{
    execution *exec = state(vm);
    uint32_t floor = exec == NULL ? 0 : exec->counter != NULL ? exec->counter->floor : exec->active == NULL ? 0 : exec->active->floor;
    if (exec && exec->scratch_floor > floor) floor = exec->scratch_floor;
    return invoke(vm, instruction, words, count, NULL, NULL, floor, out, NULL, error);
}
bool qa_qvm_invoke_started(qa_qvm *vm, uint32_t instruction, const int32_t *words,
    size_t count, int32_t *out, bool *started, qa_error *error)
{
    if (!started) return qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "QVM source attempt requires its owner marker");
    *started = false;
    execution *exec = state(vm);
    uint32_t floor = exec == NULL ? 0 : exec->counter != NULL ? exec->counter->floor : exec->active == NULL ? 0 : exec->active->floor;
    if (exec && exec->scratch_floor > floor) floor = exec->scratch_floor;
    return invoke(vm, instruction, words, count, NULL, NULL, floor, out, started, error);
}

bool qa_qvm_execution_source_callback(const qa_qvm_call *call, const qa_qvm_image *image,
    int32_t pointer, const int32_t *words, size_t count, int32_t *out, qa_error *error)
{
    qa_error local = {0};
    if (!error) error = &local;
    if (!qa_qvm_execution_token(call, error)) return false;
    qa_qvm *vm = call->vm;
    execution *exec = state(vm);
    if (!image || image != vm->image || !out || (count && !words) ||
        count > QVM_ARGUMENTS || exec->counter)
        return error_at(error, 0, "QVM source callback differs from its admitted image or source scope");
    if (pointer >= 0) {
        if (!function(image, (uint32_t)pointer, error)) return false;
        return qa_qvm_invoke(vm, (uint32_t)pointer, words, count, out, error);
    }
    if (!healthy(vm, error)) {
        if (!exec->failed && exec->cancelled) { *out = 0; return true; }
        return false;
    }
    int32_t arguments[QVM_ARGUMENTS];
    if (count) memcpy(arguments, words, count * sizeof(*arguments));
    uint32_t base;
    uint32_t floor = exec->active->floor;
    if (exec->scratch_floor > floor) floor = exec->scratch_floor;
    if (!stack_address(vm, (int64_t)exec->program_stack - 8 - (int64_t)count * 4, &base, error) ||
        base < floor)
        return error_at(error, 0, "QVM imported callback arguments exceed the active source stack reservation");
    operands stack = {0};
    execution_frame setup = {exec->active, exec->host->owner, &stack, base, floor};
    uint32_t previous_stack = exec->program_stack;
    exec->active = &setup;
    int32_t trap = (int32_t)(~(uint32_t)pointer);
    bool ok = write_word(vm, base, -1, error) && write_word(vm, base + 4, trap, error);
    for (size_t i = 0; ok && i < count; ++i)
        ok = write_word(vm, base + 8 + (uint32_t)i * 4, arguments[i], error);
    int32_t result = 0;
    if (ok) ok = stack_address(vm, (int64_t)base - 4, &exec->program_stack, error);
    host_scope host;
    if (ok) ok = open_host(vm, &host, &setup, setup.scope, HOST_SYSCALL,
        NO_INSTRUCTION, NO_INSTRUCTION, base + 8, count, error);
    if (ok) {
        ok = qa_qvm_dispatch(vm, &host.call, trap, &result, error);
        ok = close_host(vm, &host, ok, error);
    }
    if (!ok && exec->cancelled && !exec->failed) { ok = true; result = 0; }
    if (!ok) latch(vm, error);
    if (exec->failed) { *error = exec->failure; ok = false; }
    exec->program_stack = previous_stack;
    exec->active = setup.parent;
    if (ok) *out = result;
    return ok;
}

static bool scratch_span(qa_qvm *vm, size_t length, uint32_t reservation,
    uint32_t *offset, uint32_t *end, qa_error *error)
{
    execution *exec = state(vm);
    uint32_t source;
    if (!qa_qvm_source_scratch_qualify(vm->image, length, &source, error)) return false;
    uint64_t start = source;
    if (exec->scratch_floor > start) start = exec->scratch_floor;
    if (exec->active && exec->active->floor > start) start = exec->active->floor;
    uint64_t alignment = reservation ? 4u : 16u;
    start = (start + alignment - 1) & ~(alignment - 1);
    uint64_t limit = vm->image->memory_size - UINT32_C(65536);
    if (exec->program_stack < reservation)
        return error_at(error, source, "QVM argument scratch exceeds its current source stack reservation");
    uint64_t current_limit = exec->program_stack - reservation;
    if (current_limit < limit) limit = current_limit;
    if (start > limit || length > limit - start || exec->scratch_depth == SIZE_MAX)
        return error_at(error, source, "QVM nested scratch exceeds its actual source reservation");
    uint64_t finish = (start + length + 3) & ~UINT64_C(3);
    if (finish > limit || finish > exec->program_stack)
        return error_at(error, source, "QVM scratch overlaps its actual paused source stack");
    *offset = (uint32_t)start; *end = (uint32_t)finish; return true;
}
bool qa_qvm_execution_source_scratch(const qa_qvm_call *call, const qa_qvm_image *image,
    size_t length, qa_qvm_source_scratch_fn perform, void *context, qa_error *error)
{
    qa_error local = {0};
    if (!error) error = &local;
    if (!qa_qvm_execution_token(call, error)) return false;
    qa_qvm *vm = call->vm;
    execution *exec = state(vm);
    uint32_t offset, end;
    if (!image || image != vm->image || !perform || exec->counter ||
        vm->data_size != image->memory_size)
        return error_at(error, 0, "QVM source scratch differs from its admitted image or source scope");
    if (!healthy(vm, error)) return false;
    if (!scratch_span(vm, length, 0, &offset, &end, error)) return false;
    uint8_t *saved = malloc(length);
    if (!saved) return qa_qvm_error(error, QA_ERROR_MEMORY, offset, "Retaining scoped QVM source scratch");
    memcpy(saved, vm->data + offset, length);
    execution_frame *frame = exec->active;
    uint32_t previous_floor = frame->floor;
    uint32_t previous_scratch = exec->scratch_floor;
    if (end > frame->floor) frame->floor = end;
    exec->scratch_floor = end; ++exec->scratch_depth;
    bool ok = perform(context, call, offset, error);
    if (!ok && (!exec->cancelled || error->code != QA_OK)) latch(vm, error);
    qa_error first = *error, cleanup = {0};
    bool restored = qa_qvm_memory_restore_scratch(vm, offset, (qa_bytes){saved, length}, &cleanup);
    frame->floor = previous_floor;
    --exec->scratch_depth; exec->scratch_floor = previous_scratch;
    free(saved);
    if (exec->failed) { *error = exec->failure; return false; }
    if (!ok) { *error = first; return false; }
    if (!restored) { *error = cleanup; return false; }
    return true;
}

bool qa_qvm_execution_source_frame(const qa_qvm_call *call, const qa_qvm_image *image,
    qa_qvm_source_frame *out, qa_error *error)
{
    if (!qa_qvm_execution_token(call, error)) return false;
    qa_qvm *vm = call->vm;
    host_scope *host = state(vm)->host;
    source_call *source = host->owner;
    if (!image || image != vm->image || !out || host->kind != HOST_FUNCTION ||
        !source || !source->active || source->proceeded || state(vm)->counter)
        return error_at(error, 0, "QVM source frame requires its current original function before proceeding");
    int32_t extent = image->instructions[source->instruction].operand;
    uint32_t start;
    if (extent < 8 || (extent & 3) ||
        !stack_address(vm, (int64_t)source->stack - extent, &start, error) ||
        start < host->frame->floor)
        return error_at(error, source->instruction, "QVM original local frame exceeds its active stack reservation");
    *out = (qa_qvm_source_frame){start, source->stack};
    return true;
}

bool qa_qvm_execution_source_word(const qa_qvm_call *call, const qa_qvm_image *image,
    uint32_t offset, int32_t value, qa_qvm_source_word_fn perform, void *context, qa_error *error)
{
    qa_error local = {0};
    if (!error) error = &local;
    qa_qvm_source_frame frame;
    if (!perform)
        return error_at(error, offset, "QVM scoped global word requires its original function callback");
    if (!qa_qvm_execution_source_frame(call, image, &frame, error)) return false;
    if (!qa_qvm_qualify_global_word(image, offset, error)) return false;
    qa_qvm *vm = call->vm;
    execution *exec = state(vm);
    if (!healthy(vm, error)) return false;
    uint8_t saved[4], projected[4];
    if (!qa_qvm_read(vm, offset, saved, sizeof(saved), error)) return false;
    qa_store_u32le(projected, (uint32_t)value);
    bool ok = qa_qvm_write(vm, offset, (qa_bytes){projected, sizeof(projected)}, error);
    if (ok) ok = perform(context, call, error);
    if (!ok && (!exec->cancelled || error->code != QA_OK)) latch(vm, error);
    qa_error first = *error, cleanup = {0};
    bool restored = qa_qvm_memory_restore_scratch(vm, offset, (qa_bytes){saved, sizeof(saved)}, &cleanup);
    if (exec->failed) { *error = exec->failure; return false; }
    if (!ok) { *error = first; return false; }
    if (!restored) { *error = cleanup; return false; }
    return true;
}
uint32_t qa_qvm_break_count(const qa_qvm *vm) { return state(vm) == NULL ? 0 : (uint32_t)state(vm)->breaks; }

bool qa_qvm_execution_words_end(qa_qvm_word_projection **receipt, bool restore, qa_error *error)
{
    if (!receipt || !*receipt) return true;
    qa_qvm_word_projection *lease = *receipt;
    qa_qvm *vm = lease->vm;
    execution *exec = state(vm);
    if (!qa_qvm_mutable(vm, error) || !exec || exec->projections != lease || lease->closing)
        return error_at(error, 0, "QVM projected words require their last actual open lease");
    lease->closing = true;
    bool ok = !restore || qa_qvm_memory_restore_words(vm, lease->saved, lease->count, lease->observed, error);
    if (exec->projections != lease) {
        lease->closing = false;
        return error_at(error, 0, "QVM write delivery retained a nested projected-word owner");
    }
    exec->projections = lease->previous;
    free(lease->saved); free(lease); *receipt = NULL;
    return ok;
}

static bool words_begin(qa_qvm *vm, const qa_qvm_image *image,
    const qa_qvm_source_word *words, size_t count, bool project, bool observed,
    qa_qvm_word_projection **out, qa_error *error)
{
    qa_error local = {0};
    if (!error) error = &local;
    if (!qa_qvm_execution_reentry(vm, error)) return false;
    execution *exec = state(vm);
    if (!image || image != vm->image || !out || *out || !words || !count || exec->counter ||
        count > SIZE_MAX / sizeof(*words))
        return error_at(error, 0, "QVM dynamic words require their exact source image and new owner");
    if (exec->active && !healthy(vm, error)) return false;
    qa_qvm_word_projection *lease = calloc(1, sizeof(*lease));
    if (!lease) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Retaining actual QVM projected-word owner");
    lease->saved = malloc(count * sizeof(*lease->saved));
    qa_qvm_source_word *copied = malloc(count * sizeof(*copied));
    if (!lease->saved || !copied) {
        free(copied); free(lease->saved); free(lease);
        return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Retaining QVM projection bytes before effects");
    }
    memcpy(copied, words, count * sizeof(*copied));
    for (size_t i = 0; i < count; ++i) {
        if ((copied[i].offset & 3) || !qa_qvm_raw_range(vm, copied[i].offset, 4, error)) {
            free(copied); free(lease->saved); free(lease);
            return error_at(error, words[i].offset, "QVM projected word leaves actual source allocation");
        }
        lease->saved[i] = (qa_qvm_source_word){copied[i].offset, qa_load_i32le(vm->data + copied[i].offset)};
    }
    lease->vm = vm; lease->count = count; lease->previous = exec->projections; lease->observed = observed;
    exec->projections = lease; *out = lease;
    bool ok = true;
    for (size_t i = 0; project && ok && i < count; ++i) {
        if (!observed) qa_store_u32le(vm->data + copied[i].offset, (uint32_t)copied[i].value);
        else {
            uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)copied[i].value);
            ok = qa_qvm_write(vm, copied[i].offset, (qa_bytes){bytes, sizeof(bytes)}, error);
        }
    }
    free(copied);
    return ok;
}

bool qa_qvm_execution_words_is_last(const qa_qvm_word_projection *lease)
{
    execution *exec = lease && lease->vm ? state(lease->vm) : NULL;
    return exec && exec->projections == lease && !lease->closing;
}
bool qa_qvm_execution_source_returned(const qa_qvm *vm)
{
    const execution *exec = state(vm);
    return vm && !vm->retired && exec && !exec->active && !exec->host && !exec->counter &&
        !exec->scratch_depth && !vm->publication_depth && !vm->write_delivery_depth && !vm->lifecycle_depth;
}

bool qa_qvm_execution_words_begin(qa_qvm *vm, const qa_qvm_image *image,
    const qa_qvm_source_word *words, size_t count, qa_qvm_word_projection **out, qa_error *error)
{ return words_begin(vm, image, words, count, true, false, out, error); }
bool qa_qvm_execution_words_begin_observed(qa_qvm *vm, const qa_qvm_image *image,
    const qa_qvm_source_word *words, size_t count, qa_qvm_word_projection **out, qa_error *error)
{ return words_begin(vm, image, words, count, true, true, out, error); }
static bool words_capture(qa_qvm *vm, const qa_qvm_image *image,
    const uint32_t *addresses, size_t count, bool observed, qa_qvm_word_projection **out, qa_error *error)
{
    if (!addresses || !count || count > SIZE_MAX / sizeof(qa_qvm_source_word))
        return error_at(error, 0, "QVM captured words require their actual source addresses");
    qa_qvm_source_word *words = calloc(count, sizeof(*words));
    if (!words) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Retaining actual source result addresses");
    for (size_t i = 0; i < count; ++i) words[i].offset = addresses[i];
    bool ok = words_begin(vm, image, words, count, false, observed, out, error);
    free(words); return ok;
}
bool qa_qvm_execution_words_capture(qa_qvm *vm, const qa_qvm_image *image,
    const uint32_t *addresses, size_t count, qa_qvm_word_projection **out, qa_error *error)
{ return words_capture(vm, image, addresses, count, false, out, error); }
bool qa_qvm_execution_words_capture_observed(qa_qvm *vm, const qa_qvm_image *image,
    const uint32_t *addresses, size_t count, qa_qvm_word_projection **out, qa_error *error)
{ return words_capture(vm, image, addresses, count, true, out, error); }
bool qa_qvm_execution_source_bytes_write(qa_qvm *vm, const qa_qvm_image *image,
    uint32_t offset, qa_bytes bytes, qa_error *error)
{
    qa_error local = {0};
    if (!error) error = &local;
    if (!qa_qvm_execution_reentry(vm, error)) return false;
    execution *exec = state(vm);
    if (!image || image != vm->image || exec->counter || (bytes.size && !bytes.data))
        return error_at(error, offset, "QVM record refresh requires its actual Source image and counted bytes");
    if ((exec->active && !healthy(vm, error)) ||
        !qa_qvm_raw_range(vm, offset, bytes.size, error)) return false;
    if (bytes.size) memmove(vm->data + offset, bytes.data, bytes.size);
    return true;
}

bool qa_qvm_execution_scratch_run_reserved(qa_qvm *vm, const qa_qvm_image *image, size_t length,
    uint32_t reservation, qa_qvm_source_scratch_run_fn run, void *context, qa_error *error)
{
    qa_error local = {0};
    if (!error) error = &local;
    if (!qa_qvm_execution_reentry(vm, error)) return false;
    execution *exec = state(vm);
    uint32_t offset, end;
    if (!image || image != vm->image || !run || exec->counter ||
        !scratch_span(vm, length, reservation, &offset, &end, error)) return false;
    if (exec->active && !healthy(vm, error)) return false;
    uint8_t *saved = malloc(length);
    if (!saved) return qa_qvm_error(error, QA_ERROR_MEMORY, offset, "Retaining actual host-initiated source scratch");
    memcpy(saved, vm->data + offset, length);
    uint32_t previous_floor = exec->scratch_floor;
    exec->scratch_floor = end;
    ++exec->scratch_depth;
    bool ok = run(context, vm, offset, error);
    qa_error first = *error, cleanup = {0};
    bool restored = qa_qvm_memory_restore_scratch(vm, offset, (qa_bytes){saved, length}, &cleanup);
    --exec->scratch_depth; exec->scratch_floor = previous_floor; free(saved);
    if (!ok) { *error = first; return false; }
    if (!restored) { *error = cleanup; return false; }
    return true;
}

bool qa_qvm_execution_scratch_run(qa_qvm *vm, const qa_qvm_image *image, size_t length,
    qa_qvm_source_scratch_run_fn run, void *context, qa_error *error)
{ return qa_qvm_execution_scratch_run_reserved(vm, image, length, 0, run, context, error); }

static bool evaluation_stack(qa_qvm *vm, const qa_qvm_evaluation_stack *requested,
                              uint32_t *floor, uint32_t *top, qa_error *error)
{
    if (!qa_qvm_execution_reentry(vm, error)) return false;
    uint64_t initialized = (uint64_t)vm->image->data_length + vm->image->literal_length;
    uint64_t data_end = initialized + vm->image->bss_length;
    uint32_t current = state(vm)->program_stack;
    uint64_t start = requested == NULL ? (data_end + 3) & ~UINT64_C(3) : requested->floor;
    uint32_t inherited = state(vm)->active == NULL ? 0 : state(vm)->active->floor;
    if (state(vm)->scratch_floor > inherited) inherited = state(vm)->scratch_floor;
    if (requested == NULL && start < inherited) start = inherited;
    uint64_t end = requested == NULL ? current : requested->top;
    if (start < initialized || start < inherited || start >= end || end > vm->data_size ||
        current > end || ((start | end) & 3) != 0)
        return error_at(error, 0, "QVM evaluation stack is outside its reservation or active caller");
    *floor = (uint32_t)start; *top = current; return true;
}
bool qa_qvm_evaluate_region(qa_qvm *vm, uint32_t owner, const int32_t *arguments, size_t argument_count,
                            const qa_qvm_region_evaluation *evaluation, const int32_t *inputs,
                            const qa_qvm_evaluation_stack *requested, int32_t *out, qa_error *error)
{
    uint32_t floor, top;
    if (!evaluation_stack(vm, requested, &floor, &top, error)) return false;
    if (evaluation == NULL) return error_at(error, owner, "Missing QVM region evaluation");
    if (!qualify(vm->image, owner, evaluation->entry, evaluation->join, evaluation,
                 vm->options.semantics == QA_QVM_INTERPRETED, error)) return false;
    (void)top;
    if (evaluation->input_count != 0 && inputs == NULL) return error_at(error, owner, "Missing QVM region live-in values");
    if (evaluation->input_count > SIZE_MAX / (sizeof(uint32_t) + sizeof(int32_t)))
        return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "QVM region inputs are too large");
    size_t bytes = evaluation->input_count * sizeof(uint32_t);
    uint32_t *storage = bytes == 0 ? NULL : malloc(bytes * 2);
    if (bytes != 0 && storage == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM region live-ins");
    int32_t *values = bytes == 0 ? NULL : (int32_t *)(storage + evaluation->input_count);
    if (bytes != 0) { memcpy(storage, evaluation->inputs, bytes); memcpy(values, inputs, bytes); }
    qa_qvm_region_evaluation copied = *evaluation; copied.inputs = storage;
    bool ok = invoke(vm, owner, arguments, argument_count, &copied, values, floor, out, NULL, error);
    free(storage); return ok;
}
bool qa_qvm_evaluate_call_region(const qa_qvm_call *call, const qa_qvm_region_evaluation *evaluation,
                                 const int32_t *inputs, int32_t *out, qa_error *error)
{
    qa_error local_error = {0};
    if (error == NULL) error = &local_error;
    if (!qa_qvm_execution_token(call, error)) return false;
    qa_qvm *vm = call->vm; execution *exec = state(vm);
    source_call *source = exec->host->owner;
    if (exec->host->kind != HOST_FUNCTION || source == NULL || source->proceeded || evaluation == NULL || out == NULL)
        return error_at(error, 0, "QVM call region must consume an unused original continuation");
    if (!qualify(vm->image, source->instruction, evaluation->entry, evaluation->join, evaluation,
                 vm->options.semantics == QA_QVM_INTERPRETED, error)) return false;
    if ((evaluation->input_count != 0 && inputs == NULL)
        || evaluation->input_count > SIZE_MAX / (sizeof(uint32_t) + sizeof(int32_t)))
        return error_at(error, 0, "QVM call region has invalid live-ins");
    if (!healthy(vm, error)) { if (!exec->failed && exec->cancelled != NULL) { *out = 0; return true; } return false; }
    size_t bytes = evaluation->input_count * sizeof(uint32_t);
    uint32_t *storage = bytes == 0 ? NULL : malloc(bytes * 2);
    if (bytes != 0 && storage == NULL) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM call region inputs");
    int32_t *values = bytes == 0 ? NULL : (int32_t *)(storage + evaluation->input_count);
    if (bytes != 0) { memcpy(storage, evaluation->inputs, bytes); memcpy(values, inputs, bytes); }
    qa_qvm_region_evaluation copied = *evaluation; copied.inputs = storage;
    source->proceeded = true;
    int32_t result;
    bool ok = execute(vm, source->instruction, source->stack, source->operands, source, &copied, values, &result, NULL, error);
    free(storage);
    if (!ok && exec->cancelled != NULL && !exec->failed) { *out = 0; return true; }
    if (!ok) { latch(vm, error); return false; }
    *out = result; return true;
}
bool qa_qvm_evaluate_counter(qa_qvm *vm, qa_qvm_source_word *words, size_t word_count,
                             const uint32_t *functions, size_t function_count,
                             uint32_t instruction, const int32_t *arguments, size_t argument_count,
                             const qa_qvm_evaluation_stack *requested, qa_error *error)
{
    uint32_t floor, top;
    if (!evaluation_stack(vm, requested, &floor, &top, error)) return false;
    execution *exec = state(vm);
    uint64_t source_end = (uint64_t)vm->image->data_length + vm->image->literal_length + vm->image->bss_length;
    if (exec->counter != NULL || !words || !word_count || word_count > SIZE_MAX / sizeof(*words) ||
        functions == NULL || function_count == 0 || function_count > SIZE_MAX / (2 * sizeof(uint32_t)))
        return error_at(error, 0, "QVM counter needs isolated words and distinct original functions");
    for (size_t i = 0; i < word_count; ++i) {
        if ((words[i].offset & 3) || (uint64_t)words[i].offset + 4 > source_end ||
            (uint64_t)words[i].offset + 4 > floor)
            return error_at(error, words[i].offset, "QVM counter word leaves original nonstack data");
        for (size_t j = 0; j < i; ++j) if (words[j].offset == words[i].offset)
            return error_at(error, words[i].offset, "QVM counter words overlap");
    }
    qa_qvm_source_word *values = malloc(word_count * sizeof(*values));
    if (!values) return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Retaining isolated QVM counter values");
    memcpy(values, words, word_count * sizeof(*values));
    uint32_t *ends = malloc(function_count * 2 * sizeof(*ends));
    if (ends == NULL) { free(values); return qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Allocating QVM counter function bounds"); }
    uint32_t *admitted_functions = ends + function_count;
    memcpy(admitted_functions, functions, function_count * sizeof(*functions));
    functions = admitted_functions;
    bool admitted = false, ok = true;
    for (size_t i = 0; ok && i < function_count; ++i) {
        if (!function(vm->image, functions[i], error)) { ok = false; break; }
        for (size_t j = 0; j < i; ++j)
            if (functions[j] == functions[i]) { ok = error_at(error, functions[i], "Duplicate QVM counter function"); break; }
        uint32_t end = function_end(vm->image, functions[i]);
        ends[i] = end == vm->image->instruction_count ? vm->image->code_length : vm->image->instructions[end].byte_offset;
        if (functions[i] == instruction) admitted = true;
    }
    if (ok && !admitted) ok = error_at(error, instruction, "QVM counter entry is not admitted");
    counter_evaluation counter = {floor, top, values, word_count, functions, ends, function_count, 100000};
    if (ok) {
        size_t stack_size = top > floor ? (size_t)(top - floor) : 0;
        uint8_t *saved_stack = stack_size ? malloc(stack_size) : NULL;
        if (stack_size && !saved_stack) {
            ok = qa_qvm_error(error, QA_ERROR_MEMORY, 0, "Retaining isolated QVM counter stack bytes");
        } else {
            if (stack_size) memcpy(saved_stack, vm->data + floor, stack_size);
            exec->counter = &counter;
            int32_t result;
            ok = invoke(vm, instruction, arguments, argument_count, NULL, NULL, floor, &result, NULL, error);
            exec->counter = NULL;
            if (stack_size) memcpy(vm->data + floor, saved_stack, stack_size);
            free(saved_stack);
        }
    }
    free(ends);
    if (ok) memcpy(words, values, word_count * sizeof(*words));
    free(values);
    return ok;
}

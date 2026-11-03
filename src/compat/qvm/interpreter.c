/*
 * Port of src/compat/qvm/interpreter.ts, plus qa_qvmx_* forms of memory.ts and
 * operations.ts. The TS generator and Promise machinery collapses to direct
 * synchronous recursion (docs/PORTING.md). TS try/catch/finally maps onto
 * qa_range_error_catch at the same boundaries, and cancellation is the same
 * thrown signal as in TS, so a hook's own catch and cleanup still run and the
 * exact target invocation returns zero.
 * GPL-2.0-or-later.
 */
#include "qa/compat/qvm/interpreter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/operations.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/network/common/js_number.h"

/* ---------- mirrors of memory.ts (qvm-b) ---------- */

/* DataView scalar access over a qa_qvmx_view (TS DataView over guest
 * bytes; bounds via memory-writes.ts index(), little-endian). */
void qa_qvmx_view_check(const qa_qvmx_view *view, int64_t offset, int64_t width) {
  if (offset < 0 || width < 0 || offset > (int64_t)view->length - width)
    qa_range_error("Offset is outside the bounds of the DataView");
}
int32_t qa_qvmx_view_i32(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 4);
  const uint8_t *p = view->bytes + offset;
  uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  int32_t out;
  memcpy(&out, &v, sizeof out);
  return out;
}
/* The TS view is always a QvmWritableView: stores inside an owned allocation
 * capture and publish; the store itself is one little-endian write. */
static void view_store(qa_qvmx_view *view, int64_t offset, const uint8_t *bytes, size_t width) {
  uint8_t *target = view->bytes + offset;
  qa_qvm_memory_writes *owner = qa_qvmx_writes_for(target);
  if (owner != NULL) qa_qvm_memory_writes_store(owner, target - owner->bytes, bytes, width);
  else memcpy(target, bytes, width);
}
void qa_qvmx_view_set_i32(qa_qvmx_view *view, int64_t offset, int32_t value) {
  qa_qvmx_view_check(view, offset, 4);
  uint32_t v;
  memcpy(&v, &value, sizeof v);
  uint8_t bytes[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  view_store(view, offset, bytes, 4);
}
float qa_qvmx_view_f32(const qa_qvmx_view *view, int64_t offset) {
  int32_t bits = qa_qvmx_view_i32(view, offset);
  float out;
  memcpy(&out, &bits, sizeof out);
  /* DataView.getFloat32 yields the canonical quiet NaN for every payload. */
  return isnan(out) ? NAN : out;
}
void qa_qvmx_view_set_f32(qa_qvmx_view *view, int64_t offset, float value) {
  int32_t bits;
  memcpy(&bits, &value, sizeof bits);
  qa_qvmx_view_set_i32(view, offset, bits);
}
uint8_t qa_qvmx_view_u8(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 1);
  return view->bytes[offset];
}
void qa_qvmx_view_set_u8(qa_qvmx_view *view, int64_t offset, uint8_t value) {
  qa_qvmx_view_check(view, offset, 1);
  view_store(view, offset, &value, 1);
}
uint16_t qa_qvmx_view_u16(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 2);
  const uint8_t *p = view->bytes + offset;
  return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}
void qa_qvmx_view_set_u16(qa_qvmx_view *view, int64_t offset, uint16_t value) {
  qa_qvmx_view_check(view, offset, 2);
  uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
  view_store(view, offset, bytes, 2);
}

static bool is_safe_integer(double v) {
  return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0;
}
static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }

size_t qa_qvmx_memory_pointer_offset(const qa_qvmx_memory *memory, double word, bool *is_null) {
  if (!is_integer(word) || word < -2147483648.0 || word > 2147483647.0)
    qa_range_error("QVM pointer must be a signed 32-bit word");
  int32_t w = (int32_t)word;
  if (w == 0) {
    if (is_null != NULL) *is_null = true;
    return 0;
  }
  if (is_null != NULL) *is_null = false;
  return (size_t)((uint32_t)w & (uint32_t)(memory->length - 1));
}

static void memory_range(const qa_qvmx_memory *memory, double offset, double length) {
  if (!is_safe_integer(offset) || !is_safe_integer(length) || offset < 0 || length < 0 ||
      offset > (double)memory->length || length > (double)memory->length - offset)
    qa_range_error("QVM raw memory range exceeds allocation");
}

qa_qvmx_view qa_qvmx_memory_span(const qa_qvmx_memory *memory, double word, double length,
                                 double relative_offset) {
  bool is_null = false;
  size_t base = qa_qvmx_memory_pointer_offset(memory, word, &is_null);
  if (is_null) qa_range_error("QVM memory span requires a nonnull pointer");
  double start = (double)base + relative_offset;
  if (!is_safe_integer(relative_offset) || !is_safe_integer(length) || length < 0 || start < 0 ||
      start > (double)memory->length || length > (double)memory->length - start)
    qa_range_error("QVM memory span exceeds allocation or has an invalid length");
  qa_qvmx_view view = {memory->bytes + (size_t)start, (size_t)length};
  return view;
}

qa_qvmx_view qa_qvmx_memory_view(const qa_qvmx_memory *memory, double word, double length,
                                 double relative_offset) {
  return qa_qvmx_memory_span(memory, word, length, relative_offset);
}

qa_qvmx_view qa_qvmx_memory_data_view(const qa_qvmx_memory *memory, double byte_offset,
                                      double byte_length) {
  memory_range(memory, byte_offset, byte_length);
  qa_qvmx_view view = {memory->bytes + (size_t)byte_offset, (size_t)byte_length};
  return view;
}

char *qa_qvmx_memory_read_string(const qa_qvmx_memory *memory, double word) {
  bool is_null = false;
  size_t base = qa_qvmx_memory_pointer_offset(memory, word, &is_null);
  if (is_null) qa_range_error("QVM string requires a nonnull pointer");
  size_t end = base;
  while (end < memory->length && memory->bytes[end] != 0) end++;
  if (end >= memory->length)
    qa_range_error("QVM string has no terminator before the allocation ends");
  size_t length = end - base;
  char *text = malloc(length + 1);
  if (text == NULL) return NULL;
  memcpy(text, memory->bytes + base, length);
  text[length] = '\0';
  return text;
}

static void q_strncpyz(uint8_t *destination, size_t destination_length, const char *source,
                       double size) {
  if (destination == NULL) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: NULL dest");
  if (size < 1) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: destsize < 1");
  if (!is_integer(size) || size > (double)destination_length)
    qa_range_error("Q_strncpyz destination exceeds its allocation");
  size_t count = (size_t)size;
  size_t index = 0;
  size_t source_length = strlen(source);
  for (; index + 1 < count; index++) {
    uint8_t byte = index < source_length ? (uint8_t)source[index] : 0;
    destination[index] = byte;
    if (byte == 0) break;
  }
  if (index < count) memset(destination + index, 0, count - index);
}

void qa_qvmx_memory_write_string(qa_qvmx_memory *memory, double word, const char *text,
                                 double capacity) {
  bool is_null = false;
  size_t base = qa_qvmx_memory_pointer_offset(memory, word, &is_null);
  if (!is_null && (!is_integer(capacity) || capacity > (double)(memory->length - base)))
    qa_range_error("Q_strncpyz destination exceeds QVM allocation");
  if (is_null) {
    q_strncpyz(NULL, 0, text, capacity);
    return;
  }
  qa_qvm_memory_writes *owner = qa_qvmx_writes_for(memory->bytes);
  if (owner != NULL) qa_qvm_memory_writes_assert_writable(owner);
  if (capacity < 1) {
    q_strncpyz(memory->bytes + base, memory->length - base, text, capacity);
    return;
  }
  qa_qvm_write_capture *capture = owner != NULL && qa_qvm_memory_writes_get_intercepts(owner)
      ? qa_qvm_memory_writes_before(owner, (int64_t)base, (int64_t)capacity) : NULL;
  q_strncpyz(memory->bytes + base, memory->length - base, text, capacity);
  if (owner != NULL) qa_qvm_memory_writes_after(owner, capture);
}

void qa_qvmx_memory_write_bounded_string(qa_qvmx_memory *memory, double word, const char *text,
                                         double capacity) {
  bool is_null = false;
  size_t base = qa_qvmx_memory_pointer_offset(memory, word, &is_null);
  if (is_null || !is_safe_integer(capacity) || capacity < 1 ||
      capacity > (double)(memory->length - base))
    qa_range_error("Bounded string copy exceeds QVM allocation");
  qa_qvm_memory_writes *owner = qa_qvmx_writes_for(memory->bytes);
  if (owner != NULL) qa_qvm_memory_writes_assert_writable(owner);
  qa_qvm_write_capture *capture = owner != NULL && qa_qvm_memory_writes_get_intercepts(owner)
      ? qa_qvm_memory_writes_before(owner, (int64_t)base, (int64_t)capacity) : NULL;
  q_strncpyz(memory->bytes + base, memory->length - base, text, capacity);
  if (owner != NULL) qa_qvm_memory_writes_after(owner, capture);
}

/* Bulk writes follow QvmMemory: assert, validate, capture, mutate, publish. */
static qa_qvm_memory_writes *bulk_owner(const qa_qvmx_memory *memory) {
  qa_qvm_memory_writes *owner = qa_qvmx_writes_for(memory->bytes);
  if (owner != NULL) qa_qvm_memory_writes_assert_writable(owner);
  return owner;
}
static qa_qvm_write_capture *bulk_capture(qa_qvm_memory_writes *owner, double offset, double length) {
  return owner != NULL && qa_qvm_memory_writes_get_intercepts(owner)
      ? qa_qvm_memory_writes_before(owner, (int64_t)offset, (int64_t)length) : NULL;
}

void qa_qvmx_memory_write_bytes(qa_qvmx_memory *memory, double byte_offset,
                                const uint8_t *bytes, size_t length) {
  qa_qvm_memory_writes *owner = bulk_owner(memory);
  memory_range(memory, byte_offset, (double)length);
  qa_qvm_write_capture *capture = bulk_capture(owner, byte_offset, (double)length);
  memmove(memory->bytes + (size_t)byte_offset, bytes, length);
  if (owner != NULL) qa_qvm_memory_writes_after(owner, capture);
}

void qa_qvmx_memory_fill_bytes(qa_qvmx_memory *memory, double byte_offset, double byte_length,
                               double value) {
  qa_qvm_memory_writes *owner = bulk_owner(memory);
  memory_range(memory, byte_offset, byte_length);
  qa_qvm_write_capture *capture = bulk_capture(owner, byte_offset, byte_length);
  memset(memory->bytes + (size_t)byte_offset, (int)(qa_uint32(value) & 255u), (size_t)byte_length);
  if (owner != NULL) qa_qvm_memory_writes_after(owner, capture);
}

void qa_qvmx_memory_copy_bytes(qa_qvmx_memory *memory, double destination, double source,
                               double byte_length) {
  qa_qvm_memory_writes *owner = bulk_owner(memory);
  memory_range(memory, destination, byte_length);
  memory_range(memory, source, byte_length);
  qa_qvm_write_capture *capture = bulk_capture(owner, destination, byte_length);
  memmove(memory->bytes + (size_t)destination, memory->bytes + (size_t)source, (size_t)byte_length);
  if (owner != NULL) qa_qvm_memory_writes_after(owner, capture);
}

/* ---------- mirrors of operations.ts ---------- */

int32_t qa_qvmx_evaluate_unary(qa_qvm_opcode opcode, int32_t word) {
  return qa_qvm_evaluate_unary(opcode, word);
}
int32_t qa_qvmx_evaluate_binary(qa_qvm_opcode opcode, int32_t left, int32_t right) {
  return qa_qvm_evaluate_binary(opcode, left, right);
}
bool qa_qvmx_evaluate_branch(qa_qvm_opcode opcode, int32_t left, int32_t right) {
  return qa_qvm_evaluate_branch(opcode, left, right);
}

/* ---------- interpreter.ts ---------- */

#define THROWN_CAPACITY 512
#define CANCEL_SIGNAL "QVM function scope cancelled"
#define HOST_SCOPE_MESSAGE "QVM recursive call requires the active syscall or function hook with no pending child"

typedef struct operands {
  int32_t cells[256];
  bool has[256];
  int depth;
  bool debug;
} operands;

static void operands_reserve(operands *o) {
  if (o->depth == 255) {
    if (o->debug) qa_common_error(QA_COMMON_ERROR_DROP, "VM opStack overflow");
    qa_range_error("QVM operand stack overflow");
  }
  o->depth++;
}
static void operands_push(operands *o, int32_t word) {
  operands_reserve(o);
  o->cells[o->depth] = word;
  o->has[o->depth] = true;
}
static int32_t operands_peek(operands *o) {
  if (!o->has[o->depth]) {
    if (o->debug) qa_common_error(QA_COMMON_ERROR_DROP, "QVM reads an uninitialized operand");
    qa_range_error("QVM reads an uninitialized operand");
  }
  return o->cells[o->depth];
}
static void operands_set(operands *o, int32_t word) {
  o->cells[o->depth] = word;
  o->has[o->depth] = true;
}
static void operands_drop(operands *o) {
  if (o->depth == 0) {
    if (o->debug) qa_common_error(QA_COMMON_ERROR_DROP, "VM opStack underflow");
    qa_range_error("QVM operand stack underflow");
  }
  o->depth--;
}
static int32_t operands_pop(operands *o) {
  int32_t word = operands_peek(o);
  operands_drop(o);
  return word;
}
static void operands_complement_previous(operands *o) {
  if (o->depth == 0) {
    if (o->debug) qa_common_error(QA_COMMON_ERROR_DROP, "VM opStack underflow");
    qa_range_error("QVM operand stack underflow");
  }
  /* ~peek in int32_t, written one slot down: the pinned interpreter's OP_BCOM. */
  o->cells[o->depth - 1] = ~operands_peek(o);
  o->has[o->depth - 1] = true;
}
static int32_t operands_result(operands *o) {
  if (o->depth != 1) {
    char message[64];
    snprintf(message, sizeof message, "Interpreter error: opStack = %d", o->depth);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  return operands_peek(o);
}
static void operands_truncate(operands *o, int depth) {
  if (depth < 0 || depth > o->depth) qa_range_error("QVM cancellation lost its caller operands");
  while (o->depth > depth) {
    o->has[o->depth] = false;
    o->depth--;
  }
}

/* TS reads this.instructions[instructionIndex]: a fraction, NaN or out-of-range
 * index finds no OP_ENTER and fails. -1 makes execute fail the same way. */
static int64_t invocation_index(double value) {
  return is_integer(value) && value >= 0 && value < 9007199254740992.0 ? (int64_t)value : -1;
}

static int32_t signed_word(double value) {
  if (!is_integer(value) || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("QVM host value must be a signed 32-bit integer");
  return (int32_t)value;
}

/* Entries live until the interpreter dies: a removal closure may run twice, and
 * a running intercept holds its snapshot, exactly as TS retains these objects. */
typedef struct hook_entry {
  int64_t index;
  qa_qvm_function_hook hook;
  bool invocations;
  bool active;
  struct qa_qvm_interpreter *interp;
} hook_entry;

typedef struct observer_entry {
  int64_t index;
  qa_qvm_function_observer observer;
  bool active;
  struct qa_qvm_interpreter *interp;
} observer_entry;

typedef struct resolver_entry {
  qa_qvm_function_resolver resolver;
  struct qa_qvm_interpreter *interp;
} resolver_entry;

typedef struct counter_scope {
  int64_t address;
  int32_t value;
  int64_t stack_start, stack_end;
  int64_t remaining;
  const double *functions;
  size_t function_count;
  int64_t *range_start, *range_end;
} counter_scope;

typedef struct source_region {
  qa_qvm_region_binding binding;
  int64_t entry_pc, join_pc, stack, frame_size;
  int state; /* 0 ready 1 running 2 complete */
} source_region;

typedef struct source_call source_call;

/* A thrown error: its text, and for a cancellation which call's signal it is
 * (TS compares the Error object by identity). */
typedef struct thrown {
  bool set;
  source_call *signal;
  char message[THROWN_CAPACITY];
  const char *name; /* the thrown class (CommonError keeps its code), RangeError when unknown */
  int code;
  long long detail;
} thrown;

struct source_call {
  int64_t stack, return_pc;
  operands *ops;
  int operand_depth;
  source_call *parent;
  bool active;
  bool branches_bound;
  int64_t *branch_pcs;
  qa_qvm_branch_binding *branch_bindings;
  size_t branch_count;
  source_region *regions;
  size_t region_count;
  bool has_evaluation;
  qa_qvmx_region_evaluation eval_region;
  int64_t *eval_region_inputs;
  double *eval_inputs;
  size_t eval_input_count;
  int64_t eval_frame_size;
  bool cancellation_open, cancellation_requested;
  uint64_t cancellation_serial;
  thrown cancellation_failure;
  source_call *next_scope; /* interpreter's open cancellation scopes */
};

typedef struct invocation {
  operands *ops;
  operands owned_ops;
  int64_t sp;
  source_call *function_scope;
  struct invocation *previous;
  bool asynchronous;
  struct invocation *validate_parent; /* TS composes the parent frame's validate */
  void (*validate)(void *ctx);
  void *validate_ctx;
  int64_t *returns; /* compiled semantics: CALL/RET control outside guest memory */
  size_t return_count, return_capacity;
} invocation;

struct qa_qvm_interpreter {
  uint8_t *memory;
  size_t memory_length;
  bool memory_owned, pointers_owned, code_owned;
  qa_qvmx_memory address_space;
  qa_qvm_memory guest;
  bool guest_ready;
  qa_qvm_interpreter_system_call system_call;
  qa_qvm_store_effect store_effect;
  bool has_store_effect;
  const qa_qvm_instruction *instructions;
  size_t instruction_count;
  char *source;
  int64_t source_data_end, memory_initialized_end;
  int32_t *code;
  size_t code_length;
  int32_t *instruction_pointers;
  int64_t data_mask;
  int64_t program_stack;
  int64_t call_level;
  bool root_active, debug, compiled;
  int32_t breaks;
  invocation *active;
  counter_scope *counter;
  hook_entry **hooks;
  size_t hook_count, hook_capacity;
  observer_entry **observers;
  size_t observer_count, observer_capacity;
  void **retired; /* removed hook/observer/resolver entries */
  size_t retired_count, retired_capacity;
  resolver_entry *resolver;
  const qa_qvm_vm_registration *registration;
  const qa_qvm_allocation_profile *allocation_profile;
  qa_qvm_symbols symbols;
  uint64_t identity;
  uint64_t next_serial;
  source_call *open_scopes;
  source_call *current_signal;
  /* Allocations the interpreter must release or whose liveness it must check. */
  void **symbol_blocks;
  size_t symbol_block_count, symbol_block_capacity;
  qa_qvm_allocation *allocations;
  size_t allocation_count, allocation_capacity;
};

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

/* TS assertAllocation(). */
static void assert_allocation(const qa_qvm_interpreter *interp) {
  qa_qvm_memory_writes_assert_live(&interp->guest.writes);
  if (interp->registration != NULL) {
    qa_qvm_vm_binding binding = qa_qvm_vm_registration_get_binding(interp->registration);
    if (binding.kind != NULL && strcmp(binding.kind, "freed") == 0)
      qa_range_error("QVM registration has been freed");
  }
  for (size_t i = 0; i < interp->allocation_count; i++)
    if (interp->allocations[i].assert_live != NULL)
      interp->allocations[i].assert_live(interp->allocations[i].live_ctx);
}

/* TS live(). */
static void assert_live(const qa_qvm_interpreter *interp) {
  qa_qvm_memory_writes_assert_not_publishing(&interp->guest.writes);
  assert_allocation(interp);
}

static void assert_live_cb(void *ctx) { assert_live((const qa_qvm_interpreter *)ctx); }

/* TS QvmOpcode reverse-enum names for the trace>1 debug print. */
static const char *const opcode_names[] = {
    "OP_UNDEF", "OP_IGNORE", "OP_BREAK", "OP_ENTER", "OP_LEAVE", "OP_CALL",
    "OP_PUSH", "OP_POP", "OP_CONST", "OP_LOCAL", "OP_JUMP", "OP_EQ", "OP_NE",
    "OP_LTI", "OP_LEI", "OP_GTI", "OP_GEI", "OP_LTU", "OP_LEU", "OP_GTU",
    "OP_GEU", "OP_EQF", "OP_NEF", "OP_LTF", "OP_LEF", "OP_GTF", "OP_GEF",
    "OP_LOAD1", "OP_LOAD2", "OP_LOAD4", "OP_STORE1", "OP_STORE2", "OP_STORE4",
    "OP_ARG", "OP_BLOCK_COPY", "OP_SEX8", "OP_SEX16", "OP_NEGI", "OP_ADD",
    "OP_SUB", "OP_DIVI", "OP_DIVU", "OP_MODI", "OP_MODU", "OP_MULI", "OP_MULU",
    "OP_BAND", "OP_BOR", "OP_BXOR", "OP_BCOM", "OP_LSH", "OP_RSHI", "OP_RSHU",
    "OP_NEGF", "OP_ADDF", "OP_SUBF", "OP_DIVF", "OP_MULF", "OP_CVIF", "OP_CVFI",
};

static int64_t target_pc(qa_qvm_interpreter *interp, double index) {
  if (!is_integer(index) || index < 0 || index >= (double)interp->instruction_count)
    qa_range_error("%s: invalid QVM instruction index %.0f", interp->source, index);
  return interp->instruction_pointers[(size_t)index];
}

static int64_t source_instruction(qa_qvm_interpreter *interp, int64_t pc) {
  int64_t low = 0, high = (int64_t)interp->instruction_count - 1;
  while (low <= high) {
    int64_t middle = (low + high) >> 1;
    int32_t candidate = interp->instruction_pointers[middle];
    if (candidate == pc) return middle;
    if (candidate < pc) low = middle + 1; else high = middle - 1;
  }
  qa_range_error("%s: caller PC is not an original instruction", interp->source);
}

static int32_t code_word(qa_qvm_interpreter *interp, int64_t pc) {
  if (pc < 0 || pc >= (int64_t)interp->code_length)
    qa_range_error("%s: invalid QVM byte PC %lld", interp->source, (long long)pc);
  return interp->code[pc];
}

static void interp_range(qa_qvm_interpreter *interp, int64_t address, int64_t length) {
  if (address < 0 || address > (int64_t)interp->memory_length - length)
    qa_range_error("%s: QVM memory access %lld+%lld exceeds allocation", interp->source,
                   (long long)address, (long long)length);
}

static int64_t interp_stack(qa_qvm_interpreter *interp, int64_t address) {
  interp_range(interp, address, 0);
  if (address % 4 != 0) {
    if (interp->debug) qa_common_error(QA_COMMON_ERROR_DROP, "VM program stack misaligned");
    qa_range_error("QVM program stack is misaligned");
  }
  return address;
}

static bool counter_read(qa_qvm_interpreter *interp, int64_t address, int64_t length,
                         int32_t *out) {
  counter_scope *scope = interp->counter;
  if (scope == NULL || address + length <= scope->address || address >= scope->address + 4)
    return false;
  if (address != scope->address || length != 4)
    qa_range_error("QVM counter evaluation cannot partially read its isolated word");
  *out = scope->value;
  return true;
}

static bool counter_write(qa_qvm_interpreter *interp, int64_t address, int64_t length,
                          int32_t value) {
  counter_scope *scope = interp->counter;
  if (scope == NULL) return false;
  if (address == scope->address && length == 4) {
    scope->value = value;
    return true;
  }
  int64_t current = interp->active != NULL ? interp->active->sp : scope->stack_end;
  int64_t low = scope->stack_start > current ? scope->stack_start : current;
  if (address < low || address + length > scope->stack_end)
    qa_range_error("QVM counter evaluation attempted an unrelated source write");
  return false;
}

/* One guest store: observed through the allocation's writes owner unless a
 * counter evaluation isolates it. */
static void guest_store(qa_qvm_interpreter *interp, int64_t address, uint32_t value, size_t width) {
  uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
  if (interp->counter == NULL && qa_qvm_memory_writes_get_intercepts(&interp->guest.writes)) {
    qa_qvm_memory_writes_store(&interp->guest.writes, address, bytes, width);
    return;
  }
  memcpy(interp->memory + address, bytes, width);
}

static int32_t raw_word(const qa_qvm_interpreter *interp, int64_t address) {
  const uint8_t *p = interp->memory + address;
  uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  int32_t out;
  memcpy(&out, &v, sizeof out);
  return out;
}

static int32_t read_word(qa_qvm_interpreter *interp, int64_t address) {
  interp_range(interp, address, 4);
  int32_t value;
  if (interp->counter != NULL && counter_read(interp, address, 4, &value)) return value;
  return raw_word(interp, address);
}

static void write_word(qa_qvm_interpreter *interp, int64_t address, int32_t word) {
  interp_range(interp, address, 4);
  if (interp->counter != NULL && counter_write(interp, address, 4, word)) return;
  guest_store(interp, address, (uint32_t)word, 4);
}

static int64_t source_argument_bytes(qa_qvm_interpreter *interp, int64_t caller_instruction) {
  int64_t index = caller_instruction;
  while (index >= 0) {
    const qa_qvm_instruction *instruction = &interp->instructions[index--];
    if (instruction->opcode != QA_QVM_OP_ENTER) continue;
    if (instruction->operand < 8 || instruction->operand % 4 != 0)
      qa_range_error("QVM source call lacks an aligned caller frame");
    int64_t bytes = instruction->operand - 8;
    if (bytes > QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS * 4) bytes = QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS * 4;
    return bytes;
  }
  qa_range_error("QVM source call has no original caller frame");
}

/* ----- thrown errors and cancellation ----- */

static void capture_thrown(qa_qvm_interpreter *interp, const char *message, thrown *out) {
  out->set = true;
  out->signal = strcmp(message, CANCEL_SIGNAL) == 0 ? interp->current_signal : NULL;
  snprintf(out->message, sizeof out->message, "%.*s", (int)(sizeof out->message - 1), message);
  out->name = "RangeError";
  out->code = 0;
  out->detail = 0;
}

static noreturn void rethrow(qa_qvm_interpreter *interp, const thrown *error) {
  interp->current_signal = error->signal;
  qa_raise(error->name != NULL ? error->name : "RangeError", error->code, error->detail, "%s", error->message);
}

static void cancellation_failure(source_call *scope, const thrown *error) {
  for (source_call *call = scope; call != NULL; call = call->parent)
    if (call->active && call->cancellation_requested && error->signal == call) return;
  for (source_call *call = scope; call != NULL; call = call->parent)
    if (call->active && call->cancellation_open && !call->cancellation_failure.set)
      call->cancellation_failure = *error;
}

static void check_cancellation(qa_qvm_interpreter *interp, source_call *scope) {
  source_call *cancelled = NULL;
  for (source_call *call = scope; call != NULL; call = call->parent) {
    if (!call->active || !call->cancellation_open) continue;
    if (call->cancellation_failure.set) rethrow(interp, &call->cancellation_failure);
    if (call->cancellation_requested) cancelled = call;
  }
  if (cancelled != NULL) {
    interp->current_signal = cancelled;
    qa_range_error(CANCEL_SIGNAL);
  }
}

/* A capability encodes its interpreter and a never-reused serial, so an expired
 * one stays distinguishable without retaining the dead call. */
#define TOKEN_SERIAL_BITS 40
static uint64_t next_interpreter_identity = 1;

static qa_qvm_cancellation_scope make_token(qa_qvm_interpreter *interp, uint64_t serial) {
  qa_qvm_cancellation_scope scope;
  scope.token = (const void *)(uintptr_t)((interp->identity << TOKEN_SERIAL_BITS) | serial);
  return scope;
}

static void cancel_function(qa_qvm_interpreter *interp, qa_qvm_cancellation_scope capability,
                            source_call *function_scope) {
  uint64_t token = (uint64_t)(uintptr_t)capability.token;
  uint64_t serial = token & (((uint64_t)1 << TOKEN_SERIAL_BITS) - 1);
  if ((token >> TOKEN_SERIAL_BITS) != interp->identity || serial == 0 || serial >= interp->next_serial)
    qa_range_error("QVM cancellation scope belongs to another interpreter");
  source_call *target = interp->open_scopes;
  while (target != NULL && target->cancellation_serial != serial) target = target->next_scope;
  if (target == NULL) qa_range_error("QVM cancellation scope has expired");
  if (target->cancellation_requested) qa_range_error("QVM cancellation scope has already been used");
  source_call *current = function_scope;
  while (current != NULL && current != target) current = current->parent;
  if (current == NULL) qa_range_error("QVM cancellation scope is not an ancestor of this call");
  check_cancellation(interp, function_scope);
  target->cancellation_requested = true;
  interp->current_signal = target;
  qa_range_error(CANCEL_SIGNAL);
}

/* ----- host call scopes (TS hostCall) ----- */

typedef struct host_scope {
  qa_qvm_interpreter *interp;
  invocation *frame;
  source_call *function_scope;
  bool open;
  thrown failure;
  void *extra;
} host_scope;

static void run_validate(invocation *frame) {
  if (frame->validate_parent != NULL) run_validate(frame->validate_parent);
  if (frame->validate != NULL) frame->validate(frame->validate_ctx);
}

static void host_available(host_scope *scope) {
  if (!scope->open || scope->interp->active != scope->frame) qa_range_error(HOST_SCOPE_MESSAGE);
  assert_live(scope->interp);
  run_validate(scope->frame);
}

typedef struct control_job {
  host_scope *scope;
  void (*operation)(void *ctx);
  void *ctx;
} control_job;

static void control_body(void *ctx) {
  control_job *job = ctx;
  host_available(job->scope);
  job->operation(job->ctx);
}

/* TS scope.control: a failure stays recorded even if the callback catches it. */
static void host_control(host_scope *scope, void (*operation)(void *ctx), void *ctx) {
  control_job job = {scope, operation, ctx};
  char message[THROWN_CAPACITY];
  if (qa_range_error_catch(control_body, &job, message, sizeof message)) return;
  thrown error;
  capture_thrown(scope->interp, message, &error);
  if (!scope->failure.set) scope->failure = error;
  cancellation_failure(scope->function_scope, &error);
  rethrow(scope->interp, &error);
}

typedef double (*host_perform_fn)(void *ctx, host_scope *scope);

typedef struct host_job {
  host_scope *scope;
  host_perform_fn perform;
  void *ctx;
  int32_t result;
} host_job;

static void host_body(void *ctx) {
  host_job *job = ctx;
  double value = job->perform(job->ctx, job->scope);
  /* TS complete(), still inside hostCall's try. */
  host_scope *scope = job->scope;
  scope->open = false;
  if (scope->failure.set) rethrow(scope->interp, &scope->failure);
  check_cancellation(scope->interp, scope->function_scope);
  assert_live(scope->interp);
  run_validate(scope->frame);
  job->result = signed_word(value);
}

static int32_t host_call(qa_qvm_interpreter *interp, invocation *frame, host_perform_fn perform,
                         void *ctx, source_call *function_scope) {
  host_scope scope;
  scope.interp = interp;
  scope.frame = frame;
  scope.function_scope = function_scope;
  scope.open = true;
  scope.failure.set = false;
  scope.failure.signal = NULL;
  scope.extra = NULL;
  host_job job = {&scope, perform, ctx, 0};
  char message[THROWN_CAPACITY];
  if (qa_range_error_catch(host_body, &job, message, sizeof message)) return job.result;
  scope.open = false;
  thrown error;
  capture_thrown(interp, message, &error);
  cancellation_failure(function_scope, &error);
  rethrow(interp, &error);
}

/* hostCall bracketed by the TS programStack save/restore finally. */
static int32_t host_call_stack(qa_qvm_interpreter *interp, invocation *frame, host_perform_fn perform,
                               void *ctx, source_call *function_scope, int64_t stack) {
  int64_t previous = interp->program_stack;
  interp->program_stack = stack;
  host_scope scope;
  scope.interp = interp;
  scope.frame = frame;
  scope.function_scope = function_scope;
  scope.open = true;
  scope.failure.set = false;
  scope.failure.signal = NULL;
  scope.extra = NULL;
  host_job job = {&scope, perform, ctx, 0};
  char message[THROWN_CAPACITY];
  bool ok = qa_range_error_catch(host_body, &job, message, sizeof message);
  if (ok) {
    interp->program_stack = previous;
    return job.result;
  }
  scope.open = false;
  thrown error;
  capture_thrown(interp, message, &error);
  cancellation_failure(function_scope, &error);
  interp->program_stack = previous;
  rethrow(interp, &error);
}

/* ----- execute entry forms ----- */

typedef struct evaluation_local {
  bool has;
  qa_qvmx_region_evaluation region;
  const double *inputs;
  size_t input_count;
  int64_t frame_size;
} evaluation_local;

typedef struct execute_options {
  const double *args;
  size_t arg_count;
  int64_t instruction_index;
  source_call *source;
  source_call *function_scope;
  const qa_qvm_read_only_evaluation *read_only;
  bool asynchronous;
  invocation *validate_parent;
  void (*validate)(void *ctx);
  void *validate_ctx;
} execute_options;

static int32_t execute(qa_qvm_interpreter *interp, const execute_options *options,
                       const evaluation_local *read_only_evaluation);

static int64_t qualify_evaluation(qa_qvm_interpreter *interp, int64_t owner,
                                  const qa_qvm_region_evaluation *region, const char *access);

/* TS executeSync: read-only regions qualify before the frame exists. */
static int32_t execute_sync(qa_qvm_interpreter *interp, const execute_options *options) {
  evaluation_local evaluation;
  memset(&evaluation, 0, sizeof evaluation);
  if (options->read_only != NULL) {
    const qa_qvm_read_only_evaluation *read_only = options->read_only;
    evaluation.frame_size =
        qualify_evaluation(interp, options->instruction_index, &read_only->region, "read-only");
    if (read_only->input_count != read_only->region.input_count)
      qa_range_error("Read-only QVM region live-ins differ from its qualified frame");
    for (size_t i = 0; i < read_only->input_count; i++) (void)signed_word(read_only->inputs[i]);
    evaluation.has = true;
    evaluation.region = read_only->region;
    evaluation.inputs = read_only->inputs;
    evaluation.input_count = read_only->input_count;
  }
  return execute(interp, options, &evaluation);
}

static double host_invoke(void *ctx, const double *args, size_t arg_count, bool has_index,
                          double instruction_index,
                          const qa_qvm_read_only_evaluation *evaluation) {
  host_scope *scope = ctx;
  host_available(scope);
  check_cancellation(scope->interp, scope->function_scope);
  execute_options options;
  memset(&options, 0, sizeof options);
  options.args = args;
  options.arg_count = arg_count;
  options.instruction_index = has_index ? invocation_index(instruction_index) : 0;
  options.function_scope = scope->function_scope;
  options.read_only = evaluation;
  return execute_sync(scope->interp, &options);
}

typedef struct async_child_job {
  qa_qvm_interpreter *interp;
  const execute_options *options;
  int32_t result;
} async_child_job;

static void async_child_body(void *ctx) {
  async_child_job *job = ctx;
  if (job->options->validate_parent != NULL) run_validate(job->options->validate_parent);
  if (job->options->validate != NULL) job->options->validate(job->options->validate_ctx);
  evaluation_local none;
  memset(&none, 0, sizeof none);
  job->result = execute(job->interp, job->options, &none);
}

/* TS runAsync. A child completes before this returns; a sync frame cannot start
 * one, which TS reports when the hook returns the promise; and a failed child
 * fails its parent even if ignored (scope.failure). */
static int32_t run_async_child(host_scope *scope, execute_options *options) {
  host_available(scope);
  check_cancellation(scope->interp, scope->function_scope);
  if (!scope->frame->asynchronous) qa_range_error("Synchronous QVM call received an asynchronous syscall");
  async_child_job job = {scope->interp, options, 0};
  char message[THROWN_CAPACITY];
  if (qa_range_error_catch(async_child_body, &job, message, sizeof message)) return job.result;
  thrown error;
  capture_thrown(scope->interp, message, &error);
  scope->failure = error;
  rethrow(scope->interp, &error);
}

static double host_invoke_async_validated(void *ctx, const double *args, size_t arg_count,
                                          double instruction_index, void (*validate)(void *ctx),
                                          void *validate_ctx) {
  host_scope *scope = ctx;
  execute_options options;
  memset(&options, 0, sizeof options);
  options.args = args;
  options.arg_count = arg_count;
  options.instruction_index = invocation_index(instruction_index);
  options.function_scope = scope->function_scope;
  options.asynchronous = true;
  options.validate_parent = scope->frame;
  options.validate = validate;
  options.validate_ctx = validate_ctx;
  return run_async_child(scope, &options);
}

static double host_invoke_async(void *ctx, const double *args, size_t arg_count, bool has_index,
                                double instruction_index) {
  return host_invoke_async_validated(ctx, args, arg_count, has_index ? instruction_index : 0, NULL, NULL);
}

typedef struct cancel_job {
  qa_qvm_interpreter *interp;
  qa_qvm_cancellation_scope capability;
  source_call *function_scope;
} cancel_job;

static void cancel_body(void *ctx) {
  cancel_job *job = ctx;
  cancel_function(job->interp, job->capability, job->function_scope);
}

static void host_cancel(void *ctx, qa_qvm_cancellation_scope capability) {
  host_scope *scope = ctx;
  cancel_job job = {scope->interp, capability, scope->function_scope};
  host_control(scope, cancel_body, &job);
}

/* The TS `invokeAsync` of an effect scope: scope.control(() => { throw ... }). */
typedef struct refusal_job { const char *message; } refusal_job;
static void refusal_body(void *ctx) { qa_range_error("%s", ((refusal_job *)ctx)->message); }
static double refuse_async(host_scope *scope, const char *message) {
  refusal_job job = {message};
  host_control(scope, refusal_body, &job);
  return 0;
}
static double refuse_store(void *ctx, const double *args, size_t count, bool has, double index) {
  (void)args; (void)count; (void)has; (void)index;
  return refuse_async(ctx, "QVM store effects cannot start asynchronous calls");
}
static double refuse_decision(void *ctx, const double *args, size_t count, bool has, double index) {
  (void)args; (void)count; (void)has; (void)index;
  return refuse_async(ctx, "QVM decision effects cannot start asynchronous calls");
}
static double refuse_invocation(void *ctx, const double *args, size_t count, bool has, double index) {
  (void)args; (void)count; (void)has; (void)index;
  return refuse_async(ctx, "QVM invocation effects cannot start asynchronous calls");
}
static double refuse_store_validated(void *ctx, const double *args, size_t count, double index,
                                     void (*validate)(void *), void *validate_ctx) {
  (void)validate; (void)validate_ctx;
  return refuse_store(ctx, args, count, true, index);
}
static double refuse_decision_validated(void *ctx, const double *args, size_t count, double index,
                                        void (*validate)(void *), void *validate_ctx) {
  (void)validate; (void)validate_ctx;
  return refuse_decision(ctx, args, count, true, index);
}
static double refuse_invocation_validated(void *ctx, const double *args, size_t count, double index,
                                          void (*validate)(void *), void *validate_ctx) {
  (void)validate; (void)validate_ctx;
  return refuse_invocation(ctx, args, count, true, index);
}

static void init_effect_scope(qa_qvm_syscall *out, qa_qvm_interpreter *interp, host_scope *scope) {
  memset(out, 0, sizeof *out);
  out->memory = interp->memory;
  out->memory_length = interp->memory_length;
  out->guest = &interp->address_space;
  out->ctx = scope;
  out->invoke = host_invoke;
  out->cancel_function = host_cancel;
}

static void run_store_effect(qa_qvm_interpreter *interp, qa_qvm_syscall *effect_scope,
                             void (*perform)(void *), void *perform_ctx) {
  if (!interp->has_store_effect) {
    perform(perform_ctx);
    return;
  }
  interp->store_effect.call(interp->store_effect.ctx, effect_scope, perform, perform_ctx);
}

typedef struct publish_job {
  qa_qvm_interpreter *interp;
  qa_qvm_perform_fn perform;
  void *perform_ctx;
} publish_job;

static double publish_perform(void *ctx, host_scope *scope) {
  publish_job *job = ctx;
  qa_qvm_syscall effect;
  init_effect_scope(&effect, job->interp, scope);
  effect.invoke_async = refuse_store;
  effect.invoke_async_validated = refuse_store_validated;
  run_store_effect(job->interp, &effect, job->perform, job->perform_ctx);
  return 0;
}

/* TS publishEffect: QvmMemory's effect, around after-publication callbacks. */
static void publish_effect(void *ctx, qa_qvm_perform_fn perform, void *perform_ctx) {
  qa_qvm_interpreter *interp = ctx;
  invocation *frame = interp->active;
  if (frame == NULL) {
    perform(perform_ctx);
    return;
  }
  publish_job job = {interp, perform, perform_ctx};
  (void)host_call_stack(interp, frame, publish_perform, &job, frame->function_scope, frame->sp - 4);
}

/* ----- regions and evaluation qualification ----- */

static int64_t to_region_word(double value) {
  return is_safe_integer(value) ? (int64_t)value : INT64_MIN;
}

static int64_t qualify_region(qa_qvm_interpreter *interp, int64_t owner, double entry, double join) {
  return qa_qvm_qualify_region(interp->instructions, interp->instruction_count, owner,
                               to_region_word(entry), to_region_word(join));
}

static int64_t qualify_evaluation(qa_qvm_interpreter *interp, int64_t owner,
                                  const qa_qvm_region_evaluation *region, const char *access) {
  return qa_qvm_qualify_region_evaluation(interp->instructions, interp->instruction_count, owner,
                                          region, access);
}

/* ----- QvmFunctionCall ----- */

typedef struct intercept_state {
  qa_qvm_interpreter *interp;
  invocation *frame;
  source_call *source;
  int64_t instruction_index;
  bool proceeded;
  thrown failure; /* a failed original cannot be swallowed by its hook */
} intercept_state;

typedef struct scope_open_job {
  intercept_state *state;
  qa_qvm_cancellation_scope result;
} scope_open_job;

static void cancellation_scope_body(void *ctx) {
  scope_open_job *job = ctx;
  intercept_state *state = job->state;
  source_call *source = state->source;
  if (state->proceeded || source->cancellation_open)
    qa_range_error("QVM cancellation scope must open once before proceeding");
  qa_qvm_interpreter *interp = state->interp;
  source->cancellation_open = true;
  source->cancellation_serial = interp->next_serial++;
  source->next_scope = interp->open_scopes;
  interp->open_scopes = source;
  job->result = make_token(interp, source->cancellation_serial);
}

static qa_qvm_cancellation_scope intercept_cancellation_scope(void *ctx) {
  host_scope *scope = ctx;
  scope_open_job job = {scope->extra, {NULL}};
  host_control(scope, cancellation_scope_body, &job);
  return job.result;
}

typedef struct branches_job {
  intercept_state *state;
  const qa_qvm_branch_binding *bindings;
  size_t count;
} branches_job;

static void branches_body(void *ctx) {
  branches_job *job = ctx;
  intercept_state *state = job->state;
  qa_qvm_interpreter *interp = state->interp;
  source_call *source = state->source;
  if (state->proceeded || source->branches_bound)
    qa_range_error("QVM branches must bind once before proceeding");
  int64_t end = state->instruction_index + 1;
  while (end < (int64_t)interp->instruction_count &&
         code_word(interp, target_pc(interp, (double)end)) != QA_QVM_OP_ENTER)
    end++;
  size_t count = job->count;
  free(source->branch_pcs);
  free(source->branch_bindings);
  source->branch_pcs = xmalloc(count * sizeof(int64_t));
  source->branch_bindings = xmalloc(count * sizeof(qa_qvm_branch_binding));
  for (size_t i = 0; i < count; i++) {
    double index = job->bindings[i].instruction_index;
    if (!is_safe_integer(index) || index <= (double)state->instruction_index || index >= (double)end)
      qa_range_error("QVM branch is outside its owning function");
    int64_t pc = target_pc(interp, index);
    int32_t opcode = code_word(interp, pc);
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) duplicate = duplicate || source->branch_pcs[j] == pc;
    if (opcode < QA_QVM_OP_EQ || opcode > QA_QVM_OP_GEF || duplicate)
      qa_range_error("QVM branch requires a distinct original conditional instruction");
    source->branch_pcs[i] = pc;
    source->branch_bindings[i] = job->bindings[i];
  }
  source->branch_count = count;
  source->branches_bound = true;
}

static void intercept_branches(void *ctx, const qa_qvm_branch_binding *bindings, size_t count) {
  host_scope *scope = ctx;
  branches_job job = {scope->extra, bindings, count};
  host_control(scope, branches_body, &job);
}

typedef struct regions_job {
  intercept_state *state;
  const qa_qvm_region_binding *bindings;
  size_t count;
  size_t *order;
} regions_job;

static void regions_body(void *ctx) {
  regions_job *job = ctx;
  intercept_state *state = job->state;
  qa_qvm_interpreter *interp = state->interp;
  source_call *source = state->source;
  if (state->proceeded || source->regions != NULL)
    qa_range_error("QVM regions must bind once before proceeding");
  size_t count = job->count;
  source_region *regions = calloc(count == 0 ? 1 : count, sizeof(source_region));
  if (regions == NULL) qa_range_error("QVM memory allocation failed");
  /* Held by the call until bound, so a failed qualification cannot leak it. */
  source->regions = regions;
  source->region_count = 0;
  size_t *order = xmalloc(count * sizeof *order);
  job->order = order;
  for (size_t i = 0; i < count; i++) order[i] = i;
  for (size_t i = 1; i < count; i++) {
    size_t key = order[i], j = i;
    while (j > 0 && job->bindings[order[j - 1]].entry > job->bindings[key].entry) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = key;
  }
  double previous = (double)state->instruction_index;
  for (size_t n = 0; n < count; n++) {
    const qa_qvm_region_binding *binding = &job->bindings[order[n]];
    if (binding->entry < previous) qa_range_error("QVM original regions overlap");
    int64_t frame_size = qualify_region(interp, state->instruction_index, binding->entry, binding->join);
    regions[n].binding = *binding;
    regions[n].entry_pc = target_pc(interp, binding->entry);
    regions[n].join_pc = target_pc(interp, binding->join);
    regions[n].frame_size = frame_size;
    regions[n].stack = source->stack - frame_size;
    regions[n].state = 0;
    previous = binding->join;
  }
  source->region_count = count;
}

static void regions_with_cleanup(void *ctx) {
  regions_job *job = ctx;
  char message[THROWN_CAPACITY];
  bool ok = qa_range_error_catch(regions_body, job, message, sizeof message);
  free(job->order);
  if (!ok) {
    /* A failed binding leaves none bound, as TS assigns only at the end. */
    free(job->state->source->regions);
    job->state->source->regions = NULL;
    job->state->source->region_count = 0;
    qa_range_error("%s", message);
  }
}

static void intercept_regions(void *ctx, const qa_qvm_region_binding *bindings, size_t count) {
  host_scope *scope = ctx;
  regions_job job = {scope->extra, bindings, count, NULL};
  host_control(scope, regions_with_cleanup, &job);
}

static void begin_body(intercept_state *state) {
  if (state->proceeded) qa_range_error("QVM function continuation can only run once");
  state->proceeded = true;
}

typedef struct proceed_job {
  intercept_state *state;
  const qa_qvmx_region_evaluation *region;
  const double *inputs;
  size_t input_count;
  int32_t result;
} proceed_job;

static void init_source_options(execute_options *options, intercept_state *state) {
  static const double unused_args[10] = {0};
  memset(options, 0, sizeof *options);
  options->args = unused_args;
  options->arg_count = 10;
  options->instruction_index = state->instruction_index;
  options->source = state->source;
}

static void proceed_body(void *ctx) {
  proceed_job *job = ctx;
  intercept_state *state = job->state;
  begin_body(state);
  source_call *source = state->source;
  if (job->region != NULL) {
    int64_t frame_size = qualify_evaluation(state->interp, state->instruction_index, job->region, "source");
    if (job->input_count != job->region->input_count)
      qa_range_error("QVM region live-ins differ from its qualified frame");
    for (size_t i = 0; i < job->input_count; i++) (void)signed_word(job->inputs[i]);
    source->has_evaluation = true;
    source->eval_region = *job->region;
    source->eval_region_inputs = xmalloc(job->region->input_count * sizeof(int64_t));
    memcpy(source->eval_region_inputs, job->region->inputs, job->region->input_count * sizeof(int64_t));
    source->eval_region.inputs = source->eval_region_inputs;
    source->eval_inputs = xmalloc(job->input_count * sizeof(double));
    memcpy(source->eval_inputs, job->inputs, job->input_count * sizeof(double));
    source->eval_input_count = job->input_count;
    source->eval_frame_size = frame_size;
  }
  execute_options options;
  init_source_options(&options, state);
  evaluation_local none;
  memset(&none, 0, sizeof none);
  job->result = execute(state->interp, &options, &none);
}

/* TS scope.run wrapped in the failure-recording try of proceed/evaluateRegion. */
static double run_proceed(host_scope *scope, proceed_job *job) {
  host_available(scope);
  check_cancellation(scope->interp, scope->function_scope);
  char message[THROWN_CAPACITY];
  if (qa_range_error_catch(proceed_body, job, message, sizeof message)) return job->result;
  intercept_state *state = scope->extra;
  thrown error;
  capture_thrown(scope->interp, message, &error);
  state->failure = error;
  rethrow(scope->interp, &error);
}

static double intercept_evaluate_region(void *ctx, qa_qvmx_region_evaluation region,
                                        const double *inputs, size_t input_count) {
  host_scope *scope = ctx;
  proceed_job job = {scope->extra, &region, inputs, input_count, 0};
  return run_proceed(scope, &job);
}

static double intercept_proceed(void *ctx) {
  host_scope *scope = ctx;
  proceed_job job = {scope->extra, NULL, NULL, 0, 0};
  return run_proceed(scope, &job);
}

static double intercept_proceed_async(void *ctx) {
  host_scope *scope = ctx;
  intercept_state *state = scope->extra;
  /* TS begin() runs inside runAsync's operation, after its availability checks. */
  host_available(scope);
  check_cancellation(scope->interp, scope->function_scope);
  if (!scope->frame->asynchronous) qa_range_error("Synchronous QVM call received an asynchronous syscall");
  begin_body(state);
  execute_options options;
  init_source_options(&options, state);
  options.asynchronous = true;
  options.validate_parent = scope->frame;
  return run_async_child(scope, &options);
}

typedef struct effect_job {
  host_scope *scope;
  intercept_state *state;
  void (*perform)(void *);
  void *perform_ctx;
} effect_job;

static void effect_body(void *ctx) {
  effect_job *job = ctx;
  check_cancellation(job->state->interp, job->state->source);
  qa_qvm_syscall effect;
  init_effect_scope(&effect, job->state->interp, job->scope);
  effect.invoke_async = refuse_invocation;
  effect.invoke_async_validated = refuse_invocation_validated;
  run_store_effect(job->state->interp, &effect, job->perform, job->perform_ctx);
}

static void intercept_effect(void *ctx, void (*perform)(void *), void *perform_ctx) {
  host_scope *scope = ctx;
  effect_job job = {scope, scope->extra, perform, perform_ctx};
  host_control(scope, effect_body, &job);
}

typedef struct hook_job {
  intercept_state *state;
  const qa_qvm_function_hook *hook;
  int64_t caller_instruction;
  int64_t bytes;
} hook_job;

static double hook_perform(void *ctx, host_scope *scope) {
  hook_job *job = ctx;
  intercept_state *state = job->state;
  qa_qvm_interpreter *interp = state->interp;
  int64_t sp = state->source->stack;
  scope->extra = state;
  qa_qvm_function_call call;
  memset(&call, 0, sizeof call);
  call.instruction_index = (double)state->instruction_index;
  call.has_caller_instruction = job->caller_instruction >= 0;
  call.caller_instruction = (double)job->caller_instruction;
  call.execution = state->frame->asynchronous ? "asynchronous" : "synchronous";
  call.words.bytes = interp->memory + sp + 8;
  call.words.length = (size_t)job->bytes;
  call.memory = interp->memory;
  call.memory_length = interp->memory_length;
  call.guest = &interp->address_space;
  call.ctx = scope;
  call.invoke = host_invoke;
  call.invoke_async = host_invoke_async;
  call.invoke_async_validated = host_invoke_async_validated;
  call.cancel_function = host_cancel;
  call.cancellation_scope = intercept_cancellation_scope;
  call.branches = intercept_branches;
  call.regions = intercept_regions;
  call.evaluate_region = intercept_evaluate_region;
  call.effect = intercept_effect;
  call.proceed = intercept_proceed;
  call.proceed_async = intercept_proceed_async;
  double value;
  if (job->hook != NULL) value = job->hook->call(job->hook->ctx, &call);
  else value = state->frame->asynchronous ? intercept_proceed_async(scope) : intercept_proceed(scope);
  if (state->failure.set) rethrow(interp, &state->failure);
  return value;
}

typedef struct observation_host {
  host_scope *scope;
  int64_t sp, bytes;
  bool open;
} observation_host;

static double observation_argument(void *ctx, double index) {
  observation_host *o = ctx;
  if (!o->open) qa_range_error("QVM function observation has ended");
  if (!is_safe_integer(index) || index < 0 || index >= (double)o->bytes / 4)
    qa_range_error("QVM argument index outside source call");
  return raw_word(o->scope->interp, o->sp + 8 + (int64_t)index * 4);
}

static double observation_invoke(void *ctx, const double *args, size_t arg_count, bool has_index,
                                 double instruction_index,
                                 const qa_qvm_read_only_evaluation *evaluation) {
  observation_host *o = ctx;
  return host_invoke(o->scope, args, arg_count, has_index, instruction_index, evaluation);
}
static double observation_invoke_async(void *ctx, const double *args, size_t arg_count, bool has_index,
                                       double instruction_index) {
  observation_host *o = ctx;
  return host_invoke_async(o->scope, args, arg_count, has_index, instruction_index);
}
static double observation_invoke_async_validated(void *ctx, const double *args, size_t arg_count,
                                                 double instruction_index, void (*validate)(void *),
                                                 void *validate_ctx) {
  observation_host *o = ctx;
  return host_invoke_async_validated(o->scope, args, arg_count, instruction_index, validate, validate_ctx);
}
static void observation_cancel(void *ctx, qa_qvm_cancellation_scope capability) {
  observation_host *o = ctx;
  host_cancel(o->scope, capability);
}

typedef struct observe_job {
  observer_entry *entry;
  int64_t instruction_index, sp, bytes;
} observe_job;

static double observe_perform(void *ctx, host_scope *scope) {
  observe_job *job = ctx;
  observation_host host = {scope, job->sp, job->bytes, true};
  qa_qvm_function_observation observation;
  memset(&observation, 0, sizeof observation);
  observation.instruction_index = (double)job->instruction_index;
  observation.ctx = &host;
  observation.invoke = observation_invoke;
  observation.invoke_async = observation_invoke_async;
  observation.invoke_async_validated = observation_invoke_async_validated;
  observation.cancel_function = observation_cancel;
  observation.argument = observation_argument;
  observation.words.bytes = scope->interp->memory + job->sp + 8;
  observation.words.length = (size_t)job->bytes;
  job->entry->observer.observe(job->entry->observer.ctx, &observation);
  host.open = false;
  return 0;
}

typedef struct intercept_job {
  intercept_state *state;
  observer_entry **observers;
  size_t observer_count;
  const qa_qvm_function_hook *hook;
  int64_t caller_instruction, bytes;
  int32_t result;
} intercept_job;

/* TS observeNext(0): observers share this entry, then the hook or the body. */
static void intercept_body(void *ctx) {
  intercept_job *job = ctx;
  intercept_state *state = job->state;
  for (size_t i = 0; i < job->observer_count; i++) {
    if (!job->observers[i]->active) continue;
    observe_job observe = {job->observers[i], state->instruction_index, state->source->stack, job->bytes};
    (void)host_call(state->interp, state->frame, observe_perform, &observe, state->source);
  }
  hook_job hook = {state, job->hook, job->caller_instruction, job->bytes};
  job->result = host_call(state->interp, state->frame, hook_perform, &hook, state->source);
}

static void source_cleanup(qa_qvm_interpreter *interp, source_call *source) {
  source->active = false;
  if (source->cancellation_open) {
    source_call **link = &interp->open_scopes;
    while (*link != NULL && *link != source) link = &(*link)->next_scope;
    if (*link == source) *link = source->next_scope;
  }
  free(source->branch_pcs);
  free(source->branch_bindings);
  free(source->regions);
  free(source->eval_region_inputs);
  free(source->eval_inputs);
  source->branch_pcs = NULL;
  source->branch_bindings = NULL;
  source->regions = NULL;
  source->eval_region_inputs = NULL;
  source->eval_inputs = NULL;
}

typedef struct complete_check {
  qa_qvm_interpreter *interp;
  source_call *source;
} complete_check;

static void complete_check_body(void *ctx) {
  complete_check *check = ctx;
  check_cancellation(check->interp, check->source);
}

/* TS intercept. complete and failed end the source call; only its own requested
 * signal becomes the zero result. */
static int32_t intercept(qa_qvm_interpreter *interp, invocation *frame, int64_t sp,
                         int64_t return_pc, int64_t instruction_index,
                         const qa_qvm_function_hook *hook, bool with_observers,
                         int64_t argument_bytes_override) {
  source_call source;
  memset(&source, 0, sizeof source);
  source.stack = sp;
  source.return_pc = return_pc;
  source.ops = frame->ops;
  source.operand_depth = frame->ops->depth;
  source.parent = frame->function_scope;
  source.active = true;
  int64_t caller_instruction = return_pc < 0 ? -1 : source_instruction(interp, return_pc - 1);
  int64_t bytes = argument_bytes_override;
  if (bytes < 0) {
    if (caller_instruction < 0) qa_range_error("QVM host call lost its argument frame");
    bytes = source_argument_bytes(interp, caller_instruction);
  }
  interp_range(interp, sp + 8, bytes);
  /* The observer list seen at this OP_CALL is fixed; `active` stays live. */
  size_t observer_count = 0;
  observer_entry **observers = NULL;
  if (with_observers) {
    observers = xmalloc(interp->observer_count * sizeof *observers);
    for (size_t i = 0; i < interp->observer_count; i++)
      if (interp->observers[i]->index == instruction_index) observers[observer_count++] = interp->observers[i];
  }
  intercept_state state;
  state.interp = interp;
  state.frame = frame;
  state.source = &source;
  state.instruction_index = instruction_index;
  state.proceeded = false;
  state.failure.set = false;
  state.failure.signal = NULL;
  intercept_job job = {&state, observers, observer_count, hook, caller_instruction, bytes, 0};
  char message[THROWN_CAPACITY];
  bool ok = qa_range_error_catch(intercept_body, &job, message, sizeof message);
  free(observers);
  thrown error;
  if (ok) {
    /* complete(): check, then end the call either way. */
    char check_message[THROWN_CAPACITY];
    complete_check check = {interp, &source};
    if (qa_range_error_catch(complete_check_body, &check, check_message, sizeof check_message)) {
      source_cleanup(interp, &source);
      return job.result;
    }
    capture_thrown(interp, check_message, &error);
    source_cleanup(interp, &source);
    rethrow(interp, &error);
  }
  capture_thrown(interp, message, &error);
  /* failed(). */
  if (source.cancellation_open && source.cancellation_failure.set) {
    thrown failure = source.cancellation_failure;
    source_cleanup(interp, &source);
    rethrow(interp, &failure);
  }
  if (source.cancellation_open && source.cancellation_requested && error.signal == &source) {
    source_cleanup(interp, &source);
    interp->current_signal = NULL;
    operands_truncate(source.ops, source.operand_depth);
    write_word(interp, source.stack, (int32_t)source.return_pc);
    return 0;
  }
  source_cleanup(interp, &source);
  rethrow(interp, &error);
}

/* ----- branch decisions and regions ----- */

typedef struct decision_job {
  qa_qvm_interpreter *interp;
  qa_qvm_branch_binding *binding;
  bool taken;
  bool chosen;
  qa_qvm_syscall *control;
} decision_job;

static void decision_effect(void *ctx) {
  decision_job *job = ctx;
  job->chosen = job->binding->decide(job->binding->ctx, job->taken, job->control);
}

static double decision_perform(void *ctx, host_scope *scope) {
  decision_job *job = ctx;
  /* TS hands decide only { cancelFunction }; the effect scope also reenters. */
  qa_qvm_syscall control;
  init_effect_scope(&control, job->interp, scope);
  control.invoke_async = refuse_decision;
  control.invoke_async_validated = refuse_decision_validated;
  job->control = &control;
  run_store_effect(job->interp, &control, decision_effect, job);
  return job->chosen ? 1 : 0;
}

static int32_t decision(qa_qvm_interpreter *interp, invocation *frame, source_call *owner,
                        qa_qvm_branch_binding *binding, bool taken) {
  decision_job job = {interp, binding, taken, false, NULL};
  return host_call_stack(interp, frame, decision_perform, &job, owner, frame->sp - 4);
}

typedef struct region_host {
  host_scope *scope;
  source_call *owner;
  source_region *region;
  bool open;
} region_host;

typedef struct region_op {
  region_host *host;
  const double *args;
  size_t arg_count;
  bool has_index;
  double instruction_index;
  double offset;
  qa_qvm_cancellation_scope capability;
  double result;
  int kind; /* 0 invoke, 1 cancel, 2 local word */
} region_op;

/* TS current(): every control operation is scope.control plus this expiry check. */
static void region_op_body(void *ctx) {
  region_op *op = ctx;
  region_host *r = op->host;
  host_scope *scope = r->scope;
  if (!r->open || !r->owner->active || scope->interp->active != scope->frame ||
      scope->frame->sp != r->region->stack)
    qa_range_error("QVM region callback has expired");
  switch (op->kind) {
    case 0:
      op->result = host_invoke(scope, op->args, op->arg_count, op->has_index, op->instruction_index, NULL);
      break;
    case 1:
      host_cancel(scope, op->capability);
      break;
    default:
      if (!is_safe_integer(op->offset) || op->offset < 8 || (int64_t)op->offset % 4 != 0 ||
          op->offset + 4 > (double)r->region->frame_size)
        qa_range_error("QVM region local is outside its original frame");
      op->result = read_word(scope->interp, r->region->stack + (int64_t)op->offset);
      break;
  }
}

static double region_invoke(void *ctx, const double *args, size_t arg_count, bool has_index,
                            double instruction_index,
                            const qa_qvm_read_only_evaluation *evaluation) {
  (void)evaluation; /* QvmRegionControl.invoke has no evaluation parameter. */
  region_host *r = ctx;
  region_op op = {r, args, arg_count, has_index, instruction_index, 0, {NULL}, 0, 0};
  host_control(r->scope, region_op_body, &op);
  return op.result;
}

static void region_cancel(void *ctx, qa_qvm_cancellation_scope capability) {
  region_host *r = ctx;
  region_op op = {r, NULL, 0, false, 0, 0, capability, 0, 1};
  host_control(r->scope, region_op_body, &op);
}

static double region_local_word(void *ctx, double offset) {
  region_host *r = ctx;
  region_op op = {r, NULL, 0, false, 0, offset, {NULL}, 0, 2};
  host_control(r->scope, region_op_body, &op);
  return op.result;
}

static double region_refuse(void *ctx, const double *args, size_t count, bool has, double index) {
  (void)args; (void)count; (void)has; (void)index;
  region_host *r = ctx;
  return refuse_async(r->scope, "QVM region effects cannot suspend");
}
static double region_refuse_validated(void *ctx, const double *args, size_t count, double index,
                                      void (*validate)(void *), void *validate_ctx) {
  (void)validate; (void)validate_ctx;
  return region_refuse(ctx, args, count, true, index);
}

typedef struct region_job {
  qa_qvm_interpreter *interp;
  source_call *owner;
  source_region *region;
  bool complete_phase;
  double result;
  qa_qvm_region_control *control;
} region_job;

static void region_effect(void *ctx) {
  region_job *job = ctx;
  qa_qvm_region_binding *binding = &job->region->binding;
  if (job->complete_phase) {
    if (binding->completed != NULL) binding->completed(binding->ctx, job->control);
    return;
  }
  const char *choice = binding->run(binding->ctx, job->control);
  if (choice == NULL || (strcmp(choice, "execute") != 0 && strcmp(choice, "skip") != 0))
    qa_range_error("QVM region requires one execution decision");
  job->result = strcmp(choice, "execute") == 0 ? 1 : 0;
}

static double region_perform(void *ctx, host_scope *scope) {
  region_job *job = ctx;
  region_host host = {scope, job->owner, job->region, true};
  qa_qvm_region_control control;
  memset(&control, 0, sizeof control);
  control.ctx = &host;
  control.invoke = region_invoke;
  control.cancel_function = region_cancel;
  control.local_word = region_local_word;
  job->control = &control;
  qa_qvm_syscall effect;
  memset(&effect, 0, sizeof effect);
  effect.memory = job->interp->memory;
  effect.memory_length = job->interp->memory_length;
  effect.guest = &job->interp->address_space;
  effect.ctx = &host;
  effect.invoke = region_invoke;
  effect.cancel_function = region_cancel;
  effect.invoke_async = region_refuse;
  effect.invoke_async_validated = region_refuse_validated;
  run_store_effect(job->interp, &effect, region_effect, job);
  host.open = false;
  return job->result;
}

static int32_t run_region(qa_qvm_interpreter *interp, invocation *frame, source_call *owner,
                          source_region *region, bool complete_phase) {
  if (!owner->active || frame->sp != region->stack || frame->ops->depth != owner->operand_depth)
    qa_range_error("QVM region lost its original frame or operand boundary");
  region_job job = {interp, owner, region, complete_phase, 0, NULL};
  return host_call_stack(interp, frame, region_perform, &job, owner, region->stack - 4);
}

static int64_t evaluation_stack_start(qa_qvm_interpreter *interp,
                                      const qa_qvm_evaluation_stack *stack) {
  if (stack == NULL) return (interp->source_data_end + 3) / 4 * 4;
  if (!is_safe_integer(stack->start) || !is_safe_integer(stack->end) ||
      (int64_t)stack->start % 4 != 0 || (int64_t)stack->end % 4 != 0 ||
      stack->start < (double)interp->memory_initialized_end || stack->start >= stack->end ||
      stack->end > (double)interp->memory_length || (double)interp->program_stack > stack->end)
    qa_range_error("QVM evaluation stack is outside its declared source reservation or active caller");
  return (int64_t)stack->start;
}

/* TS debugString(): `${indent()}${operands.count}` via registration print. */
static void debug_print(qa_qvm_interpreter *interp, int operand_count, const char *tail) {
  if (interp->registration == NULL) return;
  if (interp->call_level < 0) qa_range_error("VM indentation requires a nonnegative call level");
  int64_t count = interp->call_level < 20 ? interp->call_level : 20;
  char indent[41];
  memset(indent, ' ', (size_t)(2 * count));
  indent[2 * count] = '\0';
  char line[1200];
  snprintf(line, sizeof line, "%s%d%s", indent, operand_count, tail);
  qa_qvm_vm_registration_print(interp->registration, line);
}

typedef struct trap_job {
  qa_qvm_interpreter *interp;
  int64_t sp;
} trap_job;

static double trap_perform(void *ctx, host_scope *scope) {
  trap_job *job = ctx;
  qa_qvm_interpreter *interp = job->interp;
  qa_qvm_syscall call;
  memset(&call, 0, sizeof call);
  call.words.bytes = interp->memory + job->sp + 4;
  call.words.length = interp->memory_length - (size_t)(job->sp + 4);
  call.memory = interp->memory;
  call.memory_length = interp->memory_length;
  call.guest = &interp->address_space;
  call.ctx = scope;
  call.invoke = host_invoke;
  call.invoke_async = host_invoke_async;
  call.invoke_async_validated = host_invoke_async_validated;
  call.cancel_function = host_cancel;
  return interp->system_call.call(interp->system_call.ctx, &call);
}

static void push_return(invocation *frame, int64_t sp, int64_t return_pc) {
  if (frame->return_count + 2 > frame->return_capacity) {
    size_t capacity = frame->return_capacity == 0 ? 32 : frame->return_capacity * 2;
    int64_t *grown = realloc(frame->returns, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    frame->returns = grown;
    frame->return_capacity = capacity;
  }
  frame->returns[frame->return_count++] = sp;
  frame->returns[frame->return_count++] = return_pc;
}

typedef struct execute_job {
  qa_qvm_interpreter *interp;
  const execute_options *options;
  const evaluation_local *evaluation;
  invocation *frame;
  bool debug;
  qa_qvm_execution_profile profile;
  int64_t evaluation_floor;
  int64_t evaluation_stack;
  size_t argument_words;
  int32_t result;
} execute_job;

static hook_entry *find_hook(qa_qvm_interpreter *interp, int64_t index) {
  for (size_t i = 0; i < interp->hook_count; i++)
    if (interp->hooks[i]->index == index) return interp->hooks[i];
  return NULL;
}

static bool has_observer(const qa_qvm_interpreter *interp, int64_t index) {
  for (size_t i = 0; i < interp->observer_count; i++)
    if (interp->observers[i]->index == index) return true;
  return false;
}

static void execute_body(void *ctx) {
  execute_job *job = ctx;
  qa_qvm_interpreter *interp = job->interp;
  const execute_options *options = job->options;
  invocation *frame = job->frame;
  source_call *source = options->source;
  int64_t instruction_index = options->instruction_index;
  bool debug = job->debug;
  int32_t trace = debug ? job->profile.trace : 0;
  operands *ops = frame->ops;

  evaluation_local evaluation = *job->evaluation;
  if (!evaluation.has && source != NULL && source->has_evaluation) {
    evaluation.has = true;
    evaluation.region = source->eval_region;
    evaluation.inputs = source->eval_inputs;
    evaluation.input_count = source->eval_input_count;
    evaluation.frame_size = source->eval_frame_size;
  }

  if (source == NULL) {
    int64_t sp = frame->sp;
    write_word(interp, sp, -1);
    write_word(interp, sp + 4, 0);
    for (size_t i = 0; i < job->argument_words; i++)
      write_word(interp, sp + 8 + (int64_t)i * 4, i < options->arg_count ? (int32_t)options->args[i] : 0);
    interp->call_level = 0;
    if (interp->registration != NULL) qa_qvm_vm_registration_debug(interp->registration, 0);
    hook_entry *binding = find_hook(interp, instruction_index);
    if (binding != NULL && binding->invocations && options->read_only == NULL && interp->counter == NULL) {
      interp->program_stack = sp - 4;
      qa_qvm_function_hook hook = binding->hook;
      job->result = intercept(interp, frame, sp, -1, instruction_index, &hook, false,
                              (int64_t)job->argument_words * 4);
      return;
    }
  }
  int64_t pc = target_pc(interp, (double)instruction_index);
  bool evaluation_started = false;
  qa_qvm_function_symbol profile_symbol = {0, NULL, NULL, 0};
  if (debug) profile_symbol = qa_qvm_symbols_value_to_function_symbol(&interp->symbols, 0);
  /* frame->sp stays current so reentrant paths read the live stack (TS frame.stack()). */
#define sp (frame->sp)
  /* QVM_INSTRUCTION_BUDGET per root invocation: retail frames use a few million; this only stops code that never returns. */
  int64_t executed = 0;
  for (;;) {
    if (++executed > 100000000) qa_common_error(QA_COMMON_ERROR_DROP, "VM instruction budget exceeded");
    counter_scope *counter = interp->counter;
    if (counter != NULL) {
      bool in_range = false;
      for (size_t i = 0; i < counter->function_count; i++)
        in_range = in_range || (pc >= counter->range_start[i] && pc < counter->range_end[i]);
      if (--counter->remaining < 0 || !in_range)
        qa_range_error("QVM counter evaluation escaped its admitted original functions or instruction budget");
    }
    source_call *owner = counter == NULL ? frame->function_scope : NULL;
    if (evaluation.has && sp == job->evaluation_stack - evaluation.frame_size &&
        pc == target_pc(interp, (double)evaluation.region.join)) {
      if (ops->depth != (source != NULL ? source->operand_depth : 0))
        qa_range_error("QVM region evaluation lost its caller operands");
      job->result = !evaluation.region.has_result ? 0 : read_word(interp, sp + evaluation.region.result);
      return;
    }
    if (owner != NULL && owner->regions != NULL) {
      /* TS keys both maps by pc: the last sorted binding owns a duplicate key. */
      source_region *completed = NULL, *entered = NULL;
      for (size_t i = 0; i < owner->region_count; i++) {
        if (owner->regions[i].join_pc == pc) completed = &owner->regions[i];
        if (owner->regions[i].entry_pc == pc) entered = &owner->regions[i];
      }
      if (completed != NULL && sp == completed->stack && completed->state == 1) {
        completed->state = 2;
        (void)run_region(interp, frame, owner, completed, true);
      }
      if (entered != NULL && sp == entered->stack) {
        if (entered->state != 0) qa_range_error("QVM region can execute only once per invocation");
        entered->state = 1;
        if (run_region(interp, frame, owner, entered, false) == 0) {
          entered->state = 2;
          pc = entered->join_pc;
          continue;
        }
      }
    }
    if (debug) {
      if (pc < 0 || pc >= (int64_t)interp->code_length)
        qa_common_error(QA_COMMON_ERROR_DROP, "VM pc out of range");
      if (sp <= (int64_t)interp->memory_length - 0x20000)
        qa_common_error(QA_COMMON_ERROR_DROP, "VM stack overflow");
      if ((sp & 3) != 0) qa_common_error(QA_COMMON_ERROR_DROP, "VM program stack misaligned");
    }
    int32_t opcode = code_word(interp, pc++);
    if (debug) {
      if (trace > 1) {
        if (opcode < 0 || opcode >= (int32_t)(sizeof opcode_names / sizeof opcode_names[0]))
          qa_common_error(QA_COMMON_ERROR_DROP, "Bad VM instruction");
        char tail[64];
        snprintf(tail, sizeof tail, " %s\n", opcode_names[opcode]);
        debug_print(interp, ops->depth, tail);
      }
      qa_qvm_function_symbol_set_profile_count(
          &profile_symbol,
          (int32_t)((uint32_t)qa_qvm_function_symbol_get_profile_count(&profile_symbol) + 1u));
    }
    switch ((qa_qvm_opcode)opcode) {
      case QA_QVM_OP_UNDEF: case QA_QVM_OP_IGNORE:
        if (debug) qa_common_error(QA_COMMON_ERROR_DROP, "Bad VM instruction");
        break;
      case QA_QVM_OP_BREAK:
        if (counter != NULL) qa_range_error("QVM counter evaluation cannot trigger a debug break");
        interp->breaks = (int32_t)((uint32_t)interp->breaks + 1u);
        break;
      case QA_QVM_OP_CONST: operands_push(ops, code_word(interp, pc)); pc += 4; break;
      case QA_QVM_OP_LOCAL:
        operands_push(ops, (int32_t)(uint32_t)(uint64_t)(sp + code_word(interp, pc)));
        pc += 4;
        break;
      case QA_QVM_OP_PUSH: operands_reserve(ops); break;
      case QA_QVM_OP_POP: operands_drop(ops); break;
      case QA_QVM_OP_ENTER: {
        if (debug)
          profile_symbol = qa_qvm_symbols_value_to_function_symbol(&interp->symbols, (int32_t)pc);
        int32_t size = code_word(interp, pc);
        if (job->evaluation_floor >= 0 &&
            (size < 0 || size % 4 != 0 || sp - size < job->evaluation_floor))
          qa_range_error("QVM evaluation stack would overlap source data");
        sp = interp_stack(interp, sp - size);
        pc += 4;
        if (evaluation.has && !evaluation_started &&
            pc - 5 == target_pc(interp, (double)instruction_index)) {
          evaluation_started = true;
          for (size_t i = 0; i < evaluation.region.input_count; i++) {
            if (i >= evaluation.input_count) qa_range_error("Missing QVM region live-in");
            write_word(interp, sp + evaluation.region.inputs[i], (int32_t)evaluation.inputs[i]);
          }
          pc = target_pc(interp, (double)evaluation.region.entry);
        }
        if (debug) {
          write_word(interp, sp + 4, (int32_t)(sp + size));
          if (trace != 0) {
            char symbol[1024], tail[1100];
            qa_qvm_symbols_value_to_symbol(&interp->symbols, (int32_t)(pc - 5), symbol, sizeof symbol);
            snprintf(tail, sizeof tail, "---> %s\n", symbol);
            debug_print(interp, ops->depth, tail);
            if (job->profile.break_function != 0 && pc - 5 == job->profile.break_function)
              interp->breaks = (int32_t)((uint32_t)interp->breaks + 1u);
            interp->call_level++;
          }
        }
        break;
      }
      case QA_QVM_OP_LEAVE: {
        sp = interp_stack(interp, sp + code_word(interp, pc));
        int64_t target;
        if (!interp->compiled) {
          target = read_word(interp, sp);
        } else {
          if (frame->return_count == 0) qa_range_error("QVM function returned with an invalid program stack");
          target = frame->returns[--frame->return_count];
          if (frame->return_count == 0 || frame->returns[--frame->return_count] != sp)
            qa_range_error("QVM function returned with an invalid program stack");
        }
        if (debug) {
          profile_symbol = qa_qvm_symbols_value_to_function_symbol(&interp->symbols, (int32_t)target);
          if (trace != 0) {
            char symbol[1024], tail[1100];
            interp->call_level--;
            qa_qvm_symbols_value_to_symbol(&interp->symbols, (int32_t)target, symbol, sizeof symbol);
            snprintf(tail, sizeof tail, "<--- %s\n", symbol);
            debug_print(interp, ops->depth, tail);
          }
        }
        if (source != NULL && sp == source->stack && (!interp->compiled || frame->return_count == 0)) {
          if (target != source->return_pc || ops->depth != source->operand_depth + 1)
            qa_range_error("QVM function returned with an invalid caller stack");
          job->result = operands_pop(ops);
          return;
        }
        if (target == -1) {
          job->result = operands_result(ops);
          return;
        }
        pc = target;
        break;
      }
      case QA_QVM_OP_CALL: {
        int64_t return_pc = pc;
        write_word(interp, sp, (int32_t)pc);
        int32_t target = operands_pop(ops);
        if (target >= 0) {
          if (counter != NULL) {
            bool admitted = false;
            for (size_t i = 0; i < counter->function_count; i++)
              admitted = admitted || counter->functions[i] == (double)target;
            if (!admitted) qa_range_error("QVM counter evaluation called an undeclared source function");
          }
          qa_qvm_function_hook hook;
          bool has_hook = false, has_observers = false;
          if (counter == NULL) {
            hook_entry *binding = find_hook(interp, target);
            has_observers = has_observer(interp, target);
            if (binding != NULL) {
              hook = binding->hook;
              has_hook = true;
            } else if (interp->resolver != NULL) {
              int32_t first = read_word(interp, sp + 8);
              int64_t bytes = source_argument_bytes(interp, source_instruction(interp, return_pc - 1));
              interp_range(interp, sp + 8, bytes);
              qa_qvmx_view words = {interp->memory + sp + 8, (size_t)bytes};
              has_hook = interp->resolver->resolver.resolve(interp->resolver->resolver.ctx, target, first, &words, &hook);
              if (has_hook && code_word(interp, target_pc(interp, target)) != QA_QVM_OP_ENTER)
                qa_range_error("QVM resolver requires a function entry instruction");
            }
          }
          if (!has_hook && !has_observers) {
            if (interp->compiled) {
              /* QVM_MAX_RETURN_WORDS: return records kept outside guest memory, two words per call. */
              if (frame->return_count >= 2u * 65536u) qa_common_error(QA_COMMON_ERROR_DROP, "VM call depth exceeded");
              push_return(frame, sp, return_pc);
            }
            pc = target_pc(interp, target);
          } else {
            int64_t saved_level = interp->call_level, saved_stack = interp->program_stack;
            interp->program_stack = sp - 4;
            /* The enclosing execute's catch restores this frame on a throw. */
            int32_t value = intercept(interp, frame, sp, return_pc, target, has_hook ? &hook : NULL, has_observers, -1);
            operands_push(ops, value);
            pc = !interp->compiled ? read_word(interp, sp) : return_pc;
            interp->call_level = saved_level;
            interp->program_stack = saved_stack;
          }
        } else {
          if (counter != NULL) qa_range_error("QVM counter evaluation cannot call an engine service");
          if (trace != 0) {
            char tail[64];
            snprintf(tail, sizeof tail, "---> systemcall(%lld)\n", (long long)(-1 - (int64_t)target));
            debug_print(interp, ops->depth, tail);
          }
          int64_t saved_level = interp->call_level;
          interp->program_stack = sp - 4;
          int32_t saved_frame = debug ? read_word(interp, sp + 4) : 0;
          write_word(interp, sp + 4, (int32_t)(-1 - (int64_t)target));
          interp_range(interp, sp + 4, 4);
          trap_job trap = {interp, sp};
          int32_t value = host_call(interp, frame, trap_perform, &trap, frame->function_scope);
          if (debug) write_word(interp, sp + 4, saved_frame);
          operands_push(ops, value);
          pc = !interp->compiled ? read_word(interp, sp) : return_pc;
          interp->call_level = saved_level;
          if (trace != 0) {
            char symbol[1024], tail[1100];
            qa_qvm_symbols_value_to_symbol(&interp->symbols, (int32_t)pc, symbol, sizeof symbol);
            snprintf(tail, sizeof tail, "<--- %s\n", symbol);
            debug_print(interp, ops->depth, tail);
          }
        }
        break;
      }
      case QA_QVM_OP_JUMP: pc = target_pc(interp, operands_pop(ops)); break;
      case QA_QVM_OP_LOAD1: {
        int64_t address = operands_peek(ops) & interp->data_mask;
        int32_t ignored;
        if (counter != NULL) (void)counter_read(interp, address, 1, &ignored);
        operands_set(ops, interp->memory[address]);
        break;
      }
      case QA_QVM_OP_LOAD2: {
        int64_t address = operands_peek(ops) & interp->data_mask;
        interp_range(interp, address, 2);
        int32_t ignored;
        if (counter != NULL) (void)counter_read(interp, address, 2, &ignored);
        operands_set(ops, (int32_t)((uint32_t)interp->memory[address] | ((uint32_t)interp->memory[address + 1] << 8)));
        break;
      }
      case QA_QVM_OP_LOAD4:
        if (debug && (operands_peek(ops) & 3) != 0)
          qa_common_error(QA_COMMON_ERROR_DROP, "OP_LOAD4 misaligned");
        operands_set(ops, read_word(interp, operands_peek(ops) & interp->data_mask));
        break;
      case QA_QVM_OP_STORE1: {
        int32_t value = operands_pop(ops);
        int64_t left4 = operands_pop(ops);
        int64_t address = left4 & interp->data_mask;
        if (counter != NULL) (void)counter_write(interp, address, 1, value);
        guest_store(interp, address, (uint32_t)value, 1);
        break;
      }
      case QA_QVM_OP_STORE2: {
        int32_t value = operands_pop(ops);
        long long left3 = operands_pop(ops);
        int64_t address = left3 & (interp->data_mask & ~1LL);
        interp_range(interp, address, 2);
        if (counter != NULL) (void)counter_write(interp, address, 2, value);
        guest_store(interp, address, (uint32_t)value, 2);
        break;
      }
      case QA_QVM_OP_STORE4: {
        int32_t value = operands_pop(ops);
        long long left2 = operands_pop(ops);
        int64_t address = left2 & (interp->data_mask & ~3LL);
        write_word(interp, address, value);
        break;
      }
      case QA_QVM_OP_ARG: {
        int64_t address = sp + code_word(interp, pc++);
        write_word(interp, address, operands_pop(ops));
        break;
      }
      case QA_QVM_OP_BLOCK_COPY: {
        if (counter != NULL) qa_range_error("QVM counter evaluation cannot copy source memory");
        int32_t source_addr = operands_pop(ops);
        int32_t destination = operands_pop(ops);
        int32_t count = code_word(interp, pc);
        pc += 4;
        if (!interp->compiled) {
          int64_t from = source_addr & interp->data_mask, to = destination & interp->data_mask;
          int64_t source_count = ((from + count) & interp->data_mask) - from;
          int64_t copied = ((to + source_count) & interp->data_mask) - to;
          if (((from | to | copied) & 3) != 0)
            qa_common_error(QA_COMMON_ERROR_DROP, "OP_BLOCK_COPY not dword aligned");
          for (int64_t word = (copied >> 2) - 1; word >= 0; word--)
            write_word(interp, to + word * 4, read_word(interp, from + word * 4));
        } else {
          if (count < 0 || source_addr < 0 || destination < 0 ||
              (int64_t)source_addr + count > interp->data_mask ||
              (int64_t)destination + count > interp->data_mask)
            qa_common_error(QA_COMMON_ERROR_DROP, "OP_BLOCK_COPY out of range");
          /* The range check above is a deliberate safety deviation (id's JIT masks the pointers). Within range the copy
           * matches vm_x86.c: whole words only (count >> 2) and strictly forward, so an overlapping copy smears. */
          const int64_t word_bytes = (int64_t)((count >> 2) << 2);
          if (destination > source_addr && destination < (int64_t)source_addr + word_bytes) {
            for (int64_t offset = 0; offset < word_bytes; offset += 4)
              qa_qvm_memory_copy_bytes(&interp->guest, destination + offset, source_addr + offset, 4);
          } else qa_qvm_memory_copy_bytes(&interp->guest, destination, source_addr, word_bytes);
        }
        break;
      }
      case QA_QVM_OP_BCOM:
        if (interp->compiled) operands_set(ops, ~operands_peek(ops));
        else operands_complement_previous(ops);
        break;
      case QA_QVM_OP_SEX8: case QA_QVM_OP_SEX16: case QA_QVM_OP_NEGI:
      case QA_QVM_OP_NEGF: case QA_QVM_OP_CVIF: case QA_QVM_OP_CVFI:
        operands_set(ops, qa_qvm_evaluate_unary((qa_qvm_opcode)opcode, operands_peek(ops)));
        break;
      case QA_QVM_OP_ADD: case QA_QVM_OP_SUB: case QA_QVM_OP_DIVI: case QA_QVM_OP_DIVU:
      case QA_QVM_OP_MODI: case QA_QVM_OP_MODU: case QA_QVM_OP_MULI: case QA_QVM_OP_MULU:
      case QA_QVM_OP_BAND: case QA_QVM_OP_BOR: case QA_QVM_OP_BXOR:
      case QA_QVM_OP_LSH: case QA_QVM_OP_RSHI: case QA_QVM_OP_RSHU:
      case QA_QVM_OP_ADDF: case QA_QVM_OP_SUBF: case QA_QVM_OP_DIVF: case QA_QVM_OP_MULF: {
        int32_t right = operands_pop(ops);
        operands_set(ops, qa_qvm_evaluate_binary((qa_qvm_opcode)opcode, operands_peek(ops), right));
        break;
      }
      case QA_QVM_OP_EQ: case QA_QVM_OP_NE: case QA_QVM_OP_LTI: case QA_QVM_OP_LEI:
      case QA_QVM_OP_GTI: case QA_QVM_OP_GEI: case QA_QVM_OP_LTU: case QA_QVM_OP_LEU:
      case QA_QVM_OP_GTU: case QA_QVM_OP_GEU: case QA_QVM_OP_EQF: case QA_QVM_OP_NEF:
      case QA_QVM_OP_LTF: case QA_QVM_OP_LEF: case QA_QVM_OP_GTF: case QA_QVM_OP_GEF: {
        int32_t right = operands_pop(ops);
        int32_t left = operands_pop(ops);
        int32_t target = code_word(interp, pc);
        bool taken = qa_qvm_evaluate_branch((qa_qvm_opcode)opcode, left, right);
        qa_qvm_branch_binding *binding = NULL;
        if (counter == NULL && frame->function_scope != NULL && frame->function_scope->branches_bound)
          for (size_t i = 0; i < frame->function_scope->branch_count; i++)
            if (frame->function_scope->branch_pcs[i] == pc - 1)
              binding = &frame->function_scope->branch_bindings[i];
        if (binding == NULL) {
          pc = taken ? target : pc + 4;
        } else {
          source_call *branch_owner = frame->function_scope;
          if (!branch_owner->active) qa_range_error("QVM branch invocation has expired");
          int32_t chosen = decision(interp, frame, branch_owner, binding, taken);
          check_cancellation(interp, branch_owner);
          assert_live(interp);
          run_validate(frame);
          if (!branch_owner->active || interp->active != frame)
            qa_range_error("QVM branch invocation is no longer current");
          pc = chosen != 0 ? target : pc + 4;
        }
        break;
      }
      default:
        /* The release interpreter has no default trap: a return into an operand
         * slot encounters a non-opcode integer as a nop. */
        if (debug) qa_common_error(QA_COMMON_ERROR_DROP, "Bad VM instruction");
        break;
    }
  }
#undef sp
}

static int32_t execute(qa_qvm_interpreter *interp, const execute_options *options,
                       const evaluation_local *read_only_evaluation) {
  source_call *source = options->source;
  int64_t instruction_index = options->instruction_index;
  if (source == NULL) {
    if (instruction_index == 0 ? options->arg_count != 10 : options->arg_count > QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
      qa_range_error(instruction_index == 0 ? "QVM vmMain requires ten public argument words"
                                            : "QVM private call exceeds OP_ARG argument capacity");
    if (instruction_index != 0 &&
        (instruction_index < 0 || instruction_index >= (int64_t)interp->instruction_count ||
         interp->instructions[instruction_index].opcode != QA_QVM_OP_ENTER))
      qa_range_error("QVM private invocation requires an original function entry");
  }
  for (size_t i = 0; i < options->arg_count; i++) (void)signed_word(options->args[i]);
  if (source == NULL && interp->registration != NULL)
    qa_qvm_vm_registration_print_call(interp->registration,
                                      options->arg_count > 0 ? (int64_t)options->args[0] : 0);
  qa_qvm_execution_profile profile = {"release", 0, 0};
  if (interp->registration != NULL)
    profile = qa_qvm_vm_registration_execution_profile(interp->registration);
  bool debug = profile.kind != NULL && strcmp(profile.kind, "debug") == 0;
  int64_t evaluation_floor = -1;
  if (interp->counter != NULL) evaluation_floor = interp->counter->stack_start;
  else if (read_only_evaluation->has)
    evaluation_floor = evaluation_stack_start(
        interp, options->read_only != NULL && options->read_only->has_stack ? &options->read_only->stack : NULL);
  bool previous_debug = interp->debug;
  int64_t entry_stack = interp->program_stack;
  int64_t previous_call_level = interp->call_level;
  size_t argument_words = options->arg_count > 10 ? options->arg_count : 10;
  int64_t sp = source == NULL ? interp_stack(interp, entry_stack - 8 - (int64_t)argument_words * 4)
                              : source->stack;
  if (evaluation_floor >= 0 && sp < evaluation_floor)
    qa_range_error("QVM evaluation stack would overlap source data");
  interp->debug = debug;
  invocation frame;
  frame.ops = source != NULL ? source->ops : &frame.owned_ops;
  frame.owned_ops.depth = 0;
  frame.owned_ops.debug = debug;
  if (source == NULL) memset(frame.owned_ops.has, 0, sizeof frame.owned_ops.has);
  frame.sp = sp;
  frame.function_scope = source != NULL ? source
                         : options->function_scope != NULL ? options->function_scope
                         : interp->active != NULL ? interp->active->function_scope : NULL;
  frame.previous = interp->active;
  frame.asynchronous = options->asynchronous;
  frame.validate_parent = options->validate_parent;
  frame.validate = options->validate;
  frame.validate_ctx = options->validate_ctx;
  frame.returns = NULL;
  frame.return_count = 0;
  frame.return_capacity = 0;
  if (interp->compiled) push_return(&frame, sp, source != NULL ? source->return_pc : -1);
  interp->active = &frame;
  execute_job job = {interp, options, read_only_evaluation, &frame, debug, profile, evaluation_floor, sp, argument_words, 0};
  /* TS rethrows the original error object, so its class (a CommonError "drop") survives the frame cleanup. */
  qa_caught_error caught;
  bool ok = qa_try(execute_body, &job, &caught);
  thrown error;
  error.set = false;
  if (!ok) {
    capture_thrown(interp, caught.message, &error);
    error.name = caught.name;
    error.code = caught.code;
    error.detail = caught.detail;
    cancellation_failure(frame.function_scope, &error);
  }
  interp->program_stack = entry_stack;
  interp->active = frame.previous;
  if (frame.previous != NULL) {
    interp->debug = previous_debug;
    interp->call_level = previous_call_level;
  }
  free(frame.returns);
  if (!ok) rethrow(interp, &error);
  return job.result;
}

/* ---------- public QvmInterpreter API ---------- */

static uint8_t *allocate_bytes(qa_qvm_interpreter *interp, const qa_qvm_allocation_profile *profile,
                               const char *purpose, size_t bytes, const char *resource,
                               bool *owned) {
  if (profile == NULL || !profile->accounted) {
    *owned = true;
    return calloc(bytes == 0 ? 1 : bytes, 1);
  }
  qa_qvm_allocation_request request = {purpose, resource, (double)bytes};
  qa_qvm_allocation allocation = profile->allocate(profile->ctx, request);
  *owned = false;
  if (interp->allocation_count == interp->allocation_capacity) {
    size_t capacity = interp->allocation_capacity == 0 ? 8 : interp->allocation_capacity * 2;
    qa_qvm_allocation *grown = realloc(interp->allocations, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    interp->allocations = grown;
    interp->allocation_capacity = capacity;
  }
  interp->allocations[interp->allocation_count++] = allocation;
  /* The allocator's storage is the live allocation, as in the TS. */
  return (uint8_t *)allocation.bytes;
}

static uint8_t *symbols_allocate(void *ctx, size_t bytes, const char *resource) {
  qa_qvm_interpreter *interp = ctx;
  bool owned = true;
  uint8_t *block = allocate_bytes(interp, interp->allocation_profile, "VM_LoadSymbols", bytes,
                                  resource != NULL ? resource : interp->source, &owned);
  if (block == NULL) qa_range_error("QVM memory allocation failed");
  if (owned) {
    if (interp->symbol_block_count == interp->symbol_block_capacity) {
      size_t capacity = interp->symbol_block_capacity == 0 ? 64 : interp->symbol_block_capacity * 2;
      void **grown = realloc(interp->symbol_blocks, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("QVM memory allocation failed");
      interp->symbol_blocks = grown;
      interp->symbol_block_capacity = capacity;
    }
    interp->symbol_blocks[interp->symbol_block_count++] = block;
  }
  return block;
}

qa_qvm_interpreter *qa_qvm_interpreter_create(const qa_qvm_image *image,
                                              qa_qvm_interpreter_system_call system_call,
                                              const qa_qvm_allocation_profile *profile,
                                              const qa_qvm_vm_registration *registration,
                                              qa_qvm_semantics semantics,
                                              const qa_qvm_store_effect *store_effect) {
  qa_qvm_interpreter *interp = calloc(1, sizeof *interp);
  if (interp == NULL) return NULL;
  interp->identity = next_interpreter_identity++;
  interp->next_serial = 1;
  interp->system_call = system_call;
  if (store_effect != NULL) {
    interp->store_effect = *store_effect;
    interp->has_store_effect = true;
  }
  interp->instructions = image->instructions;
  interp->instruction_count = image->instruction_count;
  interp->source = malloc(strlen(image->source) + 1);
  if (interp->source == NULL) { free(interp); return NULL; }
  strcpy(interp->source, image->source);
  interp->source_data_end = image->data_length + image->literal_length + image->bss_length;
  interp->memory_initialized_end = image->data_length + image->literal_length;
  interp->compiled = semantics != NULL && strcmp(semantics, "compiled") == 0;
  interp->registration = registration;
  interp->allocation_profile = profile;
  interp->memory = allocate_bytes(interp, profile, "VM_Create:dataBase",
                                  (size_t)image->allocated_data_length, image->source, &interp->memory_owned);
  if (interp->memory == NULL) { free(interp->allocations); free(interp->source); free(interp); return NULL; }
  interp->memory_length = (size_t)image->allocated_data_length;
  if (registration != NULL)
    qa_qvm_vm_registration_bind_data(registration, interp->memory_length);
  interp->address_space.bytes = interp->memory;
  interp->address_space.length = interp->memory_length;
  /* One QvmMemory owns publication for the allocation, raw views included. */
  qa_qvm_memory_init(&interp->guest, interp->memory, interp->memory_length, publish_effect, interp);
  interp->guest_ready = true;
  qa_qvm_memory_writes_bind_raw(&interp->guest.writes);
  if (image->initialized_length > 0) memcpy(interp->memory, image->initialized_data, image->initialized_length);
  interp->data_mask = image->data_mask;
  interp->program_stack = (int64_t)interp->memory_length;
  if (registration != NULL)
    qa_qvm_vm_registration_bind_instruction_pointers_length(
        registration, (int64_t)image->instruction_count * 4);
  uint8_t *pointers = allocate_bytes(interp, profile, "VM_Create:instructionPointers",
                                     image->instruction_count * 4, image->source, &interp->pointers_owned);
  if (pointers == NULL) { qa_qvm_interpreter_destroy(interp); return NULL; }
  interp->instruction_pointers = (int32_t *)(void *)pointers;
  if (registration != NULL)
    qa_qvm_vm_registration_bind_code_length(registration, (int64_t)image->code_length);
  uint8_t *code = allocate_bytes(interp, profile, "VM_PrepareInterpreter",
                                 (size_t)image->code_length * 4, image->source, &interp->code_owned);
  if (code == NULL) { qa_qvm_interpreter_destroy(interp); return NULL; }
  interp->code = (int32_t *)(void *)code;
  interp->code_length = (size_t)image->code_length;
  qa_qvm_symbols_init(&interp->symbols, interp->instruction_pointers,
                      image->instruction_count, symbols_allocate, interp, assert_live_cb, interp);
  for (size_t i = 0; i < image->instruction_count; i++)
    interp->instruction_pointers[i] = (int32_t)image->instructions[i].byte_offset;
  for (size_t i = 0; i < image->instruction_count; i++) {
    const qa_qvm_instruction *instruction = &image->instructions[i];
    interp->code[instruction->byte_offset] = instruction->opcode;
    if (instruction->operand_width != 0)
      interp->code[instruction->byte_offset + 1] =
          instruction->opcode >= QA_QVM_OP_EQ && instruction->opcode <= QA_QVM_OP_GEF
              ? (int32_t)target_pc(interp, instruction->operand)
              : instruction->operand;
  }
  if (registration != NULL) qa_qvm_vm_registration_bind_interpreter(registration, interp);
  return interp;
}

void qa_qvm_interpreter_destroy(qa_qvm_interpreter *interp) {
  if (interp == NULL) return;
  if (interp->code != NULL) qa_qvm_symbols_destroy(&interp->symbols);
  for (size_t i = 0; i < interp->symbol_block_count; i++) free(interp->symbol_blocks[i]);
  free(interp->symbol_blocks);
  if (interp->guest_ready) {
    qa_qvm_memory_writes_unbind_raw(&interp->guest.writes);
    qa_qvm_memory_destroy(&interp->guest);
  }
  if (interp->memory_owned) free(interp->memory);
  if (interp->pointers_owned) free(interp->instruction_pointers);
  if (interp->code_owned) free(interp->code);
  free(interp->source);
  for (size_t i = 0; i < interp->hook_count; i++) free(interp->hooks[i]);
  free(interp->hooks);
  for (size_t i = 0; i < interp->observer_count; i++) free(interp->observers[i]);
  free(interp->observers);
  free(interp->resolver);
  for (size_t i = 0; i < interp->retired_count; i++) free(interp->retired[i]);
  free(interp->retired);
  free(interp->allocations);
  free(interp);
}

uint8_t *qa_qvm_interpreter_get_memory(const qa_qvm_interpreter *interp, size_t *out_length) {
  if (out_length != NULL) *out_length = interp->memory_length;
  return interp->memory;
}
qa_qvmx_memory *qa_qvm_interpreter_get_address_space(qa_qvm_interpreter *interp) {
  return &interp->address_space;
}
qa_qvm_memory *qa_qvm_interpreter_get_guest(qa_qvm_interpreter *interp) { return &interp->guest; }
double qa_qvm_interpreter_get_call_level(const qa_qvm_interpreter *interp) {
  return (double)interp->call_level;
}
bool qa_qvm_interpreter_get_is_active(const qa_qvm_interpreter *interp) { return interp->root_active; }
double qa_qvm_interpreter_get_stack_pointer(const qa_qvm_interpreter *interp) {
  return (double)interp->program_stack;
}
double qa_qvm_interpreter_get_break_count(const qa_qvm_interpreter *interp) {
  return (double)interp->breaks;
}
bool qa_qvm_interpreter_get_debug_enabled(const qa_qvm_interpreter *interp) { return interp->debug; }
qa_qvm_semantics qa_qvm_interpreter_get_semantics(const qa_qvm_interpreter *interp) {
  return interp->compiled ? "compiled" : "interpreted";
}
double qa_qvm_interpreter_get_code_length(qa_qvm_interpreter *interp) {
  assert_live(interp);
  return (double)interp->code_length;
}
double qa_qvm_interpreter_get_instruction_pointers_length(qa_qvm_interpreter *interp) {
  assert_live(interp);
  return (double)interp->instruction_count * 4;
}

static void retire_entry(qa_qvm_interpreter *interp, void *entry) {
  if (interp->retired_count == interp->retired_capacity) {
    size_t capacity = interp->retired_capacity == 0 ? 8 : interp->retired_capacity * 2;
    void **grown = realloc(interp->retired, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    interp->retired = grown;
    interp->retired_capacity = capacity;
  }
  interp->retired[interp->retired_count++] = entry;
}

static void unbind_hook(void *ctx) {
  hook_entry *entry = ctx;
  if (!entry->active) return;
  entry->active = false;
  qa_qvm_interpreter *interp = entry->interp;
  for (size_t i = 0; i < interp->hook_count; i++) {
    if (interp->hooks[i] == entry) {
      interp->hooks[i] = interp->hooks[--interp->hook_count];
      break;
    }
  }
  retire_entry(interp, entry);
}

static void unbind_observer(void *ctx) {
  observer_entry *entry = ctx;
  if (!entry->active) return;
  entry->active = false;
  qa_qvm_interpreter *interp = entry->interp;
  /* Order matters: observers run in registration order. */
  for (size_t i = 0; i < interp->observer_count; i++) {
    if (interp->observers[i] == entry) {
      memmove(&interp->observers[i], &interp->observers[i + 1],
              (interp->observer_count - i - 1) * sizeof *interp->observers);
      interp->observer_count--;
      break;
    }
  }
  retire_entry(interp, entry);
}

static void unbind_resolver(void *ctx) {
  resolver_entry *entry = ctx;
  if (entry->interp->resolver == entry) {
    entry->interp->resolver = NULL;
    retire_entry(entry->interp, entry);
  }
}

static qa_qvm_unbind bind_entry(qa_qvm_interpreter *interp, double instruction_index,
                                qa_qvm_function_hook hook, bool invocations) {
  assert_live(interp);
  if (!is_safe_integer(instruction_index) || instruction_index < 0 ||
      code_word(interp, target_pc(interp, instruction_index)) != QA_QVM_OP_ENTER)
    qa_range_error("QVM hook requires a function entry instruction");
  if (find_hook(interp, (int64_t)instruction_index) != NULL)
    qa_range_error("QVM function already has a hook");
  if (interp->hook_count == interp->hook_capacity) {
    size_t capacity = interp->hook_capacity == 0 ? 8 : interp->hook_capacity * 2;
    hook_entry **grown = realloc(interp->hooks, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    interp->hooks = grown;
    interp->hook_capacity = capacity;
  }
  hook_entry *entry = xmalloc(sizeof *entry);
  entry->index = (int64_t)instruction_index;
  entry->hook = hook;
  entry->invocations = invocations;
  entry->active = true;
  entry->interp = interp;
  interp->hooks[interp->hook_count++] = entry;
  qa_qvm_unbind result = {entry, unbind_hook};
  return result;
}

qa_qvm_unbind qa_qvm_interpreter_bind_function(qa_qvm_interpreter *interp,
                                               double instruction_index, qa_qvm_function_hook hook) {
  return bind_entry(interp, instruction_index, hook, false);
}
qa_qvm_unbind qa_qvm_interpreter_bind_invocation(qa_qvm_interpreter *interp,
                                                 double instruction_index, qa_qvm_function_hook hook) {
  return bind_entry(interp, instruction_index, hook, true);
}
qa_qvm_unbind qa_qvm_interpreter_bind_function_resolver(qa_qvm_interpreter *interp,
                                                        qa_qvm_function_resolver resolver) {
  assert_live(interp);
  if (interp->resolver != NULL) qa_range_error("QVM already has a function resolver");
  resolver_entry *entry = xmalloc(sizeof *entry);
  entry->resolver = resolver;
  entry->interp = interp;
  interp->resolver = entry;
  qa_qvm_unbind result = {entry, unbind_resolver};
  return result;
}
qa_qvm_unbind qa_qvm_interpreter_observe_function(qa_qvm_interpreter *interp,
                                                  double instruction_index,
                                                  qa_qvm_function_observer observer) {
  assert_allocation(interp);
  if (!is_safe_integer(instruction_index) || instruction_index < 0 ||
      code_word(interp, target_pc(interp, instruction_index)) != QA_QVM_OP_ENTER)
    qa_range_error("QVM observer requires a function entry instruction");
  if (interp->observer_count == interp->observer_capacity) {
    size_t capacity = interp->observer_capacity == 0 ? 8 : interp->observer_capacity * 2;
    observer_entry **grown = realloc(interp->observers, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    interp->observers = grown;
    interp->observer_capacity = capacity;
  }
  observer_entry *entry = xmalloc(sizeof *entry);
  entry->index = (int64_t)instruction_index;
  entry->observer = observer;
  entry->active = true;
  entry->interp = interp;
  interp->observers[interp->observer_count++] = entry;
  qa_qvm_unbind result = {entry, unbind_observer};
  return result;
}

void qa_qvm_interpreter_restore_data(qa_qvm_interpreter *interp, const uint8_t *data,
                                     size_t length) {
  if (interp->root_active) qa_range_error("Cannot restore an active QVM");
  assert_live(interp);
  if (length != interp->memory_length) qa_range_error("QVM checkpoint allocation mismatch");
  qa_qvm_memory_clear_write_observers(&interp->guest);
  memcpy(interp->memory, data, length);
}

char *qa_qvm_interpreter_indent(const qa_qvm_interpreter *interp) {
  if (interp->call_level < 0) qa_range_error("VM indentation requires a nonnegative call level");
  int64_t count = interp->call_level < 20 ? interp->call_level : 20;
  char *text = malloc((size_t)(2 * count) + 1);
  if (text == NULL) return NULL;
  memset(text, ' ', (size_t)(2 * count));
  text[2 * count] = '\0';
  return text;
}

char *qa_qvm_interpreter_stack_trace(qa_qvm_interpreter *interp, double program_counter,
                                     double program_stack) {
  assert_live(interp);
  size_t capacity = 256, used = 0;
  char *text = malloc(capacity);
  if (text == NULL) return NULL;
  text[0] = '\0';
  int64_t pc = (int64_t)program_counter, stack = (int64_t)program_stack;
  int count = 0;
  do {
    char symbol[1024], line[1100];
    qa_qvm_symbols_value_to_symbol(&interp->symbols, (int32_t)pc, symbol, sizeof symbol);
    int written = snprintf(line, sizeof line, "%s\n", symbol);
    while (used + (size_t)written + 1 > capacity) {
      capacity *= 2;
      char *grown = realloc(text, capacity);
      if (grown == NULL) { free(text); return NULL; }
      text = grown;
    }
    memcpy(text + used, line, (size_t)written + 1);
    used += (size_t)written;
    stack = read_word(interp, stack + 4);
    pc = read_word(interp, stack);
  } while (pc != -1 && ++count < 32);
  return text;
}

void qa_qvm_interpreter_load_symbols(qa_qvm_interpreter *interp,
                                     const qa_qvm_symbol_load_options *options) {
  qa_qvm_symbols_load(&interp->symbols, options);
}

qa_qvm_symbols *qa_qvm_interpreter_get_symbols(qa_qvm_interpreter *interp) {
  return &interp->symbols;
}

qa_qvmx_view qa_qvm_interpreter_pointer(qa_qvm_interpreter *interp, double word) {
  assert_live(interp);
  bool is_null = false;
  size_t offset = qa_qvmx_memory_pointer_offset(&interp->address_space, word, &is_null);
  qa_qvmx_view view = {is_null ? NULL : interp->memory + offset,
                       is_null ? 0 : interp->memory_length - offset};
  return view;
}

void qa_qvm_interpreter_restart(qa_qvm_interpreter *interp, const qa_qvm_data_image *image) {
  if (interp->root_active) qa_range_error("Cannot restart an active QVM");
  assert_live(interp);
  if (image->allocated_data_length > (int64_t)interp->memory_length)
    qa_range_error("QVM restart would exceed its original allocation");
  qa_qvm_memory_clear_write_observers(&interp->guest);
  memset(interp->memory, 0, (size_t)image->allocated_data_length);
  if (image->initialized_length > 0) memcpy(interp->memory, image->initialized_data, image->initialized_length);
}

typedef struct root_job {
  qa_qvm_interpreter *interp;
  const execute_options *options;
  int32_t result;
} root_job;

static void root_sync_body(void *ctx) {
  root_job *job = ctx;
  job->result = execute_sync(job->interp, job->options);
}

static void root_async_body(void *ctx) {
  root_job *job = ctx;
  /* TS executeAsync validates before it starts. */
  if (job->options->validate != NULL) job->options->validate(job->options->validate_ctx);
  evaluation_local none;
  memset(&none, 0, sizeof none);
  job->result = execute(job->interp, job->options, &none);
}

static double root_invoke(qa_qvm_interpreter *interp, const execute_options *options, void (*body)(void *)) {
  if (interp->root_active)
    qa_range_error("QVM is already active; recursive calls belong to the current syscall");
  assert_live(interp);
  interp->root_active = true;
  root_job job = {interp, options, 0};
  qa_caught_error caught;
  bool ok = qa_try(body, &job, &caught);
  interp->root_active = false;
  if (!ok) qa_rethrow(&caught);
  return job.result;
}

double qa_qvm_interpreter_invoke(qa_qvm_interpreter *interp, const double *args,
                                 size_t arg_count, double instruction_index,
                                 const qa_qvm_read_only_evaluation *evaluation) {
  execute_options options;
  memset(&options, 0, sizeof options);
  options.args = args;
  options.arg_count = arg_count;
  options.instruction_index = invocation_index(instruction_index);
  options.read_only = evaluation;
  return root_invoke(interp, &options, root_sync_body);
}

double qa_qvm_interpreter_invoke_async_validated(qa_qvm_interpreter *interp, const double *args,
                                                 size_t arg_count, double instruction_index,
                                                 void (*validate)(void *ctx), void *validate_ctx) {
  execute_options options;
  memset(&options, 0, sizeof options);
  options.args = args;
  options.arg_count = arg_count;
  options.instruction_index = invocation_index(instruction_index);
  options.asynchronous = true;
  options.validate = validate;
  options.validate_ctx = validate_ctx;
  return root_invoke(interp, &options, root_async_body);
}

double qa_qvm_interpreter_invoke_async(qa_qvm_interpreter *interp, const double *args,
                                       size_t arg_count, double instruction_index) {
  return qa_qvm_interpreter_invoke_async_validated(interp, args, arg_count, instruction_index, NULL, NULL);
}

typedef struct counter_run {
  double (*execute_fn)(void *ctx);
  void *execute_ctx;
} counter_run;

static void counter_body(void *ctx) {
  counter_run *run = ctx;
  (void)run->execute_fn(run->execute_ctx);
}

double qa_qvm_interpreter_evaluate_counter(qa_qvm_interpreter *interp, double address,
                                           double initial, const double *functions,
                                           size_t function_count,
                                           double (*execute_fn)(void *ctx), void *execute_ctx,
                                           const qa_qvm_evaluation_stack *stack) {
  assert_live(interp);
  /* TS range(address, 4) and `address % 4 !== 0` compare the number itself: a
   * fraction or NaN passes the range test and then fails the alignment test. */
  if (address < 0 || address > (double)interp->memory_length - 4)
    qa_range_error("%s: QVM memory access %s+4 exceeds allocation", interp->source, QA_JS_NUMBER(address));
  (void)signed_word(initial);
  if (fmod(address, 4) != 0 || address + 4 > (double)interp->source_data_end ||
      interp->counter != NULL)
    qa_range_error("QVM counter evaluation requires one live aligned source word and a fresh scope");
  if (function_count == 0) qa_range_error("QVM counter evaluation requires distinct original functions");
  for (size_t i = 0; i < function_count; i++)
    for (size_t j = i + 1; j < function_count; j++)
      if (functions[i] == functions[j])
        qa_range_error("QVM counter evaluation requires distinct original functions");
  for (size_t i = 0; i < function_count; i++) {
    double entry = functions[i];
    if (!is_integer(entry) || entry < 0 || entry >= (double)interp->instruction_count ||
        interp->instructions[(size_t)entry].opcode != QA_QVM_OP_ENTER)
      qa_range_error("QVM counter evaluation entry is not an original function");
  }
  int64_t stack_start = evaluation_stack_start(interp, stack);
  if (address + 4 > (double)stack_start) qa_range_error("QVM counter word overlaps its evaluation stack");
  counter_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.range_start = xmalloc(function_count * sizeof(int64_t));
  scope.range_end = xmalloc(function_count * sizeof(int64_t));
  for (size_t i = 0; i < function_count; i++) {
    double entry = functions[i];
    int64_t end = (int64_t)entry + 1;
    while (end < (int64_t)interp->instruction_count &&
           interp->instructions[end].opcode != QA_QVM_OP_ENTER)
      end++;
    scope.range_start[i] = target_pc(interp, entry);
    scope.range_end[i] = end == (int64_t)interp->instruction_count
                             ? (int64_t)interp->code_length
                             : target_pc(interp, (double)end);
  }
  scope.address = (int64_t)address;
  scope.value = (int32_t)initial;
  scope.stack_start = stack_start;
  scope.stack_end = interp->program_stack;
  scope.functions = functions;
  scope.function_count = function_count;
  scope.remaining = 100000;
  interp->counter = &scope;
  counter_run run = {execute_fn, execute_ctx};
  char message[THROWN_CAPACITY];
  bool ok = qa_range_error_catch(counter_body, &run, message, sizeof message);
  interp->counter = NULL;
  free(scope.range_start);
  free(scope.range_end);
  if (!ok) qa_range_error("%s", message);
  return scope.value;
}

/*
 * Port of src/compat/qvm/interpreter.ts, plus qa_qvmx_* mirrors of
 * memory.ts/operations.ts (qvm-b slice). The TS generator/Promise
 * machinery collapses to direct synchronous recursion; cancellation
 * uses setjmp/longjmp back to the intercepting frame, returning 0
 * from the cancelled source call as in the TS. Region qualification
 * and symbol names use the landed regions/symbols ports.
 * GPL-2.0-or-later.
 */
#include "qa/compat/qvm/interpreter.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/core/numeric.h"

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
void qa_qvmx_view_set_i32(qa_qvmx_view *view, int64_t offset, int32_t value) {
  qa_qvmx_view_check(view, offset, 4);
  uint8_t *p = view->bytes + offset;
  uint32_t v;
  memcpy(&v, &value, sizeof v);
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
float qa_qvmx_view_f32(const qa_qvmx_view *view, int64_t offset) {
  int32_t bits = qa_qvmx_view_i32(view, offset);
  float out;
  memcpy(&out, &bits, sizeof out);
  return out;
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
  view->bytes[offset] = value;
}
uint16_t qa_qvmx_view_u16(const qa_qvmx_view *view, int64_t offset) {
  qa_qvmx_view_check(view, offset, 2);
  const uint8_t *p = view->bytes + offset;
  return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}
void qa_qvmx_view_set_u16(qa_qvmx_view *view, int64_t offset, uint16_t value) {
  qa_qvmx_view_check(view, offset, 2);
  view->bytes[offset] = (uint8_t)value;
  view->bytes[offset + 1] = (uint8_t)(value >> 8);
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
    qa_range_error("QVM memory range exceeds allocation");
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
  q_strncpyz(memory->bytes + base, memory->length - base, text, capacity);
}

void qa_qvmx_memory_write_bounded_string(qa_qvmx_memory *memory, double word, const char *text,
                                         double capacity) {
  bool is_null = false;
  size_t base = qa_qvmx_memory_pointer_offset(memory, word, &is_null);
  if (is_null || !is_safe_integer(capacity) || capacity < 1 ||
      capacity > (double)(memory->length - base))
    qa_range_error("Bounded string copy exceeds QVM allocation");
  q_strncpyz(memory->bytes + base, memory->length - base, text, capacity);
}

void qa_qvmx_memory_write_bytes(qa_qvmx_memory *memory, double byte_offset,
                                const uint8_t *bytes, size_t length) {
  memory_range(memory, byte_offset, (double)length);
  memcpy(memory->bytes + (size_t)byte_offset, bytes, length);
}

void qa_qvmx_memory_fill_bytes(qa_qvmx_memory *memory, double byte_offset, double byte_length,
                               double value) {
  memory_range(memory, byte_offset, byte_length);
  memset(memory->bytes + (size_t)byte_offset, (int)((int64_t)value & 255), (size_t)byte_length);
}

void qa_qvmx_memory_copy_bytes(qa_qvmx_memory *memory, double destination, double source,
                               double byte_length) {
  memory_range(memory, destination, byte_length);
  memory_range(memory, source, byte_length);
  memmove(memory->bytes + (size_t)destination, memory->bytes + (size_t)source, (size_t)byte_length);
}

/* ---------- mirrors of operations.ts (qvm-b) ---------- */

static noreturn void unsupported_op(const char *kind, qa_qvm_opcode opcode) {
  qa_range_error("Unsupported QVM %s opcode %d", kind, (int)opcode);
}

int32_t qa_qvmx_evaluate_unary(qa_qvm_opcode opcode, int32_t word) {
  switch (opcode) {
    case QA_QVM_OP_SEX8: return (int32_t)((word << 24) >> 24);
    case QA_QVM_OP_SEX16: return (int32_t)((word << 16) >> 16);
    case QA_QVM_OP_NEGI: return (int32_t)(-(int64_t)word);
    case QA_QVM_OP_NEGF: return (int32_t)qa_float32_to_bits(-(double)qa_bits_to_float32((uint32_t)word));
    case QA_QVM_OP_CVIF: return (int32_t)qa_float32_to_bits((double)word);
    case QA_QVM_OP_CVFI: return qa_qvm_float_to_int((double)qa_bits_to_float32((uint32_t)word));
    default: unsupported_op("unary", opcode);
  }
}

static int shift_count(int32_t word) {
  if (word < 0 || word > 31) qa_range_error("QVM shift count outside 0..31");
  return word;
}

int32_t qa_qvmx_evaluate_binary(qa_qvm_opcode opcode, int32_t left, int32_t right) {
  switch (opcode) {
    case QA_QVM_OP_ADD: return (int32_t)((uint32_t)left + (uint32_t)right);
    case QA_QVM_OP_SUB: return (int32_t)((uint32_t)left - (uint32_t)right);
    case QA_QVM_OP_DIVI: case QA_QVM_OP_MODI:
      if (right == 0) qa_range_error("QVM integer division or modulo by zero");
      if (left == INT32_MIN && right == -1)
        qa_range_error("QVM signed division or modulo overflow");
      return opcode == QA_QVM_OP_DIVI ? (int32_t)trunc((double)left / (double)right)
                                      : (int32_t)((double)left - trunc((double)left / (double)right) * (double)right);
    case QA_QVM_OP_DIVU: case QA_QVM_OP_MODU:
      if (right == 0) qa_range_error("QVM integer division or modulo by zero");
      return (int32_t)(opcode == QA_QVM_OP_DIVU ? (uint32_t)left / (uint32_t)right
                                                : (uint32_t)left % (uint32_t)right);
    case QA_QVM_OP_MULI: case QA_QVM_OP_MULU:
      return (int32_t)((uint32_t)left * (uint32_t)right);
    case QA_QVM_OP_BAND: return left & right;
    case QA_QVM_OP_BOR: return left | right;
    case QA_QVM_OP_BXOR: return left ^ right;
    case QA_QVM_OP_LSH: return (int32_t)((uint32_t)left << shift_count(right));
    case QA_QVM_OP_RSHI: return left >> shift_count(right);
    case QA_QVM_OP_RSHU: return (int32_t)((uint32_t)left >> shift_count(right));
    case QA_QVM_OP_ADDF:
      return (int32_t)qa_float32_to_bits((double)qa_bits_to_float32((uint32_t)left) + (double)qa_bits_to_float32((uint32_t)right));
    case QA_QVM_OP_SUBF:
      return (int32_t)qa_float32_to_bits((double)qa_bits_to_float32((uint32_t)left) - (double)qa_bits_to_float32((uint32_t)right));
    case QA_QVM_OP_DIVF:
      return (int32_t)qa_float32_to_bits((double)qa_bits_to_float32((uint32_t)left) / (double)qa_bits_to_float32((uint32_t)right));
    case QA_QVM_OP_MULF:
      return (int32_t)qa_float32_to_bits((double)qa_bits_to_float32((uint32_t)left) * (double)qa_bits_to_float32((uint32_t)right));
    default: unsupported_op("binary", opcode);
  }
}

bool qa_qvmx_evaluate_branch(qa_qvm_opcode opcode, int32_t left, int32_t right) {
  switch (opcode) {
    case QA_QVM_OP_EQ: return left == right;
    case QA_QVM_OP_NE: return left != right;
    case QA_QVM_OP_LTI: return left < right;
    case QA_QVM_OP_LEI: return left <= right;
    case QA_QVM_OP_GTI: return left > right;
    case QA_QVM_OP_GEI: return left >= right;
    case QA_QVM_OP_LTU: return (uint32_t)left < (uint32_t)right;
    case QA_QVM_OP_LEU: return (uint32_t)left <= (uint32_t)right;
    case QA_QVM_OP_GTU: return (uint32_t)left > (uint32_t)right;
    case QA_QVM_OP_GEU: return (uint32_t)left >= (uint32_t)right;
    case QA_QVM_OP_EQF: return qa_bits_to_float32((uint32_t)left) == qa_bits_to_float32((uint32_t)right);
    case QA_QVM_OP_NEF: return qa_bits_to_float32((uint32_t)left) != qa_bits_to_float32((uint32_t)right);
    case QA_QVM_OP_LTF: return qa_bits_to_float32((uint32_t)left) < qa_bits_to_float32((uint32_t)right);
    case QA_QVM_OP_LEF: return qa_bits_to_float32((uint32_t)left) <= qa_bits_to_float32((uint32_t)right);
    case QA_QVM_OP_GTF: return qa_bits_to_float32((uint32_t)left) > qa_bits_to_float32((uint32_t)right);
    case QA_QVM_OP_GEF: return qa_bits_to_float32((uint32_t)left) >= qa_bits_to_float32((uint32_t)right);
    default: unsupported_op("branch", opcode);
  }
}

/* ---------- interpreter.ts ---------- */

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
static void operands_truncate(operands *o, double depth) {
  if (!is_safe_integer(depth) || depth < 0 || depth > o->depth)
    qa_range_error("QVM cancellation lost its caller operands");
  while (o->depth > (int)depth) {
    o->has[o->depth] = false;
    o->depth--;
  }
}

static int32_t signed_word(double value) {
  if (!is_integer(value) || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("QVM host value must be a signed 32-bit integer");
  return (int32_t)value;
}

typedef struct hook_entry {
  int64_t index;
  qa_qvm_function_hook hook;
  bool invocations;
  bool active;
} hook_entry;

typedef struct observer_entry {
  int64_t index;
  qa_qvm_function_observer observer;
  bool active;
} observer_entry;

typedef struct counter_scope {
  int64_t address;
  int32_t value;
  int64_t stack_start, stack_end;
  int64_t remaining;
  const double *functions;
  size_t function_count;
  /* ranges in PC space */
  int64_t *range_start, *range_end;
} counter_scope;

typedef struct source_region {
  qa_qvm_region_binding binding;
  int64_t entry_pc, join_pc, stack, frame_size;
  int state; /* 0 ready 1 running 2 complete */
} source_region;

typedef struct source_call {
  int64_t stack, return_pc;
  operands *ops;
  int operand_depth;
  struct source_call *parent;
  bool active;
  /* branches: pc -> binding index into branch list */
  int64_t *branch_pcs;
  qa_qvm_branch_binding *branch_bindings;
  size_t branch_count;
  source_region *regions;
  size_t region_count;
  bool has_evaluation;
  qa_qvmx_region_evaluation eval_region;
  double *eval_inputs;
  size_t eval_input_count;
  int64_t eval_frame_size;
  bool cancellation_open, cancellation_requested;
  jmp_buf cancel_jump;
  bool cancel_armed;
} source_call;

typedef struct invocation {
  operands *ops;
  operands owned_ops;
  int64_t *sp;
  source_call *function_scope;
  struct invocation *previous;
} invocation;

struct qa_qvm_interpreter {
  uint8_t *memory;
  size_t memory_length;
  bool memory_owned;
  qa_qvmx_memory address_space;
  qa_qvm_interpreter_system_call system_call;
  qa_qvm_store_effect store_effect;
  bool has_store_effect;
  const qa_qvm_instruction *instructions;
  size_t instruction_count;
  char *source; /* owned copy */
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
  hook_entry *hooks;
  size_t hook_count, hook_capacity;
  observer_entry *observers;
  size_t observer_count, observer_capacity;
  bool has_resolver;
  qa_qvm_function_resolver resolver;
  const qa_qvm_vm_registration *registration;
  const qa_qvm_allocation_profile *allocation_profile; /* borrowed, for symbols */
  qa_qvm_symbols symbols;
  /* cancellation scopes: token pointer -> source_call */
  source_call **cancel_targets;
  size_t cancel_count, cancel_capacity;
};

/* TS live()/assertAllocation(): the C address space has no publishing
 * state, so only the freed-registration check remains observable. */
static void assert_live(const qa_qvm_interpreter *interp) {
  if (interp->registration != NULL) {
    qa_qvm_vm_binding binding = qa_qvm_vm_registration_get_binding(interp->registration);
    if (binding.kind != NULL && strcmp(binding.kind, "freed") == 0)
      qa_range_error("QVM registration has been freed");
  }
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
    qa_range_error("%s: invalid QVM instruction index %g", interp->source, index);
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
  int64_t current = interp->active != NULL ? *interp->active->sp : scope->stack_end;
  int64_t low = scope->stack_start > current ? scope->stack_start : current;
  if (address < low || address + length > scope->stack_end)
    qa_range_error("QVM counter evaluation attempted an unrelated source write");
  return false;
}

static int32_t read_word(qa_qvm_interpreter *interp, int64_t address) {
  interp_range(interp, address, 4);
  int32_t value;
  if (interp->counter != NULL && counter_read(interp, address, 4, &value)) return value;
  qa_qvmx_view view = {interp->memory, interp->memory_length};
  return qa_qvmx_view_i32(&view, address);
}

static void write_word(qa_qvm_interpreter *interp, int64_t address, int32_t word) {
  interp_range(interp, address, 4);
  if (interp->counter != NULL && counter_write(interp, address, 4, word)) return;
  qa_qvmx_view view = {interp->memory, interp->memory_length};
  qa_qvmx_view_set_i32(&view, address, word);
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

static double execute(qa_qvm_interpreter *interp, const double *args, size_t arg_count,
                      int64_t instruction_index, source_call *source,
                      source_call *function_scope,
                      const qa_qvm_read_only_evaluation *read_only);


/* Host-call scope shared by trap/intercept/decision/region. */
typedef struct host_ctx {
  qa_qvm_interpreter *interp;
  invocation *frame;
  source_call *function_scope;
} host_ctx;

static void check_cancellation(source_call *scope) {
  for (source_call *call = scope; call != NULL; call = call->parent)
    if (call->active && call->cancellation_requested && call->cancel_armed) longjmp(call->cancel_jump, 1);
}

static _Noreturn void host_cancel(void *ctx, qa_qvm_cancellation_scope scope) {
  host_ctx *host = ctx;
  source_call *target = NULL;
  for (size_t i = 0; i < host->interp->cancel_count; i++)
    if (host->interp->cancel_targets[i] != NULL &&
        (const void *)host->interp->cancel_targets[i] == scope.token)
      target = host->interp->cancel_targets[i];
  if (target == NULL) qa_range_error("QVM cancellation scope belongs to another interpreter");
  if (!target->active) qa_range_error("QVM cancellation scope has expired");
  if (!target->cancellation_open || target->cancellation_requested)
    qa_range_error("QVM cancellation scope has already been used");
  source_call *current = host->function_scope;
  while (current != NULL && current != target) current = current->parent;
  if (current == NULL) qa_range_error("QVM cancellation scope is not an ancestor of this call");
  target->cancellation_requested = true;
  if (target->cancel_armed) longjmp(target->cancel_jump, 1);
  qa_range_error("QVM function scope cancelled");
}

static double host_invoke(void *ctx, const double *args, size_t arg_count, bool has_index,
                          double instruction_index,
                          const qa_qvm_read_only_evaluation *evaluation) {
  host_ctx *host = ctx;
  (void)has_index;
  check_cancellation(host->function_scope);
  assert_live(host->interp);
  return execute(host->interp, args, arg_count, (int64_t)instruction_index, NULL,
                 host->function_scope, evaluation);
}

static double host_invoke_async(void *ctx, const double *args, size_t arg_count, bool has_index,
                                double instruction_index) {
  return host_invoke(ctx, args, arg_count, has_index, instruction_index, NULL);
}

static double run_store_effect(qa_qvm_interpreter *interp, host_ctx *host,
                               void (*perform)(void *), void *perform_ctx) {
  if (!interp->has_store_effect) {
    perform(perform_ctx);
    return 0;
  }
  qa_qvm_syscall scope;
  memset(&scope, 0, sizeof scope);
  scope.memory = interp->memory;
  scope.memory_length = interp->memory_length;
  scope.guest = &interp->address_space;
  scope.ctx = host;
  scope.invoke = host_invoke;
  scope.invoke_async = host_invoke_async;
  scope.cancel_function = host_cancel;
  interp->store_effect.call(interp->store_effect.ctx, &scope, perform, perform_ctx);
  return 0;
}

typedef struct intercept_ctx {
  qa_qvm_interpreter *interp;
  invocation *frame;
  source_call *source;
  int64_t instruction_index;
  bool proceeded;
} intercept_ctx;

static qa_qvm_cancellation_scope intercept_cancellation_scope(void *ctx) {
  intercept_ctx *ic = ctx;
  if (ic->proceeded || ic->source->cancellation_open)
    qa_range_error("QVM cancellation scope must open once before proceeding");
  ic->source->cancellation_open = true;
  qa_qvm_interpreter *interp = ic->interp;
  if (interp->cancel_count == interp->cancel_capacity) {
    size_t capacity = interp->cancel_capacity == 0 ? 8 : interp->cancel_capacity * 2;
    source_call **grown = realloc(interp->cancel_targets, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    interp->cancel_targets = grown;
    interp->cancel_capacity = capacity;
  }
  interp->cancel_targets[interp->cancel_count++] = ic->source;
  qa_qvm_cancellation_scope scope = {(const void *)ic->source};
  return scope;
}

static void intercept_branches(void *ctx, const qa_qvm_branch_binding *bindings, size_t count) {
  intercept_ctx *ic = ctx;
  qa_qvm_interpreter *interp = ic->interp;
  if (ic->proceeded || ic->source->branch_bindings != NULL)
    qa_range_error("QVM branches must bind once before proceeding");
  int64_t end = ic->instruction_index + 1;
  while (end < (int64_t)interp->instruction_count &&
         code_word(interp, target_pc(interp, (double)end)) != QA_QVM_OP_ENTER)
    end++;
  ic->source->branch_pcs = malloc((count == 0 ? 1 : count) * sizeof(int64_t));
  ic->source->branch_bindings = malloc((count == 0 ? 1 : count) * sizeof(qa_qvm_branch_binding));
  if (ic->source->branch_pcs == NULL || ic->source->branch_bindings == NULL)
    qa_range_error("out of memory");
  ic->source->branch_count = count;
  for (size_t i = 0; i < count; i++) {
    double index = bindings[i].instruction_index;
    if (!is_safe_integer(index) || index <= (double)ic->instruction_index || index >= (double)end)
      qa_range_error("QVM branch is outside its owning function");
    int64_t pc = target_pc(interp, index);
    int32_t opcode = code_word(interp, pc);
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) duplicate = duplicate || ic->source->branch_pcs[j] == pc;
    if (opcode < QA_QVM_OP_EQ || opcode > QA_QVM_OP_GEF || duplicate)
      qa_range_error("QVM branch requires a distinct original conditional instruction");
    ic->source->branch_pcs[i] = pc;
    ic->source->branch_bindings[i] = bindings[i];
  }
}

static int64_t to_region_word(double value) {
  return is_safe_integer(value) ? (int64_t)value : INT64_MIN;
}

static qa_qvmy_region_instruction *region_instructions(qa_qvm_interpreter *interp) {
  qa_qvmy_region_instruction *list =
      malloc((interp->instruction_count == 0 ? 1 : interp->instruction_count) * sizeof *list);
  if (list == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < interp->instruction_count; i++) {
    list[i].opcode = (int32_t)interp->instructions[i].opcode;
    list[i].operand_width = interp->instructions[i].operand_width;
    list[i].operand = interp->instructions[i].operand;
    list[i].byte_offset = interp->instructions[i].byte_offset;
  }
  return list;
}

static int64_t qualify_region(qa_qvm_interpreter *interp, int64_t owner, double entry, double join) {
  qa_qvmy_region_instruction *list = region_instructions(interp);
  int64_t frame = qa_qvm_qualify_region(list, interp->instruction_count, owner,
                                         to_region_word(entry), to_region_word(join));
  free(list);
  return frame;
}

static int64_t qualify_evaluation(qa_qvm_interpreter *interp, int64_t owner,
                                   const qa_qvm_region_evaluation *region, const char *access) {
  qa_qvmy_region_instruction *list = region_instructions(interp);
  int64_t frame = qa_qvm_qualify_region_evaluation(list, interp->instruction_count, owner, region,
                                                    access);
  free(list);
  return frame;
}

static void intercept_regions(void *ctx, const qa_qvm_region_binding *bindings, size_t count) {
  intercept_ctx *ic = ctx;
  qa_qvm_interpreter *interp = ic->interp;
  if (ic->proceeded || ic->source->regions != NULL)
    qa_range_error("QVM regions must bind once before proceeding");
  ic->source->regions = calloc(count == 0 ? 1 : count, sizeof(source_region));
  if (ic->source->regions == NULL) qa_range_error("out of memory");
  ic->source->region_count = count;
  /* TS sorts a copy of the bindings by entry before the overlap check. */
  size_t *order = malloc((count == 0 ? 1 : count) * sizeof *order);
  if (order == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) order[i] = i;
  for (size_t i = 1; i < count; i++) {
    size_t key = order[i], j = i;
    while (j > 0 && bindings[order[j - 1]].entry > bindings[key].entry) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = key;
  }
  double previous = (double)ic->instruction_index;
  for (size_t n = 0; n < count; n++) {
    size_t i = order[n];
    if (bindings[i].entry < previous) qa_range_error("QVM original regions overlap");
    int64_t frame_size = qualify_region(interp, ic->instruction_index, bindings[i].entry,
                                         bindings[i].join);
    previous = bindings[i].join;
    ic->source->regions[n].binding = bindings[i];
    ic->source->regions[n].entry_pc = target_pc(interp, bindings[i].entry);
    ic->source->regions[n].join_pc = target_pc(interp, bindings[i].join);
    ic->source->regions[n].frame_size = frame_size;
    ic->source->regions[n].stack = ic->source->stack - frame_size;
    ic->source->regions[n].state = 0;
  }
  free(order);
}

static double intercept_evaluate_region(void *ctx, qa_qvmx_region_evaluation region,
                                        const double *inputs, size_t input_count) {
  intercept_ctx *ic = ctx;
  if (ic->proceeded) qa_range_error("QVM function continuation can only run once");
  ic->proceeded = true;
  int64_t frame_size = qualify_evaluation(ic->interp, ic->instruction_index, &region, "source");
  if (input_count != region.input_count)
    qa_range_error("QVM region live-ins differ from its qualified frame");
  for (size_t i = 0; i < input_count; i++) (void)signed_word(inputs[i]);
  ic->source->has_evaluation = true;
  ic->source->eval_region = region;
  ic->source->eval_inputs = malloc((input_count == 0 ? 1 : input_count) * sizeof(double));
  if (ic->source->eval_inputs == NULL) qa_range_error("out of memory");
  memcpy(ic->source->eval_inputs, inputs, input_count * sizeof(double));
  ic->source->eval_input_count = input_count;
  ic->source->eval_frame_size = frame_size;
  static const double unused_args[10] = {0};
  return execute(ic->interp, unused_args, 10, ic->instruction_index, ic->source, NULL, NULL);
}

static void intercept_effect(void *ctx, void (*perform)(void *), void *perform_ctx) {
  intercept_ctx *ic = ctx;
  host_ctx host = {ic->interp, ic->frame, ic->source};
  check_cancellation(ic->source);
  (void)run_store_effect(ic->interp, &host, perform, perform_ctx);
}

static double intercept_proceed(void *ctx) {
  intercept_ctx *ic = ctx;
  if (ic->proceeded) qa_range_error("QVM function continuation can only run once");
  ic->proceeded = true;
  static const double unused_args[10] = {0};
  return execute(ic->interp, unused_args, 10, ic->instruction_index, ic->source, NULL, NULL);
}

typedef struct observation_host {
  host_ctx host; /* must stay first */
  int64_t sp, bytes;
  bool open;
} observation_host;

static double observation_argument(void *ctx, double index) {
  observation_host *o = ctx;
  if (!o->open) qa_range_error("QVM function observation has ended");
  if (!is_safe_integer(index) || index < 0 || index >= (double)o->bytes / 4)
    qa_range_error("QVM argument index outside source call");
  qa_qvmx_view view = {o->host.interp->memory + o->sp + 8, (size_t)o->bytes};
  return qa_qvmx_view_i32(&view, (int64_t)index * 4);
}

static double intercept(qa_qvm_interpreter *interp, invocation *frame, int64_t sp,
                        int64_t return_pc, int64_t instruction_index,
                        const qa_qvm_function_hook *hook, bool has_hook,
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
  /* Observers run first. */
  for (size_t i = 0; i < interp->observer_count; i++) {
    observer_entry *entry = &interp->observers[i];
    if (!entry->active || entry->index != instruction_index) continue;
    observation_host ohost = {{interp, frame, &source}, sp, bytes, true};
    qa_qvm_function_observation observation;
    memset(&observation, 0, sizeof observation);
    observation.instruction_index = (double)instruction_index;
    observation.ctx = &ohost;
    observation.invoke = host_invoke;
    observation.invoke_async = host_invoke_async;
    observation.cancel_function = host_cancel;
    observation.argument = observation_argument;
    entry->observer.observe(entry->observer.ctx, &observation);
    ohost.open = false;
  }
  intercept_ctx ic = {interp, frame, &source, instruction_index, false};
  host_ctx host = {interp, frame, &source};
  qa_qvm_function_call call;
  memset(&call, 0, sizeof call);
  call.instruction_index = (double)instruction_index;
  call.has_caller_instruction = caller_instruction >= 0;
  call.caller_instruction = (double)caller_instruction;
  call.execution = "synchronous";
  call.words.bytes = interp->memory + sp + 8;
  call.words.length = (size_t)bytes;
  call.memory = interp->memory;
  call.memory_length = interp->memory_length;
  call.guest = &interp->address_space;
  call.ctx = &ic;
  call.invoke = host_invoke;
  /* Rebind ctx-carrying callbacks to the intercept/host contexts. */
  call.invoke_async = host_invoke_async;
  call.cancel_function = host_cancel;
  call.cancellation_scope = intercept_cancellation_scope;
  call.branches = intercept_branches;
  call.regions = intercept_regions;
  call.evaluate_region = intercept_evaluate_region;
  call.effect = intercept_effect;
  call.proceed = intercept_proceed;
  call.proceed_async = intercept_proceed;
  /* The invoke/cancel callbacks receive call.ctx (&ic) in our struct
   * convention mismatch: route through ic by storing host inside ic's
   * frame via a wrapper is unnecessary because host_* only use
   * frame/function_scope, replicated here by using &host for the
   * duration through a thread-local style static is avoided: instead
   * set call.ctx to &ic and make host functions accept intercept_ctx
   * layout-compatible first members (interp, frame, source-as-scope).
   * host_ctx and intercept_ctx share their first three members. */
  double result;
  if (setjmp(source.cancel_jump) != 0) {
    source.cancel_armed = false;
    operands_truncate(source.ops, source.operand_depth);
    write_word(interp, source.stack, (int32_t)source.return_pc);
    result = 0;
  } else {
    source.cancel_armed = true;
    result = has_hook ? hook->call(hook->ctx, &call) : intercept_proceed(&ic);
    source.cancel_armed = false;
    result = (double)signed_word(result);
  }
  (void)host;
  source.active = false;
  free(source.branch_pcs);
  free(source.branch_bindings);
  free(source.regions);
  free(source.eval_inputs);
  check_cancellation(source.parent);
  return result;
}

typedef struct evaluation_local {
  qa_qvmx_region_evaluation region;
  const double *inputs;
  size_t input_count;
  int64_t frame_size;
} evaluation_local;

static double decision(qa_qvm_interpreter *interp, invocation *frame, source_call *owner,
                         qa_qvm_branch_binding *binding, bool taken) {
  int64_t previous_stack = interp->program_stack;
  interp->program_stack = *frame->sp - 4;
  host_ctx host = {interp, frame, owner};
  qa_qvm_syscall control;
  memset(&control, 0, sizeof control);
  control.ctx = &host;
  control.cancel_function = host_cancel;
  control.memory = interp->memory;
  control.memory_length = interp->memory_length;
  control.guest = &interp->address_space;
  double result = binding->decide(binding->ctx, taken, &control) ? 1 : 0;
  interp->program_stack = previous_stack;
  return result;
}

typedef struct region_host {
  host_ctx host; /* must stay first: invoke/cancel receive this pointer */
  source_region *region;
  bool open;
} region_host;

/* TS current(): every region control operation checks expiry first. */
static void region_check_current(region_host *r) {
  if (!r->open || !r->host.function_scope->active || r->host.interp->active != r->host.frame ||
      *r->host.frame->sp != r->region->stack)
    qa_range_error("QVM region callback has expired");
}
static double region_invoke(void *ctx, const double *args, size_t arg_count, bool has_index,
                            double instruction_index,
                            const qa_qvm_read_only_evaluation *evaluation) {
  region_check_current(ctx);
  return host_invoke(ctx, args, arg_count, has_index, instruction_index, evaluation);
}
static _Noreturn void region_cancel(void *ctx, qa_qvm_cancellation_scope scope) {
  region_check_current(ctx);
  host_cancel(ctx, scope);
}

static double region_local_word(void *ctx, double offset) {
  region_host *r = ctx;
  region_check_current(r);
  if (!is_safe_integer(offset) || offset < 8 || (int64_t)offset % 4 != 0 ||
      offset + 4 > (double)r->region->frame_size)
    qa_range_error("QVM region local is outside its original frame");
  return read_word(r->host.interp, r->region->stack + (int64_t)offset);
}

static double run_region(qa_qvm_interpreter *interp, invocation *frame, source_call *owner,
                           source_region *region, bool complete_phase) {
  if (!owner->active || *frame->sp != region->stack || frame->ops->depth != owner->operand_depth)
    qa_range_error("QVM region lost its original frame or operand boundary");
  int64_t previous_stack = interp->program_stack;
  interp->program_stack = region->stack - 4;
  region_host rhost = {{interp, frame, owner}, region, true};
  qa_qvm_region_control control;
  memset(&control, 0, sizeof control);
  control.ctx = &rhost;
  control.invoke = region_invoke;
  control.cancel_function = region_cancel;
  control.local_word = region_local_word;
  double result = 0;
  if (complete_phase) {
    if (region->binding.completed != NULL)
      region->binding.completed(region->binding.ctx, &control);
  } else {
    const char *choice = region->binding.run(region->binding.ctx, &control);
    if (choice == NULL || (strcmp(choice, "execute") != 0 && strcmp(choice, "skip") != 0))
      qa_range_error("QVM region requires one execution decision");
    result = strcmp(choice, "execute") == 0 ? 1 : 0;
  }
  rhost.open = false;
  interp->program_stack = previous_stack;
  return result;
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

static double execute(qa_qvm_interpreter *interp, const double *args, size_t arg_count,
                      int64_t instruction_index, source_call *source,
                      source_call *function_scope,
                      const qa_qvm_read_only_evaluation *read_only) {
  if (source == NULL) {
    if (instruction_index == 0 ? arg_count != 10 : arg_count > QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
      qa_range_error(instruction_index == 0 ? "QVM vmMain requires ten public argument words"
                                            : "QVM private call exceeds OP_ARG argument capacity");
    if (instruction_index != 0 &&
        (instruction_index < 0 || instruction_index >= (int64_t)interp->instruction_count ||
         interp->instructions[instruction_index].opcode != QA_QVM_OP_ENTER))
      qa_range_error("QVM private invocation requires an original function entry");
  }
  for (size_t i = 0; i < arg_count; i++) (void)signed_word(args[i]);
  if (source == NULL && interp->registration != NULL)
    qa_qvm_vm_registration_print_call(interp->registration,
                                      arg_count > 0 ? (int64_t)args[0] : 0);
  qa_qvm_execution_profile profile = {"release", 0, 0};
  if (interp->registration != NULL)
    profile = qa_qvm_vm_registration_execution_profile(interp->registration);
  bool debug = profile.kind != NULL && strcmp(profile.kind, "debug") == 0;
  bool previous_debug = interp->debug;
  int32_t trace = debug ? profile.trace : 0;
  bool has_evaluation = false;
  evaluation_local evaluation;
  memset(&evaluation, 0, sizeof evaluation);
  const qa_qvm_evaluation_stack *read_only_stack = NULL;
  if (read_only != NULL) {
    int64_t frame_size =
        qualify_evaluation(interp, instruction_index, &read_only->region, "read-only");
    if (read_only->input_count != read_only->region.input_count)
      qa_range_error("Read-only QVM region live-ins differ from its qualified frame");
    for (size_t i = 0; i < read_only->input_count; i++) (void)signed_word(read_only->inputs[i]);
    has_evaluation = true;
    evaluation.region = read_only->region;
    evaluation.inputs = read_only->inputs;
    evaluation.input_count = read_only->input_count;
    evaluation.frame_size = frame_size;
    if (read_only->has_stack) read_only_stack = &read_only->stack;
  } else if (source != NULL && source->has_evaluation) {
    has_evaluation = true;
    evaluation.region = source->eval_region;
    evaluation.inputs = source->eval_inputs;
    evaluation.input_count = source->eval_input_count;
    evaluation.frame_size = source->eval_frame_size;
  }
  int64_t evaluation_floor = -1;
  if (interp->counter != NULL) evaluation_floor = interp->counter->stack_start;
  else if (read_only != NULL) evaluation_floor = evaluation_stack_start(interp, read_only_stack);
  int64_t entry_stack = interp->program_stack;
  int64_t previous_call_level = interp->call_level;
  size_t argument_words = arg_count > 10 ? arg_count : 10;
  int64_t sp = source == NULL ? interp_stack(interp, entry_stack - 8 - (int64_t)argument_words * 4)
                              : source->stack;
  if (evaluation_floor >= 0 && sp < evaluation_floor)
    qa_range_error("QVM evaluation stack would overlap source data");
  interp->debug = debug;
  int64_t evaluation_stack = sp;
  invocation frame;
  memset(&frame, 0, sizeof frame);
  frame.ops = source != NULL ? source->ops : &frame.owned_ops;
  if (source == NULL) frame.owned_ops.debug = debug;
  frame.sp = &sp;
  frame.function_scope = source != NULL ? source
                         : function_scope != NULL ? function_scope
                         : interp->active != NULL ? interp->active->function_scope : NULL;
  frame.previous = interp->active;
  /* compiled semantics return stack */
  int64_t returns[1024];
  size_t return_count = 0;
  if (interp->compiled) {
    returns[return_count++] = sp;
    returns[return_count++] = source != NULL ? source->return_pc : -1;
  }
  interp->active = &frame;
  double final_result = 0;
  bool finished = false;
  if (source == NULL) {
    write_word(interp, sp, -1);
    write_word(interp, sp + 4, 0);
    for (size_t i = 0; i < argument_words; i++)
      write_word(interp, sp + 8 + (int64_t)i * 4, i < arg_count ? (int32_t)args[i] : 0);
    interp->call_level = 0;
    if (interp->registration != NULL) qa_qvm_vm_registration_debug(interp->registration, 0);
    if (read_only == NULL && interp->counter == NULL) {
      for (size_t i = 0; i < interp->hook_count && !finished; i++) {
        hook_entry *entry = &interp->hooks[i];
        if (entry->active && entry->invocations && entry->index == instruction_index) {
          interp->program_stack = sp - 4;
          final_result = intercept(interp, &frame, sp, -1, instruction_index, &entry->hook, true,
                                   (int64_t)argument_words * 4);
          finished = true;
        }
      }
    }
  }
  int64_t pc = finished ? 0 : target_pc(interp, (double)instruction_index);
  bool evaluation_started = false;
  qa_qvm_function_symbol profile_symbol = {0, NULL, NULL, 0};
  if (debug) profile_symbol = qa_qvm_symbols_value_to_function_symbol(&interp->symbols, 0);
  operands *ops = frame.ops;
  while (!finished) {
    counter_scope *counter = interp->counter;
    if (counter != NULL) {
      bool in_range = false;
      for (size_t i = 0; i < counter->function_count; i++)
        in_range = in_range || (pc >= counter->range_start[i] && pc < counter->range_end[i]);
      if (--counter->remaining < 0 || !in_range)
        qa_range_error("QVM counter evaluation escaped its admitted original functions or instruction budget");
    }
    source_call *owner = counter == NULL ? frame.function_scope : NULL;
    if (has_evaluation && sp == evaluation_stack - evaluation.frame_size &&
        pc == target_pc(interp, evaluation.region.join)) {
      if (ops->depth != (source != NULL ? source->operand_depth : 0))
        qa_range_error("QVM region evaluation lost its caller operands");
      final_result = !evaluation.region.has_result
                         ? 0
                         : read_word(interp, sp + (int64_t)evaluation.region.result);
      finished = true;
      break;
    }
    if (owner != NULL && owner->regions != NULL) {
      for (size_t i = 0; i < owner->region_count; i++) {
        source_region *region = &owner->regions[i];
        if (region->join_pc == pc && sp == region->stack && region->state == 1) {
          region->state = 2;
          (void)run_region(interp, &frame, owner, region, true);
        }
      }
      for (size_t i = 0; i < owner->region_count; i++) {
        source_region *region = &owner->regions[i];
        if (region->entry_pc == pc && sp == region->stack) {
          if (region->state != 0) qa_range_error("QVM region can execute only once per invocation");
          region->state = 1;
          double execute_region = run_region(interp, &frame, owner, region, false);
          if (execute_region == 0) {
            region->state = 2;
            pc = region->join_pc;
          }
        }
      }
      if (owner->regions != NULL && owner->region_count > 0) {
        /* re-evaluate pc after a possible skip */
      }
    }
    if (interp->debug) {
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
        if (interp->debug) qa_common_error(QA_COMMON_ERROR_DROP, "Bad VM instruction");
        break;
      case QA_QVM_OP_BREAK:
        if (counter != NULL) qa_range_error("QVM counter evaluation cannot trigger a debug break");
        interp->breaks = (int32_t)((uint32_t)interp->breaks + 1);
        break;
      case QA_QVM_OP_CONST: operands_push(ops, code_word(interp, pc)); pc += 4; break;
      case QA_QVM_OP_LOCAL:
        operands_push(ops, (int32_t)(sp + code_word(interp, pc)));
        pc += 4;
        break;
      case QA_QVM_OP_PUSH: operands_reserve(ops); break;
      case QA_QVM_OP_POP: operands_drop(ops); break;
      case QA_QVM_OP_ENTER: {
        if (debug)
          profile_symbol = qa_qvm_symbols_value_to_function_symbol(&interp->symbols, (int32_t)pc);
        int32_t size = code_word(interp, pc);
        if (evaluation_floor >= 0 && (size < 0 || size % 4 != 0 || sp - size < evaluation_floor))
          qa_range_error("QVM evaluation stack would overlap source data");
        sp = interp_stack(interp, sp - size);
        pc += 4;
        if (has_evaluation && !evaluation_started &&
            pc - 5 == target_pc(interp, (double)instruction_index)) {
          evaluation_started = true;
          for (size_t i = 0; i < evaluation.region.input_count; i++) {
            if (i >= evaluation.input_count) qa_range_error("Missing QVM region live-in");
            write_word(interp, sp + (int64_t)evaluation.region.inputs[i],
                       (int32_t)evaluation.inputs[i]);
          }
          pc = target_pc(interp, evaluation.region.entry);
        }
        if (interp->debug) {
          write_word(interp, sp + 4, (int32_t)(sp + size));
          if (trace != 0) {
            char symbol[1024], tail[1100];
            qa_qvm_symbols_value_to_symbol(&interp->symbols, (int32_t)(pc - 5), symbol,
                                           sizeof symbol);
            snprintf(tail, sizeof tail, "---> %s\n", symbol);
            debug_print(interp, ops->depth, tail);
            if (profile.break_function != 0 && pc - 5 == profile.break_function)
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
          if (return_count == 0) qa_range_error("QVM function returned with an invalid program stack");
          target = returns[--return_count];
          if (return_count == 0 || returns[--return_count] != sp)
            qa_range_error("QVM function returned with an invalid program stack");
        }
        if (debug) {
          profile_symbol =
              qa_qvm_symbols_value_to_function_symbol(&interp->symbols, (int32_t)target);
          if (trace != 0) {
            char symbol[1024], tail[1100];
            interp->call_level--;
            qa_qvm_symbols_value_to_symbol(&interp->symbols, (int32_t)target, symbol,
                                           sizeof symbol);
            snprintf(tail, sizeof tail, "<--- %s\n", symbol);
            debug_print(interp, ops->depth, tail);
          }
        }
        if (source != NULL && sp == source->stack && (!interp->compiled || return_count == 0)) {
          if (target != source->return_pc || ops->depth != source->operand_depth + 1)
            qa_range_error("QVM function returned with an invalid caller stack");
          final_result = operands_pop(ops);
          finished = true;
          break;
        }
        if (target == -1) {
          final_result = operands_result(ops);
          finished = true;
          break;
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
          const qa_qvm_function_hook *hook = NULL;
          bool has_hook = false, has_observers = false, has_binding = false;
          if (counter == NULL) {
            for (size_t i = 0; i < interp->hook_count; i++)
              if (interp->hooks[i].active && interp->hooks[i].index == target) {
                hook = &interp->hooks[i].hook;
                has_hook = true;
                has_binding = true;
              }
            for (size_t i = 0; i < interp->observer_count; i++)
              has_observers = has_observers ||
                              (interp->observers[i].active && interp->observers[i].index == target);
            if (!has_hook && interp->has_resolver) {
              qa_qvm_function_hook resolved;
              qa_qvmx_view words = {interp->memory + sp + 8,
                                    (size_t)source_argument_bytes(interp, source_instruction(interp, return_pc - 1))};
              if (interp->resolver.resolve(interp->resolver.ctx, target, read_word(interp, sp + 8),
                                           &words, &resolved)) {
                /* resolved hook is used immediately below via a copy */
                if (code_word(interp, target_pc(interp, target)) != QA_QVM_OP_ENTER)
                  qa_range_error("QVM resolver requires a function entry instruction");
                int64_t saved_level = interp->call_level, saved_stack = interp->program_stack;
                interp->program_stack = sp - 4;
                double value = intercept(interp, &frame, sp, return_pc, target, &resolved, true, -1);
                interp->call_level = saved_level;
                interp->program_stack = saved_stack;
                operands_push(ops, (int32_t)value);
                pc = !interp->compiled ? read_word(interp, sp) : return_pc;
                break;
              }
            }
          }
          (void)has_binding;
          if (!has_hook && !has_observers) {
            if (interp->compiled) {
              if (return_count + 2 > sizeof returns / sizeof returns[0])
                qa_range_error("QVM function returned with an invalid program stack");
              returns[return_count++] = sp;
              returns[return_count++] = return_pc;
            }
            pc = target_pc(interp, target);
          } else {
            int64_t saved_level = interp->call_level, saved_stack = interp->program_stack;
            interp->program_stack = sp - 4;
            double value = intercept(interp, &frame, sp, return_pc, target, hook, has_hook, -1);
            interp->call_level = saved_level;
            interp->program_stack = saved_stack;
            operands_push(ops, (int32_t)value);
            pc = !interp->compiled ? read_word(interp, sp) : return_pc;
          }
        } else {
          if (counter != NULL) qa_range_error("QVM counter evaluation cannot call an engine service");
          if (trace != 0) {
            char tail[64];
            snprintf(tail, sizeof tail, "---> systemcall(%lld)\n", (long long)(-1 - target));
            debug_print(interp, ops->depth, tail);
          }
          int64_t saved_level = interp->call_level;
          interp->program_stack = sp - 4;
          int32_t saved_frame = interp->debug ? read_word(interp, sp + 4) : 0;
          write_word(interp, sp + 4, -1 - target);
          interp_range(interp, sp + 4, 4);
          host_ctx host = {interp, &frame, frame.function_scope};
          qa_qvm_syscall call;
          memset(&call, 0, sizeof call);
          call.words.bytes = interp->memory + sp + 4;
          call.words.length = interp->memory_length - (size_t)(sp + 4);
          call.memory = interp->memory;
          call.memory_length = interp->memory_length;
          call.guest = &interp->address_space;
          call.ctx = &host;
          call.invoke = host_invoke;
          call.invoke_async = host_invoke_async;
          call.cancel_function = host_cancel;
          double value = interp->system_call.call(interp->system_call.ctx, &call);
          value = signed_word(value);
          check_cancellation(frame.function_scope);
          assert_live(interp);
          if (interp->debug) write_word(interp, sp + 4, saved_frame);
          operands_push(ops, (int32_t)value);
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
        qa_qvmx_view view = {interp->memory, interp->memory_length};
        operands_set(ops, qa_qvmx_view_u16(&view, address));
        break;
      }
      case QA_QVM_OP_LOAD4:
        if (interp->debug && (operands_peek(ops) & 3) != 0)
          qa_common_error(QA_COMMON_ERROR_DROP, "OP_LOAD4 misaligned");
        operands_set(ops, read_word(interp, operands_peek(ops) & interp->data_mask));
        break;
      case QA_QVM_OP_STORE1: {
        int32_t value = operands_pop(ops);
        int64_t address = operands_pop(ops) & interp->data_mask;
        if (counter != NULL) (void)counter_write(interp, address, 1, value);
        interp->memory[address] = (uint8_t)value;
        break;
      }
      case QA_QVM_OP_STORE2: {
        int32_t value = operands_pop(ops);
        int64_t address = operands_pop(ops) & (interp->data_mask & ~1LL);
        interp_range(interp, address, 2);
        if (counter != NULL) (void)counter_write(interp, address, 2, value);
        qa_qvmx_view view = {interp->memory, interp->memory_length};
        qa_qvmx_view_set_u16(&view, address, (uint16_t)value);
        break;
      }
      case QA_QVM_OP_STORE4: {
        int32_t value = operands_pop(ops);
        write_word(interp, operands_pop(ops) & (interp->data_mask & ~3LL), value);
        break;
      }
      case QA_QVM_OP_ARG:
        write_word(interp, sp + code_word(interp, pc++), operands_pop(ops));
        break;
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
          qa_qvmx_memory_copy_bytes(&interp->address_space, destination, source_addr, count);
        }
        break;
      }
      case QA_QVM_OP_BCOM:
        if (interp->compiled) operands_set(ops, ~operands_peek(ops));
        else operands_complement_previous(ops);
        break;
      case QA_QVM_OP_SEX8: case QA_QVM_OP_SEX16: case QA_QVM_OP_NEGI:
      case QA_QVM_OP_NEGF: case QA_QVM_OP_CVIF: case QA_QVM_OP_CVFI:
        operands_set(ops, qa_qvmx_evaluate_unary((qa_qvm_opcode)opcode, operands_peek(ops)));
        break;
      case QA_QVM_OP_ADD: case QA_QVM_OP_SUB: case QA_QVM_OP_DIVI: case QA_QVM_OP_DIVU:
      case QA_QVM_OP_MODI: case QA_QVM_OP_MODU: case QA_QVM_OP_MULI: case QA_QVM_OP_MULU:
      case QA_QVM_OP_BAND: case QA_QVM_OP_BOR: case QA_QVM_OP_BXOR:
      case QA_QVM_OP_LSH: case QA_QVM_OP_RSHI: case QA_QVM_OP_RSHU:
      case QA_QVM_OP_ADDF: case QA_QVM_OP_SUBF: case QA_QVM_OP_DIVF: case QA_QVM_OP_MULF: {
        int32_t right = operands_pop(ops);
        operands_set(ops, qa_qvmx_evaluate_binary((qa_qvm_opcode)opcode, operands_peek(ops), right));
        break;
      }
      case QA_QVM_OP_EQ: case QA_QVM_OP_NE: case QA_QVM_OP_LTI: case QA_QVM_OP_LEI:
      case QA_QVM_OP_GTI: case QA_QVM_OP_GEI: case QA_QVM_OP_LTU: case QA_QVM_OP_LEU:
      case QA_QVM_OP_GTU: case QA_QVM_OP_GEU: case QA_QVM_OP_EQF: case QA_QVM_OP_NEF:
      case QA_QVM_OP_LTF: case QA_QVM_OP_LEF: case QA_QVM_OP_GTF: case QA_QVM_OP_GEF: {
        int32_t right = operands_pop(ops);
        int32_t left = operands_pop(ops);
        int32_t target = code_word(interp, pc);
        bool taken = qa_qvmx_evaluate_branch((qa_qvm_opcode)opcode, left, right);
        qa_qvm_branch_binding *binding = NULL;
        if (counter == NULL && frame.function_scope != NULL)
          for (size_t i = 0; i < frame.function_scope->branch_count; i++)
            if (frame.function_scope->branch_pcs[i] == pc - 1)
              binding = &frame.function_scope->branch_bindings[i];
        if (binding == NULL) {
          pc = taken ? target : pc + 4;
        } else {
          source_call *branch_owner = frame.function_scope;
          if (branch_owner == NULL || !branch_owner->active)
            qa_range_error("QVM branch invocation has expired");
          double chosen = decision(interp, &frame, branch_owner, binding, taken);
          check_cancellation(branch_owner);
          if (!branch_owner->active || interp->active != &frame)
            qa_range_error("QVM branch invocation is no longer current");
          pc = chosen != 0 ? target : pc + 4;
        }
        break;
      }
      default:
        if (interp->debug) qa_common_error(QA_COMMON_ERROR_DROP, "Bad VM instruction");
        break;
    }
  }
  interp->program_stack = entry_stack;
  interp->active = frame.previous;
  if (frame.previous != NULL) {
    interp->debug = previous_debug;
    interp->call_level = previous_call_level;
  }
  return final_result;
}

/* ---------- public QvmInterpreter API ---------- */

static uint8_t *allocate_bytes(qa_qvm_interpreter *interp, const qa_qvm_allocation_profile *profile,
                               const char *purpose, size_t bytes, const char *resource,
                               bool *owned) {
  (void)interp;
  if (profile == NULL || !profile->accounted) {
    *owned = true;
    return calloc(bytes == 0 ? 1 : bytes, 1);
  }
  qa_qvm_allocation_request request = {purpose, resource, (double)bytes};
  qa_qvm_allocation allocation = profile->allocate(profile->ctx, request);
  *owned = false;
  /* The allocator's storage is the live allocation, as in the TS. */
  return (uint8_t *)allocation.bytes;
}

static uint8_t *symbols_allocate(void *ctx, size_t bytes, const char *resource) {
  qa_qvm_interpreter *interp = ctx;
  bool owned = true;
  return allocate_bytes(interp, interp->allocation_profile, "VM_LoadSymbols", bytes,
                        resource != NULL ? resource : interp->source, &owned);
}

qa_qvm_interpreter *qa_qvm_interpreter_create(const qa_qvm_image *image,
                                              qa_qvm_interpreter_system_call system_call,
                                              const qa_qvm_allocation_profile *profile,
                                              const qa_qvm_vm_registration *registration,
                                              qa_qvm_semantics semantics,
                                              const qa_qvm_store_effect *store_effect) {
  qa_qvm_interpreter *interp = calloc(1, sizeof *interp);
  if (interp == NULL) return NULL;
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
  bool owned = true;
  interp->memory = allocate_bytes(interp, profile, "VM_Create:dataBase",
                                  (size_t)image->allocated_data_length, image->source, &owned);
  if (interp->memory == NULL) { free(interp->source); free(interp); return NULL; }
  interp->memory_owned = owned;
  interp->memory_length = (size_t)image->allocated_data_length;
  if (registration != NULL)
    qa_qvm_vm_registration_bind_data(registration, interp->memory_length);
  interp->address_space.bytes = interp->memory;
  interp->address_space.length = interp->memory_length;
  /* QvmMemory construction validates the allocation shape. */
  size_t length = interp->memory_length;
  if (length == 0 || length > 0x40000000u || (length & (length - 1)) != 0)
    qa_range_error("QVM memory allocation must be a nonzero power of two at most 2^30 bytes");
  memcpy(interp->memory, image->initialized_data, image->initialized_length);
  interp->data_mask = image->data_mask;
  interp->program_stack = (int64_t)interp->memory_length;
  if (registration != NULL)
    qa_qvm_vm_registration_bind_instruction_pointers_length(
        registration, (int64_t)image->instruction_count * 4);
  bool pointers_owned = true;
  uint8_t *pointers = allocate_bytes(interp, profile, "VM_Create:instructionPointers",
                                     image->instruction_count * 4, image->source, &pointers_owned);
  if (pointers == NULL) { qa_qvm_interpreter_destroy(interp); return NULL; }
  (void)pointers_owned;
  interp->instruction_pointers = (int32_t *)pointers;
  if (registration != NULL)
    qa_qvm_vm_registration_bind_code_length(registration, (int64_t)image->code_length);
  bool code_owned = true;
  uint8_t *code = allocate_bytes(interp, profile, "VM_PrepareInterpreter",
                                 (size_t)image->code_length * 4, image->source, &code_owned);
  if (code == NULL) { qa_qvm_interpreter_destroy(interp); return NULL; }
  (void)code_owned;
  interp->code = (int32_t *)code;
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
  qa_qvm_symbols_destroy(&interp->symbols);
  if (interp->memory_owned) free(interp->memory);
  free(interp->instruction_pointers);
  free(interp->code);
  free(interp->source);
  free(interp->hooks);
  free(interp->observers);
  free(interp->cancel_targets);
  free(interp);
}

uint8_t *qa_qvm_interpreter_get_memory(const qa_qvm_interpreter *interp, size_t *out_length) {
  if (out_length != NULL) *out_length = interp->memory_length;
  return interp->memory;
}
qa_qvmx_memory *qa_qvm_interpreter_get_address_space(qa_qvm_interpreter *interp) {
  return &interp->address_space;
}
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
double qa_qvm_interpreter_get_code_length(qa_qvm_interpreter *interp) {
  assert_live(interp);
  return (double)interp->code_length;
}
double qa_qvm_interpreter_get_instruction_pointers_length(qa_qvm_interpreter *interp) {
  assert_live(interp);
  return (double)interp->instruction_count * 4;
}

typedef struct unbind_hook_ctx { qa_qvm_interpreter *interp; hook_entry *entry; } unbind_hook_ctx;
static void unbind_hook(void *ctx) {
  unbind_hook_ctx *u = ctx;
  u->entry->active = false;
  free(u);
}
typedef struct unbind_observer_ctx { qa_qvm_interpreter *interp; observer_entry *entry; } unbind_observer_ctx;
static void unbind_observer(void *ctx) {
  unbind_observer_ctx *u = ctx;
  u->entry->active = false;
  free(u);
}
typedef struct unbind_resolver_ctx { qa_qvm_interpreter *interp; } unbind_resolver_ctx;
static void unbind_resolver(void *ctx) {
  unbind_resolver_ctx *u = ctx;
  u->interp->has_resolver = false;
  free(u);
}

static qa_qvm_unbind bind_entry(qa_qvm_interpreter *interp, double instruction_index,
                                qa_qvm_function_hook hook, bool invocations) {
  assert_live(interp);
  if (!is_safe_integer(instruction_index) || instruction_index < 0 ||
      code_word(interp, target_pc(interp, instruction_index)) != QA_QVM_OP_ENTER)
    qa_range_error("QVM hook requires a function entry instruction");
  for (size_t i = 0; i < interp->hook_count; i++)
    if (interp->hooks[i].active && interp->hooks[i].index == (int64_t)instruction_index)
      qa_range_error("QVM function already has a hook");
  if (interp->hook_count == interp->hook_capacity) {
    size_t capacity = interp->hook_capacity == 0 ? 8 : interp->hook_capacity * 2;
    hook_entry *grown = realloc(interp->hooks, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    interp->hooks = grown;
    interp->hook_capacity = capacity;
  }
  hook_entry *entry = &interp->hooks[interp->hook_count++];
  entry->index = (int64_t)instruction_index;
  entry->hook = hook;
  entry->invocations = invocations;
  entry->active = true;
  unbind_hook_ctx *u = malloc(sizeof *u);
  if (u == NULL) qa_range_error("out of memory");
  u->interp = interp;
  u->entry = entry;
  qa_qvm_unbind result = {u, unbind_hook};
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
  if (interp->has_resolver) qa_range_error("QVM already has a function resolver");
  interp->resolver = resolver;
  interp->has_resolver = true;
  unbind_resolver_ctx *u = malloc(sizeof *u);
  if (u == NULL) qa_range_error("out of memory");
  u->interp = interp;
  qa_qvm_unbind result = {u, unbind_resolver};
  return result;
}
qa_qvm_unbind qa_qvm_interpreter_observe_function(qa_qvm_interpreter *interp,
                                                  double instruction_index,
                                                  qa_qvm_function_observer observer) {
  assert_live(interp);
  if (!is_safe_integer(instruction_index) || instruction_index < 0 ||
      code_word(interp, target_pc(interp, instruction_index)) != QA_QVM_OP_ENTER)
    qa_range_error("QVM observer requires a function entry instruction");
  if (interp->observer_count == interp->observer_capacity) {
    size_t capacity = interp->observer_capacity == 0 ? 8 : interp->observer_capacity * 2;
    observer_entry *grown = realloc(interp->observers, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    interp->observers = grown;
    interp->observer_capacity = capacity;
  }
  observer_entry *entry = &interp->observers[interp->observer_count++];
  entry->index = (int64_t)instruction_index;
  entry->observer = observer;
  entry->active = true;
  unbind_observer_ctx *u = malloc(sizeof *u);
  if (u == NULL) qa_range_error("out of memory");
  u->interp = interp;
  u->entry = entry;
  qa_qvm_unbind result = {u, unbind_observer};
  return result;
}

void qa_qvm_interpreter_restore_data(qa_qvm_interpreter *interp, const uint8_t *data,
                                     size_t length) {
  if (interp->root_active) qa_range_error("Cannot restore an active QVM");
  assert_live(interp);
  if (length != interp->memory_length) qa_range_error("QVM checkpoint allocation mismatch");
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
  memset(interp->memory, 0, (size_t)image->allocated_data_length);
  memcpy(interp->memory, image->initialized_data, image->initialized_length);
}

double qa_qvm_interpreter_invoke(qa_qvm_interpreter *interp, const double *args,
                                 size_t arg_count, double instruction_index,
                                 const qa_qvm_read_only_evaluation *evaluation) {
  if (interp->root_active)
    qa_range_error("QVM is already active; recursive calls belong to the current syscall");
  assert_live(interp);
  interp->root_active = true;
  double result = execute(interp, args, arg_count, (int64_t)instruction_index, NULL, NULL, evaluation);
  interp->root_active = false;
  return result;
}

double qa_qvm_interpreter_invoke_async(qa_qvm_interpreter *interp, const double *args,
                                       size_t arg_count, double instruction_index) {
  return qa_qvm_interpreter_invoke(interp, args, arg_count, instruction_index, NULL);
}

double qa_qvm_interpreter_evaluate_counter(qa_qvm_interpreter *interp, double address,
                                           double initial, const double *functions,
                                           size_t function_count,
                                           double (*execute_fn)(void *ctx), void *execute_ctx,
                                           const qa_qvm_evaluation_stack *stack) {
  assert_live(interp);
  interp_range(interp, (int64_t)address, 4);
  (void)signed_word(initial);
  if ((int64_t)address % 4 != 0 || address + 4 > (double)interp->source_data_end ||
      interp->counter != NULL)
    qa_range_error("QVM counter evaluation requires one live aligned source word and a fresh scope");
  if (function_count == 0) qa_range_error("QVM counter evaluation requires distinct original functions");
  for (size_t i = 0; i < function_count; i++)
    for (size_t j = i + 1; j < function_count; j++)
      if (functions[i] == functions[j])
        qa_range_error("QVM counter evaluation requires distinct original functions");
  counter_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.range_start = malloc(function_count * sizeof(int64_t));
  scope.range_end = malloc(function_count * sizeof(int64_t));
  if (scope.range_start == NULL || scope.range_end == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < function_count; i++) {
    double entry = functions[i];
    if (!is_integer(entry) || entry < 0 || entry >= (double)interp->instruction_count ||
        interp->instructions[(size_t)entry].opcode != QA_QVM_OP_ENTER)
      qa_range_error("QVM counter evaluation entry is not an original function");
    int64_t end = (int64_t)entry + 1;
    while (end < (int64_t)interp->instruction_count &&
           interp->instructions[end].opcode != QA_QVM_OP_ENTER)
      end++;
    scope.range_start[i] = target_pc(interp, entry);
    scope.range_end[i] = end == (int64_t)interp->instruction_count
                             ? (int64_t)interp->code_length
                             : target_pc(interp, (double)end);
  }
  int64_t stack_start = evaluation_stack_start(interp, stack);
  if (address + 4 > (double)stack_start) qa_range_error("QVM counter word overlaps its evaluation stack");
  scope.address = (int64_t)address;
  scope.value = (int32_t)initial;
  scope.stack_start = stack_start;
  scope.stack_end = interp->program_stack;
  scope.functions = functions;
  scope.function_count = function_count;
  scope.remaining = 100000;
  interp->counter = &scope;
  (void)execute_fn(execute_ctx);
  interp->counter = NULL;
  double result = scope.value;
  free(scope.range_start);
  free(scope.range_end);
  return result;
}

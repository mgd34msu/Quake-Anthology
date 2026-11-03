/*
 * Port of src/guest/x64/integer-kernel.ts: the 32-bit-word integer
 * fast path over prepared semantic plans. Register words are loaded/
 * stored little-endian from the register-file bytes (the TS
 * Uint32Array view requires a little-endian host; the C loads give
 * the same values on any host). X64IntegerKernel.create's TS
 * prototype-identity checks have no C counterpart (types are
 * concrete); init fails only for a non-x86-64 state. Faults ride the
 * contracts.h throw channel; executeBlock catches them into its
 * result like the TS try/catch. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X64_INTEGER_KERNEL_H
#define QA_GUEST_X64_INTEGER_KERNEL_H

#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/x64/plan.h"

typedef struct qa_x64_kernel_operand {
  bool is_memory;
  int offset; /* register byte offset */
  int width;  /* 8|16|32|64 */
  bool high_byte;
  qa_x64_operand source; /* memory */
} qa_x64_kernel_operand;

typedef struct qa_x64_kernel_source {
  bool is_immediate;
  uint32_t low, high; /* immediate */
  qa_x64_kernel_operand operand;
} qa_x64_kernel_source;

typedef struct qa_x64_kernel_word_source {
  bool has_index; /* false = TS index null (immediate) */
  int index;
  uint32_t low, high;
} qa_x64_kernel_word_source;
typedef struct qa_x64_kernel_word_destination { int index; int width; /* 32|64 */ } qa_x64_kernel_word_destination;

typedef struct qa_x64_kernel_effective_address {
  bool has_base; int base;
  bool has_index; int index;
  int shift; /* 0..3 */
  uint32_t low, high;
  int address_bits; /* 32|64 */
} qa_x64_kernel_effective_address;

typedef struct qa_x64_kernel_target {
  bool is_constant;
  uint64_t value;   /* constant */
  bool canonical;   /* constant */
  qa_x64_kernel_operand operand;
} qa_x64_kernel_target;

typedef enum qa_x64_kernel_op_kind {
  QA_KERNEL_OP_NOP = 0, QA_KERNEL_OP_MOVE, QA_KERNEL_OP_REGISTER_MOVE, QA_KERNEL_OP_REGISTER_ALU,
  QA_KERNEL_OP_EXTEND, QA_KERNEL_OP_INCREMENT, QA_KERNEL_OP_CONDITIONAL_MOVE, QA_KERNEL_OP_SET_CONDITION,
  QA_KERNEL_OP_LEA, QA_KERNEL_OP_ALU, QA_KERNEL_OP_BRANCH, QA_KERNEL_OP_JUMP, QA_KERNEL_OP_CALL,
  QA_KERNEL_OP_PUSH, QA_KERNEL_OP_POP, QA_KERNEL_OP_RETURN,
  QA_KERNEL_OP_PASSTHROUGH /* raw-sse/numeric/numeric-memory/shift/multiply: original plan */
} qa_x64_kernel_op_kind;
const char *qa_x64_kernel_op_kind_name(qa_x64_kernel_op_kind kind);

typedef struct qa_x64_integer_operation {
  qa_x64_kernel_op_kind kind;
  qa_x64_kernel_operand destination;
  qa_x64_kernel_source source;          /* move/alu/push */
  qa_x64_kernel_operand source_operand; /* extend/conditional-move */
  qa_x64_kernel_word_destination word_destination;
  qa_x64_kernel_word_source word_source;
  qa_alu_operation alu_operation;
  bool is_signed;  /* extend */
  bool subtract;   /* increment */
  int condition;
  bool has_condition; /* branch: false = TS null */
  qa_x64_kernel_effective_address lea_source;
  qa_x64_kernel_target target; /* branch (constant) / jump / call */
  int width;                   /* push/pop: 16|64 */
  uint64_t discard;            /* return */
} qa_x64_integer_operation;

typedef struct qa_x64_integer_plan {
  qa_x64_integer_operation operation;
  const qa_x64_semantic_plan *original; /* borrowed */
} qa_x64_integer_plan;

typedef struct qa_x64_integer_step {
  uint64_t start;
  const qa_x64_integer_plan *integer;
  bool safe;
} qa_x64_integer_step;

typedef enum qa_x64_integer_block_kind {
  QA_KERNEL_BLOCK_COMPLETE = 0, QA_KERNEL_BLOCK_RETURN, QA_KERNEL_BLOCK_FAULT
} qa_x64_integer_block_kind;
typedef struct qa_x64_integer_block_result {
  qa_x64_integer_block_kind kind;
  int64_t instructions;
  qa_guest_throw fault; /* FAULT: snapshot of the thrown fault */
} qa_x64_integer_block_result;

bool qa_x64_integer_block_safe(const qa_x64_integer_plan *plan);
/* False when the TS returns null. */
bool qa_prepare_x64_integer_plan(qa_x64_integer_plan *out, const qa_x64_semantic_plan *plan);

typedef struct qa_x64_integer_kernel {
  qa_guest_processor_state *state;
  qa_sparse_guest_memory *memory;
  /* private */
  uint32_t low, high;
  uint8_t checkpoint[128];
  uint32_t memory_low, memory_high;
  /* Runs passthrough plans; NULL uses qa_execute_x64_plan. A CPU installs its
   * own so numeric and LOCK faults ride the throw channel. */
  qa_x64_flow (*execute_plan)(void *ctx, const qa_x64_semantic_plan *plan);
  void *execute_plan_ctx;
} qa_x64_integer_kernel;

/* False when the TS create() returns null (non-x86-64 state). */
bool qa_x64_integer_kernel_init(qa_x64_integer_kernel *kernel, qa_guest_processor_state *state,
                                qa_sparse_guest_memory *memory);
qa_x64_flow qa_x64_integer_kernel_execute(qa_x64_integer_kernel *kernel, const qa_x64_integer_plan *plan);
qa_x64_integer_block_result qa_x64_integer_kernel_execute_block(
    qa_x64_integer_kernel *kernel, const qa_x64_integer_step *steps, size_t step_count, int64_t budget,
    bool has_returned, uint64_t returned, bool (*after_store)(void *ctx), void *after_store_ctx);

#endif

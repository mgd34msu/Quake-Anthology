/*
 * Port of src/guest/x64/plan.ts: semantic x64 plan construction/execution.
 * X64Operand shapes and the operand/address helpers come from
 * src/guest/x64/decoder.ts (unported) and are mirrored here/in plan.c.
 * TS X64ProcessorFault/X64Unsupported throws are caught by the CPU runner
 * (unported); here they abort with the same detail via qa_range_error
 * (partial: no exception result channel yet). TS bigint operand values are
 * uint64_t bit patterns. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X64_PLAN_H
#define QA_GUEST_X64_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/guest/core/registers.h"
#include "qa/guest/floating_point/binary.h"
#include "qa/guest/floating_point/raw_sse.h"
#include "qa/guest/x86/arithmetic.h"

typedef enum qa_x64_operand_kind { QA_X64_OPERAND_REGISTER = 0, QA_X64_OPERAND_MEMORY } qa_x64_operand_kind;
typedef enum qa_x64_segment_override { QA_X64_SEG_NONE = 0, QA_X64_SEG_FS, QA_X64_SEG_GS } qa_x64_segment_override;

typedef struct qa_x64_operand {
  qa_x64_operand_kind kind;
  int width; /* 8|16|32|64 */
  /* register */
  qa_guest_register reg;
  bool high_byte;
  /* memory */
  bool has_base; qa_guest_register base;
  bool has_index; qa_guest_register index;
  uint64_t scale; /* TS bigint, small positive at decode sites */
  int64_t displacement;
  bool rip_relative;
  int address_bits; /* 32|64 */
  qa_x64_segment_override segment;
} qa_x64_operand;

/* X64PlanSource = X64Operand | bigint. */
typedef struct qa_x64_plan_source {
  bool is_immediate;
  uint64_t immediate;
  qa_x64_operand operand;
} qa_x64_plan_source;

typedef enum qa_x64_plan_kind {
  QA_X64_PLAN_NOP = 0, QA_X64_PLAN_MOVE, QA_X64_PLAN_EXTEND, QA_X64_PLAN_INCREMENT, QA_X64_PLAN_SHIFT,
  QA_X64_PLAN_MULTIPLY, QA_X64_PLAN_CONDITIONAL_MOVE, QA_X64_PLAN_SET_CONDITION, QA_X64_PLAN_LEA,
  QA_X64_PLAN_ALU, QA_X64_PLAN_BRANCH, QA_X64_PLAN_JUMP, QA_X64_PLAN_CALL, QA_X64_PLAN_PUSH, QA_X64_PLAN_POP,
  QA_X64_PLAN_RETURN, QA_X64_PLAN_NUMERIC, QA_X64_PLAN_NUMERIC_MEMORY, QA_X64_PLAN_RAW_SSE
} qa_x64_plan_kind;
const char *qa_x64_plan_kind_name(qa_x64_plan_kind kind);

typedef struct qa_x64_plan_operation {
  qa_x64_plan_kind kind;
  qa_x64_operand destination;
  qa_x64_plan_source source;      /* move/shift-target value/push/alu/jump target */
  qa_x64_operand source_operand;  /* extend/conditional-move/lea source, numeric-memory operand */
  qa_x64_operand left;            /* multiply */
  qa_x64_plan_source right;       /* multiply */
  qa_alu_operation alu_operation;
  qa_shift_operation shift_operation;
  int shift_count;                /* shift: count or -1 for TS "cl" */
  bool shift_count_is_cl;
  bool is_signed;                 /* extend */
  bool subtract;                  /* increment */
  int condition;                  /* conditional-move/set-condition/branch */
  bool has_condition;             /* branch: false = TS null */
  int64_t displacement;           /* branch */
  int width;                      /* push/pop: 16|64 */
  uint64_t discard;               /* return */
  qa_numeric_instruction instruction;        /* numeric / numeric-memory base */
  qa_raw_sse_operation raw_operation;        /* raw-sse */
  qa_numeric_operand raw_register_operand;   /* raw-sse register operand */
  bool raw_operand_is_register;
} qa_x64_plan_operation;

typedef struct qa_x64_semantic_plan {
  qa_x64_plan_operation operation;
  uint64_t next_ip;
  bool lock;
  bool ends_block;
} qa_x64_semantic_plan;

typedef enum qa_x64_flow_kind { QA_X64_ADVANCE = 0, QA_X64_BRANCH, QA_X64_HALT, QA_X64_TRAP } qa_x64_flow_kind;
const char *qa_x64_flow_kind_name(qa_x64_flow_kind kind);
typedef struct qa_x64_flow { qa_x64_flow_kind kind; uint64_t target; int vector; } qa_x64_flow;

extern const qa_x64_flow qa_x64_advance;

/* TS x64Lock(): aborts (X64ProcessorFault(6, ...)) on misuse. */
void qa_x64_lock(bool lock, const qa_x64_operand *destination, bool has_destination, bool permitted);
void qa_make_x64_plan(qa_x64_semantic_plan *out, const qa_x64_plan_operation *operation, uint64_t next_ip, bool lock);
qa_x64_flow qa_execute_x64_plan(const qa_x64_semantic_plan *plan, qa_mapped_guest_memory *memory,
                                qa_guest_processor_state *state);

#endif

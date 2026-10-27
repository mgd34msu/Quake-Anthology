/*
 * Port of src/guest/floating-point/raw-sse.ts: raw SSE moves/logic.
 * SparseGuestMemory.managed() (src/guest/core/memory.ts, unported) is the
 * memory's managed_sparse flag; its read/writeUint64Words fast paths map to
 * qa_memory_read/write_u64_words. The TS WeakMap scratch operand lives in
 * qa_guest_simd_state.sse_scratch. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_RAW_SSE_H
#define QA_GUEST_FLOATING_POINT_RAW_SSE_H

#include "qa/guest/floating_point/binary.h"

typedef enum qa_raw_sse_kind { QA_RAW_SSE_MOVE = 0, QA_RAW_SSE_LOGIC } qa_raw_sse_kind;
typedef enum qa_raw_sse_logic_op { QA_SSE_AND = 0, QA_SSE_AND_NOT, QA_SSE_OR, QA_SSE_XOR } qa_raw_sse_logic_op;
const char *qa_raw_sse_logic_op_name(qa_raw_sse_logic_op op);

typedef struct qa_raw_sse_operation {
  qa_raw_sse_kind kind;
  int register_index;
  int size;    /* move: 4|8|16 */
  bool store;  /* move */
  bool aligned; /* move */
  qa_raw_sse_logic_op operation; /* logic */
} qa_raw_sse_operation;

/* TS prepareRawSse(): false when it returns null. */
bool qa_prepare_raw_sse(qa_raw_sse_operation *out, int opcode, qa_numeric_prefix prefix, int register_index);
void qa_execute_raw_sse(qa_numeric_result *out, const qa_raw_sse_operation *operation,
                        const qa_numeric_operand *operand, qa_guest_processor_state *state,
                        qa_mapped_guest_memory *memory);

#endif

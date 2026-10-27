/*
 * Port of src/compat/qvm/operations.ts. Operands and results are
 * signed int32 words, including binary32 payloads.
 * Mirrored type (TS owner src/compat/qvm/image.ts, parallel slice):
 * qa_qvmy_op_opcode mirrors QvmOpcode with its exact numeric values.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_OPERATIONS_H
#define QA_COMPAT_QVM_OPERATIONS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum qa_qvmy_op_opcode {
  QA_QVMY_OP_UNDEF, QA_QVMY_OP_IGNORE, QA_QVMY_OP_BREAK,
  QA_QVMY_OP_ENTER, QA_QVMY_OP_LEAVE, QA_QVMY_OP_CALL, QA_QVMY_OP_PUSH, QA_QVMY_OP_POP,
  QA_QVMY_OP_CONST, QA_QVMY_OP_LOCAL, QA_QVMY_OP_JUMP,
  QA_QVMY_OP_EQ, QA_QVMY_OP_NE, QA_QVMY_OP_LTI, QA_QVMY_OP_LEI, QA_QVMY_OP_GTI, QA_QVMY_OP_GEI,
  QA_QVMY_OP_LTU, QA_QVMY_OP_LEU, QA_QVMY_OP_GTU, QA_QVMY_OP_GEU,
  QA_QVMY_OP_EQF, QA_QVMY_OP_NEF, QA_QVMY_OP_LTF, QA_QVMY_OP_LEF, QA_QVMY_OP_GTF, QA_QVMY_OP_GEF,
  QA_QVMY_OP_LOAD1, QA_QVMY_OP_LOAD2, QA_QVMY_OP_LOAD4,
  QA_QVMY_OP_STORE1, QA_QVMY_OP_STORE2, QA_QVMY_OP_STORE4,
  QA_QVMY_OP_ARG, QA_QVMY_OP_BLOCK_COPY, QA_QVMY_OP_SEX8, QA_QVMY_OP_SEX16,
  QA_QVMY_OP_NEGI, QA_QVMY_OP_ADD, QA_QVMY_OP_SUB, QA_QVMY_OP_DIVI, QA_QVMY_OP_DIVU,
  QA_QVMY_OP_MODI, QA_QVMY_OP_MODU, QA_QVMY_OP_MULI, QA_QVMY_OP_MULU,
  QA_QVMY_OP_BAND, QA_QVMY_OP_BOR, QA_QVMY_OP_BXOR, QA_QVMY_OP_BCOM,
  QA_QVMY_OP_LSH, QA_QVMY_OP_RSHI, QA_QVMY_OP_RSHU, QA_QVMY_OP_NEGF,
  QA_QVMY_OP_ADDF, QA_QVMY_OP_SUBF, QA_QVMY_OP_DIVF, QA_QVMY_OP_MULF,
  QA_QVMY_OP_CVIF, QA_QVMY_OP_CVFI,
} qa_qvmy_op_opcode;

/* TS QvmUnaryOpcode / QvmBinaryOpcode / QvmBranchOpcode narrow the
 * enum at compile time only; unsupported values abort like the TS
 * exhaustiveness throw. */
int32_t qa_qvm_evaluate_unary(qa_qvmy_op_opcode opcode, int32_t word);
int32_t qa_qvm_evaluate_binary(qa_qvmy_op_opcode opcode, int32_t left, int32_t right);
bool qa_qvm_evaluate_branch(qa_qvmy_op_opcode opcode, int32_t left, int32_t right);

#endif

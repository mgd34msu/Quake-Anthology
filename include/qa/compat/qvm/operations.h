/*
 * Port of src/compat/qvm/operations.ts. Operands and results are
 * signed int32 words, including binary32 payloads.
 * Opcodes are the landed qa_qvm_opcode (TS owner
 * src/compat/qvm/image.ts); the QA_QVMY_OP_* spellings are aliases
 * kept for this slice's existing call sites.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_OPERATIONS_H
#define QA_COMPAT_QVM_OPERATIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/image.h"

#define QA_QVMY_OP_UNDEF QA_QVM_OP_UNDEF
#define QA_QVMY_OP_IGNORE QA_QVM_OP_IGNORE
#define QA_QVMY_OP_BREAK QA_QVM_OP_BREAK
#define QA_QVMY_OP_ENTER QA_QVM_OP_ENTER
#define QA_QVMY_OP_LEAVE QA_QVM_OP_LEAVE
#define QA_QVMY_OP_CALL QA_QVM_OP_CALL
#define QA_QVMY_OP_PUSH QA_QVM_OP_PUSH
#define QA_QVMY_OP_POP QA_QVM_OP_POP
#define QA_QVMY_OP_CONST QA_QVM_OP_CONST
#define QA_QVMY_OP_LOCAL QA_QVM_OP_LOCAL
#define QA_QVMY_OP_JUMP QA_QVM_OP_JUMP
#define QA_QVMY_OP_EQ QA_QVM_OP_EQ
#define QA_QVMY_OP_NE QA_QVM_OP_NE
#define QA_QVMY_OP_LTI QA_QVM_OP_LTI
#define QA_QVMY_OP_LEI QA_QVM_OP_LEI
#define QA_QVMY_OP_GTI QA_QVM_OP_GTI
#define QA_QVMY_OP_GEI QA_QVM_OP_GEI
#define QA_QVMY_OP_LTU QA_QVM_OP_LTU
#define QA_QVMY_OP_LEU QA_QVM_OP_LEU
#define QA_QVMY_OP_GTU QA_QVM_OP_GTU
#define QA_QVMY_OP_GEU QA_QVM_OP_GEU
#define QA_QVMY_OP_EQF QA_QVM_OP_EQF
#define QA_QVMY_OP_NEF QA_QVM_OP_NEF
#define QA_QVMY_OP_LTF QA_QVM_OP_LTF
#define QA_QVMY_OP_LEF QA_QVM_OP_LEF
#define QA_QVMY_OP_GTF QA_QVM_OP_GTF
#define QA_QVMY_OP_GEF QA_QVM_OP_GEF
#define QA_QVMY_OP_LOAD1 QA_QVM_OP_LOAD1
#define QA_QVMY_OP_LOAD2 QA_QVM_OP_LOAD2
#define QA_QVMY_OP_LOAD4 QA_QVM_OP_LOAD4
#define QA_QVMY_OP_STORE1 QA_QVM_OP_STORE1
#define QA_QVMY_OP_STORE2 QA_QVM_OP_STORE2
#define QA_QVMY_OP_STORE4 QA_QVM_OP_STORE4
#define QA_QVMY_OP_ARG QA_QVM_OP_ARG
#define QA_QVMY_OP_BLOCK_COPY QA_QVM_OP_BLOCK_COPY
#define QA_QVMY_OP_SEX8 QA_QVM_OP_SEX8
#define QA_QVMY_OP_SEX16 QA_QVM_OP_SEX16
#define QA_QVMY_OP_NEGI QA_QVM_OP_NEGI
#define QA_QVMY_OP_ADD QA_QVM_OP_ADD
#define QA_QVMY_OP_SUB QA_QVM_OP_SUB
#define QA_QVMY_OP_DIVI QA_QVM_OP_DIVI
#define QA_QVMY_OP_DIVU QA_QVM_OP_DIVU
#define QA_QVMY_OP_MODI QA_QVM_OP_MODI
#define QA_QVMY_OP_MODU QA_QVM_OP_MODU
#define QA_QVMY_OP_MULI QA_QVM_OP_MULI
#define QA_QVMY_OP_MULU QA_QVM_OP_MULU
#define QA_QVMY_OP_BAND QA_QVM_OP_BAND
#define QA_QVMY_OP_BOR QA_QVM_OP_BOR
#define QA_QVMY_OP_BXOR QA_QVM_OP_BXOR
#define QA_QVMY_OP_BCOM QA_QVM_OP_BCOM
#define QA_QVMY_OP_LSH QA_QVM_OP_LSH
#define QA_QVMY_OP_RSHI QA_QVM_OP_RSHI
#define QA_QVMY_OP_RSHU QA_QVM_OP_RSHU
#define QA_QVMY_OP_NEGF QA_QVM_OP_NEGF
#define QA_QVMY_OP_ADDF QA_QVM_OP_ADDF
#define QA_QVMY_OP_SUBF QA_QVM_OP_SUBF
#define QA_QVMY_OP_DIVF QA_QVM_OP_DIVF
#define QA_QVMY_OP_MULF QA_QVM_OP_MULF
#define QA_QVMY_OP_CVIF QA_QVM_OP_CVIF
#define QA_QVMY_OP_CVFI QA_QVM_OP_CVFI

/* TS QvmUnaryOpcode / QvmBinaryOpcode / QvmBranchOpcode narrow the
 * enum at compile time only; unsupported values abort like the TS
 * exhaustiveness throw. */
int32_t qa_qvm_evaluate_unary(qa_qvm_opcode opcode, int32_t word);
int32_t qa_qvm_evaluate_binary(qa_qvm_opcode opcode, int32_t left, int32_t right);
bool qa_qvm_evaluate_branch(qa_qvm_opcode opcode, int32_t left, int32_t right);

#endif

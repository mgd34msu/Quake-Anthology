/* See include/qa/compat/qvm/operations.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/operations.h"

#include <math.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

/* TS bitsToFloat32 reads through DataView.getFloat32, which yields the one
 * canonical quiet NaN for every NaN payload; fresh arithmetic NaNs keep theirs. */
static float as_float(int32_t word) {
  float value = qa_bits_to_float32((uint32_t)word);
  return isnan(value) ? NAN : value;
}
static int32_t as_word(double value) { return (int32_t)qa_float32_to_bits(value); }

int32_t qa_qvm_evaluate_unary(qa_qvm_opcode opcode, int32_t word) {
  switch (opcode) {
    case QA_QVMY_OP_SEX8: return (int32_t)((uint32_t)word << 24) >> 24;
    case QA_QVMY_OP_SEX16: return (int32_t)((uint32_t)word << 16) >> 16;
    case QA_QVMY_OP_NEGI: return (int32_t)(0u - (uint32_t)word);
    case QA_QVMY_OP_NEGF: return as_word(-(double)as_float(word));
    case QA_QVMY_OP_CVIF: return as_word((double)word);
    case QA_QVMY_OP_CVFI: return qa_qvm_float_to_int((double)as_float(word));
    default: qa_range_error("Unsupported QVM unary opcode %d", (int)opcode);
  }
}

/* The compiled module's x86 shifts take the count from CL: low five bits only. */
static int shift_count(int32_t word) { return (int)((uint32_t)word & 31u); }

int32_t qa_qvm_evaluate_binary(qa_qvm_opcode opcode, int32_t left, int32_t right) {
  switch (opcode) {
    case QA_QVMY_OP_ADD: return (int32_t)((uint32_t)left + (uint32_t)right);
    case QA_QVMY_OP_SUB: return (int32_t)((uint32_t)left - (uint32_t)right);
    case QA_QVMY_OP_DIVI:
    case QA_QVMY_OP_MODI:
      if (right == 0) qa_range_error("QVM integer division or modulo by zero");
      if (left == INT32_MIN && right == -1) qa_range_error("QVM signed division or modulo overflow");
      return opcode == QA_QVMY_OP_DIVI ? left / right : left % right;
    case QA_QVMY_OP_DIVU:
    case QA_QVMY_OP_MODU:
      if (right == 0) qa_range_error("QVM integer division or modulo by zero");
      return (int32_t)(opcode == QA_QVMY_OP_DIVU ? (uint32_t)left / (uint32_t)right
                                                  : (uint32_t)left % (uint32_t)right);
    case QA_QVMY_OP_MULI:
    case QA_QVMY_OP_MULU: return (int32_t)((uint32_t)left * (uint32_t)right);
    case QA_QVMY_OP_BAND: return left & right;
    case QA_QVMY_OP_BOR: return left | right;
    case QA_QVMY_OP_BXOR: return left ^ right;
    case QA_QVMY_OP_LSH: return (int32_t)((uint32_t)left << shift_count(right));
    case QA_QVMY_OP_RSHI: return left >> shift_count(right);
    case QA_QVMY_OP_RSHU: return (int32_t)((uint32_t)left >> shift_count(right));
    case QA_QVMY_OP_ADDF: return as_word((double)as_float(left) + (double)as_float(right));
    case QA_QVMY_OP_SUBF: return as_word((double)as_float(left) - (double)as_float(right));
    case QA_QVMY_OP_DIVF: return as_word((double)as_float(left) / (double)as_float(right));
    case QA_QVMY_OP_MULF: return as_word((double)as_float(left) * (double)as_float(right));
    default: qa_range_error("Unsupported QVM binary opcode %d", (int)opcode);
  }
}

bool qa_qvm_evaluate_branch(qa_qvm_opcode opcode, int32_t left, int32_t right) {
  switch (opcode) {
    case QA_QVMY_OP_EQ: return left == right;
    case QA_QVMY_OP_NE: return left != right;
    case QA_QVMY_OP_LTI: return left < right;
    case QA_QVMY_OP_LEI: return left <= right;
    case QA_QVMY_OP_GTI: return left > right;
    case QA_QVMY_OP_GEI: return left >= right;
    case QA_QVMY_OP_LTU: return (uint32_t)left < (uint32_t)right;
    case QA_QVMY_OP_LEU: return (uint32_t)left <= (uint32_t)right;
    case QA_QVMY_OP_GTU: return (uint32_t)left > (uint32_t)right;
    case QA_QVMY_OP_GEU: return (uint32_t)left >= (uint32_t)right;
    case QA_QVMY_OP_EQF: return as_float(left) == as_float(right);
    case QA_QVMY_OP_NEF: return as_float(left) != as_float(right);
    case QA_QVMY_OP_LTF: return as_float(left) < as_float(right);
    case QA_QVMY_OP_LEF: return as_float(left) <= as_float(right);
    case QA_QVMY_OP_GTF: return as_float(left) > as_float(right);
    case QA_QVMY_OP_GEF: return as_float(left) >= as_float(right);
    default: qa_range_error("Unsupported QVM branch opcode %d", (int)opcode);
  }
}

/*
 * Port of src/guest/x86/arithmetic.ts: ALU/shift/multiply flag semantics.
 * TS bigint operands become uint64_t (widths are 8..64, values wrap via
 * BigInt.asUintN equivalently). GuestFlags is qa_processor_flags.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_X86_ARITHMETIC_H
#define QA_GUEST_X86_ARITHMETIC_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/guest/core/registers.h"

typedef enum qa_alu_operation {
  QA_ALU_ADD = 0, QA_ALU_OR, QA_ALU_ADC, QA_ALU_SBB, QA_ALU_AND, QA_ALU_SUB, QA_ALU_XOR, QA_ALU_CMP, QA_ALU_TEST
} qa_alu_operation;
const char *qa_alu_operation_name(qa_alu_operation op);
bool qa_alu_operation_parse(const char *name, qa_alu_operation *out);

typedef enum qa_shift_operation {
  QA_SHIFT_ROL = 0, QA_SHIFT_ROR, QA_SHIFT_RCL, QA_SHIFT_RCR, QA_SHIFT_SHL, QA_SHIFT_SHR, QA_SHIFT_SAR
} qa_shift_operation;
const char *qa_shift_operation_name(qa_shift_operation op);
bool qa_shift_operation_parse(const char *name, qa_shift_operation *out);

void qa_x86_result_flags(int width, uint64_t value, qa_processor_flags *flags);
uint64_t qa_x86_alu(qa_alu_operation op, int width, uint64_t left, uint64_t right, qa_processor_flags *flags);
uint64_t qa_x86_shift(qa_shift_operation op, int width, uint64_t value, int count, qa_processor_flags *flags);
uint64_t qa_x86_signed_multiply(int width, uint64_t left, uint64_t right, qa_processor_flags *flags);
bool qa_x86_condition(int code, qa_processor_flags *flags);

#endif

/*
 * Port of src/guest/floating-point/contracts.ts: numeric instruction and
 * execution-result shapes plus NumericFault. The C shapes already live in
 * binary.h (qa_numeric_operand/instruction/context/result, with the fault
 * throw as qa_numeric_fault over the context throw channel); this header
 * is the contracts.ts spelling of that surface. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_CONTRACTS_H
#define QA_GUEST_FLOATING_POINT_CONTRACTS_H

#include "qa/guest/floating_point/binary.h"

/* NumericExecutionResult kind strings ("executed"|"unsupported"|"exception"). */
const char *qa_numeric_result_kind_name(qa_numeric_result_kind kind);

#endif

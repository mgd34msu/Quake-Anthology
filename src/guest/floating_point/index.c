/* Port of src/guest/floating-point/index.ts. The barrel's one piece
 * of runtime code, executeNumericInstruction, is
 * qa_execute_numeric_instruction in sse.c (declared in sse.h), so
 * this file carries no code of its own. GPL-2.0-or-later. */
#include "qa/guest/floating_point.h"

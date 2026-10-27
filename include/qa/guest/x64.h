/* Umbrella for src/guest/x64/index.ts (pure re-export barrel: decoder, cpu).
 * plan.h/integer_kernel.h are ported siblings the TS barrel does not
 * re-export (imported directly in TS); included here like pe.h includes
 * directories.h so umbrella consumers see every ported header. GPL-2.0-or-later. */
#ifndef QA_GUEST_X64_H
#define QA_GUEST_X64_H
#include "qa/guest/x64/cpu.h"
#include "qa/guest/x64/decoder.h"
#include "qa/guest/x64/integer_kernel.h"
#include "qa/guest/x64/plan.h"
#endif

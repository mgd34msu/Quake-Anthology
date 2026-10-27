/* Port of src/compat/q2/classic/cvars.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_CLASSIC_CVARS_H
#define QA_COMPAT_Q2_CLASSIC_CVARS_H
#include <stdbool.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/core/cvars/index.h"

typedef struct qa_compat_q2_classic_cvars qa_compat_q2_classic_cvars;
/* NULL only on OOM. memory/registry borrowed. */
qa_compat_q2_classic_cvars *qa_compat_q2_classic_cvars_create(qa_compat_q2_memory *memory, qa_cvar_registry *registry);
void qa_compat_q2_classic_cvars_destroy(qa_compat_q2_classic_cvars *c);
qa_compat_q2_memory *qa_compat_q2_classic_cvars_get_memory(const qa_compat_q2_classic_cvars *c);
qa_cvar_registry *qa_compat_q2_classic_cvars_get_registry(const qa_compat_q2_classic_cvars *c);
qa_guest_address qa_compat_q2_classic_cvars_string(qa_compat_q2_classic_cvars *c, const char *value);
/* False is the TS null pointer. */
bool qa_compat_q2_classic_cvars_pointer(qa_compat_q2_classic_cvars *c, const char *name, qa_guest_address *out);
void qa_compat_q2_classic_cvars_refresh(qa_compat_q2_classic_cvars *c);
#endif

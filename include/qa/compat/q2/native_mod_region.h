/* Port of src/compat/q2/native-mod-region.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_REGION_H
#define QA_COMPAT_Q2_NATIVE_MOD_REGION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_region.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_mod_region_authority { void *ctx; const char *error_message; bool (*current)(void *ctx); } qa_compat_q2_native_mod_region_authority;
void qa_compat_q2_validate_native_mod_region(const qa_native_mod_protection_region *region, int64_t pointer_bytes);
typedef struct qa_compat_q2_native_mod_region_execution { const qa_native_mod_protection_region *definition; size_t input_count; int phase; bool has_output; } qa_compat_q2_native_mod_region_execution;
void qa_compat_q2_native_mod_region_execution_init(qa_compat_q2_native_mod_region_execution *s, const qa_native_mod_protection_region *def, size_t input_count, int64_t pointer_bytes);
#endif

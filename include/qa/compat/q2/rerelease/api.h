/* Port of src/compat/q2/rerelease/api.ts. GPL-2.0-or-later.
 * Entry tables are built once on first use (aggregate layouts). */
#ifndef QA_COMPAT_Q2_RERELEASE_API_H
#define QA_COMPAT_Q2_RERELEASE_API_H
#include <stddef.h>
#include "qa/compat/q2/rerelease/layouts.h"

extern const qa_native_call_abi qa_compat_q2_rerelease_abi;
/* TS scalar value layouts P/I/U/Q/F/B/H/UH and aggregates T/R/V2. */
extern const qa_guest_value_layout qa_compat_q2_rr_pointer_layout;
extern const qa_guest_value_layout qa_compat_q2_rr_int_layout;
extern const qa_guest_value_layout qa_compat_q2_rr_uint_layout;
void qa_compat_q2_rerelease_api_ensure(void);
/* TS signature(parameters, result = "void"). */
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_signature(const qa_guest_value_layout *parameters, size_t count,
                                                                   bool result_is_void, qa_guest_value_layout result);
typedef struct qa_compat_q2_rerelease_api_entry { const char *name; qa_compat_q2_guest_call_signature signature; } qa_compat_q2_rerelease_api_entry;
/* Returned arrays are process-retained; count via out param. */
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_game_imports(size_t *out_count);
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_game_exports(size_t *out_count);
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_cgame_imports(size_t *out_count);
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_get_cgame_exports(size_t *out_count);
/* NULL when the name is unknown. */
const qa_compat_q2_rerelease_api_entry *qa_compat_q2_rerelease_find_entry(const qa_compat_q2_rerelease_api_entry *entries, size_t count, const char *name);
const qa_guest_layout *qa_compat_q2_rerelease_get_game_import_layout(void);
const qa_guest_layout *qa_compat_q2_rerelease_get_game_export_layout(void);
const qa_guest_layout *qa_compat_q2_rerelease_get_cgame_import_layout(void);
const qa_guest_layout *qa_compat_q2_rerelease_get_cgame_export_layout(void);
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_api_signature(void);
#endif

/* Port of src/compat/q2/native-mod-region.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_REGION_H
#define QA_COMPAT_Q2_NATIVE_MOD_REGION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/contracts/native_mod_region.h"
#include "qa/foundation/support.h"

/* TS NativeModRegionAuthority. Thrown with token=authority, message=error; the
 * authority must outlive every catch site that compares it (heap or owner-held). */
typedef struct qa_compat_q2_native_mod_region_authority {
  void *ctx;
  const char *error;
  bool (*current)(void *ctx);
} qa_compat_q2_native_mod_region_authority;
void qa_compat_q2_validate_native_mod_region(const qa_native_mod_protection_region *region, int64_t pointer_bytes);
/* TS NativeModRegionExecution: one invocation of a qualified region. */
typedef struct qa_compat_q2_native_mod_region_execution qa_compat_q2_native_mod_region_execution;
/* authority may be NULL. inputs are copied. Free with _destroy. */
qa_compat_q2_native_mod_region_execution *qa_compat_q2_native_mod_region_execution_create(
    const qa_compat_q2_native_mod_host *host, const qa_native_mod_protection_region *definition,
    const qa_guest_call_value *inputs, size_t input_count, const qa_compat_q2_native_mod_region_authority *authority);
void qa_compat_q2_native_mod_region_execution_bind(qa_compat_q2_native_mod_region_execution *s, qa_guest_address target);
void qa_compat_q2_native_mod_region_execution_unbind(qa_compat_q2_native_mod_region_execution *s);
qa_guest_call_value qa_compat_q2_native_mod_region_execution_result(const qa_compat_q2_native_mod_region_execution *s);
void qa_compat_q2_native_mod_region_execution_destroy(qa_compat_q2_native_mod_region_execution *s);
#endif

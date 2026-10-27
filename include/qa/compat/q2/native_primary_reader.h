/* Port of src/compat/q2/native-primary-reader.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_READER_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_READER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/execution.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/persistence/value.h"

typedef struct qa_compat_q2_native_region_span { double entry; double join; } qa_compat_q2_native_region_span;
double qa_compat_q2_native_offset(const qa_save_reader *reader);
qa_compat_q2_native_region_span qa_compat_q2_native_region(const qa_save_reader *reader);
qa_native_mod_scalar qa_compat_q2_native_scalar(const qa_save_reader *reader);
const char *qa_compat_q2_native_register(const qa_save_reader *reader);
qa_native_item_field qa_compat_q2_native_field(const qa_save_reader *reader);
/* TS nativeTest. Pointer-case value (when non-null) is stored in
 * *value_storage; its indirections array is malloc'd (caller frees
 * (void*)indirections.values). Returns false only on OOM. */
bool qa_compat_q2_native_test(const qa_save_reader *reader, qa_native_item_test *out, qa_native_mod_address *value_storage);
qa_native_abi qa_compat_q2_native_abi(const qa_save_reader *reader, qa_native_abi expected);
/* TS nativeSignature. Parameters array malloc'd on success (caller
 * frees). Returns false only on OOM. */
/* TS GuestCallSignature (guest/core/contracts.ts), mirrored with
 * contracts-world layouts because guest/abi headers conflict with
 * contracts/execution.h in one TU. TS owner: guest/core/contracts.ts. */
typedef struct qa_compat_q2_guest_call_signature {
  qa_native_abi abi; const qa_guest_value_layout *parameters; size_t parameter_count;
  bool result_is_void; qa_guest_value_layout result; bool variadic;
} qa_compat_q2_guest_call_signature;
bool qa_compat_q2_native_signature(const qa_save_reader *reader, qa_native_abi abi, qa_compat_q2_guest_call_signature *out);
#endif

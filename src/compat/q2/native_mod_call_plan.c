/* See include/qa/compat/q2/native_mod_call_plan.h. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_call_plan.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/guest/abi/classify.h"
static qa_abi_storage storage_of(const char *name) {
  static const char *const names[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float32", "float64", "pointer"};
  for (int i = 0; i < 11; i++) if (!strcmp(names[i], name)) return (qa_abi_storage)i;
  qa_range_error("Invalid guest storage %s", name);
}
int64_t qa_compat_q2_native_plan_stack_bytes(const char *abi_kind, const char *abi_image, int64_t pointer_bytes, const char *abi_call,
    const char *const *parameter_storages, size_t parameter_count, const char *result_storage) {
  qa_abi_value_layout *parameters = calloc(parameter_count == 0 ? 1 : parameter_count, sizeof *parameters);
  if (parameters == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < parameter_count; i++) { parameters[i].is_aggregate = false; parameters[i].storage = storage_of(parameter_storages[i]); }
  qa_guest_call_signature signature; memset(&signature, 0, sizeof signature);
  signature.abi.kind = abi_kind; signature.abi.image = abi_image; signature.abi.pointer_bytes = (int)pointer_bytes; signature.abi.call = abi_call;
  signature.parameters = parameters; signature.parameter_count = parameter_count; signature.variadic = false;
  signature.result_is_void = result_storage == NULL;
  if (result_storage != NULL) { signature.result.is_aggregate = false; signature.result.storage = storage_of(result_storage); }
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, &signature, NULL, 0)) qa_range_error("Out of memory");
  int64_t stack_bytes = plan.stack_bytes;
  qa_abi_call_plan_free(&plan); free(parameters);
  return stack_bytes;
}

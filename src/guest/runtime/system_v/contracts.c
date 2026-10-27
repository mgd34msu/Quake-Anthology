/* Port of src/guest/runtime/system-v/contracts.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/contracts.h"

#include <stdio.h>

#include "qa/foundation/support.h"

const char *qa_system_v_support_name(qa_system_v_support support) {
  switch (support) {
    case QA_SYSTEM_V_IMPLEMENTED: return "implemented";
    case QA_SYSTEM_V_UNSUPPORTED_FUNCTION: return "unsupported-function";
    case QA_SYSTEM_V_UNSUPPORTED_DATA: return "unsupported-data";
  }
  return "";
}
int qa_system_v_errno_get(qa_system_v_service_host *host) { return host->get_errno(host->ctx); }
void qa_system_v_errno_set(qa_system_v_service_host *host, int value) { host->set_errno(host->ctx, value); }
void qa_system_v_service(qa_system_v_service_host *host, const char *library, const char *name,
                         const char *const *versions, size_t version_count,
                         const qa_abi_storage *parameters, size_t parameter_count,
                         bool result_is_void, qa_abi_storage result,
                         qa_system_v_implementation implementation) {
  host->service(host->ctx, library, name, versions, version_count, parameters, parameter_count,
                result_is_void, result, implementation);
}
void qa_system_v_data(qa_system_v_service_host *host, const char *library, const char *name,
                      const char *version, qa_guest_address address, size_t byte_length) {
  host->data(host->ctx, library, name, version, address, byte_length);
}
bool qa_system_v_resolve_address(qa_system_v_service_host *host, const char *library,
                                 const char *name, const char *version, qa_guest_address *out) {
  return host->resolve_address(host->ctx, library, name, version, out);
}
qa_guest_address qa_system_v_unavailable(qa_system_v_service_host *host, const char *library,
                                         const char *name, const qa_abi_storage *parameters,
                                         size_t parameter_count, bool result_is_void,
                                         qa_abi_storage result, const char *detail) {
  return host->unavailable(host->ctx, library, name, parameters, parameter_count,
                           result_is_void, result, detail);
}
qa_guest_address qa_system_v_allocate(qa_system_v_service_host *host, size_t size) {
  return host->allocate(host->ctx, size);
}
void qa_system_v_free(qa_system_v_service_host *host, bool has_address, qa_guest_address address) {
  host->free(host->ctx, has_address, address);
}
bool qa_system_v_allocation_size(qa_system_v_service_host *host, qa_guest_address address, size_t *out) {
  return host->allocation_size(host->ctx, address, out);
}
qa_call_result qa_system_v_invoke(qa_system_v_service_host *host, const qa_guest_call_context *context,
                                  qa_guest_address target, const qa_abi_storage *parameters,
                                  size_t parameter_count, bool result_is_void, qa_abi_storage result,
                                  const qa_call_value *arguments, size_t argument_count) {
  return host->invoke(host->ctx, context, target, parameters, parameter_count,
                      result_is_void, result, arguments, argument_count);
}
void qa_unsupported_system_v_service(const char *library, const char *symbol,
                                     const char *version, const char *detail) {
  if (version == NULL)
    qa_range_error("Unsupported System V guest service %s:%s: %s", library, symbol, detail);
  else
    qa_range_error("Unsupported System V guest service %s:%s@%s: %s", library, symbol, version, detail);
}

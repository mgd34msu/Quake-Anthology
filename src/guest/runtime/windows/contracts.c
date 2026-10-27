/* Port of src/guest/runtime/windows/contracts.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/windows/contracts.h"

#include "qa/foundation/support.h"

uint32_t qa_windows_last_error_get(qa_windows_service_host *host) { return host->get_last_error(host->ctx); }
void qa_windows_last_error_set(qa_windows_service_host *host, uint32_t value) { host->set_last_error(host->ctx, value); }
void qa_windows_service(qa_windows_service_host *host, const char *library, const char *name,
                        const qa_abi_storage *parameters, size_t parameter_count,
                        bool result_is_void, qa_abi_storage result,
                        qa_windows_import_implementation implementation) {
  host->service(host->ctx, library, name, parameters, parameter_count, result_is_void, result, implementation);
}
bool qa_windows_allocate(qa_windows_service_host *host, size_t size, uint64_t heap, qa_guest_address *out) {
  return host->allocate(host->ctx, size, heap, out);
}
bool qa_windows_free(qa_windows_service_host *host, bool has_address, qa_guest_address address, uint64_t heap) {
  return host->free(host->ctx, has_address, address, heap);
}
bool qa_windows_allocation_size(qa_windows_service_host *host, qa_guest_address address, uint64_t heap, size_t *out) {
  return host->allocation_size(host->ctx, address, heap, out);
}
void qa_windows_destroy_heap(qa_windows_service_host *host, uint64_t heap) { host->destroy_heap(host->ctx, heap); }
qa_call_result qa_windows_invoke(qa_windows_service_host *host, const qa_guest_call_context *context,
                                 qa_guest_address target, const qa_abi_storage *parameters,
                                 size_t parameter_count, bool result_is_void, qa_abi_storage result,
                                 const qa_call_value *arguments, size_t argument_count, bool system) {
  return host->invoke(host->ctx, context, target, parameters, parameter_count,
                      result_is_void, result, arguments, argument_count, system);
}
bool qa_windows_resolve_address(qa_windows_service_host *host, const char *library, const char *name,
                                qa_guest_address *out) {
  return host->resolve_address(host->ctx, library, name, out);
}
bool qa_windows_library_handle(qa_windows_service_host *host, const char *library, qa_guest_address *out) {
  return host->library_handle(host->ctx, library, out);
}
bool qa_windows_load_library(qa_windows_service_host *host, const char *library, qa_guest_address *out) {
  return host->load_library(host->ctx, library, out);
}
bool qa_windows_free_library(qa_windows_service_host *host, qa_guest_address handle) {
  return host->free_library(host->ctx, handle);
}
const char *qa_windows_library_name(qa_windows_service_host *host, qa_guest_address handle) {
  return host->library_name(host->ctx, handle);
}
const qa_pe_image *const *qa_windows_images(qa_windows_service_host *host, size_t *count) {
  return host->images(host->ctx, count);
}
void qa_unsupported_windows_import(const char *library, const char *symbol, const char *detail) {
  qa_range_error("Unsupported Windows guest import %s!%s: %s", library, symbol,
                 detail != NULL ? detail : "runtime service is not implemented");
}

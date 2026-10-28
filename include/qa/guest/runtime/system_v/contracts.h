/*
 * Port of src/guest/runtime/system-v/contracts.ts: System V runtime
 * capabilities/options and the service-host interface. The interface
 * becomes a function table + ctx filled in by qa_system_v_guest_runtime.
 * UnsupportedSystemVService aborts with the same message. Capability
 * callbacks write into caller buffers (TS returns fresh Uint8Arrays).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_SYSTEM_V_CONTRACTS_H
#define QA_GUEST_RUNTIME_SYSTEM_V_CONTRACTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/runner.h"
#include "qa/guest/abi/values.h"

typedef enum qa_system_v_stream { QA_SYSTEM_V_STDOUT = 0, QA_SYSTEM_V_STDERR } qa_system_v_stream;

typedef struct qa_system_v_capabilities {
  void *ctx;
  bool (*now_seconds)(void *ctx, int64_t *out); /* NULL: absent */
  size_t (*standard_input)(void *ctx, size_t maximum_bytes, uint8_t *out); /* NULL: absent */
  int (*standard_output)(void *ctx, qa_system_v_stream stream, const uint8_t *bytes, size_t len);
  int (*standard_flush)(void *ctx, qa_system_v_stream stream);
  bool output_is_terminal;
} qa_system_v_capabilities;

typedef struct qa_system_v_runtime_options {
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_guest_callback_table *callbacks;
  qa_guest_module_identity module; /* TS memory.module */
  bool has_capabilities;
  qa_system_v_capabilities capabilities;
  const char *const *argv;
  size_t argv_count;
  const char *const *environment;
  size_t environment_count;
} qa_system_v_runtime_options;

typedef enum qa_system_v_support {
  QA_SYSTEM_V_IMPLEMENTED = 0, QA_SYSTEM_V_UNSUPPORTED_FUNCTION, QA_SYSTEM_V_UNSUPPORTED_DATA
} qa_system_v_support;
const char *qa_system_v_support_name(qa_system_v_support support);
typedef struct qa_system_v_import_coverage {
  const char *library;
  const char *name;
  const char *version; /* NULL: TS null */
  qa_system_v_support support;
  int64_t reached;
} qa_system_v_import_coverage;

typedef struct qa_system_v_initialize_options {
  qa_guest_core_call_context context;
  int64_t instruction_budget;
} qa_system_v_initialize_options;

typedef struct qa_system_v_implementation {
  void *ctx;
  qa_call_result (*invoke)(void *ctx, const qa_guest_core_call_context *context,
                           const qa_call_value *arguments, size_t argument_count);
} qa_system_v_implementation;

typedef struct qa_system_v_service_host {
  void *ctx;
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_system_v_capabilities capabilities;
  qa_abi_storage pointer_storage;        /* uint32|uint64 */
  qa_abi_storage signed_pointer_storage; /* int32|int64 */
  qa_guest_call_runner *runner;          /* valid after attach */
  qa_guest_core_address empty_string_representation;
  qa_guest_core_address thread_pointer;
  qa_guest_core_address errno_address;
  int (*get_errno)(void *ctx);
  void (*set_errno)(void *ctx, int value);
  /* versions entries may be NULL (TS null version). */
  void (*service)(void *ctx, const char *library, const char *name,
                  const char *const *versions, size_t version_count,
                  const qa_abi_storage *parameters, size_t parameter_count,
                  bool result_is_void, qa_abi_storage result, qa_system_v_implementation implementation);
  void (*data)(void *ctx, const char *library, const char *name, const char *version,
               qa_guest_core_address address, size_t byte_length);
  bool (*resolve_address)(void *ctx, const char *library, const char *name, const char *version,
                          qa_guest_core_address *out);
  qa_guest_core_address (*unavailable)(void *ctx, const char *library, const char *name,
                                  const qa_abi_storage *parameters, size_t parameter_count,
                                  bool result_is_void, qa_abi_storage result, const char *detail);
  qa_guest_core_address (*allocate)(void *ctx, size_t size);
  void (*free)(void *ctx, bool has_address, qa_guest_core_address address);
  bool (*allocation_size)(void *ctx, qa_guest_core_address address, size_t *out);
  qa_call_result (*invoke)(void *ctx, const qa_guest_core_call_context *context, qa_guest_core_address target,
                           const qa_abi_storage *parameters, size_t parameter_count,
                           bool result_is_void, qa_abi_storage result,
                           const qa_call_value *arguments, size_t argument_count);
  void (*register_destructor)(void *ctx, qa_guest_core_address target,
                              bool has_argument, qa_guest_core_address argument,
                              bool has_dso, qa_guest_core_address dso);
  void (*finalize_destructors)(void *ctx, const qa_guest_core_call_context *context,
                               bool has_dso, qa_guest_core_address dso);
  qa_guest_core_address (*tls_address)(void *ctx, uint64_t module_id, int64_t offset);
} qa_system_v_service_host;

/* Convenience wrappers mirroring host.method(...) call sites. */
int qa_system_v_errno_get(qa_system_v_service_host *host);
void qa_system_v_errno_set(qa_system_v_service_host *host, int value);
void qa_system_v_service(qa_system_v_service_host *host, const char *library, const char *name,
                         const char *const *versions, size_t version_count,
                         const qa_abi_storage *parameters, size_t parameter_count,
                         bool result_is_void, qa_abi_storage result,
                         qa_system_v_implementation implementation);
void qa_system_v_data(qa_system_v_service_host *host, const char *library, const char *name,
                      const char *version, qa_guest_core_address address, size_t byte_length);
bool qa_system_v_resolve_address(qa_system_v_service_host *host, const char *library,
                                 const char *name, const char *version, qa_guest_core_address *out);
qa_guest_core_address qa_system_v_unavailable(qa_system_v_service_host *host, const char *library,
                                         const char *name, const qa_abi_storage *parameters,
                                         size_t parameter_count, bool result_is_void,
                                         qa_abi_storage result, const char *detail);
qa_guest_core_address qa_system_v_allocate(qa_system_v_service_host *host, size_t size);
void qa_system_v_free(qa_system_v_service_host *host, bool has_address, qa_guest_core_address address);
bool qa_system_v_allocation_size(qa_system_v_service_host *host, qa_guest_core_address address, size_t *out);
qa_call_result qa_system_v_invoke(qa_system_v_service_host *host, const qa_guest_core_call_context *context,
                                  qa_guest_core_address target, const qa_abi_storage *parameters,
                                  size_t parameter_count, bool result_is_void, qa_abi_storage result,
                                  const qa_call_value *arguments, size_t argument_count);

/* TS throw new UnsupportedSystemVService(...). */
void qa_unsupported_system_v_service(const char *library, const char *symbol,
                                     const char *version, const char *detail);

#endif

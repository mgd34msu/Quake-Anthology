/*
 * Port of src/guest/runtime/windows/contracts.ts: Windows file/capability
 * shapes and the service-host interface (function table + ctx, filled in
 * by qa_windows_guest_runtime). Capability file reads write into caller
 * buffers; environment is a key/value array (TS ReadonlyMap iteration
 * order). UnsupportedWindowsImport aborts with the same message.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_WINDOWS_CONTRACTS_H
#define QA_GUEST_RUNTIME_WINDOWS_CONTRACTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/runner.h"
#include "qa/guest/abi/values.h"
#include "qa/guest/pe/image.h"

typedef struct qa_windows_file {
  void *ctx;
  size_t (*read)(void *ctx, size_t offset, uint8_t *out, size_t length);
  int64_t (*write)(void *ctx, size_t offset, const uint8_t *bytes, size_t len);
  int64_t (*size)(void *ctx);
  void (*truncate)(void *ctx, size_t length);
  void (*flush)(void *ctx);
  void (*close)(void *ctx);
} qa_windows_file;

typedef enum qa_windows_stream { QA_WINDOWS_STDOUT = 0, QA_WINDOWS_STDERR } qa_windows_stream;

typedef struct qa_windows_capabilities {
  void *ctx;
  bool (*now_milliseconds)(void *ctx, double *out); /* NULL: absent (host clock) */
  bool (*performance_counter)(void *ctx, int64_t *out);
  bool has_performance_frequency;
  int64_t performance_frequency;
  const char *command_line; /* NULL: absent */
  const char *const *environment_keys;
  const char *const *environment_values;
  size_t environment_count;
  qa_windows_file *(*open_file)(void *ctx, const char *path, bool read, bool write, int creation);
  void (*standard_output)(void *ctx, qa_windows_stream stream, const uint8_t *bytes, size_t len);
  size_t (*standard_input)(void *ctx, size_t length, uint8_t *out);
} qa_windows_capabilities;

typedef struct qa_windows_runtime_options {
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_guest_callback_table *callbacks;
  qa_guest_module_identity module; /* TS memory.module */
  bool has_capabilities;
  qa_windows_capabilities capabilities;
  bool has_process_id;
  int64_t process_id;
  bool has_thread_id;
  int64_t thread_id;
} qa_windows_runtime_options;

typedef struct qa_windows_import_coverage {
  const char *library;
  const char *name;
  bool supported;
  int64_t reached;
  int64_t failed;
  const char *last_failure; /* NULL: TS null */
} qa_windows_import_coverage;

typedef struct qa_windows_initialize_options {
  qa_guest_core_call_context context;
  int64_t instruction_budget;
} qa_windows_initialize_options;

typedef struct qa_windows_import_implementation {
  void *ctx;
  qa_call_result (*invoke)(void *ctx, const qa_guest_core_call_context *context,
                           const qa_call_value *arguments, size_t argument_count);
} qa_windows_import_implementation;

/* TS WindowsServiceHost heaps / reservations / tlsIndices / fiberSlots: kernel
 * tables a game extends while it runs, held by the host so a process image can
 * save them. Every array keeps TS Set/Map insertion order. */
typedef struct qa_windows_kernel_reservation { qa_guest_core_address address; size_t size; } qa_windows_kernel_reservation;
typedef struct qa_windows_fiber_slot {
  int64_t index;
  bool has_callback; qa_guest_core_address callback;
  bool has_value; qa_guest_core_address value;
} qa_windows_fiber_slot;
typedef struct qa_windows_kernel_tables {
  uint64_t *heaps; size_t heap_count, heap_cap;
  qa_windows_kernel_reservation *reservations; size_t reservation_count, reservation_cap;
  int64_t *tls; size_t tls_count, tls_cap;
  qa_windows_fiber_slot *fls; size_t fls_count, fls_cap;
} qa_windows_kernel_tables;
void qa_windows_kernel_tables_free(qa_windows_kernel_tables *tables);

typedef struct qa_windows_service_host {
  void *ctx;
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_windows_capabilities capabilities;
  qa_abi_storage pointer_storage; /* uint32|uint64 */
  qa_guest_call_runner *runner;
  qa_guest_core_address teb;
  int64_t process_id;
  int64_t thread_id;
  const qa_pe_image *const *(*images)(void *ctx, size_t *count);
  uint32_t (*get_last_error)(void *ctx);
  void (*set_last_error)(void *ctx, uint32_t value);
  void (*register_)(void *ctx, const char *library, const char *name,
                    qa_guest_call_signature signature, qa_windows_import_implementation implementation);
  void (*service)(void *ctx, const char *library, const char *name,
                  const qa_abi_storage *parameters, size_t parameter_count,
                  bool result_is_void, qa_abi_storage result,
                  qa_windows_import_implementation implementation);
  bool (*allocate)(void *ctx, size_t size, uint64_t heap, qa_guest_core_address *out);
  bool (*free)(void *ctx, bool has_address, qa_guest_core_address address, uint64_t heap);
  bool (*allocation_size)(void *ctx, qa_guest_core_address address, uint64_t heap, size_t *out);
  void (*destroy_heap)(void *ctx, uint64_t heap);
  qa_call_result (*invoke)(void *ctx, const qa_guest_core_call_context *context, qa_guest_core_address target,
                           const qa_abi_storage *parameters, size_t parameter_count,
                           bool result_is_void, qa_abi_storage result,
                           const qa_call_value *arguments, size_t argument_count, bool system);
  bool (*resolve_address)(void *ctx, const char *library, const char *name, qa_guest_core_address *out);
  bool (*library_handle)(void *ctx, const char *library, qa_guest_core_address *out);
  bool (*load_library)(void *ctx, const char *library, qa_guest_core_address *out);
  bool (*free_library)(void *ctx, qa_guest_core_address handle);
  const char *(*library_name)(void *ctx, qa_guest_core_address handle);
  /* Host-side state that lives as long as the runtime (a TS closure's
   * captures): release(resource) runs when the runtime is destroyed, in
   * reverse order of ownership. */
  void (*own)(void *ctx, void *resource, void (*release)(void *resource));
  /* TS heaps/reservations/tlsIndices/fiberSlots; NULL: the kernel keeps private tables. */
  qa_windows_kernel_tables *tables;
  /* TS chargeBytes; NULL charges nothing (a host with no attached runner). */
  void (*charge_bytes)(void *ctx, size_t byte_length);
} qa_windows_service_host;

/* Convenience wrappers mirroring host.method(...) call sites. */
uint32_t qa_windows_last_error_get(qa_windows_service_host *host);
void qa_windows_last_error_set(qa_windows_service_host *host, uint32_t value);
void qa_windows_service(qa_windows_service_host *host, const char *library, const char *name,
                        const qa_abi_storage *parameters, size_t parameter_count,
                        bool result_is_void, qa_abi_storage result,
                        qa_windows_import_implementation implementation);
bool qa_windows_allocate(qa_windows_service_host *host, size_t size, uint64_t heap, qa_guest_core_address *out);
bool qa_windows_free(qa_windows_service_host *host, bool has_address, qa_guest_core_address address, uint64_t heap);
bool qa_windows_allocation_size(qa_windows_service_host *host, qa_guest_core_address address, uint64_t heap, size_t *out);
void qa_windows_destroy_heap(qa_windows_service_host *host, uint64_t heap);
qa_call_result qa_windows_invoke(qa_windows_service_host *host, const qa_guest_core_call_context *context,
                                 qa_guest_core_address target, const qa_abi_storage *parameters,
                                 size_t parameter_count, bool result_is_void, qa_abi_storage result,
                                 const qa_call_value *arguments, size_t argument_count, bool system);
bool qa_windows_resolve_address(qa_windows_service_host *host, const char *library, const char *name,
                                qa_guest_core_address *out);
bool qa_windows_library_handle(qa_windows_service_host *host, const char *library, qa_guest_core_address *out);
bool qa_windows_load_library(qa_windows_service_host *host, const char *library, qa_guest_core_address *out);
bool qa_windows_free_library(qa_windows_service_host *host, qa_guest_core_address handle);
const char *qa_windows_library_name(qa_windows_service_host *host, qa_guest_core_address handle);
const qa_pe_image *const *qa_windows_images(qa_windows_service_host *host, size_t *count);
void qa_windows_own(qa_windows_service_host *host, void *resource, void (*release)(void *resource));
void qa_windows_charge_bytes(qa_windows_service_host *host, size_t byte_length);

/* TS throw new UnsupportedWindowsImport(library, symbol, context, detail?). */
void qa_unsupported_windows_import(const char *library, const char *symbol, const char *detail);

#endif

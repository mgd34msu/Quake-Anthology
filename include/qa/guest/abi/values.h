/*
 * Port of src/guest/abi/values.ts: ABI value layouts and little-endian
 * encode/decode. Also hosts the shared guest call/module/image shapes from
 * src/contracts/execution.ts and src/guest/core/contracts.ts that the guest
 * modules consume, built on qa_guest_address/qa_mapped_guest_memory from
 * core/registers.h (the contracts/execution.h spellings of GuestAddress
 * differ and must not be mixed into guest code). TS `number` layout fields
 * become int64_t; the Number.isSafeInteger checks collapse to range checks.
 * Aggregate call values own their bytes; release with qa_call_value_free().
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ABI_VALUES_H
#define QA_GUEST_ABI_VALUES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/core/registers.h"
#include "qa/guest/elf/unwind.h"

/* --- GuestStorage --- */
typedef enum qa_abi_storage {
  QA_STORAGE_INT8 = 0, QA_STORAGE_UINT8, QA_STORAGE_INT16, QA_STORAGE_UINT16,
  QA_STORAGE_INT32, QA_STORAGE_UINT32, QA_STORAGE_INT64, QA_STORAGE_UINT64,
  QA_STORAGE_FLOAT32, QA_STORAGE_FLOAT64, QA_STORAGE_POINTER
} qa_abi_storage;
const char *qa_abi_storage_name(qa_abi_storage storage);
bool qa_abi_storage_parse(const char *name, qa_abi_storage *out);

/* --- GuestLayout / GuestValueLayout --- */
typedef struct qa_abi_field_layout {
  const char *name;
  int64_t byte_offset;
  qa_abi_storage storage;
  int64_t count;
} qa_abi_field_layout;
typedef struct qa_abi_layout {
  const char *id;
  int64_t byte_length;
  int64_t alignment;
  int pointer_bytes; /* 4 | 8; byte order is always little-endian */
  const qa_abi_field_layout *fields;
  size_t field_count;
} qa_abi_layout;
typedef struct qa_abi_value_layout {
  bool is_aggregate; /* false: scalar */
  qa_abi_storage storage;
  qa_abi_layout layout;
} qa_abi_value_layout;

/* --- GuestCallValue / GuestCallResult --- */
typedef enum qa_call_value_kind {
  QA_CALL_VOID = 0, QA_CALL_INT32, QA_CALL_UINT32, QA_CALL_INT64, QA_CALL_UINT64,
  QA_CALL_FLOAT32, QA_CALL_FLOAT64, QA_CALL_POINTER, QA_CALL_AGGREGATE
} qa_call_value_kind;
const char *qa_call_value_kind_name(qa_call_value_kind kind);
typedef struct qa_call_value {
  qa_call_value_kind kind;
  double number; /* int32/uint32/float32/float64 */
  int64_t int64_value;
  uint64_t uint64_value;
  bool pointer_has; /* false: TS null */
  qa_guest_address pointer;
  qa_abi_layout layout; /* aggregate */
  uint8_t *bytes;       /* aggregate, owned */
  size_t bytes_len;
} qa_call_value;
typedef qa_call_value qa_call_result;

qa_call_value qa_call_void(void);
qa_call_value qa_call_int32(int32_t value);
qa_call_value qa_call_uint32(uint32_t value);
qa_call_value qa_call_int64(int64_t value);
qa_call_value qa_call_uint64(uint64_t value);
qa_call_value qa_call_float32(double value); /* stored frounded, like TS callers */
qa_call_value qa_call_float64(double value);
qa_call_value qa_call_pointer(bool has, qa_guest_address value);
/* Copies bytes. Returns void-kind value with bytes NULL on OOM. */
qa_call_value qa_call_aggregate(qa_abi_layout layout, const uint8_t *bytes, size_t len);
void qa_call_value_free(qa_call_value *value);
/* TS BigInt(value) for integer kinds; aborts (TypeError) otherwise. */
int64_t qa_call_value_bigint_i64(const qa_call_value *value);
uint64_t qa_call_value_bigint_u64(const qa_call_value *value);

/* --- NativeAbi / ModuleIdentity / GuestCallSignature --- */
typedef struct qa_guest_native_abi {
  const char *kind;  /* windows-i386|windows-x86-64|linux-i386|linux-x86-64 */
  const char *image; /* pe32|pe32+|elf32|elf64 */
  int pointer_bytes; /* 4 | 8 */
  const char *call;  /* cdecl|microsoft-x64|system-v-i386|system-v-x86-64|stdcall|thiscall|fastcall */
} qa_guest_native_abi;

typedef struct qa_guest_module_identity {
  const char *id;
  const char *artifact_path;
  const char *digest;
  const char *revision;
} qa_guest_module_identity;

typedef struct qa_guest_call_signature {
  qa_guest_native_abi abi;
  const qa_abi_value_layout *parameters;
  size_t parameter_count;
  bool result_is_void;
  qa_abi_value_layout result;
  bool variadic;
} qa_guest_call_signature;

/* --- GuestCallContext (callback reference reduced to native-guest data) --- */
typedef enum qa_guest_callback_ref_kind {
  QA_CALLBACK_REF_NONE = 0, QA_CALLBACK_REF_TYPESCRIPT, QA_CALLBACK_REF_QUAKEC,
  QA_CALLBACK_REF_QVM, QA_CALLBACK_REF_NATIVE_GUEST
} qa_guest_callback_ref_kind;
typedef struct qa_guest_call_context qa_guest_call_context;
struct qa_guest_call_context {
  qa_guest_module_identity module;
  qa_guest_callback_ref_kind callback_kind;
  qa_guest_module_identity callback_module;
  qa_guest_address callback_address;
  qa_guest_native_abi callback_abi;
  const qa_guest_call_context *parent;
  const void *self_view; /* TS RawEntityView, opaque to guest modules */
  const void *other;
};

/* --- GuestPermissions / GuestMapping --- */
typedef enum qa_guest_permissions {
  QA_PERM_NONE = 0, QA_PERM_READ, QA_PERM_READ_WRITE, QA_PERM_READ_EXECUTE,
  QA_PERM_READ_WRITE_EXECUTE, QA_PERM_EXECUTE
} qa_guest_permissions;
const char *qa_guest_permissions_name(qa_guest_permissions permissions);
bool qa_guest_permissions_parse(const char *name, qa_guest_permissions *out);
bool qa_guest_permissions_has_write(qa_guest_permissions permissions);
bool qa_guest_permissions_has_execute(qa_guest_permissions permissions);

typedef struct qa_guest_mapping {
  uint64_t base;
  size_t byte_length;
  qa_guest_permissions permissions;
  const char *label;
} qa_guest_mapping;

/* --- MappedGuestMemory operations beyond core/registers.h --- */
typedef struct qa_mapped_memory_ext {
  void *ctx;
  qa_guest_address (*map)(void *ctx, uint64_t base, size_t byte_length, qa_guest_permissions permissions,
                          const char *label, const uint8_t *bytes, size_t bytes_len);
  qa_guest_address (*allocate)(void *ctx, size_t byte_length, bool has_alignment, uint64_t alignment,
                               bool has_permissions, qa_guest_permissions permissions, const char *label);
  void (*unmap)(void *ctx, qa_guest_address address, size_t byte_length);
  void (*protect)(void *ctx, qa_guest_address address, size_t byte_length, qa_guest_permissions permissions);
  const qa_guest_mapping *(*mappings)(void *ctx, size_t *count);
  /* SparseGuestMemory extras; NULL selects the generic copy/write path. */
  void (*move)(void *ctx, qa_guest_address destination, qa_guest_address source, size_t byte_length);
  void (*fill)(void *ctx, qa_guest_address address, size_t byte_length, int value);
  int64_t (*find_zero)(void *ctx, qa_guest_address address, size_t maximum); /* -1: none */
} qa_mapped_memory_ext;

qa_guest_address qa_gmem_map(const qa_mapped_memory_ext *ext, uint64_t base, size_t byte_length,
                             qa_guest_permissions permissions, const char *label,
                             const uint8_t *bytes, size_t bytes_len);
qa_guest_address qa_gmem_allocate(const qa_mapped_memory_ext *ext, size_t byte_length,
                                  bool has_alignment, uint64_t alignment,
                                  bool has_permissions, qa_guest_permissions permissions, const char *label);
void qa_gmem_unmap(const qa_mapped_memory_ext *ext, qa_guest_address address, size_t byte_length);
void qa_gmem_protect(const qa_mapped_memory_ext *ext, qa_guest_address address, size_t byte_length,
                     qa_guest_permissions permissions);
const qa_guest_mapping *qa_gmem_mappings(const qa_mapped_memory_ext *ext, size_t *count);

/* Typed accessors over qa_mapped_guest_memory (MappedGuestMemory methods). */
int8_t qa_gmem_read_i8(const qa_mapped_guest_memory *mem, qa_guest_address address);
int16_t qa_gmem_read_i16(const qa_mapped_guest_memory *mem, qa_guest_address address);
int32_t qa_gmem_read_i32(const qa_mapped_guest_memory *mem, qa_guest_address address);
int64_t qa_gmem_read_i64(const qa_mapped_guest_memory *mem, qa_guest_address address);
double qa_gmem_read_f32(const qa_mapped_guest_memory *mem, qa_guest_address address);
double qa_gmem_read_f64(const qa_mapped_guest_memory *mem, qa_guest_address address);
bool qa_gmem_read_pointer(const qa_mapped_guest_memory *mem, qa_guest_address address, qa_guest_address *out);
uint64_t qa_gmem_read_pointer_raw(const qa_mapped_guest_memory *mem, qa_guest_address address);
void qa_gmem_write_i8(const qa_mapped_guest_memory *mem, qa_guest_address address, int8_t value);
void qa_gmem_write_i16(const qa_mapped_guest_memory *mem, qa_guest_address address, int16_t value);
void qa_gmem_write_i32(const qa_mapped_guest_memory *mem, qa_guest_address address, int32_t value);
void qa_gmem_write_i64(const qa_mapped_guest_memory *mem, qa_guest_address address, int64_t value);
void qa_gmem_write_f32(const qa_mapped_guest_memory *mem, qa_guest_address address, double value);
void qa_gmem_write_f64(const qa_mapped_guest_memory *mem, qa_guest_address address, double value);
void qa_gmem_write_pointer(const qa_mapped_guest_memory *mem, qa_guest_address address,
                           bool has, qa_guest_address value);

/* --- GuestSymbolName / GuestImport / GuestExport / GuestImage --- */
typedef struct qa_guest_symbol_name {
  bool is_ordinal;
  const char *name;    /* name kind */
  const char *version; /* name kind, NULL = TS null */
  int ordinal;         /* ordinal kind */
} qa_guest_symbol_name;

typedef struct qa_guest_import {
  const char *library;
  qa_guest_symbol_name symbol;
  qa_guest_address slot;
  bool weak;
} qa_guest_import;

typedef struct qa_guest_export {
  qa_guest_symbol_name symbol;
  bool is_forward;
  qa_guest_address address; /* address target */
  const char *forward_library;
  qa_guest_symbol_name forward_symbol;
} qa_guest_export;

typedef struct qa_guest_tls_template {
  qa_guest_module_identity image;
  uint8_t *initialized; /* owned copy in images built by loaders */
  size_t initialized_len;
  size_t zero_fill_bytes;
  uint64_t alignment;
  qa_guest_address *callbacks;
  size_t callback_count;
} qa_guest_tls_template;

typedef struct qa_guest_image {
  qa_guest_module_identity module;
  qa_guest_native_abi abi;
  qa_guest_address base;
  uint64_t preferred_base;
  uint64_t byte_length;
  bool has_entry_point;
  qa_guest_address entry_point;
  qa_guest_mapping *mappings;
  size_t mapping_count;
  qa_guest_import *imports;
  size_t import_count;
  qa_guest_export *exports;
  size_t export_count;
  bool has_tls;
  qa_guest_tls_template tls;
  qa_guest_address *initializers;
  size_t initializer_count;
  qa_guest_address *finalizers;
  size_t finalizer_count;
  qa_guest_unwind_region *unwind;
  size_t unwind_count;
} qa_guest_image;
/* Frees loader-owned arrays and any malloc'd strings they own (tracked by
 * the loaders in their image structs; this frees only the arrays/bytes). */
void qa_guest_image_free(qa_guest_image *image);

/* --- GuestHostCallback / GuestImportResolver --- */
typedef struct qa_guest_host_callback {
  const char *id;
  qa_guest_call_signature signature;
  void *ctx;
  qa_call_result (*invoke)(void *ctx, const qa_guest_call_context *context,
                           const qa_call_value *arguments, size_t argument_count);
} qa_guest_host_callback;

typedef enum qa_guest_import_resolution_kind {
  QA_RESOLUTION_GUEST = 0, QA_RESOLUTION_HOST, QA_RESOLUTION_UNRESOLVED
} qa_guest_import_resolution_kind;
typedef struct qa_guest_import_resolution {
  qa_guest_import_resolution_kind kind;
  qa_guest_address address; /* guest/host */
  qa_guest_module_identity module; /* guest */
  const qa_guest_host_callback *callback; /* host */
  qa_guest_import import_; /* unresolved */
  const char *detail; /* unresolved */
} qa_guest_import_resolution;

typedef struct qa_guest_import_resolver {
  void *ctx;
  qa_guest_import_resolution (*resolve)(void *ctx, const qa_guest_import *import_,
                                        const qa_guest_image *requesting);
} qa_guest_import_resolver;

/* --- values.ts functions --- */
int qa_storage_bytes(qa_abi_storage storage, int pointer_bytes);
int64_t qa_value_bytes(const qa_abi_value_layout *layout, int pointer_bytes);
int64_t qa_argument_bytes(const qa_abi_value_layout *layout, int pointer_bytes);
int64_t qa_value_alignment(const qa_abi_value_layout *layout, int pointer_bytes);
void qa_validate_value_layout(const qa_abi_value_layout *layout, int pointer_bytes);
qa_abi_value_layout qa_inferred_layout(const qa_call_value *value, bool promote);
/* storage must not be float32/float64 (TS Exclude); result truncated to width. */
uint64_t qa_encode_integer_value(qa_abi_storage storage, const qa_call_value *value,
                                 const qa_mapped_guest_memory *memory);
/* Malloc'd bytes; caller frees. NULL only on OOM. */
uint8_t *qa_encode_value(const qa_abi_value_layout *layout, const qa_call_value *value,
                         const qa_mapped_guest_memory *memory, size_t *out_len);
qa_call_value qa_decode_value(const qa_abi_value_layout *layout, const uint8_t *bytes, size_t len,
                              const qa_mapped_guest_memory *memory);
uint8_t *qa_encode_argument_value(const qa_abi_value_layout *layout, const qa_call_value *value,
                                  const qa_mapped_guest_memory *memory, size_t *out_len);
int64_t qa_align_up(int64_t value, int64_t alignment);
int64_t qa_align_down(int64_t value, int64_t alignment);
uint64_t qa_align_down_u64(uint64_t value, uint64_t alignment);

#endif

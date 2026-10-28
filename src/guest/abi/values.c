/* Port of src/guest/abi/values.ts. GPL-2.0-or-later. */
#include "qa/guest/abi/values.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const char *const storage_names[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32",
                                            "int64", "uint64", "float32", "float64", "pointer"};
const char *qa_abi_storage_name(qa_abi_storage storage) {
  return (int)storage >= 0 && (int)storage <= (int)QA_STORAGE_POINTER ? storage_names[storage] : "";
}
bool qa_abi_storage_parse(const char *name, qa_abi_storage *out) {
  if (name == NULL || out == NULL) return false;
  for (int i = 0; i <= (int)QA_STORAGE_POINTER; i++)
    if (strcmp(name, storage_names[i]) == 0) { *out = (qa_abi_storage)i; return true; }
  return false;
}
static const char *const value_kind_names[] = {"void", "int32", "uint32", "int64", "uint64",
                                               "float32", "float64", "pointer", "aggregate"};
const char *qa_call_value_kind_name(qa_call_value_kind kind) {
  return (int)kind >= 0 && (int)kind <= (int)QA_CALL_AGGREGATE ? value_kind_names[kind] : "";
}

static qa_call_value value_new(qa_call_value_kind kind) {
  qa_call_value v; memset(&v, 0, sizeof v); v.kind = kind; return v;
}
qa_call_value qa_call_void(void) { return value_new(QA_CALL_VOID); }
qa_call_value qa_call_int32(int32_t value) { qa_call_value v = value_new(QA_CALL_INT32); v.number = (double)value; return v; }
qa_call_value qa_call_uint32(uint32_t value) { qa_call_value v = value_new(QA_CALL_UINT32); v.number = (double)value; return v; }
qa_call_value qa_call_int64(int64_t value) { qa_call_value v = value_new(QA_CALL_INT64); v.int64_value = value; return v; }
qa_call_value qa_call_uint64(uint64_t value) { qa_call_value v = value_new(QA_CALL_UINT64); v.uint64_value = value; return v; }
qa_call_value qa_call_float32(double value) { qa_call_value v = value_new(QA_CALL_FLOAT32); v.number = (double)(float)value; return v; }
qa_call_value qa_call_float64(double value) { qa_call_value v = value_new(QA_CALL_FLOAT64); v.number = value; return v; }
qa_call_value qa_call_pointer(bool has, qa_guest_core_address value) {
  qa_call_value v = value_new(QA_CALL_POINTER); v.pointer_has = has; v.pointer = value; return v;
}
qa_call_value qa_call_aggregate(qa_abi_layout layout, const uint8_t *bytes, size_t len) {
  qa_call_value v = value_new(QA_CALL_AGGREGATE);
  v.layout = layout; v.bytes_len = len;
  v.bytes = (uint8_t *)malloc(len == 0 ? 1 : len);
  if (v.bytes == NULL) { v.kind = QA_CALL_VOID; v.bytes_len = 0; return v; }
  if (len > 0) memcpy(v.bytes, bytes, len);
  return v;
}
void qa_call_value_free(qa_call_value *value) {
  if (value == NULL) return;
  free(value->bytes); value->bytes = NULL; value->bytes_len = 0;
}
int64_t qa_call_value_bigint_i64(const qa_call_value *value) {
  switch (value->kind) {
    case QA_CALL_INT32: case QA_CALL_UINT32: return (int64_t)value->number;
    case QA_CALL_INT64: return value->int64_value;
    case QA_CALL_UINT64: return (int64_t)value->uint64_value;
    default: qa_range_error("Guest integer required");
  }
}
uint64_t qa_call_value_bigint_u64(const qa_call_value *value) { return (uint64_t)qa_call_value_bigint_i64(value); }

static const char *const perm_names[] = {"none", "read", "read-write", "read-execute",
                                         "read-write-execute", "execute"};
const char *qa_guest_permissions_name(qa_guest_permissions permissions) {
  return (int)permissions >= 0 && (int)permissions <= (int)QA_PERM_EXECUTE ? perm_names[permissions] : "";
}
bool qa_guest_permissions_parse(const char *name, qa_guest_permissions *out) {
  if (name == NULL || out == NULL) return false;
  for (int i = 0; i <= (int)QA_PERM_EXECUTE; i++)
    if (strcmp(name, perm_names[i]) == 0) { *out = (qa_guest_permissions)i; return true; }
  return false;
}
bool qa_guest_permissions_has_write(qa_guest_permissions p) {
  return p == QA_PERM_READ_WRITE || p == QA_PERM_READ_WRITE_EXECUTE;
}
bool qa_guest_permissions_has_execute(qa_guest_permissions p) {
  return p == QA_PERM_READ_EXECUTE || p == QA_PERM_READ_WRITE_EXECUTE || p == QA_PERM_EXECUTE;
}

qa_guest_core_address qa_gmem_map(const qa_mapped_memory_ext *ext, uint64_t base, size_t byte_length,
                             qa_guest_permissions permissions, const char *label,
                             const uint8_t *bytes, size_t bytes_len) {
  if (ext == NULL || ext->map == NULL) qa_range_error("Mapped guest memory has no map operation");
  return ext->map(ext->ctx, base, byte_length, permissions, label, bytes, bytes_len);
}
qa_guest_core_address qa_gmem_allocate(const qa_mapped_memory_ext *ext, size_t byte_length,
                                  bool has_alignment, uint64_t alignment,
                                  bool has_permissions, qa_guest_permissions permissions, const char *label) {
  if (ext == NULL || ext->allocate == NULL) qa_range_error("Mapped guest memory has no allocate operation");
  return ext->allocate(ext->ctx, byte_length, has_alignment, alignment, has_permissions, permissions, label);
}
void qa_gmem_unmap(const qa_mapped_memory_ext *ext, qa_guest_core_address address, size_t byte_length) {
  if (ext == NULL || ext->unmap == NULL) qa_range_error("Mapped guest memory has no unmap operation");
  ext->unmap(ext->ctx, address, byte_length);
}
void qa_gmem_protect(const qa_mapped_memory_ext *ext, qa_guest_core_address address, size_t byte_length,
                     qa_guest_permissions permissions) {
  if (ext == NULL || ext->protect == NULL) qa_range_error("Mapped guest memory has no protect operation");
  ext->protect(ext->ctx, address, byte_length, permissions);
}
const qa_guest_mapping *qa_gmem_mappings(const qa_mapped_memory_ext *ext, size_t *count) {
  if (ext == NULL || ext->mappings == NULL) { if (count != NULL) *count = 0; return NULL; }
  return ext->mappings(ext->ctx, count);
}

int8_t qa_gmem_read_i8(const qa_mapped_guest_memory *m, qa_guest_core_address a) { return (int8_t)qa_memory_read_u8(m, a); }
int16_t qa_gmem_read_i16(const qa_mapped_guest_memory *m, qa_guest_core_address a) { return (int16_t)qa_memory_read_u16(m, a); }
int32_t qa_gmem_read_i32(const qa_mapped_guest_memory *m, qa_guest_core_address a) { return (int32_t)qa_memory_read_u32(m, a); }
int64_t qa_gmem_read_i64(const qa_mapped_guest_memory *m, qa_guest_core_address a) { return (int64_t)qa_memory_read_u64(m, a); }
double qa_gmem_read_f32(const qa_mapped_guest_memory *m, qa_guest_core_address a) {
  uint32_t bits = qa_memory_read_u32(m, a); float f; memcpy(&f, &bits, 4); return (double)f;
}
double qa_gmem_read_f64(const qa_mapped_guest_memory *m, qa_guest_core_address a) {
  uint64_t bits = qa_memory_read_u64(m, a); double d; memcpy(&d, &bits, 8); return d;
}
uint64_t qa_gmem_read_pointer_raw(const qa_mapped_guest_memory *m, qa_guest_core_address a) {
  return m->pointer_bytes == 4 ? qa_memory_read_u32(m, a) : qa_memory_read_u64(m, a);
}
bool qa_gmem_read_pointer(const qa_mapped_guest_memory *m, qa_guest_core_address a, qa_guest_core_address *out) {
  return qa_memory_pointer(m, qa_gmem_read_pointer_raw(m, a), out);
}
void qa_gmem_write_i8(const qa_mapped_guest_memory *m, qa_guest_core_address a, int8_t v) { qa_memory_write_u8(m, a, (uint8_t)v); }
void qa_gmem_write_i16(const qa_mapped_guest_memory *m, qa_guest_core_address a, int16_t v) { qa_memory_write_u16(m, a, (uint16_t)v); }
void qa_gmem_write_i32(const qa_mapped_guest_memory *m, qa_guest_core_address a, int32_t v) { qa_memory_write_u32(m, a, (uint32_t)v); }
void qa_gmem_write_i64(const qa_mapped_guest_memory *m, qa_guest_core_address a, int64_t v) { qa_memory_write_u64(m, a, (uint64_t)v); }
void qa_gmem_write_f32(const qa_mapped_guest_memory *m, qa_guest_core_address a, double v) {
  float f = (float)v; uint32_t bits; memcpy(&bits, &f, 4); qa_memory_write_u32(m, a, bits);
}
void qa_gmem_write_f64(const qa_mapped_guest_memory *m, qa_guest_core_address a, double v) {
  uint64_t bits; memcpy(&bits, &v, 8); qa_memory_write_u64(m, a, bits);
}
void qa_gmem_write_pointer(const qa_mapped_guest_memory *m, qa_guest_core_address a, bool has, qa_guest_core_address v) {
  uint64_t raw = has ? v.byte_offset : 0;
  if (has) (void)qa_memory_offset(m, v, 0);
  if (m->pointer_bytes == 4) qa_memory_write_u32(m, a, (uint32_t)raw); else qa_memory_write_u64(m, a, raw);
}

void qa_guest_image_free(qa_guest_image *image) {
  if (image == NULL) return;
  free(image->mappings); free(image->imports); free(image->exports);
  free(image->tls.initialized); free(image->tls.callbacks);
  free(image->initializers); free(image->finalizers);
  qa_elf_unwind_regions_free(image->unwind, image->unwind_count);
  memset(image, 0, sizeof *image);
}

int qa_storage_bytes(qa_abi_storage storage, int pointer_bytes) {
  switch (storage) {
    case QA_STORAGE_INT8: case QA_STORAGE_UINT8: return 1;
    case QA_STORAGE_INT16: case QA_STORAGE_UINT16: return 2;
    case QA_STORAGE_INT32: case QA_STORAGE_UINT32: case QA_STORAGE_FLOAT32: return 4;
    case QA_STORAGE_INT64: case QA_STORAGE_UINT64: case QA_STORAGE_FLOAT64: return 8;
    case QA_STORAGE_POINTER: return pointer_bytes;
  }
  return 0;
}
int64_t qa_value_bytes(const qa_abi_value_layout *layout, int pointer_bytes) {
  return layout->is_aggregate ? layout->layout.byte_length
                              : qa_storage_bytes(layout->storage, pointer_bytes);
}
int64_t qa_argument_bytes(const qa_abi_value_layout *layout, int pointer_bytes) {
  int64_t bytes = qa_value_bytes(layout, pointer_bytes);
  return !layout->is_aggregate && bytes < 4 ? 4 : bytes;
}
int64_t qa_value_alignment(const qa_abi_value_layout *layout, int pointer_bytes) {
  if (layout->is_aggregate) return layout->layout.alignment;
  int size = qa_storage_bytes(layout->storage, pointer_bytes);
  return pointer_bytes < size ? pointer_bytes : size;
}
void qa_validate_value_layout(const qa_abi_value_layout *layout, int pointer_bytes) {
  if (!layout->is_aggregate) return;
  const qa_abi_layout *record = &layout->layout;
  if (record->pointer_bytes != pointer_bytes || record->byte_length <= 0 || record->alignment <= 0
      || (record->alignment & (record->alignment - 1)) != 0 || record->alignment > 4096)
    qa_range_error("Invalid aggregate ABI layout");
  for (size_t i = 0; i < record->field_count; i++) {
    const qa_abi_field_layout *field = &record->fields[i];
    if (field->byte_offset < 0 || field->count < 0
        || field->byte_offset + field->count * qa_storage_bytes(field->storage, pointer_bytes) > record->byte_length)
      qa_range_error("Aggregate field %s exceeds its record", field->name != NULL ? field->name : "");
  }
}
qa_abi_value_layout qa_inferred_layout(const qa_call_value *value, bool promote) {
  qa_abi_value_layout layout; memset(&layout, 0, sizeof layout);
  if (value->kind == QA_CALL_AGGREGATE) { layout.is_aggregate = true; layout.layout = value->layout; return layout; }
  qa_abi_storage storage;
  switch (value->kind) {
    case QA_CALL_INT32: storage = QA_STORAGE_INT32; break;
    case QA_CALL_UINT32: storage = QA_STORAGE_UINT32; break;
    case QA_CALL_INT64: storage = QA_STORAGE_INT64; break;
    case QA_CALL_UINT64: storage = QA_STORAGE_UINT64; break;
    case QA_CALL_FLOAT32: storage = promote ? QA_STORAGE_FLOAT64 : QA_STORAGE_FLOAT32; break;
    case QA_CALL_FLOAT64: storage = QA_STORAGE_FLOAT64; break;
    case QA_CALL_POINTER: storage = QA_STORAGE_POINTER; break;
    default: storage = QA_STORAGE_INT32; break; /* void never inferred at TS call sites */
  }
  layout.storage = storage;
  return layout;
}
uint64_t qa_encode_integer_value(qa_abi_storage storage, const qa_call_value *value,
                                 const qa_mapped_guest_memory *memory) {
  if (storage == QA_STORAGE_POINTER) {
    if (value->kind != QA_CALL_POINTER) qa_range_error("Pointer ABI argument requires a guest pointer");
    return !value->pointer_has ? 0 : qa_memory_offset(memory, value->pointer, 0).byte_offset;
  }
  if (value->kind != QA_CALL_INT32 && value->kind != QA_CALL_UINT32
      && value->kind != QA_CALL_INT64 && value->kind != QA_CALL_UINT64)
    qa_range_error("Integer ABI argument requires an integer value");
  if ((value->kind == QA_CALL_INT32 || value->kind == QA_CALL_UINT32)
      && (value->number != floor(value->number) || fabs(value->number) > 9007199254740991.0))
    qa_range_error("Integer ABI argument is not an exact integer");
  int bits = qa_storage_bytes(storage, memory->pointer_bytes) * 8;
  uint64_t raw = qa_call_value_bigint_u64(value);
  return bits >= 64 ? raw : raw & ((1ULL << bits) - 1ULL);
}
static void store_le(uint8_t *bytes, uint64_t raw, int width) {
  for (int i = 0; i < width; i++) bytes[i] = (uint8_t)(raw >> (i * 8));
}
static uint64_t load_le(const uint8_t *bytes, int width) {
  uint64_t raw = 0;
  for (int i = width - 1; i >= 0; i--) raw = (raw << 8) | bytes[i];
  return raw;
}
uint8_t *qa_encode_value(const qa_abi_value_layout *layout, const qa_call_value *value,
                         const qa_mapped_guest_memory *memory, size_t *out_len) {
  qa_validate_value_layout(layout, memory->pointer_bytes);
  if (layout->is_aggregate) {
    if (value->kind != QA_CALL_AGGREGATE
        || strcmp(value->layout.id != NULL ? value->layout.id : "", layout->layout.id != NULL ? layout->layout.id : "") != 0
        || (int64_t)value->bytes_len != layout->layout.byte_length
        || value->layout.byte_length != layout->layout.byte_length
        || value->layout.pointer_bytes != layout->layout.pointer_bytes)
      qa_range_error("Aggregate call value differs from its signature");
    size_t len = value->bytes_len;
    uint8_t *bytes = (uint8_t *)malloc(len == 0 ? 1 : len);
    if (bytes == NULL) return NULL;
    if (len > 0) memcpy(bytes, value->bytes, len);
    if (out_len != NULL) *out_len = len;
    return bytes;
  }
  int width = qa_storage_bytes(layout->storage, memory->pointer_bytes);
  uint8_t *bytes = (uint8_t *)calloc((size_t)width, 1);
  if (bytes == NULL) return NULL;
  if (out_len != NULL) *out_len = (size_t)width;
  if (layout->storage == QA_STORAGE_FLOAT32 || layout->storage == QA_STORAGE_FLOAT64) {
    if (value->kind != QA_CALL_FLOAT32 && value->kind != QA_CALL_FLOAT64) {
      free(bytes);
      qa_range_error("Floating ABI argument requires a floating value");
    }
    if (layout->storage == QA_STORAGE_FLOAT32) { float f = (float)value->number; memcpy(bytes, &f, 4); }
    else memcpy(bytes, &value->number, 8);
    return bytes;
  }
  store_le(bytes, qa_encode_integer_value(layout->storage, value, memory), width);
  return bytes;
}
qa_call_value qa_decode_value(const qa_abi_value_layout *layout, const uint8_t *bytes, size_t len,
                              const qa_mapped_guest_memory *memory) {
  if ((int64_t)len != qa_value_bytes(layout, memory->pointer_bytes))
    qa_range_error("ABI value byte length differs from its layout");
  if (layout->is_aggregate) return qa_call_aggregate(layout->layout, bytes, len);
  switch (layout->storage) {
    case QA_STORAGE_INT8: return qa_call_int32((int8_t)bytes[0]);
    case QA_STORAGE_UINT8: return qa_call_uint32(bytes[0]);
    case QA_STORAGE_INT16: return qa_call_int32((int16_t)load_le(bytes, 2));
    case QA_STORAGE_UINT16: return qa_call_uint32((uint32_t)load_le(bytes, 2));
    case QA_STORAGE_INT32: return qa_call_int32((int32_t)load_le(bytes, 4));
    case QA_STORAGE_UINT32: return qa_call_uint32((uint32_t)load_le(bytes, 4));
    case QA_STORAGE_INT64: return qa_call_int64((int64_t)load_le(bytes, 8));
    case QA_STORAGE_UINT64: return qa_call_uint64(load_le(bytes, 8));
    case QA_STORAGE_FLOAT32: { float f; memcpy(&f, bytes, 4); return qa_call_float32((double)f); }
    case QA_STORAGE_FLOAT64: { double d; memcpy(&d, bytes, 8); return qa_call_float64(d); }
    case QA_STORAGE_POINTER: {
      qa_guest_core_address address = {0};
      bool has = qa_memory_pointer(memory, load_le(bytes, memory->pointer_bytes), &address);
      return qa_call_pointer(has, address);
    }
  }
  return qa_call_void();
}
uint8_t *qa_encode_argument_value(const qa_abi_value_layout *layout, const qa_call_value *value,
                                  const qa_mapped_guest_memory *memory, size_t *out_len) {
  size_t len = 0;
  uint8_t *bytes = qa_encode_value(layout, value, memory, &len);
  if (bytes == NULL) return NULL;
  int64_t size = qa_argument_bytes(layout, memory->pointer_bytes);
  if ((int64_t)len == size) { if (out_len != NULL) *out_len = len; return bytes; }
  bool signed_ = !layout->is_aggregate
      && (layout->storage == QA_STORAGE_INT8 || layout->storage == QA_STORAGE_INT16
          || layout->storage == QA_STORAGE_INT32 || layout->storage == QA_STORAGE_INT64);
  uint8_t fill = signed_ && len > 0 && (bytes[len - 1] & 0x80) != 0 ? 255 : 0;
  uint8_t *extended = (uint8_t *)malloc((size_t)size);
  if (extended == NULL) { free(bytes); return NULL; }
  memset(extended, fill, (size_t)size);
  memcpy(extended, bytes, len);
  free(bytes);
  if (out_len != NULL) *out_len = (size_t)size;
  return extended;
}
int64_t qa_align_up(int64_t value, int64_t alignment) {
  return (int64_t)ceil((double)value / (double)alignment) * alignment;
}
int64_t qa_align_down(int64_t value, int64_t alignment) { return value - value % alignment; }
uint64_t qa_align_down_u64(uint64_t value, uint64_t alignment) { return value - value % alignment; }

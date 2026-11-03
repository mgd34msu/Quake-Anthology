/* See include/qa/compat/q2/native_mod_host_api.h. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_host_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_compat_q2_native_mod_finally(void (*body)(void *), void *body_ctx, void (*cleanup)(void *), void *cleanup_ctx) {
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(body, body_ctx, &thrown);
  cleanup(cleanup_ctx);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}

int qa_compat_q2_native_storage_bytes(const char *s, int64_t pointer_bytes) {
  if (!strcmp(s, "pointer")) return (int)pointer_bytes;
  if (!strcmp(s, "int8") || !strcmp(s, "uint8")) return 1;
  if (!strcmp(s, "int16") || !strcmp(s, "uint16")) return 2;
  if (!strcmp(s, "int32") || !strcmp(s, "uint32") || !strcmp(s, "float32")) return 4;
  if (!strcmp(s, "int64") || !strcmp(s, "uint64") || !strcmp(s, "float64")) return 8;
  qa_range_error("Invalid guest storage %s", s);
}
int qa_compat_q2_native_scalar_bytes(const char *e) {
  size_t n = strlen(e);
  if (n >= 2 && !strcmp(e + n - 2, "64")) return 8;
  if (n >= 2 && !strcmp(e + n - 2, "16")) return 2;
  if (n >= 1 && e[n - 1] == '8') return 1;
  return 4;
}
static void put_le(uint8_t *out, uint64_t value, int bytes) { for (int i = 0; i < bytes; i++) out[i] = (uint8_t)(value >> (8 * i)); }
static uint64_t get_le(const uint8_t *in, int bytes) { uint64_t v = 0; for (int i = 0; i < bytes; i++) v |= (uint64_t)in[i] << (8 * i); return v; }
int qa_compat_q2_native_encode_scalar(const qa_compat_q2_memory *memory, const char *storage, const qa_guest_call_value *value, uint8_t *out) {
  int bytes = qa_compat_q2_native_storage_bytes(storage, memory->pointer_bytes);
  if (!strcmp(storage, "pointer")) {
    if (value->kind != QA_GUEST_VALUE_POINTER) qa_range_error("Pointer ABI argument requires a guest pointer");
    uint64_t raw = value->pointer_value.has_value ? (uint64_t)qa_compat_q2_mem_offset(memory, value->pointer_value.value, 0).byte_offset : 0;
    put_le(out, raw, bytes); return bytes;
  }
  if (!strcmp(storage, "float32") || !strcmp(storage, "float64")) {
    if (value->kind != QA_GUEST_VALUE_FLOAT32 && value->kind != QA_GUEST_VALUE_FLOAT64) qa_range_error("Floating ABI argument requires a floating value");
    if (bytes == 4) { float f = (float)value->number_value; uint32_t u; memcpy(&u, &f, 4); put_le(out, u, 4); }
    else { double d = value->number_value; uint64_t u; memcpy(&u, &d, 8); put_le(out, u, 8); }
    return bytes;
  }
  uint64_t raw;
  switch (value->kind) {
    case QA_GUEST_VALUE_INT32: case QA_GUEST_VALUE_UINT32:
      if (value->number_value != trunc(value->number_value) || fabs(value->number_value) > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Integer ABI argument is not an exact integer");
      raw = (uint64_t)(int64_t)value->number_value; break;
    case QA_GUEST_VALUE_INT64: raw = (uint64_t)value->int64_value; break;
    case QA_GUEST_VALUE_UINT64: raw = value->uint64_value; break;
    default: qa_range_error("Integer ABI argument requires an integer value");
  }
  put_le(out, raw, bytes); /* BigInt.asUintN at the storage width. */
  return bytes;
}
qa_guest_call_value qa_compat_q2_native_decode_scalar(const qa_compat_q2_memory *memory, const char *s, const uint8_t *bytes) {
  qa_guest_call_value v; memset(&v, 0, sizeof v);
  int n = qa_compat_q2_native_storage_bytes(s, memory->pointer_bytes);
  uint64_t raw = get_le(bytes, n);
  if (!strcmp(s, "pointer")) { v.kind = QA_GUEST_VALUE_POINTER; v.pointer_value = qa_compat_q2_mem_pointer(memory, (int64_t)raw); return v; }
  if (!strcmp(s, "float32")) { uint32_t u = (uint32_t)raw; float f; memcpy(&f, &u, 4); v.kind = QA_GUEST_VALUE_FLOAT32; v.number_value = f; return v; }
  if (!strcmp(s, "float64")) { double d; memcpy(&d, &raw, 8); v.kind = QA_GUEST_VALUE_FLOAT64; v.number_value = d; return v; }
  if (!strcmp(s, "int8")) { v.kind = QA_GUEST_VALUE_INT32; v.number_value = (int8_t)raw; return v; }
  if (!strcmp(s, "int16")) { v.kind = QA_GUEST_VALUE_INT32; v.number_value = (int16_t)raw; return v; }
  if (!strcmp(s, "int32")) { v.kind = QA_GUEST_VALUE_INT32; v.number_value = (int32_t)raw; return v; }
  if (!strcmp(s, "int64")) { v.kind = QA_GUEST_VALUE_INT64; v.int64_value = (int64_t)raw; return v; }
  if (!strcmp(s, "uint64")) { v.kind = QA_GUEST_VALUE_UINT64; v.uint64_value = raw; return v; }
  v.kind = QA_GUEST_VALUE_UINT32; v.number_value = (double)raw; return v; /* uint8|uint16|uint32 */
}

qa_guest_address qa_compat_q2_native_mod_image_rva(const qa_compat_q2_native_mod_host *host, double rva) {
  return qa_compat_q2_mem_offset(host->memory, host->image_base, (int64_t)rva);
}
qa_guest_address qa_compat_q2_native_mod_entry_address(const qa_compat_q2_native_mod_host *host, qa_native_mod_entry entry) {
  return !strcmp(entry.kind, "export") ? host->entry(host->ctx, entry.name) : qa_compat_q2_native_mod_image_rva(host, entry.rva);
}
uint64_t qa_compat_q2_native_mod_stack(const qa_compat_q2_native_mod_host *host) {
  return host->runner->read_register(host->runner->ctx, "rsp", host->memory->pointer_bytes == 4 ? 32 : 64);
}
double qa_compat_q2_native_mod_table_slot(const qa_compat_q2_native_mod_host *host, qa_guest_address address, const char *message) {
  qa_compat_q2_native_mod_entities table = host->entities(host->ctx);
  int64_t difference = address.byte_offset - table.base.byte_offset, stride = (int64_t)table.stride;
  if (difference < 0 || difference % stride != 0 || (double)(difference / stride) >= table.count) qa_range_error("%s", message);
  return (double)(difference / stride);
}
bool qa_compat_q2_text_equal(const char *a, const char *b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }
bool qa_compat_q2_regular_armor_equal(qa_regular_armor_state a, qa_regular_armor_state b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case QA_REGULAR_ARMOR_NONE: return true;
    case QA_REGULAR_ARMOR_Q1: return a.points == b.points && a.absorption == b.absorption && qa_compat_q2_text_equal(a.item, b.item);
    case QA_REGULAR_ARMOR_Q2: return a.points == b.points && a.normal_protection == b.normal_protection && a.energy_protection == b.energy_protection && qa_compat_q2_text_equal(a.item, b.item);
    case QA_REGULAR_ARMOR_Q3: return a.points == b.points && a.protection == b.protection;
    case QA_REGULAR_ARMOR_SOURCE: return a.points == b.points && qa_compat_q2_text_equal(a.item, b.item);
  }
  return false;
}
bool qa_compat_q2_powered_protection_equal(qa_powered_protection_state a, qa_powered_protection_state b) {
  return qa_compat_q2_text_equal(a.kind, b.kind) && (!strcmp(a.kind, "none") || a.cells == b.cells);
}
void *qa_compat_q2_grow(void **items, size_t *count, size_t *capacity, size_t element) {
  if (*count == *capacity) {
    size_t next = *capacity == 0 ? 8 : *capacity * 2;
    void *grown = realloc(*items, next * element);
    if (grown == NULL) qa_range_error("Out of memory");
    *items = grown; *capacity = next;
  }
  char *slot = (char *)*items + (*count)++ * element;
  memset(slot, 0, element); return slot;
}
qa_source_time qa_compat_q2_services_time(const qa_mod_host_services *services) { return services->time(services->ctx); }
double qa_compat_q2_time_seconds(qa_source_time time) { return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000; }
uint64_t qa_compat_q2_native_mod_bind_region(const qa_compat_q2_native_mod_host *host, qa_guest_address entry, qa_guest_address join, void *cb_ctx,
    void (*cb)(void *, qa_compat_q2_inline_continuation *), void *accepts_ctx, bool (*accepts)(void *)) {
  return host->runner->bind_inline_region_accepts(host->runner->ctx, entry, join, host->abi, cb_ctx, cb, accepts_ctx, accepts);
}
void qa_compat_q2_native_mod_unbind_region(const qa_compat_q2_native_mod_host *host, uint64_t handle) { host->runner->remove_inline_region(host->runner->ctx, handle); }
qa_compat_q2_native_entry *qa_compat_q2_native_mod_bind_entry(const qa_compat_q2_native_mod_host *host, qa_guest_address address, const char *id,
    qa_compat_q2_guest_call_signature signature, void *ctx, qa_compat_q2_native_entry_execute_fn execute, void *accepts_ctx, bool (*accepts)(void *)) {
  return qa_compat_q2_native_entry_bind(host->memory, host->runner, host->ctx, host->invoke, address, id, signature, ctx, execute, accepts_ctx, accepts);
}

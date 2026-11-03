/* Port of src/compat/q2/native-mod-region.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_region.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool safe_integer(double v) { return isfinite(v) && v == trunc(v) && fabs(v) <= (double)QA_MAX_SAFE_INTEGER; }
static int width(int bytes) {
  switch (bytes) { case 1: return 8; case 2: return 16; case 4: return 32; case 8: return 64; default: qa_range_error("Invalid native region scalar width"); }
}
/* TS /^r(?:[89]|1[0-5])$/. */
static bool extended_register(const char *r) {
  if (r[0] != 'r') return false;
  if ((r[1] == '8' || r[1] == '9') && r[2] == '\0') return true;
  return r[1] == '1' && r[2] >= '0' && r[2] <= '5' && r[3] == '\0';
}
typedef struct extent { const char *bank; double offset, length; } extent;
static extent extent_of(const qa_native_mod_protection_region *region, qa_native_mod_region_location location, int64_t pointer_bytes) {
  double length = qa_compat_q2_native_storage_bytes(location.storage, pointer_bytes), pb = (double)pointer_bytes; extent e;
  if (location.kind == QA_NATIVE_MOD_LOCATION_REGISTER) {
    if (pointer_bytes == 4 && (length > 4 || extended_register(location.reg))) qa_range_error("Native region register exceeds the source architecture");
    e.bank = location.reg; e.offset = 0; e.length = length; return e;
  }
  if (!safe_integer(location.offset) || location.offset < 0) qa_range_error("Invalid native region field offset");
  if (location.kind == QA_NATIVE_MOD_LOCATION_SIMD) {
    if (!safe_integer(location.index) || location.index < 0 || location.index >= (pointer_bytes == 4 ? 8 : 16) || location.offset + length > 16)
      qa_range_error("Native region SIMD field exceeds its register");
    e.bank = "simd"; e.offset = location.index * 16 + location.offset; e.length = length; return e;
  }
  if (location.offset + length > region->frame.stack_bytes + pb + region->frame.argument_bytes
      || (location.offset < region->frame.stack_bytes + pb && location.offset + length > region->frame.stack_bytes))
    qa_range_error("Native region field exceeds its frame or overlaps the return address");
  e.bank = "stack"; e.offset = location.offset; e.length = length; return e;
}
void qa_compat_q2_validate_native_mod_region(const qa_native_mod_protection_region *region, int64_t pointer_bytes) {
  double boundaries[4] = {region->frame.entry, region->entry, region->join, region->frame.exit};
  bool invalid = false;
  for (int i = 0; i < 4; i++) if (!safe_integer(boundaries[i]) || boundaries[i] < 0) invalid = true;
  for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) if (boundaries[i] == boundaries[j]) invalid = true;
  double sizes[2] = {region->frame.stack_bytes, region->frame.argument_bytes};
  for (int i = 0; i < 2; i++) if (!safe_integer(sizes[i]) || sizes[i] < 0 || fmod(sizes[i], (double)pointer_bytes) != 0) invalid = true;
  if (region->call.skip_count != 0 || !strcmp(region->result.storage, "pointer")) invalid = true;
  if (invalid) qa_range_error("Invalid standalone native region frame");
  extent *ranges = calloc(region->input_count == 0 ? 1 : region->input_count, sizeof *ranges);
  if (ranges == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < region->input_count; i++) ranges[i] = extent_of(region, region->inputs[i].target, pointer_bytes);
  (void)extent_of(region, region->result, pointer_bytes);
  for (size_t i = 0; i < region->input_count; i++) for (size_t j = 0; j < i; j++)
    if (!strcmp(ranges[j].bank, ranges[i].bank) && ranges[i].offset < ranges[j].offset + ranges[j].length && ranges[j].offset < ranges[i].offset + ranges[i].length)
      qa_range_error("Native region inputs overlap");
  free(ranges);
}

typedef enum phase_kind { PHASE_PROLOGUE, PHASE_REGION, PHASE_EXECUTING, PHASE_EPILOGUE } phase_kind;
typedef struct stack_input { qa_guest_address address; uint8_t saved[8]; size_t length; } stack_input;
struct qa_compat_q2_native_mod_region_execution {
  const qa_compat_q2_native_mod_host *host;
  const qa_native_mod_protection_region *definition;
  qa_guest_call_value *inputs; size_t input_count;
  const qa_compat_q2_native_mod_region_authority *authority;
  bool has_entry_stack; uint64_t entry_stack;
  void *before;
  phase_kind phase;
  bool has_output; qa_guest_call_value output;
  stack_input *stack_inputs; size_t stack_input_count, stack_input_capacity;
  uint64_t observe; uint64_t regions[3];
};
typedef qa_compat_q2_native_mod_region_execution execution;
static qa_guest_address at(const execution *s, double rva) { return qa_compat_q2_native_mod_image_rva(s->host, rva); }
static void assert_current(const execution *s) {
  if (s->authority != NULL && !s->authority->current(s->authority->ctx)) qa_compat_q2_native_throw(s->authority, s->authority->error);
}
static bool current(const execution *s) {
  return s->has_entry_stack && qa_compat_q2_native_mod_stack(s->host) == s->entry_stack - (uint64_t)s->definition->frame.stack_bytes;
}
static qa_guest_address stack_address(const execution *s, double offset) {
  qa_guest_address_result address = qa_compat_q2_mem_pointer(s->host->memory, (int64_t)(qa_compat_q2_native_mod_stack(s->host) + (uint64_t)offset));
  if (!address.has_value) qa_range_error("Native region has no live stack");
  return address.value;
}
static void read_location(const execution *s, qa_native_mod_region_location location, uint8_t *out) {
  const qa_compat_q2_native_mod_host *host = s->host; const qa_compat_q2_memory *memory = host->memory;
  int count = qa_compat_q2_native_storage_bytes(location.storage, memory->pointer_bytes);
  if (location.kind == QA_NATIVE_MOD_LOCATION_STACK) { memory->copy(memory->ctx, stack_address(s, location.offset), out, count); return; }
  if (location.kind == QA_NATIVE_MOD_LOCATION_SIMD) { memcpy(out, host->runner->simd_xmm + (size_t)(location.index * 16 + location.offset), (size_t)count); return; }
  uint64_t raw = host->runner->read_register(host->runner->ctx, location.reg, width(count));
  for (int i = 0; i < count; i++) out[i] = (uint8_t)(raw >> (8 * i));
}
static void write_location(execution *s, qa_native_mod_region_location location, const qa_guest_call_value *value) {
  const qa_compat_q2_native_mod_host *host = s->host; const qa_compat_q2_memory *memory = host->memory;
  uint8_t bytes[8]; int length = qa_compat_q2_native_encode_scalar(memory, location.storage, value, bytes);
  if (location.kind == QA_NATIVE_MOD_LOCATION_STACK) {
    qa_guest_address address = stack_address(s, location.offset);
    stack_input *saved = qa_compat_q2_grow((void **)&s->stack_inputs, &s->stack_input_count, &s->stack_input_capacity, sizeof *saved);
    saved->address = address; saved->length = (size_t)length;
    memory->copy(memory->ctx, address, saved->saved, length);
    memory->write(memory->ctx, address, bytes, (size_t)length);
  } else if (location.kind == QA_NATIVE_MOD_LOCATION_SIMD) {
    memcpy(host->runner->simd_xmm + (size_t)(location.index * 16 + location.offset), bytes, (size_t)length);
  } else {
    uint64_t raw = 0; for (int i = 0; i < length; i++) raw |= (uint64_t)bytes[i] << (8 * i);
    host->runner->write_register(host->runner->ctx, location.reg, width(length), raw);
  }
}
execution *qa_compat_q2_native_mod_region_execution_create(const qa_compat_q2_native_mod_host *host, const qa_native_mod_protection_region *definition,
    const qa_guest_call_value *inputs, size_t input_count, const qa_compat_q2_native_mod_region_authority *authority) {
  qa_compat_q2_validate_native_mod_region(definition, host->memory->pointer_bytes);
  if (input_count != definition->input_count) qa_range_error("Native region input count differs from its declaration");
  execution *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->host = host; s->definition = definition; s->authority = authority; s->input_count = input_count; s->phase = PHASE_PROLOGUE;
  s->inputs = calloc(input_count == 0 ? 1 : input_count, sizeof *s->inputs); if (s->inputs == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < input_count; i++) s->inputs[i] = inputs[i];
  double rvas[4] = {definition->frame.entry, definition->entry, definition->join, definition->frame.exit};
  for (int i = 0; i < 4; i++) host->memory->check(host->memory->ctx, at(s, rvas[i]), 1, "execute");
  return s;
}
static void observe_entry(void *ctx) { execution *s = ctx; if (!s->has_entry_stack) { s->has_entry_stack = true; s->entry_stack = qa_compat_q2_native_mod_stack(s->host); } }
static bool accepts_prologue(void *ctx) { return ((execution *)ctx)->phase == PHASE_PROLOGUE; }
static bool accepts_region(void *ctx) { return ((execution *)ctx)->phase == PHASE_REGION; }
static bool accepts_epilogue(void *ctx) { return ((execution *)ctx)->phase == PHASE_EPILOGUE; }
static void on_prologue(void *ctx, qa_compat_q2_inline_continuation *continuation) {
  execution *s = ctx; const qa_compat_q2_native_mod_host *host = s->host;
  assert_current(s); if (!current(s)) qa_range_error("Native donor prologue differs from its declared stack frame");
  s->before = host->runner->capture_processor(host->runner->ctx);
  for (size_t i = 0; i < s->definition->input_count; i++) write_location(s, s->definition->inputs[i].target, &s->inputs[i]);
  s->phase = PHASE_REGION; continuation->skip(continuation->ctx);
}
static void on_region(void *ctx, qa_compat_q2_inline_continuation *continuation) {
  execution *s = ctx; const qa_compat_q2_native_mod_host *host = s->host; const qa_compat_q2_memory *memory = host->memory;
  assert_current(s); if (!current(s)) qa_range_error("Native donor region left its declared stack frame");
  s->phase = PHASE_EXECUTING; continuation->execute(continuation->ctx); assert_current(s);
  uint8_t bytes[16]; read_location(s, s->definition->result, bytes);
  s->output = qa_compat_q2_native_decode_scalar(memory, s->definition->result.storage, bytes); s->has_output = true;
  if (s->before == NULL) qa_range_error("Native donor region skipped its declared prologue");
  while (s->stack_input_count > 0) { stack_input *saved = &s->stack_inputs[--s->stack_input_count]; memory->write(memory->ctx, saved->address, saved->saved, saved->length); }
  host->runner->restore_processor(host->runner->ctx, s->before); host->set_instruction_pointer(host->ctx, at(s, s->definition->join));
  s->phase = PHASE_EPILOGUE;
}
static void on_epilogue(void *ctx, qa_compat_q2_inline_continuation *continuation) {
  execution *s = ctx;
  assert_current(s); if (!current(s)) qa_range_error("Native donor epilogue left its declared stack frame");
  continuation->skip(continuation->ctx);
}
void qa_compat_q2_native_mod_region_execution_bind(execution *s, qa_guest_address target) {
  const qa_compat_q2_native_mod_host *host = s->host; const qa_native_mod_protection_region *d = s->definition;
  s->observe = host->runner->observe_entry(host->runner->ctx, target, s, observe_entry);
  s->regions[0] = qa_compat_q2_native_mod_bind_region(host, at(s, d->frame.entry), at(s, d->entry), s, on_prologue, s, accepts_prologue);
  s->regions[1] = qa_compat_q2_native_mod_bind_region(host, at(s, d->entry), at(s, d->join), s, on_region, s, accepts_region);
  s->regions[2] = qa_compat_q2_native_mod_bind_region(host, at(s, d->join), at(s, d->frame.exit), s, on_epilogue, s, accepts_epilogue);
}
void qa_compat_q2_native_mod_region_execution_unbind(execution *s) {
  const qa_compat_q2_native_mod_host *host = s->host;
  for (int i = 2; i >= 0; i--) if (s->regions[i] != 0) { qa_compat_q2_native_mod_unbind_region(host, s->regions[i]); s->regions[i] = 0; }
  if (s->observe != 0) { host->runner->remove_entry(host->runner->ctx, s->observe); s->observe = 0; }
}
qa_guest_call_value qa_compat_q2_native_mod_region_execution_result(const execution *s) {
  if (s->phase != PHASE_EPILOGUE || !s->has_output) qa_range_error("Native donor did not complete its declared region");
  return s->output;
}
void qa_compat_q2_native_mod_region_execution_destroy(execution *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_region_execution_unbind(s);
  if (s->before != NULL && s->host->runner->free_processor != NULL) s->host->runner->free_processor(s->host->runner->ctx, s->before);
  free(s->stack_inputs); free(s->inputs); free(s);
}

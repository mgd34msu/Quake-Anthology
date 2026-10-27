/* Port of src/guest/abi/adapter.ts. GPL-2.0-or-later. */
#include "qa/guest/abi/adapter.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/floating_point/x87.h"

static void type_error(const char *message) { qa_range_error("%s", message); }

const char *qa_guest_execution_stop_kind_name(qa_guest_execution_stop_kind kind) {
  switch (kind) {
    case QA_STOP_BUDGET: return "budget";
    case QA_STOP_RETURN: return "return";
    case QA_STOP_HOST_CALL: return "host-call";
    case QA_STOP_HALT: return "halt";
    case QA_STOP_EXCEPTION: return "exception";
    case QA_STOP_UNSUPPORTED: return "unsupported";
  }
  return "";
}

qa_guest_address qa_guest_pointer(const qa_mapped_guest_memory *memory, uint64_t raw) {
  qa_guest_address address = {0};
  if (!qa_memory_pointer(memory, raw, &address)) qa_range_error("ABI address cannot be null");
  return address;
}
void qa_x86_abi_adapter_init(qa_x86_abi_adapter *adapter, qa_guest_native_abi abi) { adapter->abi = abi; }

static int register_width(const qa_guest_cpu *cpu) { return cpu->state->architecture == QA_GUEST_I386 ? 32 : 64; }
static uint64_t stack_pointer(const qa_guest_cpu *cpu) {
  return qa_integer_register_file_read(&cpu->state->registers, QA_REG_RSP, register_width(cpu), false);
}
static void set_stack_pointer(qa_guest_cpu *cpu, uint64_t value) {
  qa_integer_register_file_write(&cpu->state->registers, QA_REG_RSP, register_width(cpu), value, false);
}
static uint64_t mask_bits(uint64_t value, int bits) { return bits >= 64 ? value : value & ((1ULL << bits) - 1ULL); }
static int64_t sign_extend(uint64_t value, int bits) {
  if (bits >= 64) return (int64_t)value;
  uint64_t sign = 1ULL << (bits - 1);
  return (int64_t)((value & ((sign << 1) - 1)) ^ sign) - (int64_t)sign;
}
static void store_le(uint8_t *bytes, uint64_t raw, int width) {
  for (int i = 0; i < width; i++) bytes[i] = (uint8_t)(raw >> (i * 8));
}
static uint64_t load_le(const uint8_t *bytes, int width) {
  uint64_t raw = 0;
  for (int i = width - 1; i >= 0; i--) raw = (raw << 8) | bytes[i];
  return raw;
}
static size_t xmm_length(const qa_guest_cpu *cpu) { return cpu->state->simd.xmm_len; }

static uint8_t *read_locations(qa_guest_cpu *cpu, const qa_abi_location *locations,
                               size_t location_count, size_t size) {
  uint8_t *output = (uint8_t *)calloc(size == 0 ? 1 : size, 1);
  if (output == NULL) return NULL;
  for (size_t i = 0; i < location_count; i++) {
    const qa_abi_location *location = &locations[i];
    if (location->offset + location->bytes > (int64_t)size) {
      free(output); qa_range_error("ABI part exceeds its value");
    }
    if (location->kind == QA_LOCATION_INTEGER) {
      uint64_t value = qa_integer_register_file_read(&cpu->state->registers, location->reg,
                                                     register_width(cpu), false);
      store_le(output + location->offset, value, (int)location->bytes);
    } else if (location->kind == QA_LOCATION_SSE) {
      size_t start = (size_t)location->sse_register * 16;
      if (start + (size_t)location->bytes > xmm_length(cpu)) {
        free(output); qa_range_error("ABI XMM register is unavailable");
      }
      memcpy(output + location->offset, cpu->state->simd.xmm + start, (size_t)location->bytes);
    } else {
      qa_guest_address address = qa_guest_pointer(cpu->memory, stack_pointer(cpu) + (uint64_t)location->stack_offset);
      qa_memory_copy_into(cpu->memory, address, output, (size_t)location->offset, (size_t)location->bytes);
    }
  }
  return output;
}

static void write_locations(qa_guest_cpu *cpu, const qa_abi_location *locations,
                            size_t location_count, const uint8_t *bytes, size_t bytes_len) {
  for (size_t i = 0; i < location_count; i++) {
    const qa_abi_location *location = &locations[i];
    if (location->offset + location->bytes > (int64_t)bytes_len)
      qa_range_error("ABI value is shorter than its assigned location");
    const uint8_t *part = bytes + location->offset;
    if (location->kind == QA_LOCATION_INTEGER) {
      uint64_t raw = load_le(part, (int)location->bytes);
      qa_integer_register_file_write(&cpu->state->registers, location->reg, register_width(cpu), raw, false);
    } else if (location->kind == QA_LOCATION_SSE) {
      size_t start = (size_t)location->sse_register * 16;
      if (start + (size_t)location->bytes > xmm_length(cpu))
        qa_range_error("ABI XMM register is unavailable");
      memcpy(cpu->state->simd.xmm + start, part, (size_t)location->bytes);
    } else {
      qa_guest_address address = qa_guest_pointer(cpu->memory, stack_pointer(cpu) + (uint64_t)location->stack_offset);
      qa_memory_write(cpu->memory, address, part, (size_t)location->bytes);
    }
  }
}

/* Returns false when the TS fast path declines (null). */
static bool read_scalar(qa_guest_cpu *cpu, const qa_abi_value_layout *layout,
                        const qa_abi_location *locations, size_t location_count, qa_call_value *out) {
  if (layout->is_aggregate || location_count != 1) return false;
  const qa_abi_location *location = &locations[0];
  if (location->offset != 0) return false;
  if (layout->storage == QA_STORAGE_FLOAT32 || layout->storage == QA_STORAGE_FLOAT64) {
    if (location->bytes != (layout->storage == QA_STORAGE_FLOAT32 ? 4 : 8)) return false;
    double value;
    if (location->kind == QA_LOCATION_STACK) {
      qa_guest_address address = qa_guest_pointer(cpu->memory, stack_pointer(cpu) + (uint64_t)location->stack_offset);
      value = layout->storage == QA_STORAGE_FLOAT32 ? qa_gmem_read_f32(cpu->memory, address)
                                                    : qa_gmem_read_f64(cpu->memory, address);
    } else if (location->kind == QA_LOCATION_SSE) {
      size_t start = (size_t)location->sse_register * 16;
      if (start + (size_t)location->bytes > xmm_length(cpu))
        qa_range_error("ABI XMM register is unavailable");
      if (layout->storage == QA_STORAGE_FLOAT32) { float f; memcpy(&f, cpu->state->simd.xmm + start, 4); value = (double)f; }
      else memcpy(&value, cpu->state->simd.xmm + start, 8);
    } else return false;
    *out = layout->storage == QA_STORAGE_FLOAT32 ? qa_call_float32(value) : qa_call_float64(value);
    return true;
  }
  if (location->kind == QA_LOCATION_SSE
      || (location->bytes != 8 && location->bytes != 4 && location->bytes != 2 && location->bytes != 1))
    return false;
  uint64_t raw;
  if (location->kind == QA_LOCATION_INTEGER)
    raw = qa_integer_register_file_read(&cpu->state->registers, location->reg, register_width(cpu), false);
  else {
    qa_guest_address address = qa_guest_pointer(cpu->memory, stack_pointer(cpu) + (uint64_t)location->stack_offset);
    raw = location->bytes == 8 ? qa_memory_read_u64(cpu->memory, address)
        : location->bytes == 4 ? qa_memory_read_u32(cpu->memory, address)
        : location->bytes == 2 ? qa_memory_read_u16(cpu->memory, address)
                               : qa_memory_read_u8(cpu->memory, address);
  }
  switch (layout->storage) {
    case QA_STORAGE_INT8: *out = qa_call_int32((int32_t)sign_extend(raw, 8)); return true;
    case QA_STORAGE_UINT8: *out = qa_call_uint32((uint32_t)mask_bits(raw, 8)); return true;
    case QA_STORAGE_INT16: *out = qa_call_int32((int32_t)sign_extend(raw, 16)); return true;
    case QA_STORAGE_UINT16: *out = qa_call_uint32((uint32_t)mask_bits(raw, 16)); return true;
    case QA_STORAGE_INT32: *out = qa_call_int32((int32_t)sign_extend(raw, 32)); return true;
    case QA_STORAGE_UINT32: *out = qa_call_uint32((uint32_t)mask_bits(raw, 32)); return true;
    case QA_STORAGE_INT64: *out = qa_call_int64((int64_t)raw); return true;
    case QA_STORAGE_UINT64: *out = qa_call_uint64(raw); return true;
    case QA_STORAGE_POINTER: {
      qa_guest_address address = {0};
      bool has = qa_memory_pointer(cpu->memory, mask_bits(raw, cpu->memory->pointer_bytes * 8), &address);
      *out = qa_call_pointer(has, address);
      return true;
    }
    default: return false;
  }
}

static qa_call_value read_argument(qa_guest_cpu *cpu, const qa_abi_argument *argument) {
  if (!argument->indirect) {
    qa_call_value value;
    if (read_scalar(cpu, &argument->layout, argument->locations, argument->location_count, &value))
      return value;
    size_t size = (size_t)qa_argument_bytes(&argument->layout, cpu->memory->pointer_bytes);
    uint8_t *bytes = read_locations(cpu, argument->locations, argument->location_count, size);
    if (bytes == NULL) qa_range_error("ABI argument allocation failed");
    if (argument->layout.is_aggregate) {
      qa_call_value result = qa_call_aggregate(argument->layout.layout, bytes, size);
      free(bytes);
      return result;
    }
    qa_call_value result = qa_decode_value(&argument->layout, bytes,
        (size_t)qa_value_bytes(&argument->layout, cpu->memory->pointer_bytes), cpu->memory);
    free(bytes);
    return result;
  }
  qa_abi_value_layout pointer_layout; memset(&pointer_layout, 0, sizeof pointer_layout);
  pointer_layout.storage = QA_STORAGE_POINTER;
  qa_call_value pointer;
  if (!read_scalar(cpu, &pointer_layout, argument->locations, argument->location_count, &pointer)) {
    size_t size = (size_t)cpu->memory->pointer_bytes;
    uint8_t *bytes = read_locations(cpu, argument->locations, argument->location_count, size);
    if (bytes == NULL) qa_range_error("ABI argument allocation failed");
    pointer = qa_decode_value(&pointer_layout, bytes, size, cpu->memory);
    free(bytes);
  }
  if (pointer.kind != QA_CALL_POINTER || !pointer.pointer_has)
    qa_range_error("Indirect aggregate has a null guest address");
  size_t size = (size_t)qa_value_bytes(&argument->layout, cpu->memory->pointer_bytes);
  uint8_t *bytes = (uint8_t *)malloc(size == 0 ? 1 : size);
  if (bytes == NULL) qa_range_error("ABI argument allocation failed");
  qa_memory_copy(cpu->memory, pointer.pointer, bytes, size);
  qa_call_value result = argument->layout.is_aggregate
      ? qa_call_aggregate(argument->layout.layout, bytes, size)
      : qa_decode_value(&argument->layout, bytes, size, cpu->memory);
  free(bytes);
  return result;
}

static qa_guest_address return_buffer(qa_guest_cpu *cpu, const qa_abi_call_plan *plan) {
  if (plan->result.kind != QA_ABI_RESULT_MEMORY) type_error("Call does not return an aggregate in memory");
  qa_call_value pointer = read_argument(cpu, &plan->result.pointer);
  if (pointer.kind != QA_CALL_POINTER || !pointer.pointer_has)
    qa_range_error("Aggregate return pointer is null");
  return pointer.pointer;
}

static void check(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu, const qa_guest_call_signature *signature) {
  if (strcmp(signature->abi.kind, adapter->abi.kind) != 0
      || strcmp(signature->abi.call, adapter->abi.call) != 0
      || cpu->memory->pointer_bytes != adapter->abi.pointer_bytes
      || (cpu->state->architecture == QA_GUEST_I386) != (adapter->abi.pointer_bytes == 4))
    type_error("ABI, signature, processor, and memory architecture disagree");
}

void qa_x86_abi_adapter_enter(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu, qa_guest_address target,
                              const qa_guest_call_signature *signature,
                              const qa_call_value *arguments_, size_t argument_count,
                              qa_guest_address return_address) {
  check(adapter, cpu, signature);
  qa_memory_check(cpu->memory, target, 1, QA_ACCESS_EXECUTE);
  qa_memory_check(cpu->memory, return_address, 1, QA_ACCESS_EXECUTE);
  qa_abi_value_layout *layouts_storage = NULL;
  const qa_abi_value_layout *layouts = signature->parameters;
  size_t layout_count = signature->parameter_count;
  if (argument_count != signature->parameter_count) {
    layouts_storage = (qa_abi_value_layout *)malloc(sizeof(qa_abi_value_layout) * (argument_count == 0 ? 1 : argument_count));
    if (layouts_storage == NULL) qa_range_error("Argument layout missing");
    for (size_t i = 0; i < argument_count; i++)
      layouts_storage[i] = i < signature->parameter_count ? signature->parameters[i]
                                                          : qa_inferred_layout(&arguments_[i], signature->variadic);
    layouts = layouts_storage; layout_count = argument_count;
  }
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, signature, layouts, layout_count)) {
    free(layouts_storage); qa_range_error("Argument allocation missing");
  }
  int word = adapter->abi.pointer_bytes;
  /* Encode first, exactly like TS (before any stack mutation). */
  typedef struct encoded { bool is_bigint; uint64_t raw; uint8_t *bytes; size_t len; } encoded;
  encoded *enc = (encoded *)calloc(argument_count == 0 ? 1 : argument_count, sizeof(encoded));
  qa_guest_address *indirect = (qa_guest_address *)calloc(plan.argument_count == 0 ? 1 : plan.argument_count, sizeof(qa_guest_address));
  bool *has_indirect = (bool *)calloc(plan.argument_count == 0 ? 1 : plan.argument_count, sizeof(bool));
  if (enc == NULL || indirect == NULL || has_indirect == NULL) qa_range_error("Argument allocation missing");
  for (size_t i = 0; i < argument_count; i++) {
    const qa_abi_value_layout *layout = &layouts[i];
    const qa_abi_argument *argument = &plan.arguments[i];
    const qa_abi_location *location = argument->location_count == 1 ? &argument->locations[0] : NULL;
    if (!argument->indirect && location != NULL && location->kind == QA_LOCATION_INTEGER
        && location->offset == 0 && !layout->is_aggregate
        && layout->storage != QA_STORAGE_FLOAT32 && layout->storage != QA_STORAGE_FLOAT64) {
      uint64_t raw = qa_encode_integer_value(layout->storage, &arguments_[i], cpu->memory);
      if (layout->storage == QA_STORAGE_INT8 || layout->storage == QA_STORAGE_INT16)
        raw = mask_bits((uint64_t)sign_extend(raw, layout->storage == QA_STORAGE_INT8 ? 8 : 16), 32);
      enc[i].is_bigint = true; enc[i].raw = raw;
    } else {
      enc[i].bytes = qa_encode_argument_value(layout, &arguments_[i], cpu->memory, &enc[i].len);
      if (enc[i].bytes == NULL) qa_range_error("Argument allocation missing");
    }
  }
  uint64_t caller_stack = stack_pointer(cpu);
  uint64_t temporary_top = caller_stack;
  bool has_output = false;
  qa_guest_address output = {0};
  if (plan.result.kind == QA_ABI_RESULT_MEMORY) {
    int64_t size = qa_value_bytes(&plan.result.layout, word);
    int64_t alignment = qa_value_alignment(&plan.result.layout, word);
    if (alignment < 16) alignment = 16;
    temporary_top = qa_align_down_u64(temporary_top - (uint64_t)size, (uint64_t)alignment);
    output = qa_guest_pointer(cpu->memory, temporary_top);
    has_output = true;
  }
  for (size_t i = 0; i < plan.argument_count; i++) {
    if (!plan.arguments[i].indirect) continue;
    int64_t size = qa_value_bytes(&plan.arguments[i].layout, word);
    temporary_top = qa_align_down_u64(temporary_top - (uint64_t)size, 16);
    indirect[i] = qa_guest_pointer(cpu->memory, temporary_top);
    has_indirect[i] = true;
  }
  uint64_t entry_stack = qa_align_down_u64(temporary_top - (uint64_t)(plan.stack_bytes - word),
                                           (uint64_t)plan.stack_alignment) - (uint64_t)word;
  int64_t frame_size = (int64_t)(caller_stack - entry_stack);
  if (frame_size < 0) qa_range_error("Guest call frame exceeds safe bounds");
  qa_memory_check(cpu->memory, qa_guest_pointer(cpu->memory, entry_stack), (size_t)frame_size, QA_ACCESS_WRITE);
  qa_gmem_write_pointer(cpu->memory, qa_guest_pointer(cpu->memory, entry_stack), true, return_address);
  set_stack_pointer(cpu, entry_stack);
  if (has_output && plan.result.kind == QA_ABI_RESULT_MEMORY) {
    size_t size = (size_t)qa_value_bytes(&plan.result.layout, word);
    uint8_t *zero = (uint8_t *)calloc(size == 0 ? 1 : size, 1);
    if (zero == NULL) qa_range_error("Argument allocation missing");
    qa_memory_write(cpu->memory, output, zero, size);
    free(zero);
    qa_abi_value_layout pointer_layout; memset(&pointer_layout, 0, sizeof pointer_layout);
    pointer_layout.storage = QA_STORAGE_POINTER;
    qa_call_value pointer_value = qa_call_pointer(true, output);
    size_t len = 0;
    uint8_t *bytes = qa_encode_value(&pointer_layout, &pointer_value, cpu->memory, &len);
    if (bytes == NULL) qa_range_error("Argument allocation missing");
    write_locations(cpu, plan.result.pointer.locations, plan.result.pointer.location_count, bytes, len);
    free(bytes);
  }
  for (size_t i = 0; i < plan.argument_count; i++) {
    const qa_abi_argument *argument = &plan.arguments[i];
    if (enc[i].is_bigint) {
      if (argument->location_count == 0 || argument->locations[0].kind != QA_LOCATION_INTEGER)
        qa_range_error("Integer argument has no assigned register");
      qa_integer_register_file_write(&cpu->state->registers, argument->locations[0].reg,
                                     register_width(cpu), enc[i].raw, false);
    } else if (!has_indirect[i]) {
      write_locations(cpu, argument->locations, argument->location_count, enc[i].bytes, enc[i].len);
    } else {
      qa_memory_write(cpu->memory, indirect[i], enc[i].bytes, enc[i].len);
      qa_abi_value_layout pointer_layout; memset(&pointer_layout, 0, sizeof pointer_layout);
      pointer_layout.storage = QA_STORAGE_POINTER;
      qa_call_value pointer_value = qa_call_pointer(true, indirect[i]);
      size_t len = 0;
      uint8_t *bytes = qa_encode_value(&pointer_layout, &pointer_value, cpu->memory, &len);
      if (bytes == NULL) qa_range_error("Argument allocation missing");
      write_locations(cpu, argument->locations, argument->location_count, bytes, len);
      free(bytes);
    }
  }
  if (strcmp(adapter->abi.kind, "linux-x86-64") == 0 && signature->variadic)
    qa_integer_register_file_write(&cpu->state->registers, QA_REG_RAX, 8, (uint64_t)plan.vector_registers, false);
  qa_processor_flags_set(&cpu->state->flags, QA_FLAG_DIRECTION, false);
  cpu->state->instruction_pointer = target.byte_offset;
  for (size_t i = 0; i < argument_count; i++) free(enc[i].bytes);
  free(enc); free(indirect); free(has_indirect); free(layouts_storage);
  qa_abi_call_plan_free(&plan);
}

bool qa_x86_abi_adapter_arguments(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                                  const qa_guest_call_signature *signature,
                                  const qa_abi_value_layout *variadic_layouts, size_t variadic_count,
                                  qa_call_value **out, size_t *out_count) {
  check(adapter, cpu, signature);
  qa_abi_value_layout *combined = NULL;
  const qa_abi_value_layout *layouts = signature->parameters;
  size_t layout_count = signature->parameter_count;
  if (variadic_count > 0) {
    layout_count = signature->parameter_count + variadic_count;
    combined = (qa_abi_value_layout *)malloc(sizeof(qa_abi_value_layout) * layout_count);
    if (combined == NULL) return false;
    memcpy(combined, signature->parameters, sizeof(qa_abi_value_layout) * signature->parameter_count);
    memcpy(combined + signature->parameter_count, variadic_layouts, sizeof(qa_abi_value_layout) * variadic_count);
    layouts = combined;
  }
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, signature, layouts, layout_count)) { free(combined); return false; }
  free(combined);
  qa_call_value *values = (qa_call_value *)calloc(plan.argument_count == 0 ? 1 : plan.argument_count, sizeof(qa_call_value));
  if (values == NULL) { qa_abi_call_plan_free(&plan); return false; }
  for (size_t i = 0; i < plan.argument_count; i++) values[i] = read_argument(cpu, &plan.arguments[i]);
  *out = values; *out_count = plan.argument_count;
  qa_abi_call_plan_free(&plan);
  return true;
}

qa_call_value qa_x86_abi_adapter_argument(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                                          const qa_guest_call_signature *signature, int64_t index) {
  check(adapter, cpu, signature);
  if (index < 0) qa_range_error("Guest argument index is outside its signature");
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, signature, NULL, 0)) qa_range_error("Guest argument index is outside its signature");
  if ((uint64_t)index >= plan.argument_count) {
    qa_abi_call_plan_free(&plan);
    qa_range_error("Guest argument index is outside its signature");
  }
  qa_call_value value = read_argument(cpu, &plan.arguments[index]);
  qa_abi_call_plan_free(&plan);
  return value;
}

qa_call_result qa_x86_abi_adapter_return_value(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                                               const qa_guest_call_signature *signature) {
  check(adapter, cpu, signature);
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, signature, NULL, 0)) qa_range_error("ABI argument allocation failed");
  qa_call_result result = qa_call_void();
  switch (plan.result.kind) {
    case QA_ABI_RESULT_VOID: break;
    case QA_ABI_RESULT_REGISTERS: {
      qa_call_value value;
      if (read_scalar(cpu, &plan.result.layout, plan.result.locations, plan.result.location_count, &value)) {
        result = value;
        break;
      }
      size_t size = (size_t)qa_value_bytes(&plan.result.layout, adapter->abi.pointer_bytes);
      uint8_t *bytes = read_locations(cpu, plan.result.locations, plan.result.location_count, size);
      if (bytes == NULL) qa_range_error("ABI argument allocation failed");
      result = plan.result.layout.is_aggregate
          ? qa_call_aggregate(plan.result.layout.layout, bytes, size)
          : qa_decode_value(&plan.result.layout, bytes, size, cpu->memory);
      free(bytes);
      break;
    }
    case QA_ABI_RESULT_MEMORY: {
      qa_guest_address address = qa_guest_pointer(cpu->memory,
          qa_integer_register_file_read(&cpu->state->registers, QA_REG_RAX, register_width(cpu), false));
      size_t size = (size_t)qa_value_bytes(&plan.result.layout, adapter->abi.pointer_bytes);
      uint8_t *bytes = (uint8_t *)malloc(size == 0 ? 1 : size);
      if (bytes == NULL) qa_range_error("ABI argument allocation failed");
      qa_memory_copy(cpu->memory, address, bytes, size);
      result = plan.result.layout.is_aggregate
          ? qa_call_aggregate(plan.result.layout.layout, bytes, size)
          : qa_decode_value(&plan.result.layout, bytes, size, cpu->memory);
      free(bytes);
      break;
    }
    case QA_ABI_RESULT_X87: {
      qa_x87_storage storage = plan.result.storage == QA_STORAGE_FLOAT32 ? QA_X87_FLOAT32 : QA_X87_FLOAT64;
      double value = qa_read_x87_return(&cpu->state->x87, storage);
      uint32_t top = (cpu->state->x87.status_word >> 11) & 7;
      cpu->state->x87.tag_word |= 3u << (top * 2);
      cpu->state->x87.status_word = (cpu->state->x87.status_word & ~0x3800u) | (((top + 1) & 7) << 11);
      result = plan.result.storage == QA_STORAGE_FLOAT32 ? qa_call_float32(value) : qa_call_float64(value);
      break;
    }
  }
  qa_abi_call_plan_free(&plan);
  return result;
}

void qa_x86_abi_adapter_leave(qa_x86_abi_adapter *adapter, qa_guest_cpu *cpu,
                              const qa_guest_call_signature *signature, qa_call_result *result) {
  check(adapter, cpu, signature);
  qa_abi_call_plan plan;
  if (!qa_plan_guest_call(&plan, signature, NULL, 0)) qa_range_error("ABI argument allocation failed");
  int word = adapter->abi.pointer_bytes;
  uint64_t entry_stack = stack_pointer(cpu);
  qa_guest_address stack_address = qa_guest_pointer(cpu->memory, entry_stack);
  uint64_t address = word == 8 ? qa_memory_read_u64(cpu->memory, stack_address)
                               : qa_memory_read_u32(cpu->memory, stack_address);
  if (address == 0) { qa_abi_call_plan_free(&plan); qa_range_error("Guest return address is null"); }
  if (plan.result.kind == QA_ABI_RESULT_VOID) {
    if (result->kind != QA_CALL_VOID) { qa_abi_call_plan_free(&plan); type_error("Void callback returned a value"); }
  } else {
    if (result->kind == QA_CALL_VOID) { qa_abi_call_plan_free(&plan); type_error("Nonvoid callback did not return a value"); }
    if (plan.result.kind == QA_ABI_RESULT_X87) {
      if (result->kind != QA_CALL_FLOAT32 && result->kind != QA_CALL_FLOAT64) {
        qa_abi_call_plan_free(&plan); type_error("x87 callback result must be floating point");
      }
      qa_write_x87_return(&cpu->state->x87, result->number,
                          plan.result.storage == QA_STORAGE_FLOAT32 ? QA_X87_FLOAT32 : QA_X87_FLOAT64);
    } else {
      const qa_abi_value_layout *layout = &plan.result.layout;
      const qa_abi_location *location = (plan.result.kind == QA_ABI_RESULT_REGISTERS
          && plan.result.location_count == 1) ? &plan.result.locations[0] : NULL;
      if (location != NULL && location->kind == QA_LOCATION_INTEGER && location->offset == 0
          && !layout->is_aggregate && layout->storage != QA_STORAGE_FLOAT32
          && layout->storage != QA_STORAGE_FLOAT64) {
        qa_integer_register_file_write(&cpu->state->registers, location->reg, register_width(cpu),
                                       qa_encode_integer_value(layout->storage, result, cpu->memory), false);
      } else {
        size_t len = 0;
        uint8_t *bytes = qa_encode_value(layout, result, cpu->memory, &len);
        if (bytes == NULL) qa_range_error("ABI argument allocation failed");
        if (plan.result.kind == QA_ABI_RESULT_REGISTERS)
          write_locations(cpu, plan.result.locations, plan.result.location_count, bytes, len);
        else {
          qa_guest_address destination = return_buffer(cpu, &plan);
          qa_memory_write(cpu->memory, destination, bytes, len);
          qa_integer_register_file_write(&cpu->state->registers, QA_REG_RAX, register_width(cpu),
                                         destination.byte_offset, false);
        }
        free(bytes);
      }
    }
  }
  set_stack_pointer(cpu, entry_stack + (uint64_t)(word + plan.callee_pop_bytes));
  cpu->state->instruction_pointer = address;
  qa_abi_call_plan_free(&plan);
}

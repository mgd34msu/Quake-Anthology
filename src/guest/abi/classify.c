/* Port of src/guest/abi/classify.ts. GPL-2.0-or-later. */
#include "qa/guest/abi/classify.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS TypeError/Error have no RangeError channel distinction in C: abort. */
static void type_error(const char *message) { qa_range_error("%s", message); }

const char *qa_eightbyte_class_name(qa_eightbyte_class kind) {
  switch (kind) {
    case QA_EIGHTBYTE_NONE: return "none";
    case QA_EIGHTBYTE_INTEGER: return "integer";
    case QA_EIGHTBYTE_SSE: return "sse";
  }
  return "";
}

bool qa_classify_system_v_aggregate(const qa_abi_value_layout *layout, qa_eightbyte_class *classes, size_t *count) {
  qa_validate_value_layout(layout, 8);
  if (!layout->is_aggregate) {
    classes[0] = (layout->storage == QA_STORAGE_FLOAT32 || layout->storage == QA_STORAGE_FLOAT64)
        ? QA_EIGHTBYTE_SSE : QA_EIGHTBYTE_INTEGER;
    *count = 1;
    return true;
  }
  if (layout->layout.byte_length > 16) return false;
  size_t n = (size_t)((layout->layout.byte_length + 7) / 8);
  for (size_t i = 0; i < n; i++) classes[i] = QA_EIGHTBYTE_NONE;
  for (size_t f = 0; f < layout->layout.field_count; f++) {
    const qa_abi_field_layout *field = &layout->layout.fields[f];
    int64_t size = qa_storage_bytes(field->storage, 8);
    if (field->byte_offset % size != 0) return false;
    for (int64_t index = 0; index < field->count; index++) {
      int64_t start = field->byte_offset + index * size, end = start + size;
      for (int64_t slot = start / 8; slot < (end + 7) / 8; slot++) {
        if (slot < 0 || (size_t)slot >= n) qa_range_error("Aggregate classification exceeds layout");
        qa_eightbyte_class next = (field->storage == QA_STORAGE_FLOAT32 || field->storage == QA_STORAGE_FLOAT64)
            ? QA_EIGHTBYTE_SSE : QA_EIGHTBYTE_INTEGER;
        classes[slot] = (classes[slot] == QA_EIGHTBYTE_INTEGER || next == QA_EIGHTBYTE_INTEGER)
            ? QA_EIGHTBYTE_INTEGER : QA_EIGHTBYTE_SSE;
      }
    }
  }
  *count = n;
  return true;
}

static bool hidden_result(const qa_guest_call_signature *signature) {
  if (signature->result_is_void || !signature->result.is_aggregate) return false;
  const char *kind = signature->abi.kind, *call = signature->abi.call;
  if (strcmp(kind, "windows-i386") == 0 && strcmp(call, "thiscall") == 0) return true;
  if (strcmp(kind, "linux-i386") == 0) return true;
  if (strcmp(kind, "linux-x86-64") == 0) {
    qa_eightbyte_class classes[2]; size_t count = 0;
    return !qa_classify_system_v_aggregate(&signature->result, classes, &count);
  }
  int64_t length = signature->result.layout.byte_length;
  return !(length == 1 || length == 2 || length == 4 || length == 8);
}

typedef struct builder {
  const qa_guest_call_signature *signature;
  int word;
  bool microsoft64, system64;
  qa_guest_register integer_registers[6];
  int integer_register_count;
  int64_t stack_bytes, stack_alignment;
  int integer_count, vector_count, ordinal;
  bool oom;
} builder;

static qa_abi_argument argument_new(qa_abi_value_layout layout, bool indirect,
                                    const qa_abi_location *locations, size_t count, builder *b) {
  qa_abi_argument argument; memset(&argument, 0, sizeof argument);
  argument.layout = layout; argument.indirect = indirect; argument.location_count = count;
  argument.locations = (qa_abi_location *)malloc(sizeof(qa_abi_location) * (count == 0 ? 1 : count));
  if (argument.locations == NULL) { b->oom = true; return argument; }
  if (count > 0) memcpy(argument.locations, locations, sizeof(qa_abi_location) * count);
  return argument;
}

static qa_abi_argument stack_assign(builder *b, qa_abi_value_layout layout, bool indirect) {
  int64_t size = indirect ? b->word : qa_argument_bytes(&layout, b->word);
  int64_t alignment = strcmp(b->signature->abi.kind, "windows-i386") == 0 ? 4
      : indirect ? b->word
      : (b->word > qa_value_alignment(&layout, b->word) ? b->word : qa_value_alignment(&layout, b->word));
  if (alignment > b->stack_alignment) b->stack_alignment = alignment;
  b->stack_bytes = qa_align_up(b->stack_bytes - b->word, alignment) + b->word;
  qa_abi_location location; memset(&location, 0, sizeof location);
  location.kind = QA_LOCATION_STACK; location.stack_offset = b->stack_bytes; location.bytes = size;
  b->stack_bytes += qa_align_up(size, b->word);
  return argument_new(layout, indirect, &location, 1, b);
}

static qa_abi_argument assign(builder *b, qa_abi_value_layout layout, bool is_hidden, int parameter_index) {
  const qa_guest_call_signature *signature = b->signature;
  int64_t size = qa_argument_bytes(&layout, b->word);
  bool floating = !layout.is_aggregate
      && (layout.storage == QA_STORAGE_FLOAT32 || layout.storage == QA_STORAGE_FLOAT64);
  if (b->microsoft64) {
    int position = b->ordinal++;
    bool indirect = layout.is_aggregate && !(size == 1 || size == 2 || size == 4 || size == 8);
    if (position >= b->integer_register_count) return stack_assign(b, layout, indirect);
    qa_guest_register reg = b->integer_registers[position];
    qa_abi_location locations[2]; memset(locations, 0, sizeof locations);
    size_t count = 1;
    if (floating) {
      locations[0].kind = QA_LOCATION_SSE; locations[0].sse_register = position; locations[0].bytes = size;
      if (signature->variadic) {
        locations[1].kind = QA_LOCATION_INTEGER; locations[1].reg = reg; locations[1].bytes = size; count = 2;
      }
      if (position + 1 > b->vector_count) b->vector_count = position + 1;
    } else {
      locations[0].kind = QA_LOCATION_INTEGER; locations[0].reg = reg;
      locations[0].bytes = indirect ? b->word : size;
    }
    return argument_new(layout, indirect, locations, count, b);
  }
  if (b->system64) {
    qa_eightbyte_class classes[2]; size_t count = 0;
    if (!qa_classify_system_v_aggregate(&layout, classes, &count)) return stack_assign(b, layout, false);
    int need_int = 0, need_vec = 0;
    for (size_t i = 0; i < count; i++) {
      if (classes[i] == QA_EIGHTBYTE_INTEGER) need_int++;
      if (classes[i] == QA_EIGHTBYTE_SSE) need_vec++;
    }
    if (b->integer_count + need_int > b->integer_register_count || b->vector_count + need_vec > 8)
      return stack_assign(b, layout, false);
    qa_abi_location locations[2]; memset(locations, 0, sizeof locations);
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
      int64_t offset = (int64_t)i * 8, bytes = size - offset < 8 ? size - offset : 8;
      if (classes[i] == QA_EIGHTBYTE_INTEGER) {
        if (b->integer_count >= b->integer_register_count)
          qa_range_error("System V integer register allocation overflow");
        locations[n].kind = QA_LOCATION_INTEGER;
        locations[n].reg = b->integer_registers[b->integer_count++];
        locations[n].offset = offset; locations[n].bytes = bytes; n++;
      } else if (classes[i] == QA_EIGHTBYTE_SSE) {
        locations[n].kind = QA_LOCATION_SSE; locations[n].sse_register = b->vector_count++;
        locations[n].offset = offset; locations[n].bytes = bytes; n++;
      }
    }
    return argument_new(layout, false, locations, n, b);
  }
  if (strcmp(signature->abi.kind, "windows-i386") == 0 && !signature->variadic) {
    if (strcmp(signature->abi.call, "thiscall") == 0 && parameter_index == 0 && !is_hidden) {
      if (layout.is_aggregate || layout.storage != QA_STORAGE_POINTER)
        type_error("thiscall's first explicit argument must be the this pointer");
      qa_abi_location location; memset(&location, 0, sizeof location);
      location.kind = QA_LOCATION_INTEGER; location.reg = QA_REG_RCX; location.bytes = 4;
      return argument_new(layout, false, &location, 1, b);
    }
    if (strcmp(signature->abi.call, "fastcall") == 0 && !layout.is_aggregate && !floating
        && size <= 4 && b->integer_count < 2) {
      qa_abi_location location; memset(&location, 0, sizeof location);
      location.kind = QA_LOCATION_INTEGER;
      location.reg = b->integer_count++ == 0 ? QA_REG_RCX : QA_REG_RDX;
      location.bytes = size;
      return argument_new(layout, false, &location, 1, b);
    }
  }
  return stack_assign(b, layout, false);
}

bool qa_plan_guest_call(qa_abi_call_plan *out, const qa_guest_call_signature *signature,
                        const qa_abi_value_layout *layouts, size_t layout_count) {
  memset(out, 0, sizeof *out);
  if (layouts == NULL) { layouts = signature->parameters; layout_count = signature->parameter_count; }
  bool hidden = hidden_result(signature);
  if ((!signature->variadic && layout_count != signature->parameter_count)
      || layout_count < signature->parameter_count)
    qa_range_error("Call argument count differs from signature");
  int word = signature->abi.pointer_bytes;
  for (size_t i = 0; i < layout_count; i++) qa_validate_value_layout(&layouts[i], word);
  if (!signature->result_is_void) qa_validate_value_layout(&signature->result, word);

  builder b; memset(&b, 0, sizeof b);
  b.signature = signature; b.word = word;
  b.microsoft64 = strcmp(signature->abi.kind, "windows-x86-64") == 0;
  b.system64 = strcmp(signature->abi.kind, "linux-x86-64") == 0;
  if (b.microsoft64) {
    b.integer_registers[0] = QA_REG_RCX; b.integer_registers[1] = QA_REG_RDX;
    b.integer_registers[2] = QA_REG_R8; b.integer_registers[3] = QA_REG_R9;
    b.integer_register_count = 4;
  } else if (b.system64) {
    b.integer_registers[0] = QA_REG_RDI; b.integer_registers[1] = QA_REG_RSI;
    b.integer_registers[2] = QA_REG_RDX; b.integer_registers[3] = QA_REG_RCX;
    b.integer_registers[4] = QA_REG_R8; b.integer_registers[5] = QA_REG_R9;
    b.integer_register_count = 6;
  }
  b.stack_bytes = word + (b.microsoft64 ? 32 : 0);
  b.stack_alignment = (word == 8 || strcmp(signature->abi.kind, "linux-i386") == 0) ? 16 : 4;

  qa_abi_value_layout pointer_layout; memset(&pointer_layout, 0, sizeof pointer_layout);
  pointer_layout.storage = QA_STORAGE_POINTER;

  bool this_before_hidden = hidden && strcmp(signature->abi.kind, "windows-i386") == 0
      && strcmp(signature->abi.call, "thiscall") == 0 && signature->variadic;
  if (this_before_hidden
      && (layout_count == 0 || layouts[0].is_aggregate || layouts[0].storage != QA_STORAGE_POINTER))
    type_error("Variadic thiscall requires an explicit this pointer");

  qa_abi_argument *arguments_ = (qa_abi_argument *)calloc(layout_count == 0 ? 1 : layout_count, sizeof(qa_abi_argument));
  if (arguments_ == NULL) return false;
  bool have_first = false;
  qa_abi_argument first_argument; memset(&first_argument, 0, sizeof first_argument);
  if (this_before_hidden && layout_count > 0) { first_argument = assign(&b, layouts[0], false, 0); have_first = true; }
  qa_abi_argument hidden_pointer; memset(&hidden_pointer, 0, sizeof hidden_pointer);
  bool have_hidden = false;
  if (hidden) { hidden_pointer = assign(&b, pointer_layout, true, -1); have_hidden = true; }
  for (size_t i = 0; i < layout_count; i++)
    arguments_[i] = (i == 0 && have_first) ? first_argument : assign(&b, layouts[i], false, (int)i);

  qa_abi_result result; memset(&result, 0, sizeof result);
  if (signature->result_is_void) result.kind = QA_ABI_RESULT_VOID;
  else if (have_hidden) {
    result.kind = QA_ABI_RESULT_MEMORY; result.layout = signature->result; result.pointer = hidden_pointer;
  } else if (word == 4 && !signature->result.is_aggregate
             && (signature->result.storage == QA_STORAGE_FLOAT32 || signature->result.storage == QA_STORAGE_FLOAT64)) {
    result.kind = QA_ABI_RESULT_X87; result.storage = signature->result.storage;
  } else {
    result.kind = QA_ABI_RESULT_REGISTERS; result.layout = signature->result;
    int64_t size = qa_value_bytes(&signature->result, word);
    qa_abi_location locations[2]; memset(locations, 0, sizeof locations);
    size_t n = 0;
    if (b.system64) {
      qa_eightbyte_class classes[2]; size_t count = 0;
      if (!qa_classify_system_v_aggregate(&signature->result, classes, &count))
        qa_range_error("Memory result lacks hidden pointer");
      int integer_return = 0, vector_return = 0;
      for (size_t i = 0; i < count; i++) {
        int64_t offset = (int64_t)i * 8, bytes = size - offset < 8 ? size - offset : 8;
        if (classes[i] == QA_EIGHTBYTE_INTEGER) {
          locations[n].kind = QA_LOCATION_INTEGER;
          locations[n].reg = integer_return++ == 0 ? QA_REG_RAX : QA_REG_RDX;
          locations[n].offset = offset; locations[n].bytes = bytes; n++;
        } else if (classes[i] == QA_EIGHTBYTE_SSE) {
          locations[n].kind = QA_LOCATION_SSE; locations[n].sse_register = vector_return++;
          locations[n].offset = offset; locations[n].bytes = bytes; n++;
        }
      }
    } else if (b.microsoft64 && !signature->result.is_aggregate
               && (signature->result.storage == QA_STORAGE_FLOAT32 || signature->result.storage == QA_STORAGE_FLOAT64)) {
      locations[0].kind = QA_LOCATION_SSE; locations[0].sse_register = 0; locations[0].bytes = size; n = 1;
    } else {
      locations[0].kind = QA_LOCATION_INTEGER; locations[0].reg = QA_REG_RAX;
      locations[0].bytes = size < word ? size : word; n = 1;
      if (size > word) {
        locations[1].kind = QA_LOCATION_INTEGER; locations[1].reg = QA_REG_RDX;
        locations[1].offset = word; locations[1].bytes = size - word; n = 2;
      }
    }
    result.locations = (qa_abi_location *)malloc(sizeof(qa_abi_location) * (n == 0 ? 1 : n));
    if (result.locations == NULL) b.oom = true;
    else { memcpy(result.locations, locations, sizeof(qa_abi_location) * n); result.location_count = n; }
  }
  if (b.oom) {
    for (size_t i = 0; i < layout_count; i++) free(arguments_[i].locations);
    free(arguments_); free(result.locations); free(result.pointer.locations);
    return false;
  }
  int64_t callee_pop = (strcmp(signature->abi.kind, "linux-i386") == 0 && hidden) ? 4
      : (strcmp(signature->abi.kind, "windows-i386") == 0 && !signature->variadic
         && strcmp(signature->abi.call, "cdecl") != 0) ? b.stack_bytes - word : 0;
  out->abi = signature->abi;
  out->arguments = arguments_; out->argument_count = layout_count;
  out->result = result;
  out->stack_bytes = b.stack_bytes; out->stack_alignment = b.stack_alignment;
  out->callee_pop_bytes = callee_pop; out->vector_registers = b.vector_count;
  return true;
}

void qa_abi_call_plan_free(qa_abi_call_plan *plan) {
  if (plan == NULL) return;
  for (size_t i = 0; i < plan->argument_count; i++) free(plan->arguments[i].locations);
  free(plan->arguments);
  free(plan->result.locations);
  free(plan->result.pointer.locations);
  memset(plan, 0, sizeof *plan);
}

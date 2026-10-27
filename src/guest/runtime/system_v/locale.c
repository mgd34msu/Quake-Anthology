/* Port of src/guest/runtime/system-v/locale.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/locale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  char *copy = xmalloc(strlen(text) + 1);
  strcpy(copy, text);
  return copy;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static void type_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_address slot(qa_system_v_classic_locale *locale, qa_guest_address a, int64_t o) {
  return qa_cxx_abi_slot(locale->abi, a, o);
}
static void write_ptr(qa_system_v_classic_locale *locale, qa_guest_address a, qa_guest_address v) {
  qa_gmem_write_pointer(locale->abi->host->memory, a, true, v);
}
static bool read_ptr(qa_system_v_classic_locale *locale, qa_guest_address a, qa_guest_address *out) {
  return qa_gmem_read_pointer(locale->abi->host->memory, a, out);
}

static int mask(int value) {
  bool upper = value >= 65 && value <= 90, lower = value >= 97 && value <= 122;
  bool digit = value >= 48 && value <= 57, print = value >= 32 && value < 127;
  bool graph = value > 32 && value < 127, blank = value == 32 || value == 9;
  bool space = value == 32 || (value >= 9 && value <= 13);
  return (upper ? 0x100 : 0) | (lower ? 0x200 : 0) | (upper || lower ? 0x400 : 0)
      | (digit ? 0x800 : 0)
      | (digit || (value >= 65 && value <= 70) || (value >= 97 && value <= 102) ? 0x1000 : 0)
      | (space ? 0x2000 : 0) | (print ? 0x4000 : 0) | (graph ? 0x8000 : 0) | (blank ? 1 : 0)
      | ((value >= 0 && value < 32) || value == 127 ? 2 : 0)
      | (graph && !upper && !lower && !digit ? 4 : 0) | (upper || lower || digit ? 8 : 0);
}

typedef struct ctype_loc_state {
  qa_system_v_service_host *host;
  qa_guest_address slot_address;
} ctype_loc_state;
static qa_call_result ctype_loc_impl(void *ctx, const qa_guest_call_context *context,
                                     const qa_call_value *args, size_t count) {
  ctype_loc_state *state = ctx; (void)context; (void)args; (void)count;
  return qa_call_pointer(true, state->slot_address);
}

static qa_guest_address facet(qa_system_v_classic_locale *locale, const char *name, size_t size,
                              const qa_guest_address *methods, size_t method_count) {
  qa_cxx_abi_data *abi = locale->abi;
  qa_mapped_guest_memory *m = abi->host->memory;
  int p = m->pointer_bytes;
  qa_guest_address address = qa_system_v_allocate(abi->host, size);
  qa_cxx_rtti_base base = {locale->facet_base, 0, 2};
  qa_guest_address type = qa_cxx_abi_type(abi, name, &base, 1);
  char *destructor_name = xmalloc(strlen(name) + 32);
  (void)snprintf(destructor_name, strlen(name) + 32, "__guest_%s_destructor", name);
  qa_guest_address destructor = qa_cxx_abi_unsupported(abi, destructor_name);
  char *deleting_name = xmalloc(strlen(name) + 40);
  (void)snprintf(deleting_name, strlen(name) + 40, "__guest_%s_deleting_destructor", name);
  qa_guest_address deleting = qa_cxx_abi_unsupported(abi, deleting_name);
  qa_guest_address *entries = xmalloc((method_count + 2) * sizeof *entries);
  entries[0] = destructor; entries[1] = deleting;
  for (size_t i = 0; i < method_count; i++) entries[i + 2] = methods[i];
  write_ptr(locale, address, qa_cxx_abi_vtable(abi, name, type, entries, method_count + 2));
  free(entries);
  qa_gmem_write_i32(m, slot(locale, address, p), 1);
  return address;
}
static qa_guest_address text_value(qa_system_v_classic_locale *locale, const char *value, bool wide) {
  if (!wide) return qa_cxx_abi_bytes(locale->abi, value);
  qa_mapped_guest_memory *m = locale->abi->host->memory;
  qa_guest_address address = qa_system_v_allocate(locale->abi->host, (strlen(value) + 1) * 4);
  for (size_t i = 0; i < strlen(value); i++)
    qa_memory_write_u32(m, slot(locale, address, (int64_t)i * 4), (uint8_t)value[i]);
  return address;
}

typedef struct cache_writer {
  qa_system_v_classic_locale *locale;
  qa_guest_address result;
  int64_t cursor;
  int p, width;
  bool wide;
} cache_writer;
static void cache_align(cache_writer *w, int64_t n) { w->cursor = (w->cursor + n - 1) / n * n; }
static void cache_ptr(cache_writer *w, qa_guest_address value) {
  cache_align(w, w->p);
  write_ptr(w->locale, slot(w->locale, w->result, w->cursor), value);
  w->cursor += w->p;
}
static void cache_size(cache_writer *w, uint64_t value) {
  cache_align(w, w->p);
  qa_write_unsigned(w->locale->abi->host->memory, slot(w->locale, w->result, w->cursor),
                    w->p, value);
  w->cursor += w->p;
}
static void cache_char(cache_writer *w, int value) {
  cache_align(w, w->width);
  qa_write_unsigned(w->locale->abi->host->memory, slot(w->locale, w->result, w->cursor),
                    w->width, (uint64_t)value);
  w->cursor += w->width;
}
static qa_guest_address cache(qa_system_v_classic_locale *locale, const char *name, bool wide,
                              int kind) { /* 0 number, 1 money, 2 time */
  qa_cxx_abi_data *abi = locale->abi;
  qa_mapped_guest_memory *m = abi->host->memory;
  int p = m->pointer_bytes;
  cache_writer w;
  w.locale = locale;
  w.result = facet(locale, name, 1024, NULL, 0);
  qa_gmem_write_i32(m, slot(locale, w.result, p), 2);
  w.cursor = 2 * p;
  w.p = p; w.width = wide ? 4 : 1; w.wide = wide;
  if (kind == 2) {
    static const char *const days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                                       "Friday", "Saturday"};
    static const char *const months[] = {"January", "February", "March", "April", "May", "June",
                                         "July", "August", "September", "October", "November",
                                         "December"};
    const char *heads[] = {"%m/%d/%y", "%m/%d/%y", "%H:%M:%S", "%H:%M:%S", "", "", "AM", "PM", ""};
    for (size_t i = 0; i < 9; i++) cache_ptr(&w, text_value(locale, heads[i], wide));
    for (size_t i = 0; i < 7; i++) cache_ptr(&w, text_value(locale, days[i], wide));
    for (size_t i = 0; i < 7; i++) {
      char abbrev[4];
      memcpy(abbrev, days[i], 3); abbrev[3] = '\0';
      cache_ptr(&w, text_value(locale, abbrev, wide));
    }
    for (size_t i = 0; i < 12; i++) cache_ptr(&w, text_value(locale, months[i], wide));
    for (size_t i = 0; i < 12; i++) {
      char abbrev[4];
      memcpy(abbrev, months[i], 3); abbrev[3] = '\0';
      cache_ptr(&w, text_value(locale, abbrev, wide));
    }
    return w.result;
  }
  cache_ptr(&w, qa_cxx_abi_bytes(abi, ""));
  cache_size(&w, 0);
  w.cursor++;
  if (kind == 0) {
    cache_ptr(&w, text_value(locale, "true", wide)); cache_size(&w, 4);
    cache_ptr(&w, text_value(locale, "false", wide)); cache_size(&w, 5);
    cache_char(&w, 46); cache_char(&w, 44);
    const char *texts[] = {"-+xX0123456789abcdef0123456789ABCDEF",
                           "-+xX0123456789abcdefABCDEF"};
    for (size_t t = 0; t < 2; t++)
      for (size_t i = 0; texts[t][i] != '\0'; i++) cache_char(&w, (uint8_t)texts[t][i]);
  } else {
    cache_char(&w, 46); cache_char(&w, 44);
    for (int i = 0; i < 3; i++) { cache_ptr(&w, text_value(locale, "", wide)); cache_size(&w, 0); }
    cache_align(&w, 4); w.cursor += 4;
    const uint8_t pattern[] = {2, 3, 0, 4, 2, 3, 0, 4};
    qa_memory_write(m, slot(locale, w.result, w.cursor), pattern, sizeof pattern);
    w.cursor += 8;
    const char *digits = "-0123456789";
    for (size_t i = 0; digits[i] != '\0'; i++) cache_char(&w, (uint8_t)digits[i]);
  }
  return w.result;
}

/* ctypeMethods closures. */
typedef struct ctype_state {
  qa_system_v_classic_locale *locale;
  bool wide;
  int operation; /* 0 is, 1 toupper, 2 toupper_range, 3 tolower, 4 tolower_range,
                    5 widen, 6 widen_range, 7 narrow */
} ctype_state;
static uint32_t ctype_read(ctype_state *state, qa_guest_address address) {
  qa_mapped_guest_memory *m = state->locale->abi->host->memory;
  return state->wide ? qa_memory_read_u32(m, address) : qa_memory_read_u8(m, address);
}
static void ctype_write(ctype_state *state, qa_guest_address address, uint32_t value) {
  qa_mapped_guest_memory *m = state->locale->abi->host->memory;
  if (state->wide) qa_memory_write_u32(m, address, value);
  else qa_memory_write_u8(m, address, (uint8_t)value);
}
static int ctype_convert(ctype_state *state, qa_guest_address object, int c) {
  bool toupper = state->operation == 1 || state->operation == 2;
  if (state->wide)
    return toupper ? (c >= 97 && c <= 122 ? c - 32 : c) : (c >= 65 && c <= 90 ? c + 32 : c);
  qa_mapped_guest_memory *m = state->locale->abi->host->memory;
  qa_guest_address table = {0};
  if (!read_ptr(state->locale, slot(state->locale, object, (toupper ? 4 : 5) * m->pointer_bytes),
                &table))
    type_error("Missing guest ctype conversion table");
  int32_t value = qa_gmem_read_i32(m, slot(state->locale, table, (c & 255) * 4));
  return (int8_t)value;
}
static uint32_t ctype_widen_value(ctype_state *state, qa_guest_address object, int value) {
  if (!state->wide) return (uint32_t)(int8_t)value;
  qa_mapped_guest_memory *m = state->locale->abi->host->memory;
  int64_t widen = ((3 * m->pointer_bytes + 129) + 3) / 4 * 4;
  return qa_memory_read_u32(m, slot(state->locale, object, (int64_t)widen + value * 4));
}
static qa_call_result char_result(ctype_state *state, int value) {
  return state->wide ? qa_call_uint32((uint32_t)value) : qa_call_int32(value);
}
static qa_call_result ctype_impl(void *ctx, const qa_guest_call_context *context,
                                 const qa_call_value *args, size_t count) {
  ctype_state *state = ctx; (void)context;
  qa_system_v_classic_locale *locale = state->locale;
  qa_mapped_guest_memory *m = locale->abi->host->memory;
  int width = state->wide ? 4 : 1;
  switch (state->operation) {
    case 0: { /* wide is */
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      uint32_t requested = (uint32_t)qa_guest_integer(args, count, 1);
      uint32_t value = (uint32_t)qa_guest_integer(args, count, 2);
      int64_t widen = ((3 * m->pointer_bytes + 129) + 3) / 4 * 4;
      int64_t masks = (widen + 1056 + m->pointer_bytes - 1) / m->pointer_bytes * m->pointer_bytes;
      for (int bit = 0; bit < 12; bit++) {
        if ((qa_memory_read_u16(m, slot(locale, object, widen + 1024 + bit * 2)) & requested) == 0)
          continue;
        qa_guest_address table = {0};
        if (!read_ptr(locale, slot(locale, object, masks + (int64_t)bit * m->pointer_bytes),
                      &table))
          continue;
        uint32_t index1 = value >> qa_memory_read_u32(m, table);
        if (index1 >= qa_memory_read_u32(m, slot(locale, table, 4))) continue;
        uint32_t lookup1 = qa_memory_read_u32(m, slot(locale, table, 20 + (int64_t)index1 * 4));
        if (lookup1 == 0) continue;
        uint32_t index2 = (value >> qa_memory_read_u32(m, slot(locale, table, 8))) &
                          qa_memory_read_u32(m, slot(locale, table, 12));
        uint32_t lookup2 = qa_memory_read_u32(m, slot(locale, table, lookup1 + (int64_t)index2 * 4));
        if (lookup2 == 0) continue;
        uint32_t index3 = (value >> 5) & qa_memory_read_u32(m, slot(locale, table, 16));
        if (((qa_memory_read_u32(m, slot(locale, table, lookup2 + (int64_t)index3 * 4)) >>
              (value & 31)) & 1u) != 0)
          return qa_call_int32(1);
      }
      return qa_call_int32(0);
    }
    case 1: case 3: {
      int value = ctype_convert(state, qa_guest_required_pointer(args, count, 0),
                                (int)qa_guest_integer(args, count, 1));
      return char_result(state, value);
    }
    case 2: case 4: {
      qa_guest_address current = qa_guest_required_pointer(args, count, 1);
      qa_guest_address end = qa_guest_required_pointer(args, count, 2);
      while (current.byte_offset < end.byte_offset) {
        ctype_write(state, current,
                    (uint32_t)ctype_convert(state, qa_guest_required_pointer(args, count, 0),
                                            (int)ctype_read(state, current)));
        current = slot(locale, current, width);
      }
      return qa_call_pointer(true, end);
    }
    case 5: {
      uint32_t value = ctype_widen_value(
          state, qa_guest_required_pointer(args, count, 0),
          (int)(qa_guest_integer_u64(args, count, 1) & 255u));
      return char_result(state, (int)value);
    }
    case 6: {
      qa_guest_address source = qa_guest_required_pointer(args, count, 1);
      qa_guest_address target = qa_guest_required_pointer(args, count, 3);
      qa_guest_address end = qa_guest_required_pointer(args, count, 2);
      while (source.byte_offset < end.byte_offset) {
        ctype_write(state, target,
                    ctype_widen_value(state, qa_guest_required_pointer(args, count, 0),
                                      qa_memory_read_u8(m, source)));
        source = slot(locale, source, 1);
        target = slot(locale, target, width);
      }
      return qa_call_pointer(true, end);
    }
    case 7: {
      qa_guest_address object = qa_guest_required_pointer(args, count, 0);
      int input = (int)qa_guest_integer(args, count, 1);
      int64_t narrow = 3 * m->pointer_bytes;
      int value = state->wide && input < 128 &&
                      qa_memory_read_u8(m, slot(locale, object, narrow)) != 0
                  ? qa_memory_read_u8(m, slot(locale, object, narrow + 1 + input))
                  : state->wide && input > 127 ? (int)qa_guest_integer(args, count, 2) : input;
      return qa_call_int32((int8_t)value);
    }
    default: break;
  }
  return qa_call_void();
}
static qa_guest_address ctype_function(qa_system_v_classic_locale *locale, const char *name,
                                       const qa_abi_storage *parameters, size_t parameter_count,
                                       bool result_is_void, qa_abi_storage result, bool wide,
                                       int operation) {
  ctype_state *state = xmalloc(sizeof *state);
  state->locale = locale; state->wide = wide; state->operation = operation;
  qa_system_v_implementation impl = {state, ctype_impl};
  return qa_cxx_abi_function(locale->abi, name, parameters, parameter_count, result_is_void,
                             result, impl, NULL);
}
static size_t ctype_methods(qa_system_v_classic_locale *locale, bool wide,
                            qa_guest_address *out, size_t capacity) {
  (void)capacity;
  qa_cxx_abi_data *abi = locale->abi;
  qa_abi_storage type = wide ? QA_STORAGE_UINT32 : QA_STORAGE_INT32;
  char ch = wide ? 'w' : 'c';
  size_t count = 0;
  char name[80];
  if (wide) {
    qa_abi_storage params[] = {QA_STORAGE_POINTER, QA_STORAGE_UINT32, QA_STORAGE_UINT32};
    (void)snprintf(name, sizeof name, "__guest_ctype_%c_is", ch);
    out[count++] = ctype_function(locale, xstrdup(name), params, 3, false, QA_STORAGE_INT32,
                                  true, 0);
    const char *unsupported[] = {"is_range", "scan_is", "scan_not"};
    for (size_t i = 0; i < 3; i++) {
      (void)snprintf(name, sizeof name, "__guest_ctype_%c_%s", ch, unsupported[i]);
      out[count++] = qa_cxx_abi_unsupported(abi, xstrdup(name));
    }
  }
  const char *operations[] = {"toupper", "tolower"};
  for (size_t i = 0; i < 2; i++) {
    qa_abi_storage params[] = {QA_STORAGE_POINTER, type};
    (void)snprintf(name, sizeof name, "__guest_ctype_%c_%s", ch, operations[i]);
    out[count++] = ctype_function(locale, xstrdup(name), params, 2, false, type, wide,
                                  i == 0 ? 1 : 3);
    qa_abi_storage range_params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER};
    (void)snprintf(name, sizeof name, "__guest_ctype_%c_%s_range", ch, operations[i]);
    out[count++] = ctype_function(locale, xstrdup(name), range_params, 3, false,
                                  QA_STORAGE_POINTER, wide, i == 0 ? 2 : 4);
  }
  qa_abi_storage widen_params[] = {QA_STORAGE_POINTER, QA_STORAGE_INT32};
  (void)snprintf(name, sizeof name, "__guest_ctype_%c_widen", ch);
  out[count++] = ctype_function(locale, xstrdup(name), widen_params, 2, false, type, wide, 5);
  qa_abi_storage widen_range_params[] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER,
                                         QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  (void)snprintf(name, sizeof name, "__guest_ctype_%c_widen_range", ch);
  out[count++] = ctype_function(locale, xstrdup(name), widen_range_params, 4, false,
                                QA_STORAGE_POINTER, wide, 6);
  qa_abi_storage narrow_params[] = {QA_STORAGE_POINTER, type, QA_STORAGE_INT32};
  (void)snprintf(name, sizeof name, "__guest_ctype_%c_narrow", ch);
  out[count++] = ctype_function(locale, xstrdup(name), narrow_params, 3, false,
                                QA_STORAGE_INT32, wide, 7);
  (void)snprintf(name, sizeof name, "__guest_ctype_%c_narrow_range", ch);
  out[count++] = qa_cxx_abi_unsupported(abi, xstrdup(name));
  return count;
}

typedef struct built_facets { qa_guest_address ctype, num_get, num_put; } built_facets;
static built_facets construct_facets(qa_system_v_classic_locale *locale, bool wide,
                                     qa_guest_address facets, qa_guest_address caches,
                                     qa_guest_address classification, qa_guest_address lower,
                                     qa_guest_address upper) {
  qa_cxx_abi_data *abi = locale->abi;
  qa_system_v_service_host *host = abi->host;
  qa_mapped_guest_memory *m = host->memory;
  int p = m->pointer_bytes;
  char ch = wide ? 'w' : 'c';
  char names[14][160];
  (void)snprintf(names[0], 160, "St5ctypeI%cE", ch);
  /* The codecvt second char is literally 'c' in TS for both widths. */
  (void)snprintf(names[1], 160, "St7codecvtI%cc11__mbstate_tE", ch);
  (void)snprintf(names[2], 160, "St8numpunctI%cE", ch);
  (void)snprintf(names[3], 160, "St7num_getI%cSt19istreambuf_iteratorI%cSt11char_traitsI%cEEE", ch, ch, ch);
  (void)snprintf(names[4], 160, "St7num_putI%cSt19ostreambuf_iteratorI%cSt11char_traitsI%cEEE", ch, ch, ch);
  (void)snprintf(names[5], 160, "St7collateI%cE", ch);
  (void)snprintf(names[6], 160, "St10moneypunctI%cLb0EE", ch);
  (void)snprintf(names[7], 160, "St10moneypunctI%cLb1EE", ch);
  (void)snprintf(names[8], 160, "St9money_getI%cSt19istreambuf_iteratorI%cSt11char_traitsI%cEEE", ch, ch, ch);
  (void)snprintf(names[9], 160, "St9money_putI%cSt19ostreambuf_iteratorI%cSt11char_traitsI%cEEE", ch, ch, ch);
  (void)snprintf(names[10], 160, "St11__timepunctI%cE", ch);
  (void)snprintf(names[11], 160, "St8time_getI%cSt19istreambuf_iteratorI%cSt11char_traitsI%cEEE", ch, ch, ch);
  (void)snprintf(names[12], 160, "St8time_putI%cSt19ostreambuf_iteratorI%cSt11char_traitsI%cEEE", ch, ch, ch);
  (void)snprintf(names[13], 160, "St8messagesI%cE", ch);
  qa_guest_address built[14];
  for (int index = 0; index < 14; index++) {
    qa_guest_address methods[16];
    size_t method_count;
    if (index == 0) {
      method_count = ctype_methods(locale, wide, methods, 16);
    } else {
      method_count = 14;
      for (int s = 0; s < 14; s++) {
        char *method_name = xmalloc(strlen(names[index]) + 32);
        (void)snprintf(method_name, strlen(names[index]) + 32, "__guest_%s_virtual_%d",
                       names[index], s);
        methods[s] = qa_cxx_abi_unsupported(abi, method_name);
      }
    }
    size_t size = index == 0
        ? (wide ? (p == 4 ? 1264 : 1344) : (size_t)(((7 * p + 514) + p - 1) / p * p))
        : index == 10 ? (size_t)(5 * p)
        : index == 13 ? (size_t)(4 * p)
        : (index == 1 || index == 2 || index == 5 || index == 6 || index == 7)
            ? (size_t)(3 * p) : (size_t)(2 * p);
    qa_guest_address facet_address = facet(locale, names[index], size, methods, method_count);
    int id = (wide ? 14 : 0) + index;
    built[index] = facet_address;
    write_ptr(locale, slot(locale, facets, (int64_t)id * p), facet_address);
    qa_gmem_write_i32(m, slot(locale, facet_address, p), 2);
    char *id_symbol = xmalloc(strlen(names[index]) + 8);
    (void)snprintf(id_symbol, strlen(names[index]) + 8, "_ZN%s2idE", names[index]);
    qa_guest_address id_address = qa_cxx_abi_define_data(abi, id_symbol, (size_t)p, NULL);
    free(id_symbol);
    qa_write_unsigned(m, id_address, p, (uint64_t)(id + 1));
    if (index == 0) {
      write_ptr(locale, slot(locale, facet_address, 2 * p), locale->c_locale);
      if (!wide) {
        write_ptr(locale, slot(locale, facet_address, 4 * p), upper);
        write_ptr(locale, slot(locale, facet_address, 5 * p), lower);
        write_ptr(locale, slot(locale, facet_address, 6 * p), classification);
      } else {
        int64_t narrow = 3 * p;
        qa_memory_write_u8(m, slot(locale, facet_address, narrow), 1);
        for (int c = 0; c < 128; c++)
          qa_memory_write_u8(m, slot(locale, facet_address, narrow + 1 + c), (uint8_t)c);
        int64_t widen = (narrow + 129 + 3) / 4 * 4;
        for (int c = 0; c < 256; c++)
          qa_memory_write_u32(m, slot(locale, facet_address, widen + c * 4),
                              c < 128 ? (uint32_t)c : 0xffffffffu);
        int64_t masks = (widen + 1056 + p - 1) / p * p;
        for (int bit = 0; bit < 12; bit++) {
          int flag = bit < 8 ? 1 << (bit + 8) : 1 << (bit - 8);
          qa_memory_write_u16(m, slot(locale, facet_address, widen + 1024 + bit * 2),
                              (uint16_t)flag);
          /* GCC 4.8 _M_convert_to_wmask does not map the C library's blank bit. */
          if (bit == 8) continue;
          qa_guest_address table = qa_system_v_allocate(host, 56);
          const uint32_t header[] = {7, 1, 5, 3, 0, 24};
          for (size_t h = 0; h < 6; h++)
            qa_memory_write_u32(m, slot(locale, table, (int64_t)h * 4), header[h]);
          for (int group = 0; group < 4; group++) {
            qa_memory_write_u32(m, slot(locale, table, 24 + group * 4), (uint32_t)(40 + group * 4));
            uint32_t bits = 0;
            for (int offset = 0; offset < 32; offset++)
              if ((mask(group * 32 + offset) & flag) != 0) bits |= 1u << offset;
            qa_memory_write_u32(m, slot(locale, table, 40 + group * 4), bits);
          }
          write_ptr(locale, slot(locale, facet_address, masks + (int64_t)bit * p), table);
        }
      }
    } else if (index == 2 || index == 6 || index == 7 || index == 10) {
      char cache_name[96];
      if (index == 2) (void)snprintf(cache_name, sizeof cache_name, "St16__numpunct_cacheI%cE", ch);
      else if (index == 10)
        (void)snprintf(cache_name, sizeof cache_name, "St17__timepunct_cacheI%cE", ch);
      else
        (void)snprintf(cache_name, sizeof cache_name, "St18__moneypunct_cacheI%cLb%dEE", ch,
                       index == 7 ? 1 : 0);
      qa_guest_address cache_address = cache(locale, cache_name, wide,
                                             index == 2 ? 0 : index == 10 ? 2 : 1);
      write_ptr(locale, slot(locale, facet_address, 2 * p), cache_address);
      write_ptr(locale, slot(locale, caches, (int64_t)id * p), cache_address);
      if (index == 10) {
        write_ptr(locale, slot(locale, facet_address, 3 * p), locale->c_locale);
        write_ptr(locale, slot(locale, facet_address, 4 * p), qa_cxx_abi_bytes(abi, "C"));
      }
    } else if (index == 1 || index == 5 || index == 13) {
      write_ptr(locale, slot(locale, facet_address, 2 * p), locale->c_locale);
      if (index == 13)
        write_ptr(locale, slot(locale, facet_address, 3 * p), qa_cxx_abi_bytes(abi, "C"));
    }
  }
  built_facets result = {built[0], built[3], built[4]};
  return result;
}

void qa_system_v_classic_locale_init(qa_system_v_classic_locale *locale, qa_cxx_abi_data *abi) {
  memset(locale, 0, sizeof *locale);
  locale->abi = abi;
  qa_system_v_service_host *host = abi->host;
  qa_mapped_guest_memory *m = host->memory;
  int p = m->pointer_bytes;
  locale->facet_base = qa_cxx_abi_type(abi, "NSt6locale5facetE", NULL, 0);
  locale->c_locale = qa_system_v_allocate(host, (size_t)(29 * p));
  qa_guest_address tables = qa_system_v_allocate(host, 384 * 10);
  qa_guest_address classification = slot(locale, tables, 128 * 2);
  qa_guest_address lower = slot(locale, tables, 384 * 2 + 128 * 4);
  qa_guest_address upper = slot(locale, tables, 384 * 6 + 128 * 4);
  for (int c = -128; c < 256; c++) {
    int byte = c < 0 && c != -1 ? c + 256 : c;
    qa_memory_write_u16(m, slot(locale, classification, c * 2), (uint16_t)mask(byte));
    qa_gmem_write_i32(m, slot(locale, lower, c * 4), byte >= 65 && byte <= 90 ? byte + 32 : byte);
    qa_gmem_write_i32(m, slot(locale, upper, c * 4), byte >= 97 && byte <= 122 ? byte - 32 : byte);
  }
  write_ptr(locale, slot(locale, locale->c_locale, 13 * p), classification);
  write_ptr(locale, slot(locale, locale->c_locale, 14 * p), lower);
  write_ptr(locale, slot(locale, locale->c_locale, 15 * p), upper);
  qa_guest_address c_name = qa_cxx_abi_bytes(abi, "C");
  for (int i = 0; i < 13; i++)
    write_ptr(locale, slot(locale, locale->c_locale, (16 + i) * p), c_name);
  const char *loc_names[] = {"__ctype_b_loc", "__ctype_tolower_loc", "__ctype_toupper_loc"};
  qa_guest_address loc_tables[] = {classification, lower, upper};
  for (size_t i = 0; i < 3; i++) {
    qa_guest_address slot_address = qa_system_v_allocate(host, (size_t)p);
    write_ptr(locale, slot_address, loc_tables[i]);
    ctype_loc_state *state = xmalloc(sizeof *state);
    state->host = host; state->slot_address = slot_address;
    qa_system_v_implementation impl = {state, ctype_loc_impl};
    const char *versions[] = {"GLIBC_2.3", NULL};
    qa_system_v_service(host, "libc.so.6", loc_names[i], versions, 2, NULL, 0, false,
                        QA_STORAGE_POINTER, impl);
  }
  qa_guest_address facets = qa_system_v_allocate(host, (size_t)(28 * p));
  qa_guest_address caches = qa_system_v_allocate(host, (size_t)(28 * p));
  qa_guest_address names = qa_system_v_allocate(host, (size_t)(12 * p));
  write_ptr(locale, names, c_name);
  locale->implementation = qa_system_v_allocate(host, (size_t)(5 * p));
  qa_gmem_write_i32(m, locale->implementation, 2);
  write_ptr(locale, slot(locale, locale->implementation, p), facets);
  qa_write_unsigned(m, slot(locale, locale->implementation, 2 * p), p, 28);
  write_ptr(locale, slot(locale, locale->implementation, 3 * p), caches);
  write_ptr(locale, slot(locale, locale->implementation, 4 * p), names);
  built_facets narrow = construct_facets(locale, false, facets, caches, classification, lower, upper);
  built_facets wide = construct_facets(locale, true, facets, caches, classification, lower, upper);
  locale->ctype[0] = narrow.ctype; locale->ctype[1] = wide.ctype;
  locale->num_put[0] = narrow.num_put; locale->num_put[1] = wide.num_put;
  locale->num_get[0] = narrow.num_get; locale->num_get[1] = wide.num_get;
}
qa_guest_address qa_system_v_classic_locale_retain(qa_system_v_classic_locale *locale) {
  qa_mapped_guest_memory *m = locale->abi->host->memory;
  qa_gmem_write_i32(m, locale->implementation, qa_gmem_read_i32(m, locale->implementation) + 1);
  return locale->implementation;
}
void qa_system_v_classic_locale_release(qa_system_v_classic_locale *locale,
                                        qa_guest_address implementation) {
  qa_mapped_guest_memory *m = locale->abi->host->memory;
  int32_t value = qa_gmem_read_i32(m, implementation);
  if (value <= 2 && implementation.byte_offset == locale->implementation.byte_offset)
    plain_error("Released permanent classic locale reference");
  qa_gmem_write_i32(m, implementation, value - 1);
}

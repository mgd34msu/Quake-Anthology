/* Port of src/guest/pe/directories.ts. GPL-2.0-or-later. */
#include "qa/guest/pe/directories.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_pe_error(QA_PE_STAGE_HEADERS, "out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_pe_error(QA_PE_STAGE_HEADERS, "out of memory");
  return next;
}
static char *xstrndup(const char *text, size_t len) {
  char *copy = xmalloc(len + 1);
  memcpy(copy, text, len);
  copy[len] = '\0';
  return copy;
}

typedef struct import_list { qa_guest_import *items; size_t count, cap; } import_list;
static void import_push(import_list *list, qa_guest_import entry) {
  if (list->count == list->cap) {
    list->cap = list->cap == 0 ? 16 : list->cap * 2;
    list->items = xrealloc(list->items, list->cap * sizeof *list->items);
  }
  list->items[list->count++] = entry;
}
typedef struct export_list { qa_guest_export *items; size_t count, cap; } export_list;
static void export_push(export_list *list, qa_guest_export entry) {
  if (list->count == list->cap) {
    list->cap = list->cap == 0 ? 16 : list->cap * 2;
    list->items = xrealloc(list->items, list->cap * sizeof *list->items);
  }
  list->items[list->count++] = entry;
}

void qa_pe_read_imports(const qa_pe_image_reader *image, qa_pe_owned_strings *pool,
                        qa_guest_import **out, size_t *out_count) {
  qa_pe_image_reader reader = qa_pe_image_reader_for_stage(image, QA_PE_STAGE_IMPORTS);
  import_list imports; memset(&imports, 0, sizeof imports);
  uint32_t *slots = NULL; size_t slot_count = 0, slot_cap = 0;
  qa_pe_directory_entry table = qa_pe_directory(reader.pe, 1);
  if (qa_pe_directory(reader.pe, 13).rva != 0)
    qa_pe_error(QA_PE_STAGE_IMPORTS, "delay-load imports require a guest delay-loader; unsupported");
  *out = NULL; *out_count = 0;
  if (table.rva == 0) return;
  qa_pe_image_reader_range(&reader, table.rva, table.byte_length);
  int width = reader.pe->abi.pointer_bytes;
  uint64_t ordinal_bit = 1ULL << (width * 8 - 1);
  for (uint32_t at = table.rva; (uint64_t)at + 20 <= (uint64_t)table.rva + table.byte_length; at += 20) {
    uint32_t lookup = qa_pe_image_reader_u32(&reader, at);
    uint32_t timestamp = qa_pe_image_reader_u32(&reader, at + 4);
    uint32_t forward = qa_pe_image_reader_u32(&reader, at + 8);
    uint32_t name_rva = qa_pe_image_reader_u32(&reader, at + 12);
    uint32_t iat = qa_pe_image_reader_u32(&reader, at + 16);
    if (lookup == 0 && timestamp == 0 && forward == 0 && name_rva == 0 && iat == 0) {
      free(slots);
      *out = imports.items; *out_count = imports.count;
      return;
    }
    if (name_rva == 0 || iat == 0 || iat % (uint32_t)width != 0 || lookup % (uint32_t)width != 0
        || (lookup == 0 && timestamp != 0))
      qa_pe_error(QA_PE_STAGE_IMPORTS, "invalid descriptor/alignment or bound IAT without original lookup table");
    char *library = qa_pe_own_string(pool, qa_pe_image_reader_text(&reader, name_rva, reader.pe->image_size));
    if (library[0] == '\0') qa_pe_error(QA_PE_STAGE_IMPORTS, "empty import library");
    for (uint32_t index = 0;; index++) {
      uint64_t thunk = qa_pe_image_reader_pointer(&reader, (lookup != 0 ? lookup : iat) + index * (uint32_t)width);
      uint32_t slot = iat + index * (uint32_t)width;
      qa_pe_image_reader_range(&reader, slot, (size_t)width);
      if (thunk == 0) break;
      qa_guest_symbol_name symbol; memset(&symbol, 0, sizeof symbol);
      if ((thunk & ordinal_bit) != 0) {
        if ((thunk & ~(ordinal_bit | 0xffffULL)) != 0)
          qa_pe_error(QA_PE_STAGE_IMPORTS, "ordinal thunk has reserved bits");
        symbol.is_ordinal = true;
        symbol.ordinal = (int)(thunk & 0xffffULL);
      } else {
        if (thunk > 0x7fffffffULL) qa_pe_error(QA_PE_STAGE_IMPORTS, "name thunk is not a valid RVA");
        uint32_t rva = (uint32_t)thunk;
        (void)qa_pe_image_reader_u16(&reader, rva);
        char *name = qa_pe_own_string(pool, qa_pe_image_reader_text(&reader, rva + 2, reader.pe->image_size));
        if (name[0] == '\0') qa_pe_error(QA_PE_STAGE_IMPORTS, "empty import name");
        symbol.name = name;
      }
      for (size_t i = 0; i < slot_count; i++)
        if (slots[i] == slot) qa_pe_error(QA_PE_STAGE_IMPORTS, "overlapping import address slots");
      if (slot_count == slot_cap) {
        slot_cap = slot_cap == 0 ? 16 : slot_cap * 2;
        slots = xrealloc(slots, slot_cap * sizeof *slots);
      }
      slots[slot_count++] = slot;
      qa_guest_import entry; memset(&entry, 0, sizeof entry);
      entry.library = library; entry.symbol = symbol;
      entry.slot = qa_pe_image_reader_address(&reader, slot, (size_t)width);
      import_push(&imports, entry);
    }
  }
  free(slots);
  qa_pe_error(QA_PE_STAGE_IMPORTS, "unterminated import descriptor table");
}

static void forwarded_target(qa_pe_owned_strings *pool, const char *text, qa_guest_export *entry) {
  const char *separator = strrchr(text, '.');
  size_t length = strlen(text);
  if (separator == NULL || separator == text || separator == text + length - 1)
    qa_pe_error(QA_PE_STAGE_EXPORTS, "invalid export forwarder %s", text);
  char *library = qa_pe_own_string(pool, xstrndup(text, (size_t)(separator - text)));
  const char *name = separator + 1;
  entry->is_forward = true;
  entry->forward_library = library;
  if (name[0] == '#') {
    if (name[1] == '\0') qa_pe_error(QA_PE_STAGE_EXPORTS, "invalid forwarded ordinal");
    long ordinal = 0;
    for (const char *p = name + 1; *p != '\0'; p++) {
      if (!isdigit((unsigned char)*p)) qa_pe_error(QA_PE_STAGE_EXPORTS, "invalid forwarded ordinal");
      ordinal = ordinal * 10 + (*p - '0');
      if (ordinal > 65535) qa_pe_error(QA_PE_STAGE_EXPORTS, "forwarded ordinal out of range");
    }
    if (ordinal < 1) qa_pe_error(QA_PE_STAGE_EXPORTS, "forwarded ordinal out of range");
    entry->forward_symbol.is_ordinal = true;
    entry->forward_symbol.ordinal = (int)ordinal;
    return;
  }
  entry->forward_symbol.name = qa_pe_own_string(pool, xstrndup(name, strlen(name)));
}

void qa_pe_read_exports(const qa_pe_image_reader *image, qa_pe_owned_strings *pool,
                        qa_guest_export **out, size_t *out_count) {
  qa_pe_image_reader reader = qa_pe_image_reader_for_stage(image, QA_PE_STAGE_EXPORTS);
  *out = NULL; *out_count = 0;
  qa_pe_directory_entry table = qa_pe_directory(reader.pe, 0);
  if (table.rva == 0) return;
  if (table.byte_length < 40) qa_pe_error(QA_PE_STAGE_EXPORTS, "truncated export directory");
  qa_pe_image_reader_range(&reader, table.rva, table.byte_length);
  uint32_t ordinal_base = qa_pe_image_reader_u32(&reader, table.rva + 16);
  uint32_t count = qa_pe_image_reader_u32(&reader, table.rva + 20);
  uint32_t name_count = qa_pe_image_reader_u32(&reader, table.rva + 24);
  uint32_t addresses = qa_pe_image_reader_u32(&reader, table.rva + 28);
  uint32_t names = qa_pe_image_reader_u32(&reader, table.rva + 32);
  uint32_t ordinals = qa_pe_image_reader_u32(&reader, table.rva + 36);
  qa_pe_image_reader_range(&reader, addresses, (size_t)count * 4);
  qa_pe_image_reader_range(&reader, names, (size_t)name_count * 4);
  qa_pe_image_reader_range(&reader, ordinals, (size_t)name_count * 2);
  if ((uint64_t)ordinal_base + count > 0x100000000ULL)
    qa_pe_error(QA_PE_STAGE_EXPORTS, "export ordinal overflow");
  typedef struct target_entry { uint32_t index; qa_guest_export target; } target_entry;
  target_entry *targets = NULL; size_t target_count = 0, target_cap = 0;
  export_list exports; memset(&exports, 0, sizeof exports);
  for (uint32_t index = 0; index < count; index++) {
    uint32_t rva = qa_pe_image_reader_u32(&reader, addresses + index * 4);
    if (rva == 0) continue;
    qa_guest_export target; memset(&target, 0, sizeof target);
    if (rva >= table.rva && rva < table.rva + table.byte_length) {
      char *text = qa_pe_image_reader_text(&reader, rva, table.rva + table.byte_length);
      forwarded_target(pool, text, &target);
      free(text);
    } else {
      target.address = qa_pe_image_reader_address(&reader, rva, 1);
    }
    if (target_count == target_cap) {
      target_cap = target_cap == 0 ? 16 : target_cap * 2;
      targets = xrealloc(targets, target_cap * sizeof *targets);
    }
    target.symbol.is_ordinal = true;
    target.symbol.ordinal = (int)(ordinal_base + index);
    targets[target_count].index = index;
    targets[target_count].target = target;
    target_count++;
    export_push(&exports, target);
  }
  for (uint32_t index = 0; index < name_count; index++) {
    char *name = qa_pe_own_string(pool, qa_pe_image_reader_text(&reader,
        qa_pe_image_reader_u32(&reader, names + index * 4), reader.pe->image_size));
    uint16_t ordinal_index = qa_pe_image_reader_u16(&reader, ordinals + index * 2);
    const qa_guest_export *target = NULL;
    for (size_t i = 0; i < target_count; i++)
      if (targets[i].index == ordinal_index) target = &targets[i].target;
    /* TS tracks exported names in a separate set; named entries start at target_count. */
    bool seen = false;
    for (size_t i = 0; i < exports.count; i++) {
      const qa_guest_export *entry = &exports.items[i];
      if (i >= target_count && !entry->symbol.is_ordinal && entry->symbol.name != NULL
          && strcmp(entry->symbol.name, name) == 0) seen = true;
    }
    if (name[0] == '\0' || seen || target == NULL)
      qa_pe_error(QA_PE_STAGE_EXPORTS, "duplicate name or invalid export ordinal target");
    qa_guest_export entry = *target;
    entry.symbol.is_ordinal = false;
    entry.symbol.name = name;
    entry.symbol.version = NULL;
    entry.symbol.ordinal = 0;
    export_push(&exports, entry);
  }
  free(targets);
  *out = exports.items; *out_count = exports.count;
}

void qa_pe_read_tls(const qa_pe_image_reader *image, bool *has_tls, qa_guest_tls_template *tls,
                    bool *has_index, qa_guest_core_address *index_out) {
  qa_pe_image_reader reader = qa_pe_image_reader_for_stage(image, QA_PE_STAGE_TLS);
  *has_tls = false; *has_index = false;
  memset(tls, 0, sizeof *tls);
  qa_pe_directory_entry table = qa_pe_directory(reader.pe, 9);
  if (table.rva == 0) return;
  int width = reader.pe->abi.pointer_bytes;
  if (table.byte_length < (uint32_t)(width * 4 + 8)) qa_pe_error(QA_PE_STAGE_TLS, "truncated TLS directory");
  uint64_t start = qa_pe_image_reader_pointer(&reader, table.rva);
  uint64_t end = qa_pe_image_reader_pointer(&reader, table.rva + (uint32_t)width);
  uint64_t index = qa_pe_image_reader_pointer(&reader, table.rva + (uint32_t)width * 2);
  uint64_t callback_array = qa_pe_image_reader_pointer(&reader, table.rva + (uint32_t)width * 3);
  uint32_t zero_fill = qa_pe_image_reader_u32(&reader, table.rva + (uint32_t)width * 4);
  uint32_t flags = qa_pe_image_reader_u32(&reader, table.rva + (uint32_t)width * 4 + 4);
  uint32_t alignment_code = (flags >> 20) & 15;
  if (end < start || end - start > reader.pe->image_size || alignment_code == 15 || index == 0)
    qa_pe_error(QA_PE_STAGE_TLS, "invalid TLS range, alignment or index address");
  size_t size = (size_t)(end - start);
  uint8_t *initialized;
  if (start == 0 && end == 0) initialized = xmalloc(0);
  else initialized = qa_pe_image_reader_copy(&reader, qa_pe_image_reader_rva(&reader, start, size), size);
  qa_guest_core_address *callbacks = NULL; size_t callback_count = 0, cap = 0;
  if (callback_array != 0) {
    uint32_t array = qa_pe_image_reader_rva(&reader, callback_array, (size_t)width);
    for (uint32_t at = array;; at += (uint32_t)width) {
      uint64_t callback = qa_pe_image_reader_pointer(&reader, at);
      if (callback == 0) break;
      if (callback_count == cap) {
        cap = cap == 0 ? 8 : cap * 2;
        callbacks = xrealloc(callbacks, cap * sizeof *callbacks);
      }
      callbacks[callback_count++] = qa_pe_image_reader_executable(&reader,
          qa_pe_image_reader_rva(&reader, callback, 1));
    }
  }
  tls->image = reader.module;
  tls->initialized = initialized; tls->initialized_len = size;
  tls->zero_fill_bytes = zero_fill;
  tls->alignment = alignment_code == 0 ? 1 : (1ULL << (alignment_code - 1));
  tls->callbacks = callbacks; tls->callback_count = callback_count;
  *has_tls = true;
  *index_out = qa_pe_image_reader_address(&reader, qa_pe_image_reader_rva(&reader, index, 4), 4);
  *has_index = true;
}

static qa_pe_unwind_record read_unwind_entry(const qa_pe_image_reader *reader, uint32_t begin_rva,
                                             uint32_t end_rva, uint32_t info_rva,
                                             const uint32_t *ancestors, size_t ancestor_count) {
  bool cyclic = false;
  for (size_t i = 0; i < ancestor_count; i++) if (ancestors[i] == info_rva) cyclic = true;
  if (begin_rva >= end_rva || cyclic || ancestor_count > 64)
    qa_pe_error(QA_PE_STAGE_UNWIND, "invalid function range or cyclic unwind chain");
  (void)qa_pe_image_reader_executable(reader, begin_rva);
  (void)qa_pe_image_reader_executable(reader, end_rva - 1);
  if (info_rva % 4 != 0) qa_pe_error(QA_PE_STAGE_UNWIND, "unaligned unwind metadata");
  uint32_t header = qa_pe_image_reader_u32(reader, info_rva);
  int version = (int)(header & 7);
  int flags = (int)((header >> 3) & 31);
  int count = (int)((header >> 16) & 255);
  if ((version != 1 && version != 2) || (flags & ~7) != 0 || ((flags & 4) != 0 && (flags & 3) != 0))
    qa_pe_error(QA_PE_STAGE_UNWIND, "unsupported version/flags %d/%d", version, flags);
  uint32_t tail = info_rva + 4 + (uint32_t)((count + 1) / 2) * 4;
  qa_pe_unwind_record record; memset(&record, 0, sizeof record);
  record.begin_rva = begin_rva; record.end_rva = end_rva; record.unwind_info_rva = info_rva;
  record.version = version; record.flags = flags;
  uint32_t size = tail - info_rva;
  if ((flags & 4) != 0) {
    uint32_t next_ancestors[66];
    memcpy(next_ancestors, ancestors, ancestor_count * sizeof(uint32_t));
    next_ancestors[ancestor_count] = info_rva;
    qa_pe_unwind_record *chained = xmalloc(sizeof(qa_pe_unwind_record));
    *chained = read_unwind_entry(reader, qa_pe_image_reader_u32(reader, tail),
                                  qa_pe_image_reader_u32(reader, tail + 4),
                                  qa_pe_image_reader_u32(reader, tail + 8),
                                  next_ancestors, ancestor_count + 1);
    record.chained = chained;
    size += 12;
  } else if ((flags & 3) != 0) {
    record.handler_rva = qa_pe_image_reader_u32(reader, tail);
    record.has_handler_rva = true;
    (void)qa_pe_image_reader_executable(reader, record.handler_rva);
    record.handler_data_rva = tail + 4;
    record.has_handler_data_rva = true;
    size += 4;
  }
  record.metadata = qa_pe_image_reader_copy(reader, info_rva, size);
  record.metadata_len = size;
  return record;
}

void qa_pe_read_unwind(const qa_pe_image_reader *image, qa_pe_unwind_record **out, size_t *out_count) {
  qa_pe_image_reader reader = qa_pe_image_reader_for_stage(image, QA_PE_STAGE_UNWIND);
  *out = NULL; *out_count = 0;
  qa_pe_directory_entry table = qa_pe_directory(reader.pe, 3);
  if (table.rva == 0) return;
  if (reader.pe->abi.pointer_bytes != 8 || table.byte_length % 12 != 0)
    qa_pe_error(QA_PE_STAGE_UNWIND, "unsupported exception directory layout");
  qa_pe_image_reader_range(&reader, table.rva, table.byte_length);
  size_t count = table.byte_length / 12;
  qa_pe_unwind_record *records = xmalloc(count * sizeof(qa_pe_unwind_record));
  int64_t previous = -1;
  size_t n = 0;
  for (uint32_t at = table.rva; at < table.rva + table.byte_length; at += 12) {
    uint32_t begin = qa_pe_image_reader_u32(&reader, at);
    if ((int64_t)begin <= previous) qa_pe_error(QA_PE_STAGE_UNWIND, "unsorted runtime function table");
    previous = begin;
    records[n++] = read_unwind_entry(&reader, begin, qa_pe_image_reader_u32(&reader, at + 4),
                                      qa_pe_image_reader_u32(&reader, at + 8), NULL, 0);
  }
  *out = records; *out_count = n;
}

bool qa_pe_read_load_configuration(const qa_pe_image_reader *image, qa_pe_load_configuration *out) {
  qa_pe_image_reader reader = qa_pe_image_reader_for_stage(image, QA_PE_STAGE_LOAD_CONFIG);
  memset(out, 0, sizeof *out);
  qa_pe_directory_entry table = qa_pe_directory(reader.pe, 10);
  if (table.rva == 0) return false;
  uint32_t size = qa_pe_image_reader_u32(&reader, table.rva);
  if (size < 4 || size > table.byte_length)
    qa_pe_error(QA_PE_STAGE_LOAD_CONFIG, "invalid load configuration size");
  int width = reader.pe->abi.pointer_bytes;
  out->address = qa_pe_image_reader_address(&reader, table.rva, size);
  out->bytes = qa_pe_image_reader_copy(&reader, table.rva, size);
  out->bytes_len = size;
  struct { uint32_t offset4, offset8; bool *has; qa_guest_core_address *slot; } fields[3] = {
      {60, 88, &out->has_security_cookie, &out->security_cookie_address},
      {72, 112, &out->has_guard_check, &out->guard_check_slot},
      {76, 120, &out->has_guard_dispatch, &out->guard_dispatch_slot}};
  for (int i = 0; i < 3; i++) {
    uint32_t offset = width == 4 ? fields[i].offset4 : fields[i].offset8;
    if (offset + (uint32_t)width > size) continue;
    uint64_t value = qa_pe_image_reader_pointer(&reader, table.rva + offset);
    if (value == 0) continue;
    *fields[i].slot = qa_pe_image_reader_address(&reader,
        qa_pe_image_reader_rva(&reader, value, (size_t)width), (size_t)width);
    *fields[i].has = true;
  }
  uint32_t guard_offset = width == 4 ? 88 : 144;
  out->guard_flags = guard_offset + 4 <= size ? qa_pe_image_reader_u32(&reader, table.rva + guard_offset) : 0;
  return true;
}

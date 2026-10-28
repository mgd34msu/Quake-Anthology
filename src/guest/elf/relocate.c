/* Port of src/guest/elf/relocate.ts. GPL-2.0-or-later. */
#include "qa/guest/elf/relocate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("ELF: out of memory");
  return next;
}
static char *xstrdup(const char *text) {
  size_t len = strlen(text);
  char *copy = (char *)xrealloc(NULL, len + 1);
  memcpy(copy, text, len + 1);
  return copy;
}
static void elf_error(const char *format, ...) {
  char buffer[640];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  qa_range_error("ELF: %s", buffer);
}

void qa_elf_unique_symbols_init(qa_elf_unique_symbols *map) { memset(map, 0, sizeof *map); }
void qa_elf_unique_symbols_free(qa_elf_unique_symbols *map) {
  for (size_t i = 0; i < map->count; i++) free(map->entries[i].name);
  free(map->entries);
  memset(map, 0, sizeof *map);
}
static size_t unique_index(const qa_elf_unique_symbols *map, const char *name, bool *found) {
  for (size_t i = 0; i < map->count; i++)
    if (strcmp(map->entries[i].name, name) == 0) { *found = true; return i; }
  *found = false;
  return 0;
}
bool qa_elf_unique_symbols_get(const qa_elf_unique_symbols *map, const char *name, qa_guest_core_address *out) {
  bool found = false;
  size_t index = unique_index(map, name, &found);
  if (!found) return false;
  *out = map->entries[index].address;
  return true;
}
bool qa_elf_unique_symbols_has(const qa_elf_unique_symbols *map, const char *name) {
  bool found = false;
  (void)unique_index(map, name, &found);
  return found;
}
void qa_elf_unique_symbols_set(qa_elf_unique_symbols *map, const char *name, qa_guest_core_address address) {
  bool found = false;
  size_t index = unique_index(map, name, &found);
  if (found) { map->entries[index].address = address; return; }
  if (map->count == map->cap) {
    map->cap = map->cap == 0 ? 8 : map->cap * 2;
    map->entries = xrealloc(map->entries, map->cap * sizeof *map->entries);
  }
  map->entries[map->count].name = xstrdup(name);
  map->entries[map->count].address = address;
  map->count++;
}
void qa_elf_unique_symbols_delete(qa_elf_unique_symbols *map, const char *name) {
  bool found = false;
  size_t index = unique_index(map, name, &found);
  if (!found) return;
  free(map->entries[index].name);
  memmove(&map->entries[index], &map->entries[index + 1],
          (map->count - index - 1) * sizeof *map->entries);
  map->count--;
}

static void import_push(qa_elf_import_list *list, qa_guest_import import_) {
  if (list->count == list->cap) {
    list->cap = list->cap == 0 ? 16 : list->cap * 2;
    list->items = xrealloc(list->items, list->cap * sizeof *list->items);
  }
  list->items[list->count++] = import_;
}

qa_guest_core_address qa_elf_address(qa_mapped_guest_memory *memory, uint64_t value) {
  qa_guest_core_address pointer = {0};
  if (!qa_memory_pointer(memory, value, &pointer)) elf_error("required address is null");
  return pointer;
}
qa_guest_import qa_elf_symbol_import(const qa_elf_symbol *symbol, qa_guest_core_address slot) {
  qa_guest_import import_; memset(&import_, 0, sizeof import_);
  import_.library = symbol->has_version ? symbol->version.library : "";
  import_.symbol.is_ordinal = false;
  import_.symbol.name = symbol->name;
  import_.symbol.version = symbol->has_version ? symbol->version.name : NULL;
  import_.slot = slot;
  import_.weak = symbol->binding == 2;
  return import_;
}

static bool symbolic_binding(const qa_elf_inspection_full *elf) {
  uint64_t flags = 0;
  return qa_elf_dynamic_has(elf, 16)
      || ((qa_elf_dynamic_value(elf, 30, &flags) ? flags : 0) & 2) != 0;
}

static size_t external_symbol_size(qa_elf_relocation_context *context,
                                   const qa_elf_symbol *symbol, qa_guest_core_address slot) {
  size_t size = 0;
  qa_guest_import import_ = qa_elf_symbol_import(symbol, slot);
  bool found = context->resolve_symbol_size != NULL
      && context->resolve_symbol_size(context->resolve_ctx, &import_, context->image, &size);
  if (!found)
    elf_error("COPY/SIZE relocation for %s needs provider symbol size metadata", symbol->name);
  if (size > (size_t)QA_MAX_SAFE_INTEGER)
    elf_error("invalid provider symbol size for %s", symbol->name);
  return size;
}

static uint64_t got_address(qa_elf_relocation_context *context) {
  uint64_t got = 0;
  if (!qa_elf_dynamic_value(context->elf, 3, &got)) elf_error("GOT relocation lacks DT_PLTGOT");
  return context->load_bias + got;
}

uint64_t qa_elf_defined_symbol_address(uint64_t load_bias, qa_mapped_guest_memory *memory,
                                       qa_guest_core_address (*resolve_indirect)(void *ctx, qa_guest_core_address),
                                       void *resolve_ctx, const qa_elf_symbol *symbol) {
  if (symbol->section == 0xfff2) elf_error("unallocated COMMON symbol %s", symbol->name);
  if (symbol->section >= 0xff00 && symbol->section != 0xfff1)
    elf_error("unsupported reserved symbol section for %s", symbol->name);
  if (symbol->type == 6) elf_error("TLS symbol %s is an offset, not an image address", symbol->name);
  uint64_t raw = symbol->section == 0xfff1 ? symbol->value : load_bias + symbol->value;
  if (symbol->type != 10) return raw;
  if (resolve_indirect == NULL) elf_error("GNU IFUNC %s requires guest resolver execution", symbol->name);
  qa_guest_core_address resolved = resolve_indirect(resolve_ctx, qa_elf_address(memory, raw));
  qa_memory_check(memory, resolved, 1, QA_ACCESS_EXECUTE);
  return resolved.byte_offset;
}

uint64_t qa_elf_resolve_symbol(qa_elf_relocation_context *context, const qa_elf_symbol *symbol,
                               qa_guest_core_address slot, bool copy) {
  if (symbol->index == 0) return 0;
  if (symbol->binding != 0 && symbol->binding != 1 && symbol->binding != 2 && symbol->binding != 10)
    elf_error("unsupported symbol binding %d for %s", symbol->binding, symbol->name);
  bool own = symbol->section != 0;
  bool unique = symbol->binding == 10;
  if (unique && context->unique_symbols == NULL)
    elf_error("GNU unique symbols require a process-wide symbol registry");
  qa_guest_core_address existing = {0};
  bool has_existing = unique && qa_elf_unique_symbols_get(context->unique_symbols, symbol->name, &existing);
  if (has_existing) {
    qa_memory_check(context->memory, existing, 1, QA_ACCESS_READ);
    if (!own) import_push(context->imports, qa_elf_symbol_import(symbol, slot));
    return existing.byte_offset;
  }
  if (!copy && own && (symbol->binding == 0 || symbol->visibility != 0 || symbolic_binding(context->elf)))
    return qa_elf_defined_symbol_address(context->load_bias, context->memory,
                                          context->resolve_indirect, context->resolve_ctx, symbol);
  qa_guest_import import_ = qa_elf_symbol_import(symbol, slot);
  qa_guest_import_resolution resolution = context->resolver.resolve(context->resolver.ctx, &import_, context->image);
  if (resolution.kind != QA_RESOLUTION_UNRESOLVED) {
    qa_memory_check(context->memory, resolution.address, 1,
                    symbol->type == 2 || symbol->type == 10 ? QA_ACCESS_EXECUTE : QA_ACCESS_READ);
    if (unique) qa_elf_unique_symbols_set(context->unique_symbols, symbol->name, resolution.address);
    if (!own || copy) import_push(context->imports, import_);
    return resolution.address.byte_offset;
  }
  if (!copy && own) {
    uint64_t address = qa_elf_defined_symbol_address(context->load_bias, context->memory,
                                                     context->resolve_indirect, context->resolve_ctx, symbol);
    if (unique)
      qa_elf_unique_symbols_set(context->unique_symbols, symbol->name,
                                qa_elf_address(context->memory, address));
    return address;
  }
  import_push(context->imports, import_);
  if (symbol->binding == 2) return 0;
  char version_suffix[512];
  version_suffix[0] = '\0';
  if (symbol->has_version) (void)snprintf(version_suffix, sizeof version_suffix, "@%s", symbol->version.name);
  elf_error("unresolved import %s:%s%s: %s",
            import_.library != NULL && import_.library[0] != '\0' ? import_.library : "<global>",
            symbol->name, version_suffix, resolution.detail != NULL ? resolution.detail : "");
  return 0;
}

static bool int_in(int value, const int *set, size_t count) {
  for (size_t i = 0; i < count; i++) if (set[i] == value) return true;
  return false;
}
static uint64_t load_le(const uint8_t *bytes, int width) {
  uint64_t raw = 0;
  for (int i = width - 1; i >= 0; i--) raw = (raw << 8) | bytes[i];
  return raw;
}
static int64_t load_le_signed(const uint8_t *bytes, int width) {
  uint64_t raw = load_le(bytes, width);
  if (width >= 8) return (int64_t)raw;
  int bits = width * 8;
  uint64_t sign = 1ULL << (bits - 1);
  return (int64_t)((raw ^ sign) - sign);
}
static void store_le(uint8_t *bytes, uint64_t raw, int width) {
  for (int i = 0; i < width; i++) bytes[i] = (uint8_t)(raw >> (i * 8));
}

static void apply(qa_elf_relocation_context *context, const qa_elf_relocation *relocation) {
  const qa_elf_inspection_full *elf = context->elf;
  qa_mapped_guest_memory *memory = context->memory;
  uint64_t load_bias = context->load_bias;
  bool wide = elf->abi.pointer_bytes == 8;
  uint32_t type = relocation->type;
  if (type == 0) return;
  int width = wide ? ((type == 12 || type == 13) ? 2 : (type == 14 || type == 15) ? 1
      : (type == 2 || type == 4 || type == 10 || type == 11 || type == 23 || type == 32) ? 4 : 8) : 4;
  const qa_elf_symbol *symbol = relocation->symbol_index < elf->symbol_count
      ? &elf->symbols[relocation->symbol_index] : NULL;
  size_t byte_length = (type == 5 && symbol != NULL) ? symbol->size : (size_t)width;
  bool in_image = false;
  for (size_t i = 0; i < elf->segment_count; i++) {
    const qa_elf_segment_info *segment = &elf->segments[i];
    if (segment->type == 1 && relocation->address >= segment->address
        && relocation->address + byte_length <= segment->address + segment->memory_size)
      in_image = true;
  }
  if (!in_image)
    elf_error("relocation target 0x%llx lies outside this image", (unsigned long long)relocation->address);
  qa_guest_core_address slot = qa_elf_address(memory, load_bias + relocation->address);
  uint8_t *bytes = (uint8_t *)xrealloc(NULL, byte_length);
  qa_memory_copy(memory, slot, bytes, byte_length);
  int64_t addend = type == 5 ? 0
      : relocation->has_addend ? relocation->addend : load_le_signed(bytes, width);
  uint64_t place = slot.byte_offset;
  int64_t result = 0;
  bool signed_ = false;
  bool checked = wide;
  static const int tls_wide[] = {16, 17, 18, 23};
  static const int tls_narrow[] = {14, 17, 34, 35, 36, 37};
  if (type == 8 || (wide && type == 38)) {
    if (relocation->symbol_index != 0) elf_error("relative relocation has a symbol");
    result = (int64_t)(load_bias + (uint64_t)addend);
  } else if ((int)type == (wide ? 37 : 42)) {
    if (relocation->symbol_index != 0 || context->resolve_indirect == NULL)
      elf_error("IRELATIVE requires explicit guest resolver execution");
    qa_guest_core_address address = context->resolve_indirect(context->resolve_ctx,
        qa_elf_address(memory, load_bias + (uint64_t)addend));
    qa_memory_check(memory, address, 1, QA_ACCESS_EXECUTE);
    result = (int64_t)address.byte_offset;
  } else if (wide ? int_in((int)type, tls_wide, 4) : int_in((int)type, tls_narrow, 6)) {
    const qa_elf_tls_bindings *bindings = context->tls;
    if (bindings == NULL) elf_error("TLS relocation %u requires a guest thread/module allocation", type);
    qa_elf_tls_resolution target; memset(&target, 0, sizeof target);
    if (relocation->symbol_index == 0) {
      target.module = bindings->current; target.offset = 0;
    } else if (symbol == NULL || symbol->type != 6) {
      elf_error("TLS relocation references a non-TLS symbol");
    } else {
      qa_guest_import import_ = qa_elf_symbol_import(symbol, slot);
      bool local = symbol->section != 0
          && (symbol->binding == 0 || symbol->visibility != 0 || symbolic_binding(elf));
      bool resolved = false;
      if (!local && bindings->resolve != NULL)
        resolved = bindings->resolve(bindings->ctx, &import_, context->image, &target);
      if (resolved) import_push(context->imports, import_);
      else if (symbol->section != 0) {
        target.module = bindings->current; target.offset = (int64_t)symbol->value;
      } else elf_error("unresolved TLS symbol %s", symbol->name);
    }
    if (target.module.module_id == 0) elf_error("ELF TLS module IDs begin at one");
    if ((int)type == (wide ? 16 : 35)) result = (int64_t)target.module.module_id;
    else if ((int)type == (wide ? 17 : 36)) result = target.offset + (wide ? addend : 0);
    else {
      if (!target.module.has_thread_pointer_offset)
        elf_error("static TLS relocation has no signed thread-pointer offset");
      int64_t displacement = target.module.thread_pointer_offset + target.offset;
      result = (!wide && (type == 34 || type == 37) ? -displacement : displacement) + addend;
      signed_ = true;
    }
  } else {
    if (symbol == NULL)
      elf_error("relocation %u lacks symbol %llu", type, (unsigned long long)relocation->symbol_index);
    uint64_t symbol_address = qa_elf_resolve_symbol(context, symbol, slot, type == 5);
    if (type == 5) {
      if (symbol_address == 0) {
        if (!qa_elf_symbol_import(symbol, slot).weak) elf_error("COPY source is null");
        free(bytes);
        return;
      }
      for (size_t i = 0; i < context->image->mapping_count; i++) {
        const qa_guest_mapping *mapping = &context->image->mappings[i];
        if (symbol_address >= mapping->base && symbol_address < mapping->base + mapping->byte_length)
          elf_error("COPY must resolve a definition outside the requesting image");
      }
      size_t source_size = external_symbol_size(context, symbol, slot);
      size_t copied = source_size < symbol->size ? source_size : symbol->size;
      uint8_t *source = (uint8_t *)xrealloc(NULL, copied);
      qa_memory_copy(memory, qa_elf_address(memory, symbol_address), source, copied);
      qa_memory_write(memory, slot, source, copied);
      free(source);
      free(bytes);
      return;
    }
    switch (type) {
      case 1: result = (int64_t)(symbol_address + (uint64_t)addend); break;
      case 2: result = (int64_t)(symbol_address + (uint64_t)addend - place); signed_ = true; break;
      case 4: elf_error("PLT32 relocation requires a link-editor PLT address"); break;
      case 6: case 7: result = (int64_t)symbol_address; break;
      case 9:
        if (wide) elf_error("dynamic x64 GOTPCREL requires a link-editor GOT slot");
        result = (int64_t)(symbol_address + (uint64_t)addend - got_address(context));
        break;
      case 10:
        result = wide ? (int64_t)(symbol_address + (uint64_t)addend)
                      : (int64_t)(got_address(context) + (uint64_t)addend - place);
        break;
      case 11:
        if (!wide) elf_error("R_386_32PLT requires a link-editor PLT address");
        result = (int64_t)(symbol_address + (uint64_t)addend); signed_ = true;
        break;
      case 12: case 14:
        if (!wide) elf_error("unsupported i386 relocation %u", type);
        result = (int64_t)(symbol_address + (uint64_t)addend);
        break;
      case 13: case 15: case 24:
        if (!wide) elf_error("unsupported i386 relocation %u", type);
        result = (int64_t)(symbol_address + (uint64_t)addend - place); signed_ = true;
        break;
      case 32: case 33: case 38: {
        if (wide ? type == 38 : type != 38) elf_error("unsupported relocation %u", type);
        uint64_t defined = symbol->section == 0xfff1 ? symbol->value : load_bias + symbol->value;
        size_t size = (symbol->section != 0 && symbol_address == defined)
            ? symbol->size : external_symbol_size(context, symbol, slot);
        result = (int64_t)(size + (uint64_t)addend);
        break;
      }
      default:
        elf_error("unsupported %s relocation %u for %s", wide ? "x86-64" : "i386", type, symbol->name);
        break;
    }
  }
  if (width == 8) checked = false;
  int bits = width * 8;
  if (checked) {
    bool over = false;
    if (signed_) {
      int64_t lower = bits == 64 ? INT64_MIN : -(1LL << (bits - 1));
      int64_t upper = bits == 64 ? INT64_MAX : (1LL << (bits - 1)) - 1;
      over = result < lower || result > upper;
    } else {
      uint64_t upper = bits == 64 ? UINT64_MAX : (1ULL << bits) - 1;
      over = result < 0 || (uint64_t)result > upper;
    }
    if (over)
      elf_error("relocation %u overflows %s %d bits", type, signed_ ? "signed" : "unsigned", bits);
  }
  uint64_t normalized = bits >= 64 ? (uint64_t)result : (uint64_t)result & ((1ULL << bits) - 1);
  store_le(bytes, normalized, width);
  qa_memory_write(memory, slot, bytes, byte_length);
  free(bytes);
}

void qa_relocate_elf(qa_elf_relocation_context *context) {
  const qa_elf_relocation **deferred = NULL;
  size_t deferred_count = 0, cap = 0;
  int indirect_type = context->elf->abi.pointer_bytes == 8 ? 37 : 42;
  for (size_t i = 0; i < context->elf->relocation_count; i++) {
    const qa_elf_relocation *relocation = &context->elf->relocations[i];
    const qa_elf_symbol *symbol = relocation->symbol_index < context->elf->symbol_count
        ? &context->elf->symbols[relocation->symbol_index] : NULL;
    bool defer = (int)relocation->type == indirect_type
        || (symbol != NULL && symbol->type == 10 && symbol->section != 0);
    if (defer) {
      if (deferred_count == cap) {
        cap = cap == 0 ? 8 : cap * 2;
        deferred = xrealloc(deferred, cap * sizeof *deferred);
      }
      deferred[deferred_count++] = relocation;
    } else apply(context, relocation);
  }
  for (size_t i = 0; i < deferred_count; i++) apply(context, deferred[i]);
  free(deferred);
}

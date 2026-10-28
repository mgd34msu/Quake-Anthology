/* Port of src/guest/elf/index.ts (loadElf). GPL-2.0-or-later. */
#include "qa/guest/elf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("ELF: out of memory");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("ELF: out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("ELF: out of memory");
  return next;
}
static void elf_error(const char *format, ...) {
  char buffer[640];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  qa_range_error("ELF: %s", buffer);
}

#define PAGE_BYTES 4096ULL
static uint64_t page_down(uint64_t value) { return value - value % PAGE_BYTES; }
static uint64_t page_up(uint64_t value) { return (value + PAGE_BYTES - 1) / PAGE_BYTES * PAGE_BYTES; }
static qa_guest_permissions seg_permissions(uint32_t flags) {
  if ((flags & 2) != 0) return (flags & 1) != 0 ? QA_PERM_READ_WRITE_EXECUTE : QA_PERM_READ_WRITE;
  if ((flags & 1) != 0) return (flags & 4) != 0 ? QA_PERM_READ_EXECUTE : QA_PERM_EXECUTE;
  return (flags & 4) != 0 ? QA_PERM_READ : QA_PERM_NONE;
}
static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}

static char *own_string(qa_elf_guest_image *image, char *text) {
  if (image->owned_string_count == image->owned_string_cap) {
    image->owned_string_cap = image->owned_string_cap == 0 ? 8 : image->owned_string_cap * 2;
    image->owned_strings = xrealloc(image->owned_strings,
                                    image->owned_string_cap * sizeof *image->owned_strings);
  }
  image->owned_strings[image->owned_string_count++] = text;
  return text;
}
static char *make_label(qa_elf_guest_image *image, const char *artifact_path) {
  const char *suffix = ":PT_LOAD";
  size_t len = strlen(artifact_path) + strlen(suffix);
  char *label = xmalloc(len + 1);
  (void)snprintf(label, len + 1, "%s%s", artifact_path, suffix);
  return own_string(image, label);
}

static void validate_dynamic_policy(const qa_elf_inspection_full *elf) {
  static const uint64_t banned[] = {0x7fffffffULL, 0x7ffffffdULL, 0x6ffffefbULL, 0x6ffffefcULL};
  for (size_t i = 0; i < 4; i++)
    if (qa_elf_dynamic_has(elf, banned[i]))
      elf_error("dynamic filter/audit dependency tag 0x%llx is unsupported", (unsigned long long)banned[i]);
  static const uint64_t supported[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
      24, 25, 26, 27, 28, 29, 30, 32, 33, 35, 36, 37, 0x6ffffef5ULL, 0x6ffffff0ULL, 0x6ffffff9ULL, 0x6ffffffaULL,
      0x6ffffffbULL, 0x6ffffffcULL, 0x6ffffffdULL, 0x6ffffffeULL, 0x6fffffffULL};
  for (size_t i = 0; i < elf->dynamic_count; i++) {
    uint64_t tag = elf->dynamic[i].tag;
    bool ok = false;
    for (size_t j = 0; j < sizeof supported / sizeof *supported; j++) if (supported[j] == tag) ok = true;
    if (!ok) elf_error("unsupported dynamic tag 0x%llx", (unsigned long long)tag);
  }
  uint64_t flags = 0, flags1 = 0;
  if (!qa_elf_dynamic_value(elf, 30, &flags)) flags = 0;
  if ((flags & ~31ULL) != 0) elf_error("unsupported DT_FLAGS 0x%llx", (unsigned long long)flags);
  if (!qa_elf_dynamic_value(elf, 0x6ffffffbULL, &flags1)) flags1 = 0;
  if ((flags1 & ~(1ULL | 0x08000000ULL)) != 0)
    elf_error("unsupported DT_FLAGS_1 0x%llx", (unsigned long long)flags1);
}

typedef struct addr_list { qa_guest_core_address *items; size_t count, cap; } addr_list;
static void addr_push(addr_list *list, qa_guest_core_address address) {
  if (list->count == list->cap) {
    list->cap = list->cap == 0 ? 8 : list->cap * 2;
    list->items = xrealloc(list->items, list->cap * sizeof *list->items);
  }
  list->items[list->count++] = address;
}

static void pointer_array(const qa_elf_inspection_full *elf, qa_mapped_guest_memory *memory,
                          uint64_t load_bias, uint64_t address_tag, uint64_t size_tag,
                          qa_guest_core_address **out, size_t *out_count) {
  *out = NULL; *out_count = 0;
  uint64_t address = 0, size = 0;
  bool has_address = qa_elf_dynamic_value(elf, address_tag, &address);
  bool has_size = qa_elf_dynamic_value(elf, size_tag, &size);
  if (!has_address && !has_size) return;
  if (!has_address || !has_size || size % (uint64_t)elf->abi.pointer_bytes != 0)
    elf_error("incomplete initializer/finalizer array");
  size_t length = qa_elf_checked_number(size, "function array length");
  if (length == 0) return;
  (void)qa_elf_file_offset(elf->segments, elf->segment_count, address, length);
  uint8_t *bytes = xmalloc(length);
  qa_memory_copy(memory, qa_elf_address(memory, load_bias + address), bytes, length);
  addr_list list; memset(&list, 0, sizeof list);
  uint64_t sentinel = elf->abi.pointer_bytes == 8 ? UINT64_MAX : 0xffffffffULL;
  for (size_t offset = 0; offset < length; offset += (size_t)elf->abi.pointer_bytes) {
    uint64_t value = 0;
    for (int i = elf->abi.pointer_bytes - 1; i >= 0; i--) value = (value << 8) | bytes[offset + (size_t)i];
    if (value != 0 && value != sentinel) addr_push(&list, qa_elf_address(memory, value));
  }
  free(bytes);
  *out = list.items; *out_count = list.count;
}

typedef struct export_list { qa_guest_export *items; size_t count, cap; } export_list;
static void export_push(export_list *list, qa_guest_export entry) {
  if (list->count == list->cap) {
    list->cap = list->cap == 0 ? 16 : list->cap * 2;
    list->items = xrealloc(list->items, list->cap * sizeof *list->items);
  }
  list->items[list->count++] = entry;
}
static qa_guest_symbol_name export_symbol_name(const qa_elf_symbol *symbol) {
  qa_guest_symbol_name name; memset(&name, 0, sizeof name);
  name.name = symbol->name;
  name.version = symbol->has_version ? symbol->version.name : NULL;
  return name;
}
static void append_export(export_list *list, const qa_elf_symbol *symbol, qa_guest_core_address address) {
  qa_guest_export entry; memset(&entry, 0, sizeof entry);
  entry.symbol = export_symbol_name(symbol);
  entry.address = address;
  export_push(list, entry);
  if (symbol->has_version && !symbol->version.hidden) {
    qa_guest_export plain = entry;
    plain.symbol.version = NULL;
    export_push(list, plain);
  }
}

bool qa_load_elf(qa_elf_guest_image *out, const qa_elf_load_options *options) {
  memset(out, 0, sizeof *out);
  uint8_t *bytes = xmalloc(options->bytes_len);
  memcpy(bytes, options->bytes, options->bytes_len);
  out->bytes_copy = bytes;
  qa_inspect_elf(&out->elf, bytes, options->bytes_len);
  const qa_elf_inspection_full *elf = &out->elf;
  qa_mapped_guest_memory *memory = options->memory;
  qa_mapped_memory_ext *ext = options->memory_ext;
  uint64_t load_bias = options->load_bias;

  bool any_unique = false;
  for (size_t i = 0; i < elf->symbol_count; i++)
    if (elf->symbols[i].binding == 10) any_unique = true;
  if (any_unique && options->unique_symbols == NULL)
    elf_error("GNU unique symbols require a process-wide symbol registry");
  if (elf->abi.pointer_bytes != memory->pointer_bytes)
    elf_error("image and guest memory have different pointer widths");
  if (!elf->is_shared && load_bias != 0) elf_error("invalid load bias for fixed ELF executable");
  if (load_bias % PAGE_BYTES != 0) elf_error("load bias is not page aligned");
  for (size_t i = 0; i < elf->needed_library_count; i++) {
    bool provided = false;
    for (size_t j = 0; j < options->dependency_count; j++)
      if (strcmp(options->dependencies[j], elf->needed_libraries[i]) == 0) provided = true;
    if (!provided)
      elf_error("dependency %s has no guest/runtime provider", elf->needed_libraries[i]);
  }
  validate_dynamic_policy(elf);
  static const uint32_t supported_segments[] = {0, 1, 2, 3, 4, 6, 7, 0x6474e550, 0x6474e551, 0x6474e552};
  for (size_t i = 0; i < elf->segment_count; i++) {
    const qa_elf_segment_info *segment = &elf->segments[i];
    bool ok = false;
    for (size_t j = 0; j < sizeof supported_segments / sizeof *supported_segments; j++)
      if (supported_segments[j] == segment->type) ok = true;
    if (!ok) elf_error("unsupported program segment 0x%x", segment->type);
    if ((segment->type == 1 || segment->type == 7) && segment->alignment > 1
        && load_bias % segment->alignment != 0)
      elf_error("load bias violates segment alignment");
    if (segment->type == 1 && (segment->address - segment->offset) % PAGE_BYTES != 0)
      elf_error("PT_LOAD is not page congruent");
    if (segment->type == 1 && (segment->flags & ~7u) != 0)
      elf_error("unsupported PT_LOAD permission flags");
  }

  /* Planned page mappings. */
  size_t load_count = 0;
  for (size_t i = 0; i < elf->segment_count; i++)
    if (elf->segments[i].type == 1 && elf->segments[i].memory_size > 0) load_count++;
  uint64_t *boundaries = xcalloc(load_count * 2, sizeof(uint64_t));
  size_t boundary_count = 0;
  for (size_t i = 0; i < elf->segment_count; i++) {
    const qa_elf_segment_info *segment = &elf->segments[i];
    if (segment->type != 1 || segment->memory_size == 0) continue;
    boundaries[boundary_count++] = page_down(segment->address);
    boundaries[boundary_count++] = page_up(segment->address + segment->memory_size);
  }
  qsort(boundaries, boundary_count, sizeof(uint64_t), cmp_u64);
  size_t unique_count = 0;
  for (size_t i = 0; i < boundary_count; i++)
    if (unique_count == 0 || boundaries[i] != boundaries[unique_count - 1])
      boundaries[unique_count++] = boundaries[i];
  boundary_count = unique_count;
  if (boundary_count == 0) elf_error("no image span");
  uint64_t first = boundaries[0], last = boundaries[boundary_count - 1];
  uint64_t span = last - first;

  qa_guest_mapping *planned = NULL; size_t planned_count = 0, planned_cap = 0;
  const char *artifact = options->module.artifact_path != NULL ? options->module.artifact_path : "";
  for (size_t i = 0; i + 1 < boundary_count; i++) {
    uint64_t start = boundaries[i], end = boundaries[i + 1];
    const qa_elf_segment_info *final_segment = NULL;
    for (size_t j = 0; j < elf->segment_count; j++) {
      const qa_elf_segment_info *segment = &elf->segments[j];
      if (segment->type != 1 || segment->memory_size == 0) continue;
      if (page_down(segment->address) <= start
          && page_up(segment->address + segment->memory_size) >= end)
        final_segment = segment;
    }
    if (final_segment == NULL) continue;
    if (planned_count == planned_cap) {
      planned_cap = planned_cap == 0 ? 8 : planned_cap * 2;
      planned = xrealloc(planned, planned_cap * sizeof *planned);
    }
    planned[planned_count].base = load_bias + start;
    planned[planned_count].byte_length = qa_elf_checked_number(end - start, "mapping length");
    planned[planned_count].permissions = seg_permissions(final_segment->flags);
    planned[planned_count].label = make_label(out, artifact);
    planned_count++;
  }
  free(boundaries);

  for (size_t i = 0; i < planned_count; i++)
    (void)qa_gmem_map(ext, planned[i].base, planned[i].byte_length,
                      QA_PERM_READ_WRITE_EXECUTE, planned[i].label, NULL, 0);
  for (size_t i = 0; i < elf->segment_count; i++) {
    const qa_elf_segment_info *segment = &elf->segments[i];
    if (segment->type != 1 || segment->memory_size == 0) continue;
    size_t prefix = qa_elf_checked_number(segment->address - page_down(segment->address),
                                          "segment page prefix");
    if (segment->offset < prefix) elf_error("segment page precedes the input file");
    size_t file_start = segment->offset - prefix;
    if (segment->file_size > 0)
      qa_memory_write(memory, qa_elf_address(memory, load_bias + page_down(segment->address)),
                      bytes + file_start, segment->file_size + prefix);
    if (segment->memory_size > segment->file_size) {
      size_t zero_len = segment->memory_size - segment->file_size;
      uint8_t *zero = xcalloc(zero_len, 1);
      qa_memory_write(memory, qa_elf_address(memory, load_bias + segment->address + segment->file_size),
                      zero, zero_len);
      free(zero);
    }
  }

  const qa_elf_segment_info *tls_segment = NULL;
  const qa_elf_segment_info *stack_segment = NULL;
  for (size_t i = 0; i < elf->segment_count; i++) {
    if (elf->segments[i].type == 7) tls_segment = &elf->segments[i];
    if (elf->segments[i].type == 0x6474e551) stack_segment = &elf->segments[i];
  }
  for (size_t i = 0; i < elf->symbol_count; i++) {
    const qa_elf_symbol *symbol = &elf->symbols[i];
    if (symbol->type == 6 && symbol->section != 0
        && (tls_segment == NULL || symbol->value + symbol->size > tls_segment->memory_size))
      elf_error("TLS symbol %s exceeds its template", symbol->name);
  }

  export_list exports; memset(&exports, 0, sizeof exports);
  qa_guest_image image; memset(&image, 0, sizeof image);
  image.module = options->module;
  image.abi = elf->abi;
  image.base = qa_elf_address(memory, load_bias + first);
  image.preferred_base = first;
  image.byte_length = span;
  image.has_entry_point = elf->entry_point != 0;
  if (image.has_entry_point) image.entry_point = qa_elf_address(memory, load_bias + elf->entry_point);
  image.mappings = planned; image.mapping_count = planned_count;
  if (tls_segment != NULL) {
    image.has_tls = true;
    image.tls.image = options->module;
    image.tls.initialized_len = tls_segment->file_size;
    image.tls.initialized = xmalloc(tls_segment->file_size);
    memcpy(image.tls.initialized, bytes + tls_segment->offset, tls_segment->file_size);
    image.tls.zero_fill_bytes = tls_segment->memory_size - tls_segment->file_size;
    image.tls.alignment = tls_segment->alignment > 0 ? tls_segment->alignment : 1;
  }
  /* tlsExports over exported TLS symbols. */
  size_t tls_export_count = 0;
  for (size_t i = 0; i < elf->symbol_count; i++) {
    const qa_elf_symbol *s = &elf->symbols[i];
    if (s->name[0] != '\0' && s->section != 0 && (s->binding == 1 || s->binding == 2 || s->binding == 10)
        && s->visibility != 1 && s->visibility != 2 && s->type == 6) tls_export_count++;
  }
  out->tls_exports = xcalloc(tls_export_count, sizeof(qa_elf_tls_export));
  size_t tls_at = 0;
  for (size_t i = 0; i < elf->symbol_count; i++) {
    const qa_elf_symbol *s = &elf->symbols[i];
    if (!(s->name[0] != '\0' && s->section != 0 && (s->binding == 1 || s->binding == 2 || s->binding == 10)
          && s->visibility != 1 && s->visibility != 2) || s->type != 6) continue;
    out->tls_exports[tls_at].symbol = export_symbol_name(s);
    out->tls_exports[tls_at].offset = s->value;
    out->tls_exports[tls_at].byte_length = s->size;
    tls_at++;
  }
  out->tls_export_count = tls_at;

  qa_elf_import_list imports; memset(&imports, 0, sizeof imports);
  image.imports = NULL; image.import_count = 0;

  for (size_t i = 0; i < elf->symbol_count; i++) {
    const qa_elf_symbol *symbol = &elf->symbols[i];
    if (!(symbol->name[0] != '\0' && symbol->section != 0
          && (symbol->binding == 1 || symbol->binding == 2 || symbol->binding == 10)
          && symbol->visibility != 1 && symbol->visibility != 2)) continue;
    if (symbol->type == 6 || symbol->type == 10 || symbol->binding == 10) continue;
    append_export(&exports, symbol, qa_elf_address(memory,
        qa_elf_defined_symbol_address(load_bias, memory, options->resolve_indirect,
                                       options->resolve_ctx, symbol)));
  }
  image.exports = exports.items; image.export_count = exports.count;

  qa_elf_relocation_context context; memset(&context, 0, sizeof context);
  context.elf = elf; context.memory = memory; context.image = &image;
  context.load_bias = load_bias; context.resolver = options->resolver;
  context.tls = options->tls;
  context.resolve_ctx = options->resolve_ctx;
  context.resolve_indirect = options->resolve_indirect;
  context.resolve_symbol_size = options->resolve_symbol_size;
  context.unique_symbols = options->unique_symbols;
  context.imports = &imports;

  for (size_t i = 0; i < elf->symbol_count; i++) {
    const qa_elf_symbol *symbol = &elf->symbols[i];
    if (!(symbol->name[0] != '\0' && symbol->section != 0
          && (symbol->binding == 1 || symbol->binding == 2 || symbol->binding == 10)
          && symbol->visibility != 1 && symbol->visibility != 2)) continue;
    if (symbol->binding != 10) continue;
    if (symbol->type != 0 && symbol->type != 1)
      elf_error("GNU unique binding is only supported for data symbols");
    append_export(&exports, symbol, qa_elf_address(memory,
        qa_elf_resolve_symbol(&context, symbol, image.base, false)));
    image.exports = exports.items; image.export_count = exports.count;
  }
  qa_relocate_elf(&context);
  for (size_t i = 0; i < elf->symbol_count; i++) {
    const qa_elf_symbol *symbol = &elf->symbols[i];
    if (!(symbol->name[0] != '\0' && symbol->section != 0
          && (symbol->binding == 1 || symbol->binding == 2 || symbol->binding == 10)
          && symbol->visibility != 1 && symbol->visibility != 2)) continue;
    if (symbol->type != 10) continue;
    append_export(&exports, symbol, qa_elf_address(memory,
        qa_elf_defined_symbol_address(load_bias, memory, options->resolve_indirect,
                                       options->resolve_ctx, symbol)));
    image.exports = exports.items; image.export_count = exports.count;
  }
  image.imports = imports.items; image.import_count = imports.count;

  qa_guest_core_address *preinitializers = NULL; size_t preinitializer_count = 0;
  pointer_array(elf, memory, load_bias, 32, 33, &preinitializers, &preinitializer_count);
  uint64_t flags1 = 0;
  if (!qa_elf_dynamic_value(elf, 0x6ffffffbULL, &flags1)) flags1 = 0;
  bool main_executable = !elf->is_shared || elf->interpreter != NULL || (flags1 & 0x08000000ULL) != 0;
  if (!main_executable && preinitializer_count != 0)
    elf_error("DT_PREINIT_ARRAY is only valid for the main executable");
  uint64_t init = 0, fini = 0;
  bool has_init = qa_elf_dynamic_value(elf, 12, &init);
  bool has_fini = qa_elf_dynamic_value(elf, 13, &fini);
  qa_guest_core_address *init_array = NULL; size_t init_array_count = 0;
  pointer_array(elf, memory, load_bias, 25, 27, &init_array, &init_array_count);
  size_t initializer_count = (has_init && init != 0 ? 1 : 0) + init_array_count;
  qa_guest_core_address *initializers = xcalloc(initializer_count, sizeof(qa_guest_core_address));
  size_t at = 0;
  if (has_init && init != 0) initializers[at++] = qa_elf_address(memory, load_bias + init);
  for (size_t i = 0; i < init_array_count; i++) initializers[at++] = init_array[i];
  free(init_array);
  qa_guest_core_address *fini_array = NULL; size_t fini_array_count = 0;
  pointer_array(elf, memory, load_bias, 26, 28, &fini_array, &fini_array_count);
  size_t finalizer_count = fini_array_count + (has_fini && fini != 0 ? 1 : 0);
  qa_guest_core_address *finalizers = xcalloc(finalizer_count, sizeof(qa_guest_core_address));
  at = 0;
  for (size_t i = fini_array_count; i-- > 0;) finalizers[at++] = fini_array[i];
  free(fini_array);
  if (has_fini && fini != 0) finalizers[at++] = qa_elf_address(memory, load_bias + fini);

  qa_elf_unwind_view view;
  qa_elf_unwind_view_init(&view, elf);
  qa_guest_unwind_region *unwind = NULL; size_t unwind_count = 0;
  if (!qa_read_elf_unwind(&view.inspection, bytes, options->bytes_len, memory, load_bias,
                          &unwind, &unwind_count))
    elf_error("out of memory");
  qa_elf_unwind_view_free(&view);

  if (image.has_tls && tls_segment != NULL && tls_segment->file_size > 0)
    qa_memory_copy(memory, qa_elf_address(memory, load_bias + tls_segment->address),
                   image.tls.initialized, tls_segment->file_size);
  for (size_t i = 0; i < planned_count; i++)
    qa_gmem_protect(ext, qa_elf_address(memory, planned[i].base),
                    planned[i].byte_length, planned[i].permissions);
  for (size_t i = 0; i < elf->segment_count; i++) {
    const qa_elf_segment_info *segment = &elf->segments[i];
    if (segment->type != 0x6474e552) continue;
    uint64_t start = load_bias + page_down(segment->address);
    uint64_t end = load_bias + page_down(segment->address + segment->memory_size);
    for (uint64_t address = start; address < end; address += PAGE_BYTES) {
      bool covered = false;
      for (size_t j = 0; j < planned_count; j++)
        if (address >= planned[j].base && address + PAGE_BYTES <= planned[j].base + planned[j].byte_length)
          covered = true;
      if (!covered) elf_error("RELRO exceeds this image's mapped pages");
    }
    if (end > start)
      qa_gmem_protect(ext, qa_elf_address(memory, start),
                      qa_elf_checked_number(end - start, "RELRO length"), QA_PERM_READ);
  }

  /* Execute checks over lifecycle addresses. */
  for (size_t i = 0; i < preinitializer_count; i++)
    qa_memory_check(memory, preinitializers[i], 1, QA_ACCESS_EXECUTE);
  for (size_t i = 0; i < initializer_count; i++)
    qa_memory_check(memory, initializers[i], 1, QA_ACCESS_EXECUTE);
  for (size_t i = 0; i < finalizer_count; i++)
    qa_memory_check(memory, finalizers[i], 1, QA_ACCESS_EXECUTE);
  if (image.has_entry_point) qa_memory_check(memory, image.entry_point, 1, QA_ACCESS_EXECUTE);

  /* Final mappings: live mappings inside the planned ranges. */
  size_t live_count = 0;
  const qa_guest_mapping *live = qa_gmem_mappings(ext, &live_count);
  qa_guest_mapping *final_mappings = NULL; size_t final_count = 0, final_cap = 0;
  for (size_t i = 0; live != NULL && i < live_count; i++) {
    bool inside = false;
    for (size_t j = 0; j < planned_count; j++)
      if (live[i].base >= planned[j].base
          && live[i].base + live[i].byte_length <= planned[j].base + planned[j].byte_length)
        inside = true;
    if (!inside) continue;
    if (final_count == final_cap) {
      final_cap = final_cap == 0 ? 8 : final_cap * 2;
      final_mappings = xrealloc(final_mappings, final_cap * sizeof *final_mappings);
    }
    final_mappings[final_count++] = live[i];
  }
  free(image.mappings);
  image.mappings = final_mappings; image.mapping_count = final_count;
  image.initializers = initializers; image.initializer_count = initializer_count;
  image.finalizers = finalizers; image.finalizer_count = finalizer_count;
  image.unwind = unwind; image.unwind_count = unwind_count;

  out->image = image;
  out->load_bias = load_bias;
  out->has_executable_stack = stack_segment != NULL;
  out->executable_stack = stack_segment != NULL && (stack_segment->flags & 1) != 0;
  out->preinitializers = preinitializers;
  out->preinitializer_count = preinitializer_count;
  return true;
}

void qa_elf_guest_image_free(qa_elf_guest_image *image) {
  if (image == NULL) return;
  qa_guest_image_free(&image->image);
  qa_elf_inspection_full_free(&image->elf);
  free(image->preinitializers);
  free(image->tls_exports);
  for (size_t i = 0; i < image->owned_string_count; i++) free(image->owned_strings[i]);
  free(image->owned_strings);
  free(image->bytes_copy);
  memset(image, 0, sizeof *image);
}

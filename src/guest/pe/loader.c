/* Port of src/guest/pe/loader.ts. GPL-2.0-or-later. */
#include "qa/guest/pe/loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/guest/pe/directories.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_pe_error(QA_PE_STAGE_MAPPING, "out of memory");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_pe_error(QA_PE_STAGE_MAPPING, "out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_pe_error(QA_PE_STAGE_MAPPING, "out of memory");
  return next;
}
static char *make_label(qa_pe_owned_strings *pool, const char *id, const char *suffix) {
  size_t len = strlen(id) + 1 + strlen(suffix);
  char *label = xmalloc(len + 1);
  (void)snprintf(label, len + 1, "%s:%s", id, suffix);
  return qa_pe_own_string(pool, label);
}
static uint64_t mask_width(uint64_t value, int bytes) {
  return bytes >= 8 ? value : value & ((1ULL << (bytes * 8)) - 1);
}
static int64_t as_int_n(uint64_t value, int bits) {
  if (bits >= 64) return (int64_t)value;
  uint64_t sign = 1ULL << (bits - 1);
  return (int64_t)(((value & ((sign << 1) - 1)) ^ sign) - sign);
}

typedef struct patch { uint32_t rva; int width; uint64_t value; } patch;
static int cmp_patch(const void *a, const void *b) {
  uint32_t x = ((const patch *)a)->rva, y = ((const patch *)b)->rva;
  return x < y ? -1 : x > y ? 1 : 0;
}

static void relocate(qa_pe_image_reader *image) {
  qa_pe_image_reader reader = qa_pe_image_reader_for_stage(image, QA_PE_STAGE_RELOCATION);
  qa_pe_directory_entry table = qa_pe_directory(reader.pe, 5);
  int64_t delta = (int64_t)(reader.base - reader.pe->preferred_base);
  if (delta != 0 && ((reader.pe->characteristics & 1) != 0 || table.rva == 0))
    qa_pe_error(QA_PE_STAGE_RELOCATION, "image cannot be relocated away from its preferred base");
  if (table.rva == 0) return;
  uint8_t *source_bytes = qa_pe_image_reader_copy(&reader, table.rva, table.byte_length);
  qa_pe_reader source = {source_bytes, table.byte_length, QA_PE_STAGE_RELOCATION};
  patch *patches = NULL; size_t patch_count = 0, cap = 0;
  for (size_t block = 0; block < table.byte_length;) {
    uint32_t page = qa_pe_reader_u32(&source, block);
    uint32_t size = qa_pe_reader_u32(&source, block + 4);
    if (page % 4096 != 0 || size < 8 || size % 2 != 0 || size > table.byte_length - block)
      qa_pe_error(QA_PE_STAGE_RELOCATION, "invalid relocation block size/page");
    for (size_t at = block + 8; at < block + size; at += 2) {
      uint16_t entry = qa_pe_reader_u16(&source, at);
      int type = entry >> 12;
      if (type == 0) continue;
      uint32_t rva = page + (entry & 0xfff);
      patch next; memset(&next, 0, sizeof next);
      next.rva = rva;
      if (type == 10 && reader.pe->abi.pointer_bytes == 8) {
        qa_pe_image_reader_range(&reader, rva, 8);
        next.width = 8;
        next.value = qa_pe_reader_u64(&reader.reader, rva) + (uint64_t)delta;
      } else if (reader.pe->abi.pointer_bytes == 4 && type == 3) {
        next.width = 4;
        next.value = qa_pe_image_reader_u32(&reader, rva) + (uint64_t)delta;
      } else if (reader.pe->abi.pointer_bytes == 4 && (type == 1 || type == 2)) {
        next.width = 2;
        next.value = qa_pe_image_reader_u16(&reader, rva)
            + (uint64_t)(type == 1 ? delta >> 16 : delta);
      } else if (reader.pe->abi.pointer_bytes == 4 && type == 4) {
        if (at + 2 >= block + size) qa_pe_error(QA_PE_STAGE_RELOCATION, "HIGHADJ lacks its signed low word");
        at += 2;
        int64_t low = as_int_n(qa_pe_reader_u16(&source, at), 16);
        next.width = 2;
        next.value = (uint64_t)(((int64_t)qa_pe_image_reader_u16(&reader, rva) << 16) + low + delta + 0x8000) >> 16;
      } else {
        qa_pe_error(QA_PE_STAGE_RELOCATION, "unsupported base relocation type %d for %s",
                    type, reader.pe->abi.image);
      }
      if (patch_count == cap) {
        cap = cap == 0 ? 16 : cap * 2;
        patches = xrealloc(patches, cap * sizeof *patches);
      }
      patches[patch_count++] = next;
    }
    block += size;
  }
  free(source_bytes);
  qsort(patches, patch_count, sizeof(patch), cmp_patch);
  int64_t end = -1;
  for (size_t i = 0; i < patch_count; i++) {
    if ((int64_t)patches[i].rva < end) qa_pe_error(QA_PE_STAGE_RELOCATION, "overlapping relocation targets");
    end = (int64_t)patches[i].rva + patches[i].width;
    uint64_t value = mask_width(patches[i].value, patches[i].width);
    switch (patches[i].width) {
      case 2: qa_pe_reader_set_u16(&reader.reader, patches[i].rva, (uint16_t)value); break;
      case 4: qa_pe_reader_set_u32(&reader.reader, patches[i].rva, (uint32_t)value); break;
      case 8: qa_pe_reader_set_u64(&reader.reader, patches[i].rva, value); break;
    }
  }
  free(patches);
}

void qa_map_pe_image(qa_pe_image *out, const qa_map_pe_image_options *options) {
  memset(out, 0, sizeof *out);
  qa_parse_pe(&out->pe, options->bytes, options->bytes_len);
  const qa_pe_file *pe = &out->pe;
  qa_mapped_guest_memory *memory = options->memory;
  qa_mapped_memory_ext *ext = options->memory_ext;
  qa_guest_module_identity module = options->has_module ? options->module : options->memory_module;
  uint64_t base = options->has_base ? options->base : pe->preferred_base;
  int64_t maximum = options->has_maximum_image_bytes ? options->maximum_image_bytes
                                                     : 256LL * 1024 * 1024;
  if (maximum <= 0 || (int64_t)pe->image_size > maximum)
    qa_pe_error(QA_PE_STAGE_MAPPING, "image exceeds the explicit allocation limit");
  bool base_exceeds = pe->abi.pointer_bytes == 8
      ? (base != 0 && pe->image_size > (uint64_t)(0ULL - base))
      : base + pe->image_size > 0x100000000ULL;
  if (memory->pointer_bytes != pe->abi.pointer_bytes || base == 0 || base % 65536 != 0 || base_exceeds)
    qa_pe_error(QA_PE_STAGE_MAPPING, "incompatible guest pointer width or image base");
  if (qa_pe_directory(pe, 14).rva != 0)
    qa_pe_error(QA_PE_STAGE_MAPPING, "managed CLR images require an unsupported runtime");
  uint8_t *bytes = xcalloc(pe->image_size, 1);
  out->mapped_bytes = bytes;
  memcpy(bytes, options->bytes, pe->header_size);
  for (size_t i = 0; i < pe->section_count; i++)
    memcpy(bytes + pe->sections[i].rva, options->bytes + pe->sections[i].raw_offset,
           pe->sections[i].raw_size);
  qa_pe_image_reader reader;
  qa_pe_image_reader_init(&reader, bytes, pe->image_size, pe, base, memory, module, QA_PE_STAGE_MAPPING);
  for (int index = 0; index < 16; index++) {
    qa_pe_directory_entry entry = qa_pe_directory(pe, index);
    if (index != 4 && entry.rva != 0) qa_pe_image_reader_range(&reader, entry.rva, entry.byte_length);
  }
  relocate(&reader);
  qa_guest_import *imports = NULL; size_t import_count = 0;
  qa_pe_read_imports(&reader, &out->strings, &imports, &import_count);
  qa_guest_export *exports = NULL; size_t export_count = 0;
  qa_pe_read_exports(&reader, &out->strings, &exports, &export_count);
  bool has_tls = false, has_tls_index = false;
  qa_guest_tls_template tls; qa_guest_core_address tls_index = {0};
  qa_pe_read_tls(&reader, &has_tls, &tls, &has_tls_index, &tls_index);
  qa_pe_unwind_record *unwind_records = NULL; size_t unwind_record_count = 0;
  qa_pe_read_unwind(&reader, &unwind_records, &unwind_record_count);
  qa_pe_load_configuration load_config;
  bool has_load_config = qa_pe_read_load_configuration(&reader, &load_config);
  bool has_entry = pe->entry_point_rva != 0;
  qa_guest_core_address entry_point = has_entry ? qa_pe_image_reader_executable(&reader, pe->entry_point_rva)
                                           : (qa_guest_core_address){0};
  qa_guest_core_address address = qa_pe_image_reader_address(&reader, 0, 1);
  const char *module_id = module.id != NULL ? module.id : "";
  uint32_t header_mapped = (uint32_t)(((uint64_t)pe->header_size + pe->section_alignment - 1)
                                      / pe->section_alignment * pe->section_alignment);
  size_t mapping_count = 1;
  for (size_t i = 0; i < pe->section_count; i++)
    if (pe->sections[i].mapped_size > 0) mapping_count++;
  qa_guest_mapping *image_mappings = xcalloc(mapping_count, sizeof(qa_guest_mapping));
  image_mappings[0] = (qa_guest_mapping){base, header_mapped, QA_PERM_READ,
                                         make_label(&out->strings, module_id, "PE headers")};
  size_t at = 1;
  for (size_t i = 0; i < pe->section_count; i++) {
    if (pe->sections[i].mapped_size == 0) continue;
    image_mappings[at] = (qa_guest_mapping){base + pe->sections[i].rva, pe->sections[i].mapped_size,
                                            pe->sections[i].permissions,
                                            make_label(&out->strings, module_id, pe->sections[i].name)};
    at++;
  }
  char *image_label = make_label(&out->strings, module_id, "PE image");
  (void)qa_gmem_map(ext, base, pe->image_size, QA_PERM_READ_WRITE, image_label, bytes, pe->image_size);
  qa_gmem_protect(ext, address, pe->image_size, QA_PERM_NONE);
  for (size_t i = 0; i < at; i++)
    qa_gmem_protect(ext, qa_memory_offset(memory, address, (int64_t)(image_mappings[i].base - base)),
                    image_mappings[i].byte_length, image_mappings[i].permissions);

  qa_guest_unwind_region *unwind = xcalloc(unwind_record_count, sizeof(qa_guest_unwind_region));
  for (size_t i = 0; i < unwind_record_count; i++) {
    unwind[i].start = qa_pe_image_reader_address(&reader, unwind_records[i].begin_rva, 1);
    unwind[i].end = qa_pe_image_reader_address(&reader, unwind_records[i].end_rva, 0);
    unwind[i].format = QA_UNWIND_PE_X64;
    unwind[i].metadata = xmalloc(unwind_records[i].metadata_len);
    memcpy(unwind[i].metadata, unwind_records[i].metadata, unwind_records[i].metadata_len);
    unwind[i].metadata_len = unwind_records[i].metadata_len;
  }
  size_t live_count = 0;
  const qa_guest_mapping *live = qa_gmem_mappings(ext, &live_count);
  qa_guest_mapping *mappings = NULL; size_t final_count = 0, final_cap = 0;
  for (size_t i = 0; live != NULL && i < live_count; i++) {
    if (!(live[i].base >= base && live[i].base < base + pe->image_size)) continue;
    if (final_count == final_cap) {
      final_cap = final_cap == 0 ? 8 : final_cap * 2;
      mappings = xrealloc(mappings, final_cap * sizeof *mappings);
    }
    mappings[final_count++] = live[i];
  }
  free(image_mappings);

  qa_guest_image *image = &out->image;
  image->module = module;
  image->abi = pe->abi;
  image->base = address;
  image->preferred_base = pe->preferred_base;
  image->byte_length = pe->image_size;
  image->has_entry_point = has_entry;
  image->entry_point = entry_point;
  image->mappings = mappings; image->mapping_count = final_count;
  image->imports = imports; image->import_count = import_count;
  image->exports = exports; image->export_count = export_count;
  image->has_tls = has_tls;
  if (has_tls) image->tls = tls;
  image->unwind = unwind; image->unwind_count = unwind_record_count;
  out->has_tls_index = has_tls_index;
  out->tls_index_address = tls_index;
  out->has_load_configuration = has_load_config;
  if (has_load_config) out->load_configuration = load_config;
  out->unwind_records = unwind_records;
  out->unwind_record_count = unwind_record_count;
}

void qa_bind_pe_imports(const qa_pe_image *image, qa_mapped_guest_memory *memory,
                        qa_mapped_memory_ext *memory_ext, qa_guest_import_resolver resolver,
                        qa_guest_import_resolution **out, size_t *out_count) {
  if (memory->pointer_bytes != image->image.abi.pointer_bytes)
    qa_pe_error(QA_PE_STAGE_IMPORTS, "image belongs to another guest address space");
  size_t count = image->image.import_count;
  qa_guest_import_resolution *resolutions = xcalloc(count, sizeof(qa_guest_import_resolution));
  typedef struct write_entry { qa_guest_core_address slot; uint8_t *bytes; } write_entry;
  write_entry *writes = xcalloc(count, sizeof(write_entry));
  size_t live_count = 0;
  const qa_guest_mapping *live = qa_gmem_mappings(memory_ext, &live_count);
  for (size_t i = 0; i < count; i++) {
    const qa_guest_import *import_ = &image->image.imports[i];
    qa_guest_import_resolution resolution = resolver.resolve(resolver.ctx, import_, &image->image);
    if (resolution.kind == QA_RESOLUTION_UNRESOLVED)
      qa_pe_error(QA_PE_STAGE_IMPORTS, "%s: %s", import_->library,
                  resolution.detail != NULL ? resolution.detail : "");
    qa_guest_core_address address = resolution.address;
    uint64_t limit = memory->pointer_bytes == 4 ? 0x100000000ULL : UINT64_MAX;
    bool mapped = false;
    for (size_t j = 0; live != NULL && j < live_count; j++)
      if (address.byte_offset >= live[j].base
          && address.byte_offset < live[j].base + live[j].byte_length
          && live[j].permissions != QA_PERM_NONE) mapped = true;
    bool in_range = memory->pointer_bytes == 8 ? address.byte_offset > 0
        : (address.byte_offset > 0 && address.byte_offset < limit);
    if (!in_range || !mapped)
      qa_pe_error(QA_PE_STAGE_IMPORTS, "resolved symbol is not a mapped address in this guest space");
    uint8_t *bytes = xcalloc((size_t)memory->pointer_bytes, 1);
    for (int b = 0; b < memory->pointer_bytes; b++) bytes[b] = (uint8_t)(address.byte_offset >> (b * 8));
    writes[i].slot = import_->slot;
    writes[i].bytes = bytes;
    (void)qa_memory_copy; /* TS snapshots previous bytes for rollback; abort makes it moot */
    resolutions[i] = resolution;
  }
  const qa_guest_mapping **regions = xcalloc(live_count, sizeof *regions);
  size_t region_count = 0;
  for (size_t i = 0; live != NULL && i < live_count; i++) {
    bool overlaps = false;
    for (size_t j = 0; j < count; j++)
      if (writes[j].slot.byte_offset < live[i].base + live[i].byte_length
          && writes[j].slot.byte_offset + (uint64_t)memory->pointer_bytes > live[i].base)
        overlaps = true;
    if (overlaps) regions[region_count++] = &live[i];
  }
  for (size_t i = 0; i < region_count; i++) {
    qa_guest_core_address address = {0};
    if (!qa_memory_pointer(memory, regions[i]->base, &address))
      qa_pe_error(QA_PE_STAGE_IMPORTS, "null IAT region");
    qa_gmem_protect(memory_ext, address, regions[i]->byte_length,
                    qa_guest_permissions_has_execute(regions[i]->permissions)
                        ? QA_PERM_READ_WRITE_EXECUTE : QA_PERM_READ_WRITE);
  }
  for (size_t i = 0; i < count; i++)
    qa_memory_write(memory, writes[i].slot, writes[i].bytes, (size_t)memory->pointer_bytes);
  for (size_t i = 0; i < region_count; i++) {
    qa_guest_core_address address = {0};
    if (!qa_memory_pointer(memory, regions[i]->base, &address))
      qa_pe_error(QA_PE_STAGE_IMPORTS, "null IAT protection address");
    qa_gmem_protect(memory_ext, address, regions[i]->byte_length, regions[i]->permissions);
  }
  for (size_t i = 0; i < count; i++) free(writes[i].bytes);
  free(writes);
  free(regions);
  *out = resolutions;
  *out_count = count;
}

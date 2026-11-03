/* Port of src/guest/elf/parse.ts. GPL-2.0-or-later. */
#include "qa/guest/elf/parse.h"

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
static char *xstrndup(const uint8_t *bytes, size_t len) {
  char *copy = (char *)xmalloc(len + 1);
  memcpy(copy, bytes, len);
  copy[len] = '\0';
  return copy;
}
static char *xstrdup(const char *text) { return xstrndup((const uint8_t *)text, strlen(text)); }

static void elf_error(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  qa_range_error("ELF: %s", buffer);
}

const char *qa_elf_relocation_table_name(qa_elf_relocation_table table) {
  switch (table) {
    case QA_ELF_RELOC_REL: return "rel";
    case QA_ELF_RELOC_RELA: return "rela";
    case QA_ELF_RELOC_PLT_REL: return "plt-rel";
    case QA_ELF_RELOC_PLT_RELA: return "plt-rela";
    case QA_ELF_RELOC_RELR: return "relr";
  }
  return "";
}

size_t qa_elf_checked_number(uint64_t value, const char *description) {
  if (value > (uint64_t)QA_MAX_SAFE_INTEGER)
    elf_error("%s exceeds checked host indexing", description);
  return (size_t)value;
}

typedef struct reader { const uint8_t *bytes; size_t len; } reader;
static void range(reader *r, size_t offset, size_t size) {
  if (offset > r->len || size > r->len - offset)
    elf_error("file range %zu+%zu exceeds %zu bytes", offset, size, r->len);
}
static uint8_t rd_u8(reader *r, size_t offset) { range(r, offset, 1); return r->bytes[offset]; }
static uint16_t rd_u16(reader *r, size_t offset) {
  range(r, offset, 2);
  return (uint16_t)(r->bytes[offset] | (r->bytes[offset + 1] << 8));
}
static uint32_t rd_u32(reader *r, size_t offset) {
  range(r, offset, 4);
  uint32_t value = 0;
  for (int i = 3; i >= 0; i--) value = (value << 8) | r->bytes[offset + (size_t)i];
  return value;
}
static uint64_t rd_word(reader *r, size_t offset, bool wide) {
  if (!wide) return rd_u32(r, offset);
  range(r, offset, 8);
  uint64_t value = 0;
  for (int i = 7; i >= 0; i--) value = (value << 8) | r->bytes[offset + (size_t)i];
  return value;
}
static int64_t rd_signed(reader *r, size_t offset, bool wide) {
  return wide ? (int64_t)rd_word(r, offset, true) : (int32_t)rd_u32(r, offset);
}
static char *rd_string(reader *r, size_t offset, size_t maximum) {
  range(r, offset, maximum);
  size_t end = offset;
  while (end < offset + maximum && rd_u8(r, end) != 0) end++;
  if (end == offset + maximum) elf_error("unterminated string at %zu", offset);
  return xstrndup(r->bytes + offset, end - offset);
}

size_t qa_elf_file_offset(const qa_elf_segment_info *segments, size_t segment_count,
                           uint64_t address, size_t size) {
  for (size_t i = 0; i < segment_count; i++) {
    const qa_elf_segment_info *segment = &segments[i];
    if (segment->type != 1 || address < segment->address) continue;
    uint64_t displacement = address - segment->address;
    if (displacement + size <= segment->file_size)
      return segment->offset + qa_elf_checked_number(displacement, "segment displacement");
  }
  elf_error("virtual file range 0x%llx+%zu has no PT_LOAD backing",
            (unsigned long long)address, size);
  return 0;
}

static const qa_elf_dynamic_entry *dynamic_entry(const qa_elf_inspection_full *elf, uint64_t tag) {
  for (size_t i = 0; i < elf->dynamic_count; i++)
    if (elf->dynamic[i].tag == tag) return &elf->dynamic[i];
  return NULL;
}
bool qa_elf_dynamic_has(const qa_elf_inspection_full *elf, uint64_t tag) {
  return dynamic_entry(elf, tag) != NULL;
}
bool qa_elf_dynamic_value(const qa_elf_inspection_full *elf, uint64_t tag, uint64_t *out) {
  const qa_elf_dynamic_entry *entry = dynamic_entry(elf, tag);
  if (entry == NULL || entry->value_count == 0) return false;
  *out = entry->values[0];
  return true;
}
static bool dyn_value(const qa_elf_dynamic_entry *entries, size_t count, uint64_t tag, uint64_t *out) {
  for (size_t i = 0; i < count; i++)
    if (entries[i].tag == tag && entries[i].value_count > 0) { *out = entries[i].values[0]; return true; }
  return false;
}

typedef struct version_entry { int index; char *name; char *library; bool weak; } version_entry;

typedef struct string_ctx {
  reader *r;
  const qa_elf_segment_info *segments;
  size_t segment_count;
  bool has_strtab;
  uint64_t strtab;
  size_t strsize;
} string_ctx;
static char *dynamic_string(string_ctx *ctx, uint64_t index) {
  size_t offset = qa_elf_checked_number(index, "string index");
  if (!ctx->has_strtab || offset >= ctx->strsize) elf_error("dynamic string exceeds DT_STRTAB/DT_STRSZ");
  size_t file = qa_elf_file_offset(ctx->segments, ctx->segment_count, ctx->strtab + index, ctx->strsize - offset);
  return rd_string(ctx->r, file, ctx->strsize - offset);
}

static void read_dynamic(reader *r, const qa_elf_segment_info *segments, size_t segment_count,
                         bool wide, qa_elf_dynamic_entry **out, size_t *out_count) {
  size_t tables = 0;
  const qa_elf_segment_info *table = NULL;
  for (size_t i = 0; i < segment_count; i++)
    if (segments[i].type == 2) { tables++; table = &segments[i]; }
  *out = NULL; *out_count = 0;
  if (tables > 1) elf_error("multiple dynamic segments");
  if (table == NULL) return;
  size_t stride = wide ? 16 : 8;
  bool terminated = false;
  size_t cap = 0;
  for (size_t offset = 0; offset + stride <= table->file_size; offset += stride) {
    size_t p = table->offset + offset;
    uint64_t tag = rd_word(r, p, wide);
    (void)qa_elf_checked_number(tag, "dynamic tag");
    if (tag == 0) { terminated = true; break; }
    qa_elf_dynamic_entry *entry = NULL;
    for (size_t i = 0; i < *out_count; i++)
      if ((*out)[i].tag == tag) entry = &(*out)[i];
    if (entry != NULL && entry->value_count != 0 && tag != 1)
      elf_error("duplicate dynamic tag 0x%llx", (unsigned long long)tag);
    if (entry == NULL) {
      if (*out_count == cap) { cap = cap == 0 ? 16 : cap * 2; *out = xrealloc(*out, cap * sizeof **out); }
      entry = &(*out)[(*out_count)++];
      entry->tag = tag; entry->values = NULL; entry->value_count = 0;
    }
    entry->values = xrealloc(entry->values, (entry->value_count + 1) * sizeof(uint64_t));
    entry->values[entry->value_count++] = rd_word(r, p + (wide ? 8 : 4), wide);
  }
  if (!terminated) elf_error("dynamic table lacks DT_NULL");
}

typedef struct reloc_list { qa_elf_relocation *items; size_t count, cap; } reloc_list;
static void reloc_push(reloc_list *list, qa_elf_relocation relocation) {
  if (list->count == list->cap) {
    list->cap = list->cap == 0 ? 16 : list->cap * 2;
    list->items = xrealloc(list->items, list->cap * sizeof *list->items);
  }
  list->items[list->count++] = relocation;
}
typedef struct seen_table { uint64_t address; size_t length; bool rela; } seen_table;

static void read_reloc_table(reader *r, const qa_elf_segment_info *segments, size_t segment_count,
                              bool wide, reloc_list *list, seen_table **seen, size_t *seen_count,
                              uint64_t address, uint64_t size, bool rela, qa_elf_relocation_table table) {
  size_t stride = (size_t)(wide ? 8 : 4) * (rela ? 3 : 2);
  size_t length = qa_elf_checked_number(size, "relocation table size");
  if (length % stride != 0) elf_error("partial relocation entry");
  for (size_t i = 0; i < *seen_count; i++)
    if ((*seen)[i].address == address && (*seen)[i].length == length && (*seen)[i].rela == rela) return;
  *seen = xrealloc(*seen, (*seen_count + 1) * sizeof **seen);
  (*seen)[*seen_count] = (seen_table){address, length, rela};
  (*seen_count)++;
  size_t start = qa_elf_file_offset(segments, segment_count, address, length);
  for (size_t offset = 0; offset < length; offset += stride) {
    size_t p = start + offset;
    uint64_t info = rd_word(r, p + (wide ? 8 : 4), wide);
    qa_elf_relocation relocation; memset(&relocation, 0, sizeof relocation);
    relocation.address = rd_word(r, p, wide);
    relocation.type = (uint32_t)(info & (wide ? 0xffffffffULL : 0xffULL));
    relocation.symbol_index = info >> (wide ? 32 : 8);
    relocation.has_addend = rela;
    relocation.addend = rela ? rd_signed(r, p + (wide ? 16 : 8), wide) : 0;
    relocation.table = table;
    reloc_push(list, relocation);
  }
}

static void read_relocations(reader *r, const qa_elf_segment_info *segments, size_t segment_count,
                             const qa_elf_dynamic_entry *dynamic, size_t dynamic_count, bool wide,
                             qa_elf_relocation **out, size_t *out_count) {
  reloc_list list; memset(&list, 0, sizeof list);
  seen_table *seen = NULL; size_t seen_count = 0;
  for (int pass = 0; pass < 2; pass++) {
    bool rela = pass == 1;
    uint64_t address = 0, size = 0, entry = 0;
    bool has_address = dyn_value(dynamic, dynamic_count, rela ? 7 : 17, &address);
    bool has_size = dyn_value(dynamic, dynamic_count, rela ? 8 : 18, &size);
    bool has_entry = dyn_value(dynamic, dynamic_count, rela ? 9 : 19, &entry);
    if (!has_address && !has_size) continue;
    if (!has_address || !has_size || !has_entry || entry != (uint64_t)((wide ? 8 : 4) * (rela ? 3 : 2)))
      elf_error("incomplete REL/RELA dynamic tags");
    read_reloc_table(r, segments, segment_count, wide, &list, &seen, &seen_count,
                     address, size, rela, rela ? QA_ELF_RELOC_RELA : QA_ELF_RELOC_REL);
  }
  uint64_t plt = 0;
  if (dyn_value(dynamic, dynamic_count, 23, &plt)) {
    uint64_t kind = 0, size = 0;
    bool has_size = dyn_value(dynamic, dynamic_count, 2, &size);
    bool has_kind = dyn_value(dynamic, dynamic_count, 20, &kind);
    if ((!has_kind || (kind != 7 && kind != 17)) || !has_size)
      elf_error("incomplete PLT relocation tags");
    read_reloc_table(r, segments, segment_count, wide, &list, &seen, &seen_count,
                     plt, size, kind == 7, kind == 7 ? QA_ELF_RELOC_PLT_RELA : QA_ELF_RELOC_PLT_REL);
  }
  uint64_t relr = 0;
  if (dyn_value(dynamic, dynamic_count, 36, &relr)) {
    int width = wide ? 8 : 4;
    uint64_t size = 0, entry_size = 0;
    bool has_size = dyn_value(dynamic, dynamic_count, 35, &size);
    bool has_entry = dyn_value(dynamic, dynamic_count, 37, &entry_size);
    if (!has_size || size % (uint64_t)width != 0 || !has_entry || entry_size != (uint64_t)width)
      elf_error("invalid RELR table");
    size_t length = qa_elf_checked_number(size, "RELR table size");
    size_t offset = qa_elf_file_offset(segments, segment_count, relr, length);
    bool has_cursor = false;
    uint64_t cursor = 0;
    for (size_t i = 0; i < length; i += (size_t)width) {
      uint64_t entry = rd_word(r, offset + i, wide);
      if ((entry & 1) == 0) {
        qa_elf_relocation relocation; memset(&relocation, 0, sizeof relocation);
        relocation.address = entry; relocation.type = 8; relocation.table = QA_ELF_RELOC_RELR;
        reloc_push(&list, relocation);
        cursor = entry + (uint64_t)width; has_cursor = true;
      } else {
        if (!has_cursor) elf_error("RELR bitmap precedes address");
        for (int bit = 1; bit < width * 8; bit++) {
          if ((entry & (1ULL << bit)) == 0 && bit >= 64) continue;
          if (bit < 64 && (entry & (1ULL << bit)) != 0) {
            qa_elf_relocation relocation; memset(&relocation, 0, sizeof relocation);
            relocation.address = cursor + (uint64_t)((bit - 1) * width);
            relocation.type = 8; relocation.table = QA_ELF_RELOC_RELR;
            reloc_push(&list, relocation);
          }
        }
        cursor += (uint64_t)((width * 8 - 1) * width);
      }
    }
  }
  free(seen);
  *out = list.items; *out_count = list.count;
}

static void read_versions(reader *r, const qa_elf_segment_info *segments, size_t segment_count,
                          const qa_elf_dynamic_entry *dynamic, size_t dynamic_count, string_ctx *ctx,
                          version_entry **out, size_t *out_count) {
  *out = NULL; *out_count = 0;
  size_t cap = 0;
  for (int pass = 0; pass < 2; pass++) {
    bool needed = pass == 0;
    uint64_t address = 0, count_raw = 0;
    bool has_address = dyn_value(dynamic, dynamic_count, needed ? 0x6ffffffeULL : 0x6ffffffcULL, &address);
    (void)dyn_value(dynamic, dynamic_count, needed ? 0x6fffffffULL : 0x6ffffffdULL, &count_raw);
    size_t count = qa_elf_checked_number(count_raw, "version count");
    if (!has_address) { if (count != 0) elf_error("missing version table"); continue; }
    if (count == 0) elf_error("version table has no count");
    uint64_t current = address;
    for (size_t i = 0; i < count; i++) {
      size_t p = qa_elf_file_offset(segments, segment_count, current, needed ? 16 : 20);
      if (rd_u16(r, p) != 1) elf_error("unsupported symbol version record");
      uint16_t aux_count = needed ? rd_u16(r, p + 2) : rd_u16(r, p + 6);
      uint64_t auxiliary = current + rd_u32(r, p + (needed ? 8 : 12));
      if (aux_count == 0) elf_error("version record has no name");
      for (uint16_t j = 0; j < aux_count; j++) {
        size_t a = qa_elf_file_offset(segments, segment_count, auxiliary, needed ? 16 : 8);
        if (needed || j == 0) {
          int index = (needed ? rd_u16(r, a + 6) : rd_u16(r, p + 4)) & 0x7fff;
          uint16_t flags = needed ? rd_u16(r, a + 4) : rd_u16(r, p + 2);
          for (size_t k = 0; k < *out_count; k++)
            if ((*out)[k].index == index) elf_error("duplicate version index %d", index);
          if (*out_count == cap) { cap = cap == 0 ? 8 : cap * 2; *out = xrealloc(*out, cap * sizeof **out); }
          version_entry *entry = &(*out)[(*out_count)++];
          entry->index = index;
          entry->name = dynamic_string(ctx, rd_u32(r, a + (needed ? 8 : 0)));
          entry->library = needed ? dynamic_string(ctx, rd_u32(r, p + 4)) : xstrdup("");
          entry->weak = (flags & 2) != 0;
        }
        uint32_t next = rd_u32(r, a + (needed ? 12 : 4));
        if (j + 1 < aux_count && next == 0) elf_error("truncated version auxiliary chain");
        auxiliary += next;
      }
      uint32_t next = rd_u32(r, p + (needed ? 12 : 16));
      if (i + 1 < count && next == 0) elf_error("truncated version chain");
      current += next;
    }
  }
}

static size_t gnu_symbol_count(reader *r, const qa_elf_segment_info *segments, size_t segment_count,
                               uint64_t address, bool wide) {
  size_t header = qa_elf_file_offset(segments, segment_count, address, 16);
  uint32_t buckets = rd_u32(r, header), first = rd_u32(r, header + 4), bloom = rd_u32(r, header + 8);
  if (buckets == 0 || bloom == 0) elf_error("invalid GNU hash table");
  uint64_t bucket_address = address + 16 + (uint64_t)bloom * (wide ? 8 : 4);
  size_t bucket_offset = qa_elf_file_offset(segments, segment_count, bucket_address, (size_t)buckets * 4);
  uint64_t chains = bucket_address + (uint64_t)buckets * 4;
  size_t count = first;
  for (uint32_t bucket = 0; bucket < buckets; bucket++) {
    uint32_t symbol = rd_u32(r, bucket_offset + (size_t)bucket * 4);
    if (symbol == 0) continue;
    if (symbol < first) elf_error("GNU hash bucket precedes symbol offset");
    /* Chains of a well-formed table are disjoint and ascending; a start inside the counted range only repeats a walk. */
    if (symbol < count) continue;
    for (;;) {
      uint32_t hash = rd_u32(r, qa_elf_file_offset(segments, segment_count,
                                                   chains + (uint64_t)(symbol - first) * 4, 4));
      if (symbol + 1 > count) count = symbol + 1;
      symbol++;
      if ((hash & 1) != 0) break;
    }
  }
  return count;
}

static void read_symbols(reader *r, const qa_elf_segment_info *segments, size_t segment_count,
                         const qa_elf_section_info *sections, size_t section_count,
                         const qa_elf_dynamic_entry *dynamic, size_t dynamic_count, bool wide,
                         const version_entry *versions, size_t version_count, string_ctx *ctx,
                         qa_elf_symbol **out, size_t *out_count) {
  *out = NULL; *out_count = 0;
  uint64_t symtab = 0;
  bool has_symtab = dyn_value(dynamic, dynamic_count, 6, &symtab);
  const qa_elf_section_info *symbol_section = NULL;
  for (size_t i = 0; i < section_count; i++) {
    if (!has_symtab && sections[i].type == 2) { symbol_section = &sections[i]; break; }
    if (has_symtab && sections[i].type == 11 && sections[i].address == symtab) { symbol_section = &sections[i]; break; }
  }
  size_t stride = wide ? 24 : 16;
  size_t count, offset;
  const qa_elf_section_info *static_strings = NULL;
  if (!has_symtab) {
    if (symbol_section == NULL) return;
    if (symbol_section->link >= section_count || sections[symbol_section->link].type != 3)
      elf_error("symbol table lacks string section");
    static_strings = &sections[symbol_section->link];
    count = symbol_section->size / stride;
    offset = symbol_section->offset;
  } else {
    uint64_t syment = 0;
    if (!dyn_value(dynamic, dynamic_count, 11, &syment) || syment != stride)
      elf_error("incorrect DT_SYMENT");
    uint64_t hash = 0, gnu_hash = 0;
    if (dyn_value(dynamic, dynamic_count, 4, &hash))
      count = rd_u32(r, qa_elf_file_offset(segments, segment_count, hash, 8) + 4);
    else if (dyn_value(dynamic, dynamic_count, 0x6ffffef5ULL, &gnu_hash))
      count = gnu_symbol_count(r, segments, segment_count, gnu_hash, wide);
    else if (symbol_section != NULL) count = symbol_section->size / stride;
    else { elf_error("cannot bound dynamic symbols without hash or section metadata"); count = 0; }
    offset = qa_elf_file_offset(segments, segment_count, symtab, count * stride);
  }
  if (symbol_section != NULL
      && (symbol_section->entry_size != stride || symbol_section->size / stride != count))
    elf_error("symbol count or entry size disagrees with section metadata");
  range(r, offset, count * stride);
  uint64_t versym = 0;
  bool has_versym = dyn_value(dynamic, dynamic_count, 0x6ffffff0ULL, &versym);
  size_t version_offset = has_versym ? qa_elf_file_offset(segments, segment_count, versym, count * 2) : 0;
  qa_elf_symbol *symbols = xcalloc(count, sizeof(qa_elf_symbol));
  for (size_t index = 0; index < count; index++) {
    size_t p = offset + index * stride;
    uint8_t info = rd_u8(r, p + (wide ? 4 : 12));
    uint16_t version_word = has_versym ? rd_u16(r, version_offset + index * 2) : 1;
    int version_index = version_word & 0x7fff;
    const version_entry *version = NULL;
    if (version_index > 1) {
      for (size_t k = 0; k < version_count; k++)
        if (versions[k].index == version_index) version = &versions[k];
      if (version == NULL) elf_error("undefined version index %d", version_index);
    }
    int section = rd_u16(r, p + (wide ? 6 : 14));
    if (section == 0xffff) elf_error("extended symbol section indices are unsupported");
    qa_elf_symbol *symbol = &symbols[index];
    symbol->index = index;
    uint32_t name_index = rd_u32(r, p);
    if (static_strings != NULL) {
      if (name_index >= static_strings->size) elf_error("symbol name exceeds string table");
      symbol->name = rd_string(r, static_strings->offset + name_index, static_strings->size - name_index);
    } else {
      symbol->name = dynamic_string(ctx, name_index);
    }
    symbol->value = rd_word(r, p + (wide ? 8 : 4), wide);
    symbol->size = qa_elf_checked_number(rd_word(r, p + (wide ? 16 : 8), wide), "symbol size");
    symbol->binding = (version_index == 0 && section != 0) ? 0 : info >> 4;
    symbol->type = info & 15;
    symbol->visibility = rd_u8(r, p + (wide ? 5 : 13)) & 3;
    symbol->section = section;
    if (version != NULL) {
      symbol->has_version = true;
      symbol->version.name = xstrdup(version->name);
      symbol->version.library = xstrdup(version->library);
      symbol->version.weak = version->weak;
      symbol->version.hidden = (version_word & 0x8000) != 0;
    }
  }
  *out = symbols; *out_count = count;
}

void qa_inspect_elf(qa_elf_inspection_full *out, const uint8_t *bytes, size_t len) {
  memset(out, 0, sizeof *out);
  reader r = {bytes, len};
  reader *rp = &r;
  if (rd_u32(rp, 0) != 0x464c457f) elf_error("invalid magic");
  uint8_t elf_class = rd_u8(rp, 4);
  if (elf_class != 1 && elf_class != 2) elf_error("unsupported class %u", elf_class);
  bool wide = elf_class == 2;
  if (rd_u8(rp, 5) != 1 || rd_u8(rp, 6) != 1 || rd_u32(rp, 20) != 1)
    elf_error("only little-endian ELF version 1 is supported");
  if ((rd_u8(rp, 7) != 0 && rd_u8(rp, 7) != 3) || rd_u8(rp, 8) != 0)
    elf_error("unsupported OS ABI or ABI version");
  uint16_t type = rd_u16(rp, 16);
  if (type != 2 && type != 3) elf_error("expected ET_EXEC or ET_DYN, found %u", type);
  uint16_t machine = rd_u16(rp, 18);
  if (machine != (wide ? 62 : 3))
    elf_error("unsupported class/machine combination %u/%u", elf_class, machine);
  if (rd_u32(rp, wide ? 48 : 36) != 0) elf_error("unsupported machine flags");
  if (rd_u16(rp, wide ? 52 : 40) != (wide ? 64 : 52)) elf_error("incorrect ELF header size");
  size_t phoff = qa_elf_checked_number(rd_word(rp, wide ? 32 : 28, wide), "program table offset");
  size_t shoff = qa_elf_checked_number(rd_word(rp, wide ? 40 : 32, wide), "section table offset");
  uint16_t phsize = rd_u16(rp, wide ? 54 : 42);
  uint16_t shsize = rd_u16(rp, wide ? 58 : 46);
  size_t phnum = rd_u16(rp, wide ? 56 : 44);
  size_t shnum = rd_u16(rp, wide ? 60 : 48);
  size_t names_index = rd_u16(rp, wide ? 62 : 50);
  if (shoff != 0) {
    if (shsize != (wide ? 64 : 40)) elf_error("incorrect section header size");
    range(rp, shoff, shsize);
    if (shnum == 0) shnum = qa_elf_checked_number(rd_word(rp, shoff + (wide ? 32 : 20), wide), "extended section count");
    if (phnum == 0xffff) phnum = rd_u32(rp, shoff + (wide ? 44 : 28));
    if (names_index == 0xffff) names_index = rd_u32(rp, shoff + (wide ? 40 : 24));
  } else if (shnum != 0 || names_index != 0 || phnum == 0xffff) {
    elf_error("missing extended section header");
  }
  if (phsize != (wide ? 56 : 32) || phnum == 0) elf_error("missing or invalid program table");
  range(rp, phoff, phnum * phsize);
  range(rp, shoff, shnum * shsize);
  qa_elf_segment_info *segments = xcalloc(phnum, sizeof(qa_elf_segment_info));
  for (size_t i = 0; i < phnum; i++) {
    size_t p = phoff + i * phsize;
    qa_elf_segment_info *segment = &segments[i];
    segment->type = rd_u32(rp, p);
    segment->flags = rd_u32(rp, p + (wide ? 4 : 24));
    segment->offset = qa_elf_checked_number(rd_word(rp, p + (wide ? 8 : 4), wide), "segment offset");
    segment->address = rd_word(rp, p + (wide ? 16 : 8), wide);
    segment->file_size = qa_elf_checked_number(rd_word(rp, p + (wide ? 32 : 16), wide), "segment file size");
    segment->memory_size = qa_elf_checked_number(rd_word(rp, p + (wide ? 40 : 20), wide), "segment memory size");
    segment->alignment = rd_word(rp, p + (wide ? 48 : 28), wide);
    if (segment->type != 0) range(rp, segment->offset, segment->file_size);
    if (segment->type == 1 || segment->type == 7) {
      if (segment->file_size > segment->memory_size) elf_error("segment file size exceeds memory size");
      uint64_t alignment = segment->alignment;
      if (alignment > 1 && ((alignment & (alignment - 1)) != 0
                            || (segment->address - segment->offset) % alignment != 0))
        elf_error("invalid segment alignment or file/address congruence");
      if ((!wide && segment->address + segment->memory_size > 0x100000000ULL)
          || (wide && segment->address != 0
              && segment->memory_size > (uint64_t)(0ULL - segment->address)))
        elf_error("segment exceeds ELF address width");
    }
  }
  bool loadable = false;
  for (size_t i = 0; i < phnum; i++)
    if (segments[i].type == 1 && segments[i].memory_size != 0) loadable = true;
  if (!loadable) elf_error("no loadable image");

  typedef struct raw_section { qa_elf_section_info section; uint32_t name_offset; } raw_section;
  raw_section *raw = xcalloc(shnum, sizeof(raw_section));
  for (size_t i = 0; i < shnum; i++) {
    size_t p = shoff + i * shsize;
    raw[i].name_offset = rd_u32(rp, p);
    qa_elf_section_info *section = &raw[i].section;
    section->name = xstrdup("");
    section->type = rd_u32(rp, p + 4);
    section->flags = rd_word(rp, p + 8, wide);
    section->address = rd_word(rp, p + (wide ? 16 : 12), wide);
    section->offset = qa_elf_checked_number(rd_word(rp, p + (wide ? 24 : 16), wide), "section offset");
    section->size = qa_elf_checked_number(rd_word(rp, p + (wide ? 32 : 20), wide), "section size");
    section->link = rd_u32(rp, p + (wide ? 40 : 24));
    section->info = rd_u32(rp, p + (wide ? 44 : 28));
    section->entry_size = qa_elf_checked_number(rd_word(rp, p + (wide ? 56 : 36), wide), "section entry size");
    if (section->type != 8 && section->type != 0) range(rp, section->offset, section->size);
  }
  const raw_section *names = NULL;
  if (names_index != 0) {
    if (names_index >= shnum || raw[names_index].section.type != 3)
      elf_error("invalid section name table");
    names = &raw[names_index];
  }
  qa_elf_section_info *sections = xcalloc(shnum, sizeof(qa_elf_section_info));
  for (size_t i = 0; i < shnum; i++) {
    sections[i] = raw[i].section;
    if (names != NULL) {
      if (raw[i].name_offset >= names->section.size) elf_error("section name exceeds string table");
      free(sections[i].name);
      sections[i].name = rd_string(rp, names->section.offset + raw[i].name_offset,
                                    names->section.size - raw[i].name_offset);
    }
  }
  free(raw);

  qa_elf_dynamic_entry *dynamic = NULL; size_t dynamic_count = 0;
  read_dynamic(rp, segments, phnum, wide, &dynamic, &dynamic_count);
  string_ctx ctx; memset(&ctx, 0, sizeof ctx);
  ctx.r = rp; ctx.segments = segments; ctx.segment_count = phnum;
  ctx.has_strtab = dyn_value(dynamic, dynamic_count, 5, &ctx.strtab);
  uint64_t strsize_raw = 0;
  (void)dyn_value(dynamic, dynamic_count, 10, &strsize_raw);
  ctx.strsize = qa_elf_checked_number(strsize_raw, "dynamic string size");

  qa_elf_relocation *relocations = NULL; size_t relocation_count = 0;
  read_relocations(rp, segments, phnum, dynamic, dynamic_count, wide, &relocations, &relocation_count);
  version_entry *versions = NULL; size_t version_count = 0;
  read_versions(rp, segments, phnum, dynamic, dynamic_count, &ctx, &versions, &version_count);
  qa_elf_symbol *symbols = NULL; size_t symbol_count = 0;
  read_symbols(rp, segments, phnum, sections, shnum, dynamic, dynamic_count, wide,
               versions, version_count, &ctx, &symbols, &symbol_count);
  for (size_t k = 0; k < version_count; k++) { free(versions[k].name); free(versions[k].library); }
  free(versions);
  for (size_t i = 0; i < relocation_count; i++)
    if (relocations[i].symbol_index >= symbol_count && relocations[i].symbol_index != 0)
      elf_error("relocation symbol index exceeds symbol table");
  size_t interpreter_count = 0, tls_count = 0;
  const qa_elf_segment_info *interpreter = NULL;
  for (size_t i = 0; i < phnum; i++) {
    if (segments[i].type == 3) { interpreter_count++; interpreter = &segments[i]; }
    if (segments[i].type == 7) tls_count++;
  }
  if (interpreter_count > 1 || tls_count > 1) elf_error("multiple interpreter or TLS segments");

  out->abi = wide ? (qa_guest_native_abi){"linux-x86-64", "elf64", 8, "system-v-x86-64"}
                  : (qa_guest_native_abi){"linux-i386", "elf32", 4, "system-v-i386"};
  out->is_shared = type == 3;
  out->entry_point = rd_word(rp, 24, wide);
  out->segments = segments; out->segment_count = phnum;
  out->sections = sections; out->section_count = shnum;
  out->dynamic = dynamic; out->dynamic_count = dynamic_count;
  const qa_elf_dynamic_entry *needed = NULL;
  for (size_t i = 0; i < dynamic_count; i++)
    if (dynamic[i].tag == 1) needed = &dynamic[i];
  if (needed != NULL) {
    out->needed_library_count = needed->value_count;
    out->needed_libraries = xcalloc(needed->value_count, sizeof(char *));
    for (size_t i = 0; i < needed->value_count; i++)
      out->needed_libraries[i] = dynamic_string(&ctx, needed->values[i]);
  }
  uint64_t optional = 0;
  out->soname = dyn_value(dynamic, dynamic_count, 14, &optional) ? dynamic_string(&ctx, optional) : NULL;
  out->runpath = dyn_value(dynamic, dynamic_count, 29, &optional) ? dynamic_string(&ctx, optional) : NULL;
  out->rpath = dyn_value(dynamic, dynamic_count, 15, &optional) ? dynamic_string(&ctx, optional) : NULL;
  out->interpreter = interpreter == NULL ? NULL : rd_string(rp, interpreter->offset, interpreter->file_size);
  out->symbols = symbols; out->symbol_count = symbol_count;
  out->relocations = relocations; out->relocation_count = relocation_count;
}

void qa_elf_inspection_full_free(qa_elf_inspection_full *elf) {
  if (elf == NULL) return;
  free(elf->segments);
  for (size_t i = 0; i < elf->section_count; i++) free(elf->sections[i].name);
  free(elf->sections);
  for (size_t i = 0; i < elf->dynamic_count; i++) free(elf->dynamic[i].values);
  free(elf->dynamic);
  for (size_t i = 0; i < elf->needed_library_count; i++) free(elf->needed_libraries[i]);
  free(elf->needed_libraries);
  free(elf->soname); free(elf->interpreter); free(elf->runpath); free(elf->rpath);
  for (size_t i = 0; i < elf->symbol_count; i++) {
    free(elf->symbols[i].name);
    free(elf->symbols[i].version.name);
    free(elf->symbols[i].version.library);
  }
  free(elf->symbols);
  free(elf->relocations);
  memset(elf, 0, sizeof *elf);
}

void qa_elf_unwind_view_init(qa_elf_unwind_view *view, const qa_elf_inspection_full *elf) {
  memset(view, 0, sizeof *view);
  view->segments = xcalloc(elf->segment_count, sizeof(qa_elf_segment));
  for (size_t i = 0; i < elf->segment_count; i++) {
    view->segments[i].type = elf->segments[i].type;
    view->segments[i].address = elf->segments[i].address;
    view->segments[i].offset = elf->segments[i].offset;
    view->segments[i].file_size = elf->segments[i].file_size;
  }
  view->sections = xcalloc(elf->section_count, sizeof(qa_elf_section));
  for (size_t i = 0; i < elf->section_count; i++) {
    view->sections[i].name = elf->sections[i].name;
    view->sections[i].flags = elf->sections[i].flags;
    view->sections[i].address = elf->sections[i].address;
    view->sections[i].offset = elf->sections[i].offset;
    view->sections[i].size = elf->sections[i].size;
  }
  view->inspection.pointer_bytes = elf->abi.pointer_bytes;
  view->inspection.segments = view->segments;
  view->inspection.segment_count = elf->segment_count;
  view->inspection.sections = view->sections;
  view->inspection.section_count = elf->section_count;
}
void qa_elf_unwind_view_free(qa_elf_unwind_view *view) {
  if (view == NULL) return;
  free(view->segments);
  free(view->sections);
  memset(view, 0, sizeof *view);
}

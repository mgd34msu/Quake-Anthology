#include "internal.h"
#include "elf.h"

struct guest_elf {
    guest_elf_view view;
    uint8_t *artifact, *bytes;
    guest_elf_segment *segments;
    guest_elf_section *sections;
    guest_elf_dynamic *dynamic;
    const char **needed;
    guest_elf_version *versions;
    guest_elf_symbol *symbols;
    guest_elf_relocation *relocations;
    size_t dynamic_capacity, needed_capacity, version_capacity, relocation_capacity;
};

static bool span(uint64_t offset, uint64_t size, uint64_t limit)
{ return offset <= limit && size <= limit - offset; }

static unsigned word_bytes(const guest_elf *elf)
{ return elf->view.image.target.pointer_bytes; }

static uint64_t word(const guest_elf *elf, const uint8_t *data)
{ return word_bytes(elf) == 4 ? qa_load_u32le(data) : qa_load_u64le(data); }

static const uint8_t *file(const guest_elf *elf, uint64_t offset, uint64_t bytes, qa_error *error)
{
    if (!span(offset, bytes, elf->view.artifact.size)) {
        guest_fail(error, QA_ERROR_FORMAT, offset, "ELF file range exceeds its owned artifact"); return NULL;
    }
    return elf->artifact + (size_t)offset;
}

bool guest_elf_file_range(const guest_elf *elf, uint64_t address, size_t bytes,
    qa_bytes *out, qa_error *error)
{
    if (!elf || !out) return guest_fail(error, QA_ERROR_ARGUMENT, address, "ELF file range owner and output are required");
    for (size_t i = 0; i < elf->view.segment_count; ++i) {
        const guest_elf_segment *segment = &elf->segments[i];
        if (segment->type == 1 && address >= segment->address &&
            span(address - segment->address, bytes, segment->file_bytes)) {
            const uint8_t *data = file(elf, segment->offset + address - segment->address, bytes, error);
            if (!data) return false;
            *out = (qa_bytes){data, bytes}; return true;
        }
    }
    return guest_fail(error, QA_ERROR_FORMAT, address, "ELF dynamic range has no actual PT_LOAD file backing");
}

static const uint8_t *virtual_file(const guest_elf *elf, uint64_t address, size_t bytes, qa_error *error)
{
    qa_bytes range;
    return guest_elf_file_range(elf, address, bytes, &range, error) ? range.data : NULL;
}

bool guest_elf_range(const guest_elf *elf, uint64_t address, size_t bytes,
    qa_bytes *out, qa_error *error)
{
    if (!elf || !out || address < elf->view.first || !span(address - elf->view.first, bytes, elf->view.bytes.size))
        return guest_fail(error, QA_ERROR_ARGUMENT, address, "ELF memory range exceeds its owned virtual image");
    size_t offset = 0;
    while (offset < bytes) {
        uint64_t available = 0;
        for (size_t i = 0; i < elf->view.segment_count; ++i) {
            const guest_elf_segment *segment = &elf->segments[i];
            if (segment->type != 1 || (!segment->memory_bytes &&
                (elf->view.role != GUEST_ELF_LIBRARY || !(segment->address % QA_NATIVE_GUEST_PAGE)))) continue;
            uint64_t begin = segment->address & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
            uint64_t end = (segment->address + segment->memory_bytes + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
            if (address + offset >= begin && address + offset < end) {
                uint64_t next = end - (address + offset);
                if (available < next) available = next;
            }
        }
        if (!available) return guest_fail(error, QA_ERROR_FORMAT, address + offset, "ELF memory range crosses an actual PT_LOAD gap");
        offset += available > bytes - offset ? bytes - offset : (size_t)available;
    }
    *out = (qa_bytes){elf->bytes + (size_t)(address - elf->view.first), bytes}; return true;
}

bool guest_elf_dynamic_value(const guest_elf *elf, uint64_t tag, uint64_t *out)
{
    if (!elf || !out) return false;
    for (size_t i = 0; i < elf->view.dynamic_count; ++i)
        if (elf->dynamic[i].tag == tag) { *out = elf->dynamic[i].value; return true; }
    return false;
}

static const char *file_text(const guest_elf *elf, uint64_t offset, uint64_t bytes, qa_error *error)
{
    const uint8_t *data = file(elf, offset, bytes, error);
    if (!data) return NULL;
    if (!memchr(data, 0, (size_t)bytes)) {
        guest_fail(error, QA_ERROR_FORMAT, offset, "ELF string has no terminator in its actual table"); return NULL;
    }
    return (const char *)data;
}

static const char *dynamic_text(const guest_elf *elf, uint64_t index, qa_error *error)
{
    uint64_t address = 0, bytes = 0;
    if (!guest_elf_dynamic_value(elf, 5, &address) || !guest_elf_dynamic_value(elf, 10, &bytes) ||
        index >= bytes || index > UINT64_MAX - address || bytes - index > SIZE_MAX) {
        guest_fail(error, QA_ERROR_FORMAT, index, "ELF string index exceeds DT_STRTAB/DT_STRSZ"); return NULL;
    }
    const uint8_t *data = virtual_file(elf, address + index, (size_t)(bytes - index), error);
    if (!data) return NULL;
    if (!memchr(data, 0, (size_t)(bytes - index))) {
        guest_fail(error, QA_ERROR_FORMAT, address + index, "ELF dynamic string is unterminated"); return NULL;
    }
    return (const char *)data;
}

static bool headers(guest_elf *elf, size_t maximum, qa_error *error)
{
    bool wide = word_bytes(elf) == 8;
    const uint8_t *h = elf->artifact;
    if (h[6] != 1 || (h[7] != 0 && h[7] != 3) || h[8] || qa_load_u32le(h + 20) != 1 ||
        qa_load_u32le(h + (wide ? 48 : 36)) || qa_load_u16le(h + (wide ? 52 : 40)) != (wide ? 64 : 52))
        return guest_fail(error, QA_ERROR_FORMAT, 6, "ELF identification/header differs from its actual ABI");
    uint64_t phoff = word(elf, h + (wide ? 32 : 28)), shoff = word(elf, h + (wide ? 40 : 32));
    uint16_t phstride = qa_load_u16le(h + (wide ? 54 : 42)), shstride = qa_load_u16le(h + (wide ? 58 : 46));
    uint64_t phcount = qa_load_u16le(h + (wide ? 56 : 44)), shcount = qa_load_u16le(h + (wide ? 60 : 48));
    uint64_t names = qa_load_u16le(h + (wide ? 62 : 50));
    if (shoff) {
        const uint8_t *zero = file(elf, shoff, wide ? 64 : 40, error);
        if (!zero || shstride != (wide ? 64 : 40)) return guest_fail(error, QA_ERROR_FORMAT, shoff, "ELF section header stride differs from its class");
        if (!shcount) shcount = word(elf, zero + (wide ? 32 : 20));
        if (phcount == 0xffff) phcount = qa_load_u32le(zero + (wide ? 44 : 28));
        if (names == 0xffff) names = qa_load_u32le(zero + (wide ? 40 : 24));
    } else if (shcount || names || phcount == 0xffff)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "ELF extended counts lack their actual section zero");
    if (!phcount || phstride != (wide ? 56 : 32) ||
        phcount > SIZE_MAX / sizeof(*elf->segments) || shcount > SIZE_MAX / sizeof(*elf->sections) ||
        phcount > SIZE_MAX / phstride || (shcount && shcount > SIZE_MAX / shstride) ||
        !file(elf, phoff, phcount * phstride, error) || !file(elf, shoff, shcount * shstride, error))
        return guest_fail(error, QA_ERROR_FORMAT, phoff, "ELF program or section table exceeds its artifact");
    elf->segments = calloc((size_t)phcount, sizeof(*elf->segments));
    elf->sections = shcount ? calloc((size_t)shcount, sizeof(*elf->sections)) : NULL;
    if (!elf->segments || (shcount && !elf->sections)) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF header tables");
    elf->view.segments = elf->segments; elf->view.segment_count = (size_t)phcount;
    elf->view.sections = elf->sections; elf->view.section_count = (size_t)shcount;
    uint64_t first = UINT64_MAX, end = 0; unsigned dynamic_count = 0, interpreter_count = 0;
    for (size_t i = 0; i < phcount; ++i) {
        const uint8_t *p = elf->artifact + (size_t)phoff + i * phstride;
        guest_elf_segment *s = &elf->segments[i];
        *s = (guest_elf_segment){qa_load_u32le(p), qa_load_u32le(p + (wide ? 4 : 24)),
            word(elf, p + (wide ? 8 : 4)), word(elf, p + (wide ? 16 : 8)),
            word(elf, p + (wide ? 32 : 16)), word(elf, p + (wide ? 40 : 20)), word(elf, p + (wide ? 48 : 28))};
        if (s->type && !file(elf, s->offset, s->file_bytes, error)) return false;
        if (s->type == 1 || s->type == 7) {
            if (s->file_bytes > s->memory_bytes || s->memory_bytes > UINT64_MAX - s->address ||
                (!wide && s->address + s->memory_bytes > UINT64_C(0x100000000)) ||
                (s->alignment > 1 && ((s->alignment & (s->alignment - 1)) ||
                s->address % s->alignment != s->offset % s->alignment || elf->view.bias % s->alignment)))
                return guest_fail(error, QA_ERROR_FORMAT, s->address, "ELF segment extent or alignment exceeds its actual address domain");
        }
        if (s->type == 1 && (s->memory_bytes ||
            (elf->view.role == GUEST_ELF_LIBRARY && s->address % QA_NATIVE_GUEST_PAGE))) {
            if (s->address % QA_NATIVE_GUEST_PAGE != s->offset % QA_NATIVE_GUEST_PAGE ||
                s->address + s->memory_bytes > UINT64_MAX - (QA_NATIVE_GUEST_PAGE - 1))
                return guest_fail(error, QA_ERROR_FORMAT, s->address, "ELF load segment is not page congruent or cannot round its extent");
            uint64_t begin = s->address & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
            uint64_t limit = (s->address + s->memory_bytes + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
            if (first > begin) first = begin;
            if (end < limit) end = limit;
        } else if (s->type == 2 && ++dynamic_count > 1)
            return guest_fail(error, QA_ERROR_FORMAT, s->address, "ELF has multiple dynamic segments");
        else if (s->type == 3) {
            if (++interpreter_count > 1) return guest_fail(error, QA_ERROR_FORMAT, s->address, "ELF has multiple interpreter identities");
            if (!(elf->view.interpreter = file_text(elf, s->offset, s->file_bytes, error))) return false;
        } else if (s->type == 7) {
            if (elf->view.tls) return guest_fail(error, QA_ERROR_FORMAT, s->address, "ELF has multiple TLS templates");
            elf->view.tls = s;
        }
    }
    if (first == UINT64_MAX || end <= first || end - first > maximum || end - first > SIZE_MAX ||
        end > UINT64_MAX - elf->view.bias || (!wide && end + elf->view.bias > UINT64_C(0x100000000)))
        return guest_fail(error, QA_ERROR_MEMORY, first, "ELF image span exceeds its owned memory budget/address domain");
    elf->view.first = first; elf->view.end = end;
    elf->bytes = calloc(1, (size_t)(end - first));
    if (!elf->bytes) return guest_fail(error, QA_ERROR_MEMORY, first, "owning ELF PT_LOAD virtual bytes");
    elf->view.bytes = (qa_bytes){elf->bytes, (size_t)(end - first)};
    for (size_t i = 0; i < phcount; ++i) {
        const guest_elf_segment *s = &elf->segments[i];
        if (s->type != 1 || (!s->memory_bytes &&
            (elf->view.role != GUEST_ELF_LIBRARY || !(s->address % QA_NATIVE_GUEST_PAGE)))) continue;
        uint64_t prefix = s->address % QA_NATIVE_GUEST_PAGE;
        if (s->offset < prefix) return guest_fail(error, QA_ERROR_FORMAT, s->offset, "ELF load file page precedes its actual artifact");
        uint64_t begin = s->address - prefix, data_end = s->address + s->file_bytes;
        uint64_t allocated_end = s->address + s->memory_bytes;
        uint64_t file_end = (data_end + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
        uint64_t page_end = (allocated_end + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
        bool map_file = elf->view.role == GUEST_ELF_LIBRARY ? file_end > begin : s->file_bytes != 0;
        if (map_file) {
            uint64_t file_start = s->offset - prefix, bytes = file_end - begin;
            memset(elf->bytes + (size_t)(begin - first), 0, (size_t)bytes);
            if (bytes > elf->view.artifact.size - file_start) bytes = elf->view.artifact.size - file_start;
            memcpy(elf->bytes + (size_t)(begin - first), elf->artifact + (size_t)file_start, (size_t)bytes);
        }
        if (allocated_end > data_end) {
            if (elf->view.role == GUEST_ELF_PROGRAM) {
                /* Kernel file-zero segments are anonymous from the page base.
                 * padzero on a read-only file page fails and is ignored. */
                uint64_t zero = s->file_bytes ? ((s->flags & 2) ? data_end : file_end) : begin;
                if (zero < page_end) memset(elf->bytes + (size_t)(zero - first), 0, (size_t)(page_end - zero));
            } else {
                /* glibc temporarily makes the last file page writable. It
                 * zeroes only the requested BSS there, then maps whole anonymous
                 * pages if BSS extends beyond that file page. */
                uint64_t zero_end = allocated_end > file_end ? page_end : allocated_end;
                memset(elf->bytes + (size_t)(data_end - first), 0, (size_t)(zero_end - data_end));
            }
        }
    }
    for (size_t i = 0; i < shcount; ++i) {
        const uint8_t *p = elf->artifact + (size_t)shoff + i * shstride;
        guest_elf_section *s = &elf->sections[i];
        *s = (guest_elf_section){NULL, qa_load_u32le(p + 4), qa_load_u32le(p + (wide ? 40 : 24)),
            qa_load_u32le(p + (wide ? 44 : 28)), word(elf, p + 8), word(elf, p + (wide ? 16 : 12)),
            word(elf, p + (wide ? 24 : 16)), word(elf, p + (wide ? 32 : 20)),
            word(elf, p + (wide ? 48 : 32)), word(elf, p + (wide ? 56 : 36))};
        if (s->type != 0 && s->type != 8 && !file(elf, s->offset, s->bytes, error)) return false;
    }
    if (names && (names >= shcount || elf->sections[names].type != 3))
        return guest_fail(error, QA_ERROR_FORMAT, names, "ELF section names lack their actual string table");
    for (size_t i = 0; i < shcount; ++i) {
        if (!names) { elf->sections[i].name = ""; continue; }
        uint32_t index = qa_load_u32le(elf->artifact + (size_t)shoff + i * shstride);
        const guest_elf_section *table = &elf->sections[names];
        if (index >= table->bytes) return guest_fail(error, QA_ERROR_FORMAT, index, "ELF section name exceeds its actual table");
        if (!(elf->sections[i].name = file_text(elf, table->offset + index, table->bytes - index, error))) return false;
    }
    return true;
}

static bool singleton_tag(uint64_t tag)
{
    if ((tag >= 2 && tag <= 30) || (tag >= 32 && tag <= 37) || tag == 39) return true;
    return tag == 0x6ffffef5 || tag == 0x6ffffff0 || tag == 0x6ffffffc ||
        tag == 0x6ffffffd || tag == 0x6ffffffe || tag == 0x6fffffff;
}

static bool read_dynamic(guest_elf *elf, qa_error *error)
{
    for (size_t i = 0; i < elf->view.segment_count; ++i) {
        const guest_elf_segment *s = &elf->segments[i];
        if (s->type != 2) continue;
        unsigned stride = word_bytes(elf) * 2; bool terminated = false;
        for (uint64_t offset = 0; span(offset, stride, s->file_bytes); offset += stride) {
            const uint8_t *p = elf->artifact + (size_t)(s->offset + offset);
            uint64_t tag = word(elf, p), value = word(elf, p + word_bytes(elf)), prior;
            if (!tag) { terminated = true; break; }
            if (singleton_tag(tag) && guest_elf_dynamic_value(elf, tag, &prior))
                return guest_fail(error, QA_ERROR_FORMAT, s->offset + offset, "ELF has duplicate singleton dynamic metadata");
            if (!guest_grow((void **)&elf->dynamic, &elf->dynamic_capacity, elf->view.dynamic_count + 1, sizeof(*elf->dynamic), error)) return false;
            elf->dynamic[elf->view.dynamic_count++] = (guest_elf_dynamic){tag, value};
        }
        if (!terminated) return guest_fail(error, QA_ERROR_FORMAT, s->offset, "ELF dynamic table lacks its actual DT_NULL");
    }
    elf->view.dynamic = elf->dynamic;
    for (size_t i = 0; i < elf->view.dynamic_count; ++i) {
        guest_elf_dynamic d = elf->dynamic[i];
        if (d.tag != 1 && d.tag != 14 && d.tag != 15 && d.tag != 29) continue;
        const char *name = dynamic_text(elf, d.value, error);
        if (!name) return false;
        if (d.tag == 1) {
            if (!guest_grow((void **)&elf->needed, &elf->needed_capacity, elf->view.needed_count + 1, sizeof(*elf->needed), error)) return false;
            elf->needed[elf->view.needed_count++] = name;
        } else if (d.tag == 14) elf->view.soname = name;
        else if (d.tag == 15) elf->view.rpath = name;
        else elf->view.runpath = name;
    }
    elf->view.needed = elf->needed;
    return true;
}

static bool advance(uint64_t *address, uint32_t next, bool required, qa_error *error)
{
    if ((required && !next) || next > UINT64_MAX - *address)
        return guest_fail(error, QA_ERROR_FORMAT, *address, "ELF metadata chain is truncated or overflows its address domain");
    *address += next; return true;
}

static bool read_versions(guest_elf *elf, qa_error *error)
{
    for (unsigned needed = 0; needed < 2; ++needed) {
        uint64_t address = 0, count = 0;
        bool present = guest_elf_dynamic_value(elf, needed ? 0x6ffffffe : 0x6ffffffc, &address);
        guest_elf_dynamic_value(elf, needed ? 0x6fffffff : 0x6ffffffd, &count);
        if (!present && !count) continue;
        if (!present || !count || count > elf->view.artifact.size / (needed ? 16 : 20))
            return guest_fail(error, QA_ERROR_FORMAT, address, "ELF version chain count lacks its actual records");
        for (uint64_t i = 0; i < count; ++i) {
            const uint8_t *p = virtual_file(elf, address, needed ? 16 : 20, error);
            if (!p || qa_load_u16le(p) != 1) return guest_fail(error, QA_ERROR_FORMAT, address, "ELF version record has an invalid format revision");
            uint16_t auxiliary_count = qa_load_u16le(p + (needed ? 2 : 6));
            uint64_t auxiliary = address;
            if (!auxiliary_count) return guest_fail(error, QA_ERROR_FORMAT, address, "ELF version record lacks its actual name chain");
            if (!advance(&auxiliary, qa_load_u32le(p + (needed ? 8 : 12)), true, error)) return false;
            const char *library = needed ? dynamic_text(elf, qa_load_u32le(p + 4), error) : "";
            if (!library) return false;
            for (unsigned j = 0; j < auxiliary_count; ++j) {
                const uint8_t *a = virtual_file(elf, auxiliary, needed ? 16 : 8, error);
                if (!a) return false;
                if (needed || !j) {
                    uint16_t index = qa_load_u16le(needed ? a + 6 : p + 4) & 0x7fff;
                    const char *name = dynamic_text(elf, qa_load_u32le(a + (needed ? 8 : 0)), error);
                    if (!name) return false;
                    for (size_t n = 0; n < elf->view.version_count; ++n)
                        if (elf->versions[n].index == index) return guest_fail(error, QA_ERROR_FORMAT, index, "ELF has duplicate version identities");
                    if (!guest_grow((void **)&elf->versions, &elf->version_capacity, elf->view.version_count + 1, sizeof(*elf->versions), error)) return false;
                    elf->versions[elf->view.version_count++] = (guest_elf_version){name, library, index,
                        (qa_load_u16le(needed ? a + 4 : p + 2) & 2) != 0};
                }
                if (!advance(&auxiliary, qa_load_u32le(a + (needed ? 12 : 4)), j + 1 < auxiliary_count, error)) return false;
            }
            if (!advance(&address, qa_load_u32le(p + (needed ? 12 : 16)), i + 1 < count, error)) return false;
        }
    }
    elf->view.versions = elf->versions; return true;
}

static bool gnu_symbol_count(const guest_elf *elf, uint64_t address, size_t *out, qa_error *error)
{
    const uint8_t *header = virtual_file(elf, address, 16, error);
    if (!header) return false;
    uint32_t buckets = qa_load_u32le(header), first = qa_load_u32le(header + 4), bloom = qa_load_u32le(header + 8);
    uint64_t skip = 16 + (uint64_t)bloom * word_bytes(elf);
    if (!buckets || !bloom || skip > UINT64_MAX - address || SIZE_MAX / buckets < 4)
        return guest_fail(error, QA_ERROR_FORMAT, address, "ELF GNU hash header exceeds its actual table");
    address += skip;
    const uint8_t *table = virtual_file(elf, address, (size_t)buckets * 4, error);
    if (!table) return false;
    if ((uint64_t)buckets * 4 > UINT64_MAX - address)
        return guest_fail(error, QA_ERROR_FORMAT, address, "ELF GNU hash buckets exceed the address domain");
    uint64_t chains = address + (uint64_t)buckets * 4, count = first;
    for (uint32_t i = 0; i < buckets; ++i) {
        uint64_t symbol = qa_load_u32le(table + (size_t)i * 4);
        if (!symbol) continue;
        if (symbol < first) return guest_fail(error, QA_ERROR_FORMAT, symbol, "ELF GNU hash bucket precedes its symbol base");
        for (;;) {
            if (symbol > UINT32_MAX || (symbol - first) * 4 > UINT64_MAX - chains)
                return guest_fail(error, QA_ERROR_FORMAT, symbol, "ELF GNU hash chain exceeds its symbol/address width");
            const uint8_t *entry = virtual_file(elf, chains + (symbol - first) * 4, 4, error);
            if (!entry) return false;
            if (count < symbol + 1) count = symbol + 1;
            ++symbol;
            if (qa_load_u32le(entry) & 1) break;
        }
    }
    if (count > SIZE_MAX) return guest_fail(error, QA_ERROR_MEMORY, count, "ELF symbol count exceeds host indexing");
    *out = (size_t)count; return true;
}

static bool read_symbols(guest_elf *elf, qa_error *error)
{
    unsigned width = word_bytes(elf), stride = width == 8 ? 24 : 16;
    uint64_t address = 0, hash = 0, entry_bytes = 0;
    bool dynamic = guest_elf_dynamic_value(elf, 6, &address);
    const guest_elf_section *section = NULL, *strings = NULL, *extended = NULL;
    size_t section_index = SIZE_MAX;
    for (size_t i = 0; i < elf->view.section_count; ++i)
        if ((!dynamic && elf->sections[i].type == 2) ||
            (dynamic && elf->sections[i].type == 11 && elf->sections[i].address == address)) {
            if (section) return guest_fail(error, QA_ERROR_FORMAT, address, "ELF symbol table has ambiguous section identity");
            section = &elf->sections[i]; section_index = i;
        }
    size_t count = 0; const uint8_t *table = NULL;
    if (!dynamic) {
        if (!section) return true;
        if (section->link >= elf->view.section_count || elf->sections[section->link].type != 3 ||
            section->bytes % stride || section->bytes / stride > SIZE_MAX)
            return guest_fail(error, QA_ERROR_FORMAT, section->offset, "ELF static symbols lack their actual string table/entry extent");
        strings = &elf->sections[section->link]; count = (size_t)(section->bytes / stride);
        table = file(elf, section->offset, section->bytes, error);
    } else {
        if (!guest_elf_dynamic_value(elf, 11, &entry_bytes) || entry_bytes != stride)
            return guest_fail(error, QA_ERROR_FORMAT, address, "ELF DT_SYMENT differs from its actual class");
        uint64_t symbol_bytes = 0;
        if (guest_elf_dynamic_value(elf, 39, &symbol_bytes)) {
            if (symbol_bytes % stride || symbol_bytes / stride > SIZE_MAX)
                return guest_fail(error, QA_ERROR_FORMAT, address, "ELF DT_SYMTABSZ differs from its actual symbol width");
            count = (size_t)(symbol_bytes / stride);
        } else if (guest_elf_dynamic_value(elf, 4, &hash)) {
            const uint8_t *data = virtual_file(elf, hash, 8, error);
            if (!data) return false;
            count = qa_load_u32le(data + 4);
        } else if (guest_elf_dynamic_value(elf, 0x6ffffef5, &hash)) {
            if (!gnu_symbol_count(elf, hash, &count, error)) return false;
        } else if (section && !(section->bytes % stride) && section->bytes / stride <= SIZE_MAX)
            count = (size_t)(section->bytes / stride);
        else return guest_fail(error, QA_ERROR_FORMAT, address, "ELF dynamic symbols lack actual hash/section count bounds");
        if (count > SIZE_MAX / stride) return guest_fail(error, QA_ERROR_MEMORY, count, "ELF symbol bytes overflow host indexing");
        table = virtual_file(elf, address, count * stride, error);
    }
    if (!table || (section && (section->entry_bytes != stride || section->bytes / stride != count || section->bytes % stride)))
        return guest_fail(error, QA_ERROR_FORMAT, address, "ELF symbol count differs from its actual section metadata");
    uint64_t versions = 0, extended_address = 0; const uint8_t *version_table = NULL, *extended_table = NULL;
    if (guest_elf_dynamic_value(elf, 0x6ffffff0, &versions)) {
        if (count > SIZE_MAX / 2) return guest_fail(error, QA_ERROR_MEMORY, count, "ELF symbol version extent overflows host indexing");
        if (!(version_table = virtual_file(elf, versions, count * 2, error))) return false;
    }
    if (dynamic && guest_elf_dynamic_value(elf, 34, &extended_address)) {
        if (count > SIZE_MAX / 4) return guest_fail(error, QA_ERROR_MEMORY, count, "ELF extended symbol index extent overflows host indexing");
        if (!(extended_table = virtual_file(elf, extended_address, count * 4, error))) return false;
    } else if (section) {
        for (size_t i = 0; i < elf->view.section_count; ++i)
            if (elf->sections[i].type == 18 && elf->sections[i].link == section_index) {
                if (extended) return guest_fail(error, QA_ERROR_FORMAT, i, "ELF symbol extended-index table is ambiguous");
                extended = &elf->sections[i];
            }
        if (extended) {
            if (count > SIZE_MAX / 4 || extended->bytes != count * 4 || extended->entry_bytes != 4 ||
                !(extended_table = file(elf, extended->offset, extended->bytes, error)))
                return guest_fail(error, QA_ERROR_FORMAT, extended->offset, "ELF symbol extended-index table has an invalid extent");
        }
    }
    if (count > SIZE_MAX / sizeof(*elf->symbols)) return guest_fail(error, QA_ERROR_MEMORY, count, "ELF symbol owner extent overflows");
    elf->symbols = count ? calloc(count, sizeof(*elf->symbols)) : NULL;
    if (count && !elf->symbols) return guest_fail(error, QA_ERROR_MEMORY, count, "owning ELF symbols");
    elf->view.symbols = elf->symbols; elf->view.symbol_count = count;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *p = table + i * stride;
        uint32_t name_index = qa_load_u32le(p);
        const char *name = dynamic ? dynamic_text(elf, name_index, error) :
            name_index < strings->bytes ? file_text(elf, strings->offset + name_index, strings->bytes - name_index, error) : NULL;
        if (!name) return guest_fail(error, QA_ERROR_FORMAT, name_index, "ELF symbol name exceeds its actual string table");
        uint8_t info = p[width == 8 ? 4 : 12];
        uint16_t version_word = version_table ? qa_load_u16le(version_table + i * 2) : 1;
        uint16_t version_index = version_word & 0x7fff;
        uint32_t section_id = qa_load_u16le(p + (width == 8 ? 6 : 14));
        if (section_id == 0xffff) {
            if (!extended_table) return guest_fail(error, QA_ERROR_FORMAT, i, "ELF symbol escape index lacks its actual larger section table");
            section_id = qa_load_u32le(extended_table + i * 4);
        }
        const guest_elf_version *version = NULL;
        if (version_index > 1) for (size_t n = 0; n < elf->view.version_count; ++n)
            if (elf->versions[n].index == version_index) { version = &elf->versions[n]; break; }
        if (version_index > 1 && !version) return guest_fail(error, QA_ERROR_FORMAT, version_index, "ELF symbol references an absent actual version identity");
        elf->symbols[i] = (guest_elf_symbol){name, word(elf, p + (width == 8 ? 8 : 4)),
            word(elf, p + (width == 8 ? 16 : 8)), section_id,
            !version_index && section_id ? 0 : (uint8_t)(info >> 4), (uint8_t)(info & 15),
            (uint8_t)(p[width == 8 ? 5 : 13] & 3), version, (version_word & 0x8000) != 0};
    }
    return true;
}

static bool append_relocation(guest_elf *elf, guest_elf_relocation entry, qa_error *error)
{
    if (!guest_grow((void **)&elf->relocations, &elf->relocation_capacity,
        elf->view.relocation_count + 1, sizeof(*elf->relocations), error)) return false;
    elf->relocations[elf->view.relocation_count++] = entry; return true;
}

static bool read_relocations(guest_elf *elf, qa_error *error)
{
    typedef struct table_record { uint64_t address, bytes; bool rela; } table_record;
    table_record tables[3]; unsigned table_count = 0, width = word_bytes(elf);
    for (unsigned pass = 0; pass < 3; ++pass) {
        uint64_t address = 0, bytes = 0, entry = 0, plt_kind = 0;
        bool present = guest_elf_dynamic_value(elf, pass == 0 ? 17 : pass == 1 ? 7 : 23, &address);
        bool size_present = guest_elf_dynamic_value(elf, pass == 0 ? 18 : pass == 1 ? 8 : 2, &bytes);
        bool rela = pass == 1;
        if (pass == 2) {
            if (!present) continue;
            if (!guest_elf_dynamic_value(elf, 20, &plt_kind) || (plt_kind != 7 && plt_kind != 17))
                return guest_fail(error, QA_ERROR_FORMAT, address, "ELF PLT relocations lack their actual REL/RELA kind");
            rela = plt_kind == 7;
        } else {
            if (!present && !size_present) continue;
            if (!guest_elf_dynamic_value(elf, rela ? 9 : 19, &entry) || entry != width * (rela ? 3 : 2))
                return guest_fail(error, QA_ERROR_FORMAT, address, "ELF relocation entry width differs from its actual class");
        }
        unsigned stride = width * (rela ? 3 : 2);
        if (!present || !size_present || bytes % stride || bytes > SIZE_MAX)
            return guest_fail(error, QA_ERROR_FORMAT, address, "ELF relocation dynamic tags have an incomplete table extent");
        bool seen = false;
        for (unsigned i = 0; i < table_count; ++i)
            if (tables[i].address == address && tables[i].bytes == bytes && tables[i].rela == rela) seen = true;
        if (seen) continue;
        tables[table_count++] = (table_record){address, bytes, rela};
        if (!bytes) continue;
        const uint8_t *table = virtual_file(elf, address, (size_t)bytes, error);
        if (!table) return false;
        for (size_t offset = 0; offset < (size_t)bytes; offset += stride) {
            const uint8_t *p = table + offset; uint64_t info = word(elf, p + width);
            guest_elf_relocation relocation = {word(elf, p), rela ? word(elf, p + width * 2) : 0,
                width == 8 ? (uint32_t)info : (uint32_t)(info & 255),
                (uint32_t)(info >> (width == 8 ? 32 : 8)),
                pass == 2 ? (rela ? GUEST_ELF_PLT_RELA : GUEST_ELF_PLT_REL) : (rela ? GUEST_ELF_RELA : GUEST_ELF_REL), rela, address + offset};
            if (!append_relocation(elf, relocation, error)) return false;
        }
    }
    uint64_t address = 0, bytes = 0, stride = 0;
    bool present = guest_elf_dynamic_value(elf, 36, &address);
    bool size_present = guest_elf_dynamic_value(elf, 35, &bytes);
    bool stride_present = guest_elf_dynamic_value(elf, 37, &stride);
    if (present || size_present || stride_present) {
        if (!present || !size_present || !stride_present || stride != width || bytes % width || bytes > SIZE_MAX)
            return guest_fail(error, QA_ERROR_FORMAT, address, "ELF RELR metadata has an invalid actual extent/entry width");
        const uint8_t *table = bytes ? virtual_file(elf, address, (size_t)bytes, error) : elf->artifact;
        if (!table) return false;
        uint64_t cursor = 0; bool have_cursor = false;
        uint32_t relative = elf->view.image.target.arch == QA_NATIVE_ARCH_AARCH64 ? 1027 : 8;
        for (size_t offset = 0; offset < (size_t)bytes; offset += width) {
            uint64_t value = word(elf, table + offset);
            if (!(value & 1)) {
                if (value > UINT64_MAX - width)
                    return guest_fail(error, QA_ERROR_FORMAT, value, "ELF RELR address cannot advance within its actual domain");
                if (!append_relocation(elf, (guest_elf_relocation){value, 0, relative, 0, GUEST_ELF_RELR, false, address + offset}, error)) return false;
                cursor = value + width; have_cursor = true;
            } else {
                uint64_t covered = (width * 8 - 1) * width;
                if (!have_cursor || cursor > UINT64_MAX - covered ||
                    (width == 4 && cursor + covered > UINT64_C(0x100000000)))
                    return guest_fail(error, QA_ERROR_FORMAT, cursor, "ELF RELR bitmap has no valid actual base/extent");
                for (unsigned bit = 1; bit < width * 8; ++bit)
                    if (value & (UINT64_C(1) << bit))
                        if (!append_relocation(elf, (guest_elf_relocation){cursor + (bit - 1) * width,
                            0, relative, 0, GUEST_ELF_RELR, false, address + offset}, error)) return false;
                cursor += covered;
            }
        }
    }
    for (size_t i = 0; i < elf->view.relocation_count; ++i)
        if (elf->relocations[i].symbol && elf->relocations[i].symbol >= elf->view.symbol_count)
            return guest_fail(error, QA_ERROR_FORMAT, elf->relocations[i].symbol, "ELF relocation symbol exceeds its actual symbol table");
    elf->view.relocations = elf->relocations; return true;
}

void guest_elf_close(guest_elf **owner)
{
    if (!owner || !*owner) return;
    guest_elf *elf = *owner;
    free(elf->relocations); free(elf->symbols); free(elf->versions); free(elf->needed);
    free(elf->dynamic); free(elf->sections); free(elf->segments); free(elf->bytes); free(elf->artifact);
    free(elf); *owner = NULL;
}

const guest_elf_view *guest_elf_describe(const guest_elf *elf)
{ return elf ? &elf->view : NULL; }

bool guest_elf_open(qa_bytes source, const qa_native_image_info *expected, guest_elf_role role, uint64_t bias,
    size_t maximum, guest_elf **out, qa_error *error)
{
    if (!source.data || !expected || !out || *out || !maximum || bias % QA_NATIVE_GUEST_PAGE ||
        role < GUEST_ELF_LIBRARY || role > GUEST_ELF_PROGRAM)
        return guest_fail(error, QA_ERROR_ARGUMENT, bias, "ELF inert owner needs its actual artifact witness, aligned bias and memory budget");
    qa_native_image_info image;
    if (!(role == GUEST_ELF_PROGRAM ? qa_native_inspect_program(source, &image, error) : qa_native_inspect(source, &image, error))) return false;
    if ((image.format != QA_NATIVE_IMAGE_ELF32 && image.format != QA_NATIVE_IMAGE_ELF64) ||
        image.format != expected->format || image.target.os != expected->target.os ||
        image.target.arch != expected->target.arch || image.target.abi != expected->target.abi ||
        image.target.pointer_bytes != expected->target.pointer_bytes || image.preferred_base != expected->preferred_base ||
        image.image_bytes != expected->image_bytes || memcmp(image.digest.bytes, expected->digest.bytes, sizeof(image.digest.bytes)))
        return guest_fail(error, QA_ERROR_FORMAT, 0, "ELF artifact differs from its actual caller-qualified image witness");
    guest_elf *elf = calloc(1, sizeof(*elf));
    if (!elf) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating inert ELF owner");
    elf->artifact = malloc(source.size);
    if (!elf->artifact) { guest_elf_close(&elf); return guest_fail(error, QA_ERROR_MEMORY, 0, "owning original ELF artifact"); }
    memcpy(elf->artifact, source.data, source.size);
    elf->view.image = image; elf->view.bias = bias; elf->view.role = role;
    elf->view.artifact = (qa_bytes){elf->artifact, source.size};
    elf->view.executable = qa_load_u16le(elf->artifact + 16) == 2;
    elf->view.entry = word(elf, elf->artifact + 24);
    if (elf->view.executable && bias) {
        guest_elf_close(&elf); return guest_fail(error, QA_ERROR_ARGUMENT, bias, "fixed ELF executable requires its actual zero load bias");
    }
    bool okay = headers(elf, maximum, error) &&
        read_dynamic(elf, error) && read_versions(elf, error) && read_symbols(elf, error) && read_relocations(elf, error);
    if (!okay) { guest_elf_close(&elf); return false; }
    *out = elf; return true;
}

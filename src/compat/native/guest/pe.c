#include "internal.h"
#include "pe.h"

struct guest_pe {
    guest_pe_view view;
    uint8_t *artifact, *bytes;
    guest_pe_section *sections;
    guest_pe_import *imports;
    guest_pe_export *exports;
    guest_pe_unwind *unwind;
    size_t *functions;
    uint64_t *tls_callbacks;
    size_t import_capacity, export_capacity, unwind_capacity, tls_capacity;
};

static bool span(uint64_t offset, uint64_t length, uint64_t limit)
{ return offset <= limit && length <= limit - offset; }

static bool power(uint32_t value)
{ return value && !(value & (value - 1)); }

static bool rounded(uint32_t value, uint32_t alignment, uint32_t *out)
{
    uint64_t result = ((uint64_t)value + alignment - 1) & ~(uint64_t)(alignment - 1);
    if (result > UINT32_MAX) return false;
    *out = (uint32_t)result;
    return true;
}

bool guest_pe_range(const guest_pe *pe, uint32_t rva, size_t bytes,
    qa_bytes *out, qa_error *error)
{
    if (!pe || !out || !span(rva, bytes, pe->view.image.image_bytes))
        return guest_fail(error, QA_ERROR_FORMAT, rva, "PE image range exceeds owned bytes");
    uint64_t at = rva, end = at + bytes;
    if (at < pe->view.header_bytes) at = end < pe->view.header_bytes ? end : pe->view.header_bytes;
    for (size_t i = 0; at < end && i < pe->view.section_count; ++i) {
        const guest_pe_section *section = &pe->sections[i];
        if (at >= section->rva && at - section->rva < section->bytes) {
            uint64_t section_end = (uint64_t)section->rva + section->bytes;
            at = end < section_end ? end : section_end;
        }
    }
    if (at != end) return guest_fail(error, QA_ERROR_FORMAT, rva, "PE range crosses an unmapped image gap");
    *out = (qa_bytes){pe->bytes + rva, bytes};
    return true;
}

static const uint8_t *range(const guest_pe *pe, uint32_t rva, size_t bytes, qa_error *error)
{
    qa_bytes result;
    return guest_pe_range(pe, rva, bytes, &result, error) ? result.data : NULL;
}

static bool executable(const guest_pe *pe, uint32_t rva, qa_error *error)
{
    for (size_t i = 0; i < pe->view.section_count; ++i)
        if (rva >= pe->sections[i].rva && rva - pe->sections[i].rva < pe->sections[i].bytes &&
            (pe->sections[i].permissions & QA_NATIVE_GUEST_EXECUTE)) return true;
    return guest_fail(error, QA_ERROR_FORMAT, rva, "PE entry is outside executable source storage");
}

static const char *text(const guest_pe *pe, uint32_t rva, uint64_t limit, qa_error *error)
{
    uint64_t end = rva < pe->view.header_bytes ? pe->view.header_bytes : rva;
    for (size_t i = 0; i < pe->view.section_count; ++i)
        if (rva >= pe->sections[i].rva && rva - pe->sections[i].rva < pe->sections[i].bytes)
            end = (uint64_t)pe->sections[i].rva + pe->sections[i].bytes;
    if (end > limit) end = limit;
    if (end <= rva) { guest_fail(error, QA_ERROR_FORMAT, rva, "PE string has no source storage"); return NULL; }
    for (uint64_t at = rva; at < end; ++at) {
        if (!pe->bytes[at]) return (const char *)pe->bytes + rva;
        if (pe->bytes[at] > 127) break;
    }
    guest_fail(error, QA_ERROR_FORMAT, rva, "PE string is not terminated ASCII");
    return NULL;
}

static uint64_t pointer(const guest_pe *pe, const uint8_t *at)
{ return pe->view.image.target.pointer_bytes == 8 ? qa_load_u64le(at) : qa_load_u32le(at); }

static bool rva_from_va(const guest_pe *pe, uint64_t address, size_t bytes,
    uint32_t *out, qa_error *error)
{
    if (address < pe->view.base || address - pe->view.base > UINT32_MAX ||
        !range(pe, (uint32_t)(address - pe->view.base), bytes, error))
        return guest_fail(error, QA_ERROR_FORMAT, address, "PE absolute address is outside its owned image");
    *out = (uint32_t)(address - pe->view.base);
    return true;
}

static bool headers(guest_pe *pe, qa_error *error)
{
    qa_bytes file = pe->view.artifact;
    uint32_t offset = qa_load_u32le(file.data + 0x3c);
    const uint8_t *coff = file.data + offset + 4;
    uint16_t count = qa_load_u16le(coff + 2), optional_bytes = qa_load_u16le(coff + 16);
    size_t optional_at = (size_t)offset + 24;
    const uint8_t *optional = file.data + optional_at;
    uint32_t directory_at = pe->view.image.target.pointer_bytes == 4 ? 96 : 112;
    if (offset < 64 || count < 1 || count > 96 || !(qa_load_u16le(coff + 18) & 2) ||
        optional_bytes < directory_at)
        return guest_fail(error, QA_ERROR_FORMAT, offset, "PE executable header is incomplete");
    pe->view.section_alignment = qa_load_u32le(optional + 32);
    pe->view.file_alignment = qa_load_u32le(optional + 36);
    pe->view.header_bytes = qa_load_u32le(optional + 60);
    pe->view.entry = qa_load_u32le(optional + 16);
    pe->view.characteristics = qa_load_u16le(coff + 18);
    pe->view.dll_characteristics = qa_load_u16le(optional + 70);
    uint32_t section_alignment = pe->view.section_alignment, file_alignment = pe->view.file_alignment;
    uint64_t image_bytes = pe->view.image.image_bytes;
    if (!power(section_alignment) || !power(file_alignment) || file_alignment > 65536 ||
        section_alignment < file_alignment ||
        (section_alignment < 4096 ? section_alignment != file_alignment : file_alignment < 512) ||
        !pe->view.image.preferred_base || pe->view.image.preferred_base % 65536 ||
        image_bytes > UINT64_MAX - pe->view.image.preferred_base ||
        (pe->view.image.target.pointer_bytes == 4 &&
         pe->view.image.preferred_base + image_bytes > UINT64_C(0x100000000)) ||
        image_bytes % section_alignment || !pe->view.header_bytes ||
        pe->view.header_bytes % file_alignment || pe->view.header_bytes > image_bytes ||
        pe->view.header_bytes > file.size)
        return guest_fail(error, QA_ERROR_FORMAT, optional_at, "PE alignment or image/header extent is invalid");
    uint32_t directories = qa_load_u32le(optional + directory_at - 4);
    if (directories > 16 || directory_at + (uint64_t)directories * 8 > optional_bytes)
        return guest_fail(error, QA_ERROR_FORMAT, optional_at, "PE directory table is truncated");
    for (uint32_t i = 0; i < directories; ++i) {
        const uint8_t *entry = optional + directory_at + i * 8;
        guest_pe_directory *directory = &pe->view.directories[i];
        *directory = (guest_pe_directory){qa_load_u32le(entry), qa_load_u32le(entry + 4)};
        if (((!directory->rva != !directory->bytes) && i != 8) ||
            !span(directory->rva, directory->bytes, i == 4 ? file.size : image_bytes))
            return guest_fail(error, QA_ERROR_FORMAT, directory->rva, "PE data directory has invalid bounds");
    }
    size_t table = optional_at + optional_bytes;
    if (!span(table, (uint64_t)count * 40, pe->view.header_bytes))
        return guest_fail(error, QA_ERROR_FORMAT, table, "PE section table exceeds headers");
    pe->sections = calloc(count, sizeof(*pe->sections));
    if (!pe->sections) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating PE section metadata");
    pe->view.sections = pe->sections;
    pe->view.section_count = count;
    uint32_t end;
    if (!rounded(pe->view.header_bytes, section_alignment, &end))
        return guest_fail(error, QA_ERROR_FORMAT, optional_at, "PE aligned header extent overflows");
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *source = file.data + table + (size_t)i * 40;
        guest_pe_section *section = &pe->sections[i];
        memcpy(section->name, source, 8);
        section->virtual_bytes = qa_load_u32le(source + 8);
        section->rva = qa_load_u32le(source + 12);
        section->file_bytes = qa_load_u32le(source + 16);
        section->file_offset = qa_load_u32le(source + 20);
        section->flags = qa_load_u32le(source + 36);
        uint32_t actual = section->virtual_bytes > section->file_bytes ? section->virtual_bytes : section->file_bytes;
        if (!rounded(actual, section_alignment, &section->bytes) ||
            section->rva % section_alignment || section->rva < end ||
            !span(section->rva, section->bytes, image_bytes) ||
            !span(section->file_offset, section->file_bytes, file.size) ||
            (section->file_bytes && (section->file_offset < pe->view.header_bytes ||
             section->file_offset % file_alignment || section->file_bytes % file_alignment)))
            return guest_fail(error, QA_ERROR_FORMAT, table + (size_t)i * 40, "PE section overlaps or exceeds owned storage");
        end = section->rva + section->bytes;
        if (section->flags & UINT32_C(0x40000000)) section->permissions |= QA_NATIVE_GUEST_READ;
        if (section->flags & UINT32_C(0x80000000)) section->permissions |= QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE;
        if (section->flags & UINT32_C(0x20000000)) section->permissions |= QA_NATIVE_GUEST_EXECUTE;
    }
    pe->bytes = calloc(1, (size_t)image_bytes);
    if (!pe->bytes) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating inert PE virtual image");
    pe->view.bytes = (qa_bytes){pe->bytes, (size_t)image_bytes};
    memcpy(pe->bytes, file.data, pe->view.header_bytes);
    for (size_t i = 0; i < count; ++i) {
        const guest_pe_section *section = &pe->sections[i];
        if (section->file_bytes) memcpy(pe->bytes + section->rva,
            file.data + section->file_offset, section->file_bytes);
    }
    for (size_t i = 0; i < 16; ++i)
        if (i != 4 && pe->view.directories[i].rva && !range(pe,
            pe->view.directories[i].rva, pe->view.directories[i].bytes, error)) return false;
    return !pe->view.entry || executable(pe, pe->view.entry, error);
}

typedef struct pe_patch { uint32_t rva; uint8_t width; uint64_t value; } pe_patch;

static int compare_patch(const void *left, const void *right)
{
    uint32_t a = ((const pe_patch *)left)->rva, b = ((const pe_patch *)right)->rva;
    return (a > b) - (a < b);
}

static bool relocate(guest_pe *pe, qa_error *error)
{
    guest_pe_directory directory = pe->view.directories[5];
    uint64_t delta = pe->view.base - pe->view.image.preferred_base;
    if (delta && ((pe->view.characteristics & 1) || !directory.rva))
        return guest_fail(error, QA_ERROR_FORMAT, 0, "PE artifact cannot relocate from its preferred base");
    if (!directory.rva) return true;
    const uint8_t *source = range(pe, directory.rva, directory.bytes, error);
    if (!source) return false;
    pe_patch *patches = NULL;
    size_t count = 0, capacity = 0, block = 0;
    bool okay = true;
    while (okay && block < directory.bytes) {
        if (directory.bytes - block < 8) { okay = false; break; }
        uint32_t page = qa_load_u32le(source + block), bytes = qa_load_u32le(source + block + 4);
        if (page % 4096 || bytes < 8 || bytes % 2 || bytes > directory.bytes - block) { okay = false; break; }
        for (size_t at = block + 8; okay && at < block + bytes; at += 2) {
            uint16_t entry = qa_load_u16le(source + at), type = entry >> 12;
            if (!type) continue;
            uint64_t target = (uint64_t)page + (entry & 4095);
            uint8_t width = type == 10 && pe->view.image.target.pointer_bytes == 8 ? 8 :
                type == 3 && pe->view.image.target.pointer_bytes == 4 ? 4 :
                type >= 1 && type <= 4 && pe->view.image.target.pointer_bytes == 4 ? 2 : 0;
            if (!width || target > UINT32_MAX) { okay = false; break; }
            const uint8_t *value = range(pe, (uint32_t)target, width, error);
            if (!value) { okay = false; break; }
            uint64_t result = width == 8 ? qa_load_u64le(value) : width == 4 ? qa_load_u32le(value) : qa_load_u16le(value);
            if (type == 4) {
                if (at + 2 >= block + bytes) { okay = false; break; }
                at += 2;
                uint16_t encoded = qa_load_u16le(source + at);
                int16_t low; memcpy(&low, &encoded, sizeof(low));
                result = ((result << 16) + (uint64_t)(int64_t)low + delta + UINT64_C(0x8000)) >> 16;
            } else result += type == 1 ? delta >> 16 : delta;
            if (!guest_grow((void **)&patches, &capacity, count + 1, sizeof(*patches), error)) { okay = false; break; }
            patches[count++] = (pe_patch){(uint32_t)target, width, result};
        }
        block += bytes;
    }
    if (okay && count) qsort(patches, count, sizeof(*patches), compare_patch);
    for (size_t i = 0; okay && i < count; ++i)
        if (i && patches[i].rva < (uint64_t)patches[i - 1].rva + patches[i - 1].width) okay = false;
    for (size_t i = 0; okay && i < count; ++i) {
        uint8_t *at = pe->bytes + patches[i].rva;
        if (patches[i].width == 8) qa_store_u64le(at, patches[i].value);
        else if (patches[i].width == 4) qa_store_u32le(at, (uint32_t)patches[i].value);
        else qa_store_u16le(at, (uint16_t)patches[i].value);
    }
    free(patches);
    return okay || guest_fail(error, QA_ERROR_FORMAT, directory.rva + block, "PE base relocation table is invalid");
}

static bool import_field(guest_pe *pe, uint32_t encoded, bool relative,
    size_t bytes, uint32_t *out, qa_error *error)
{
    *out = 0;
    if (!encoded) return true;
    if (!relative) return rva_from_va(pe, encoded, bytes, out, error);
    if (!range(pe, encoded, bytes, error)) return false;
    *out = encoded;
    return true;
}

static bool read_imports(guest_pe *pe, bool delayed, qa_error *error)
{
    guest_pe_directory directory = pe->view.directories[delayed ? 13 : 1];
    if (!directory.rva) return true;
    uint32_t width = pe->view.image.target.pointer_bytes, stride = delayed ? 32 : 20;
    uint64_t end = (uint64_t)directory.rva + directory.bytes;
    for (uint64_t at = directory.rva; at + stride <= end; at += stride) {
        const uint8_t *descriptor = range(pe, (uint32_t)at, stride, error);
        if (!descriptor) return false;
        uint32_t fields[8] = {0}; bool empty = true;
        for (size_t i = 0; i < stride / 4; ++i) {
            fields[i] = qa_load_u32le(descriptor + i * 4);
            if (fields[i]) empty = false;
        }
        if (empty) { pe->view.imports = pe->imports; return true; }
        uint32_t lookup, iat, library, handle = 0, bound = 0, unload = 0;
        bool relative = true;
        if (delayed) {
            if (fields[0] & ~1u) return guest_fail(error, QA_ERROR_FORMAT, at, "PE delay descriptor has reserved attributes");
            relative = (fields[0] & 1) != 0;
            if (!import_field(pe, fields[1], relative, 1, &library, error) ||
                !import_field(pe, fields[2], relative, width, &handle, error) ||
                !import_field(pe, fields[3], relative, width, &iat, error) ||
                !import_field(pe, fields[4], relative, width, &lookup, error) ||
                !import_field(pe, fields[5], relative, width, &bound, error) ||
                !import_field(pe, fields[6], relative, width, &unload, error)) return false;
            if (!handle || !lookup) return guest_fail(error, QA_ERROR_FORMAT, at, "PE delay descriptor lacks owned handle or lookup storage");
        } else {
            lookup = fields[0]; library = fields[3]; iat = fields[4];
            if (!lookup && fields[1]) return guest_fail(error, QA_ERROR_FORMAT, at, "PE bound IAT lacks its original name table");
        }
        if (!library || !iat || iat % width || lookup % width || handle % width || bound % width || unload % width)
            return guest_fail(error, QA_ERROR_FORMAT, at, "PE import descriptor has invalid addresses or alignment");
        const char *library_name = text(pe, library, pe->view.image.image_bytes, error);
        if (!library_name || !*library_name) return guest_fail(error, QA_ERROR_FORMAT, library, "PE import library is empty or invalid");
        uint64_t table = lookup ? lookup : iat;
        for (uint64_t index = 0; ; ++index) {
            uint64_t lookup_rva = table + index * width, slot = (uint64_t)iat + index * width;
            if (lookup_rva > UINT32_MAX || slot > UINT32_MAX)
                return guest_fail(error, QA_ERROR_FORMAT, at, "PE import thunk table overflows RVAs");
            const uint8_t *value = range(pe, (uint32_t)lookup_rva, width, error);
            if (!value || !range(pe, (uint32_t)slot, width, error)) return false;
            if (bound && ((uint64_t)bound + index * width > UINT32_MAX ||
                !range(pe, bound + (uint32_t)(index * width), width, error))) return false;
            if (unload && ((uint64_t)unload + index * width > UINT32_MAX ||
                !range(pe, unload + (uint32_t)(index * width), width, error))) return false;
            uint64_t thunk = pointer(pe, value);
            if (!thunk) break;
            guest_pe_import imported = {.library = library_name, .slot = (uint32_t)slot,
                .descriptor = (uint32_t)at, .module_handle = handle, .bound_slots = bound,
                .unload_slots = unload, .delayed = delayed};
            uint64_t ordinal_bit = UINT64_C(1) << (width * 8 - 1);
            if (thunk & ordinal_bit) {
                if (thunk & ~(ordinal_bit | UINT64_C(65535)))
                    return guest_fail(error, QA_ERROR_FORMAT, lookup_rva, "PE ordinal thunk has reserved bits");
                imported.by_ordinal = true; imported.ordinal = (uint16_t)thunk;
            } else {
                uint32_t name;
                if (relative) {
                    if (thunk > INT32_MAX) return guest_fail(error, QA_ERROR_FORMAT, lookup_rva, "PE name thunk exceeds its RVA encoding");
                    name = (uint32_t)thunk;
                } else if (!rva_from_va(pe, thunk, 3, &name, error)) return false;
                const uint8_t *hint = range(pe, name, 2, error);
                if (!hint || name > UINT32_MAX - 2) return false;
                imported.hint = qa_load_u16le(hint);
                imported.name = text(pe, name + 2, pe->view.image.image_bytes, error);
                if (!imported.name || !*imported.name)
                    return guest_fail(error, QA_ERROR_FORMAT, name, "PE import symbol is empty or invalid");
            }
            for (size_t i = 0; i < pe->view.import_count; ++i)
                if (pe->imports[i].slot == imported.slot)
                    return guest_fail(error, QA_ERROR_FORMAT, slot, "PE import address slot repeats");
            if (!guest_grow((void **)&pe->imports, &pe->import_capacity,
                pe->view.import_count + 1, sizeof(*pe->imports), error)) return false;
            pe->imports[pe->view.import_count++] = imported;
        }
    }
    return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE import descriptor table has no terminator");
}

static bool forwarder_valid(const char *value)
{
    const char *dot = strrchr(value, '.');
    if (!dot || dot == value || !dot[1]) return false;
    if (dot[1] != '#') return true;
    uint32_t ordinal = 0;
    const char *at = dot + 2;
    if (!*at) return false;
    for (; *at; ++at) {
        if (*at < '0' || *at > '9' || ordinal > (65535u - (uint32_t)(*at - '0')) / 10) return false;
        ordinal = ordinal * 10 + (uint32_t)(*at - '0');
    }
    return ordinal != 0;
}

static bool read_exports(guest_pe *pe, qa_error *error)
{
    guest_pe_directory directory = pe->view.directories[0];
    if (!directory.rva) return true;
    const uint8_t *header = range(pe, directory.rva, 40, error);
    if (!header || directory.bytes < 40) return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE export header is truncated");
    uint32_t ordinal = qa_load_u32le(header + 16), count = qa_load_u32le(header + 20);
    uint32_t names_count = qa_load_u32le(header + 24), addresses = qa_load_u32le(header + 28);
    uint32_t names = qa_load_u32le(header + 32), ordinals = qa_load_u32le(header + 36);
    if ((uint64_t)ordinal + count > UINT64_C(0x100000000) || count > SIZE_MAX / 4 ||
        names_count > SIZE_MAX / 4 || !range(pe, addresses, (size_t)count * 4, error) ||
        !range(pe, names, (size_t)names_count * 4, error) || !range(pe, ordinals, (size_t)names_count * 2, error))
        return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE export tables exceed owned storage");
    size_t *targets = count ? malloc((size_t)count * sizeof(*targets)) : NULL;
    if (count && !targets) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating PE export ordinal lookup");
    bool okay = true;
    for (uint32_t i = 0; okay && i < count; ++i) {
        targets[i] = SIZE_MAX;
        uint32_t rva = qa_load_u32le(pe->bytes + addresses + (size_t)i * 4);
        if (!rva) continue;
        guest_pe_export exported = {.ordinal = ordinal + i, .rva = rva};
        if (rva >= directory.rva && rva - directory.rva < directory.bytes) {
            exported.forwarder = text(pe, rva, (uint64_t)directory.rva + directory.bytes, error);
            if (!exported.forwarder || !forwarder_valid(exported.forwarder)) { okay = false; break; }
        } else if (!range(pe, rva, 1, error)) { okay = false; break; }
        if (!guest_grow((void **)&pe->exports, &pe->export_capacity,
            pe->view.export_count + 1, sizeof(*pe->exports), error)) { okay = false; break; }
        targets[i] = pe->view.export_count;
        pe->exports[pe->view.export_count++] = exported;
    }
    for (uint32_t i = 0; okay && i < names_count; ++i) {
        uint32_t rva = qa_load_u32le(pe->bytes + names + (size_t)i * 4);
        uint16_t index = qa_load_u16le(pe->bytes + ordinals + (size_t)i * 2);
        const char *name = text(pe, rva, pe->view.image.image_bytes, error);
        if (!name || !*name || index >= count || targets[index] == SIZE_MAX) { okay = false; break; }
        for (size_t n = 0; n < pe->view.export_count; ++n)
            if (pe->exports[n].name && !strcmp(pe->exports[n].name, name)) { okay = false; break; }
        if (!okay || !guest_grow((void **)&pe->exports, &pe->export_capacity,
            pe->view.export_count + 1, sizeof(*pe->exports), error)) { okay = false; break; }
        guest_pe_export exported = pe->exports[targets[index]];
        exported.name = name;
        pe->exports[pe->view.export_count++] = exported;
    }
    free(targets);
    pe->view.exports = pe->exports;
    return okay || guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE export names, ordinal or forwarder are invalid");
}

static bool read_tls(guest_pe *pe, qa_error *error)
{
    guest_pe_directory directory = pe->view.directories[9];
    if (!directory.rva) return true;
    uint32_t width = pe->view.image.target.pointer_bytes;
    const uint8_t *header = range(pe, directory.rva, width * 4 + 8, error);
    if (!header || directory.bytes < width * 4 + 8)
        return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE TLS directory is truncated");
    uint64_t start = pointer(pe, header), end = pointer(pe, header + width);
    uint64_t index = pointer(pe, header + width * 2), callbacks = pointer(pe, header + width * 3);
    uint32_t flags = qa_load_u32le(header + width * 4 + 4), alignment = (flags >> 20) & 15;
    if (end < start || end - start > pe->view.image.image_bytes || alignment == 15 || !index ||
        !rva_from_va(pe, index, 4, &pe->view.tls.index, error))
        return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE TLS template or index address is invalid");
    pe->view.tls.present = true;
    pe->view.tls.zero_bytes = qa_load_u32le(header + width * 4);
    pe->view.tls.alignment = alignment ? 1u << (alignment - 1) : 1;
    if (start || end) {
        uint32_t rva;
        if (!rva_from_va(pe, start, (size_t)(end - start), &rva, error) ||
            !guest_pe_range(pe, rva, (size_t)(end - start), &pe->view.tls.initialized, error)) return false;
    }
    if (callbacks) {
        uint32_t first;
        if (!rva_from_va(pe, callbacks, width, &first, error)) return false;
        for (uint64_t at = first; ; at += width) {
            if (at > UINT32_MAX) return guest_fail(error, QA_ERROR_FORMAT, at, "PE TLS callback array overflows RVAs");
            const uint8_t *source = range(pe, (uint32_t)at, width, error);
            if (!source) return false;
            uint64_t callback = pointer(pe, source);
            if (!callback) break;
            uint32_t rva;
            if (!rva_from_va(pe, callback, 1, &rva, error) || !executable(pe, rva, error) ||
                !guest_grow((void **)&pe->tls_callbacks, &pe->tls_capacity,
                    pe->view.tls.callback_count + 1, sizeof(*pe->tls_callbacks), error)) return false;
            pe->tls_callbacks[pe->view.tls.callback_count++] = callback;
        }
    }
    pe->view.tls.callbacks = pe->tls_callbacks;
    return true;
}

static bool unwind_record(guest_pe *pe, uint32_t begin, uint32_t end,
    uint32_t information, uint32_t *ancestors, size_t depth, size_t *out, qa_error *error)
{
    if (begin >= end || depth >= 64 || information % 4 ||
        !executable(pe, begin, error) || !executable(pe, end - 1, error))
        return guest_fail(error, QA_ERROR_FORMAT, information, "PE unwind function or chain is invalid");
    for (size_t i = 0; i < depth; ++i)
        if (ancestors[i] == information)
            return guest_fail(error, QA_ERROR_FORMAT, information, "PE unwind metadata has a cyclic chain");
    const uint8_t *source = range(pe, information, 4, error);
    if (!source) return false;
    uint8_t version = source[0] & 7, flags = source[0] >> 3;
    if ((version != 1 && version != 2) || flags & ~7 || ((flags & 4) && (flags & 3)))
        return guest_fail(error, QA_ERROR_FORMAT, information, "PE unwind version or flags are invalid");
    size_t length = 4 + (((size_t)source[2] + 1) / 2) * 4;
    size_t extra = flags & 4 ? 12 : flags & 3 ? 4 : 0;
    source = range(pe, information, length + extra, error);
    if (!source) return false;
    guest_pe_unwind record = {.begin = begin, .end = end, .information = information,
        .version = version, .flags = flags, .chained = SIZE_MAX,
        .metadata = {source, length + extra}};
    if (flags & 4) {
        ancestors[depth] = information;
        if (!unwind_record(pe, qa_load_u32le(source + length), qa_load_u32le(source + length + 4),
            qa_load_u32le(source + length + 8), ancestors, depth + 1, &record.chained, error)) return false;
    } else if (flags & 3) {
        record.handler = qa_load_u32le(source + length);
        if (!executable(pe, record.handler, error)) return false;
        record.handler_data = information + (uint32_t)length + 4;
    }
    if (!guest_grow((void **)&pe->unwind, &pe->unwind_capacity,
        pe->view.unwind_count + 1, sizeof(*pe->unwind), error)) return false;
    *out = pe->view.unwind_count;
    pe->unwind[pe->view.unwind_count++] = record;
    return true;
}

static bool config_address(guest_pe *pe, const uint8_t *source, size_t bytes,
    uint32_t offset, uint32_t *out, qa_error *error)
{
    uint32_t width = pe->view.image.target.pointer_bytes;
    *out = 0;
    if (!span(offset, width, bytes)) return true;
    uint64_t address = pointer(pe, source + offset);
    return !address || rva_from_va(pe, address, width, out, error);
}

static bool read_lifecycle(guest_pe *pe, qa_error *error)
{
    if (!read_tls(pe, error)) return false;
    guest_pe_directory directory = pe->view.directories[3];
    if (directory.rva) {
        if (pe->view.image.target.pointer_bytes != 8 || directory.bytes % 12)
            return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE exception directory layout differs from x64 runtime functions");
        pe->view.function_count = directory.bytes / 12;
        pe->functions = pe->view.function_count ? malloc(pe->view.function_count * sizeof(*pe->functions)) : NULL;
        if (pe->view.function_count && !pe->functions)
            return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating PE runtime function index");
        pe->view.functions = pe->functions;
        uint32_t previous = 0; bool seen = false;
        for (uint64_t at = directory.rva; at < (uint64_t)directory.rva + directory.bytes; at += 12) {
            const uint8_t *source = range(pe, (uint32_t)at, 12, error);
            if (!source) return false;
            uint32_t begin = qa_load_u32le(source);
            if (seen && begin <= previous) return guest_fail(error, QA_ERROR_FORMAT, at, "PE runtime function table is unsorted");
            seen = true; previous = begin;
            uint32_t ancestors[64]; size_t index;
            if (!unwind_record(pe, begin, qa_load_u32le(source + 4), qa_load_u32le(source + 8),
                ancestors, 0, &index, error)) return false;
            pe->functions[(at - directory.rva) / 12] = index;
        }
        pe->view.unwind = pe->unwind;
    }
    directory = pe->view.directories[10];
    if (directory.rva) {
        const uint8_t *source = range(pe, directory.rva, 4, error);
        if (!source) return false;
        uint32_t size = qa_load_u32le(source);
        if (size < 4 || size > directory.bytes ||
            !guest_pe_range(pe, directory.rva, size, &pe->view.load_configuration, error))
            return guest_fail(error, QA_ERROR_FORMAT, directory.rva, "PE load configuration extent is invalid");
        source = pe->view.load_configuration.data;
        bool narrow = pe->view.image.target.pointer_bytes == 4;
        if (!config_address(pe, source, size, narrow ? 60 : 88, &pe->view.cookie, error) ||
            !config_address(pe, source, size, narrow ? 72 : 112, &pe->view.guard_check, error) ||
            !config_address(pe, source, size, narrow ? 76 : 120, &pe->view.guard_dispatch, error)) return false;
        uint32_t flags = narrow ? 88 : 144;
        if (span(flags, 4, size)) pe->view.guard_flags = qa_load_u32le(source + flags);
    }
    return true;
}

void guest_pe_close(guest_pe **owner)
{
    if (!owner || !*owner) return;
    guest_pe *pe = *owner;
    free(pe->artifact); free(pe->bytes); free(pe->sections); free(pe->imports);
    free(pe->exports); free(pe->unwind); free(pe->functions); free(pe->tls_callbacks); free(pe);
    *owner = NULL;
}

const guest_pe_view *guest_pe_describe(const guest_pe *pe)
{ return pe ? &pe->view : NULL; }

bool guest_pe_open(qa_bytes bytes, const qa_native_image_info *expected,
    uint64_t base, size_t maximum, guest_pe **out, qa_error *error)
{
    if (!out || *out || !expected || !maximum)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "inert PE loader requires artifact identity, bounds and empty output");
    qa_native_image_info actual;
    if (!qa_native_inspect(bytes, &actual, error)) return false;
    if ((actual.format != QA_NATIVE_IMAGE_PE32 && actual.format != QA_NATIVE_IMAGE_PE32_PLUS) ||
        actual.target.arch == QA_NATIVE_ARCH_AARCH64 || !qa_sha256_equal(&actual.digest, &expected->digest) ||
        actual.format != expected->format || actual.target.os != expected->target.os ||
        actual.target.arch != expected->target.arch || actual.target.abi != expected->target.abi ||
        actual.target.pointer_bytes != expected->target.pointer_bytes || actual.preferred_base != expected->preferred_base ||
        actual.image_bytes != expected->image_bytes || actual.image_bytes > maximum || actual.image_bytes > SIZE_MAX ||
        !base || base % 65536 || actual.image_bytes > UINT64_MAX - base ||
        (actual.target.pointer_bytes == 4 && base + actual.image_bytes > UINT64_C(0x100000000)))
        return guest_fail(error, QA_ERROR_FORMAT, base, "inert PE artifact identity, ABI or image extent differs");
    guest_pe *pe = calloc(1, sizeof(*pe));
    if (!pe) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating inert PE owner");
    pe->artifact = malloc(bytes.size);
    if (!pe->artifact) { guest_pe_close(&pe); return guest_fail(error, QA_ERROR_MEMORY, 0, "retaining PE artifact bytes"); }
    memcpy(pe->artifact, bytes.data, bytes.size);
    pe->view.image = actual; pe->view.base = base;
    pe->view.artifact = (qa_bytes){pe->artifact, bytes.size};
    if (!headers(pe, error) || !relocate(pe, error) || !read_imports(pe, false, error) ||
        !read_imports(pe, true, error) || !read_exports(pe, error) || !read_lifecycle(pe, error)) {
        guest_pe_close(&pe); return false;
    }
    *out = pe;
    return true;
}

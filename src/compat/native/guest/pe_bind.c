#include "internal.h"
#include "pe_bind.h"

typedef struct binding_write { uint64_t address; uint8_t bytes[8]; } binding_write;
typedef struct binding_page { uint64_t address; uint32_t permissions; } binding_page;

static bool compatible(const guest_pe_view *image, const qa_native_guest *guest, qa_error *error)
{
    return (image && guest && image->image.target.os == guest->options.image.target.os &&
        image->image.target.arch == guest->options.image.target.arch &&
        image->image.target.abi == guest->options.image.target.abi &&
        image->image.target.pointer_bytes == guest->options.image.target.pointer_bytes) ||
        guest_fail(error, QA_ERROR_ARGUMENT, 0, "PE binding needs the actual image and process ABI");
}

static bool target(const qa_native_guest *guest, uint64_t address, qa_error *error)
{
    if (!guest_ready(guest, error)) return false;
    qa_native_guest_mapping *mapping = guest_mapping(guest, address);
    return (address && mapping && mapping->permissions &&
        (guest->options.image.target.pointer_bytes != 4 || address <= UINT32_MAX)) ||
        guest_fail(error, QA_ERROR_FORMAT, address, "PE provider is not actual accessible guest storage");
}

static bool write_plan(const guest_pe_view *image, const qa_native_guest *guest,
    binding_write *writes, size_t *count, uint32_t slot, uint64_t value, qa_error *error)
{
    unsigned width = image->image.target.pointer_bytes;
    if (!slot || slot > image->bytes.size || width > image->bytes.size - slot)
        return guest_fail(error, QA_ERROR_FORMAT, slot, "PE binding slot exceeds its actual image");
    binding_write record = {.address = image->base + slot};
    if (width == 4) qa_store_u32le(record.bytes, (uint32_t)value);
    else qa_store_u64le(record.bytes, value);
    uint8_t previous[8];
    if (!qa_native_guest_read(guest, record.address, previous, width, error)) return false;
    for (size_t i = 0; i < *count; ++i) {
        if (writes[i].address == record.address && !memcmp(writes[i].bytes, record.bytes, width)) return true;
        if (writes[i].address < record.address + width && record.address < writes[i].address + width)
            return guest_fail(error, QA_ERROR_FORMAT, record.address, "PE binding slots overlap or disagree on their actual provider");
    }
    writes[(*count)++] = record; return true;
}

static bool plan(const guest_pe *pe, const qa_native_guest *guest,
    guest_pe_import_resolve_fn resolve, void *context, binding_write **out,
    size_t *count, qa_error *error)
{
    const guest_pe_view *image = guest_pe_describe(pe);
    if (!compatible(image, guest, error) || !guest_ready(guest, error)) return false;
    if (image->import_count && !resolve)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "actual PE imports require their provider resolver");
    if (image->import_count > SIZE_MAX / (2 * sizeof(**out)))
        return guest_fail(error, QA_ERROR_MEMORY, 0, "PE binding plan extent overflows");
    binding_write *writes = image->import_count ? malloc(image->import_count * 2 * sizeof(*writes)) : NULL;
    if (image->import_count && !writes) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning PE binding plan");
    bool okay = true; *count = 0;
    for (size_t i = 0; okay && i < image->import_count; ++i) {
        const guest_pe_import *import = &image->imports[i]; guest_pe_resolution resolution = {0};
        okay = resolve(context, image, import, &resolution, error) &&
            target(guest, resolution.address, error) &&
            write_plan(image, guest, writes, count, import->slot, resolution.address, error);
        if (okay && resolution.publish_module_handle) {
            if (!import->delayed || !import->module_handle)
                okay = guest_fail(error, QA_ERROR_ARGUMENT, import->descriptor, "PE handle publication needs its actual delay descriptor slot");
            else okay = target(guest, resolution.module_handle, error) &&
                write_plan(image, guest, writes, count, import->module_handle, resolution.module_handle, error);
        }
    }
    if (!okay) { free(writes); return false; }
    *out = writes; return true;
}

bool guest_pe_bind_imports(const guest_pe *pe, qa_native_guest *guest,
    guest_pe_import_resolve_fn resolve, void *context, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    binding_write *writes = NULL; size_t count = 0;
    if (!plan(pe, guest, resolve, context, &writes, &count, error)) return false;
    if (count > SIZE_MAX / (2 * sizeof(binding_page))) {
        free(writes); return guest_fail(error, QA_ERROR_MEMORY, 0, "PE binding page records overflow");
    }
    binding_page *pages = count ? malloc(count * 2 * sizeof(*pages)) : NULL;
    if (count && !pages) { free(writes); return guest_fail(error, QA_ERROR_MEMORY, 0, "owning PE binding page records"); }
    size_t page_count = 0; unsigned width = guest->options.image.target.pointer_bytes;
    for (size_t i = 0; i < count; ++i) {
        uint8_t current[8];
        if (!qa_native_guest_read(guest, writes[i].address, current, width, error)) {
            free(pages); free(writes); return false;
        }
        uint64_t first = writes[i].address & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
        uint64_t last = (writes[i].address + width - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
        for (uint64_t page = first; ; page += QA_NATIVE_GUEST_PAGE) {
            bool seen = false;
            for (size_t j = 0; j < page_count; ++j) if (pages[j].address == page) seen = true;
            if (!seen) pages[page_count++] = (binding_page){page, guest_mapping(guest, page)->permissions};
            if (page == last) break;
        }
    }
    bool okay = true, changed = false;
    for (size_t i = 0; okay && i < page_count; ++i) {
        okay = qa_native_guest_protect_range(guest, pages[i].address, QA_NATIVE_GUEST_PAGE,
            pages[i].permissions | QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, error);
        if (okay) changed = true;
    }
    for (size_t i = 0; okay && i < count; ++i)
        okay = qa_native_guest_write(guest, writes[i].address, (qa_bytes){writes[i].bytes, width}, error);
    for (size_t i = 0; okay && i < page_count; ++i)
        okay = qa_native_guest_protect_range(guest, pages[i].address, QA_NATIVE_GUEST_PAGE,
            pages[i].permissions, error);
    if (!okay && changed) guest->failed = true;
    free(pages); free(writes); return okay;
}

bool guest_pe_resolve_export(const guest_pe *pe, const qa_native_guest *guest, const char *name, uint32_t ordinal,
    bool by_ordinal, guest_pe_library_fn lookup, void *context,
    const guest_pe **out_image, uint64_t *out_address, qa_error *error)
{
    if (!pe || !out_image || !out_address || (!by_ordinal && !name))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "PE export lookup needs its actual image and symbol");
    typedef struct visited { const guest_pe *image; const char *name; uint32_t ordinal; bool by_ordinal; } visited;
    visited seen[128]; size_t count = 0; const guest_pe_view *origin = guest_pe_describe(pe);
    if (!compatible(origin, guest, error) || !guest_ready(guest, error)) return false;
    while (count < 128) {
        for (size_t i = 0; i < count; ++i)
            if (seen[i].image == pe && seen[i].by_ordinal == by_ordinal &&
                (by_ordinal ? seen[i].ordinal == ordinal : !strcmp(seen[i].name, name)))
                return guest_fail(error, QA_ERROR_FORMAT, 0, "PE export forwarder cycles through the same actual symbol");
        seen[count++] = (visited){pe, name, ordinal, by_ordinal};
        const guest_pe_view *image = guest_pe_describe(pe); const guest_pe_export *export = NULL;
        for (size_t i = 0; i < image->export_count; ++i)
            if (by_ordinal ? image->exports[i].ordinal == ordinal :
                image->exports[i].name && !strcmp(image->exports[i].name, name)) { export = &image->exports[i]; break; }
        if (!export) return guest_fail(error, QA_ERROR_NOT_FOUND, ordinal, "PE inventory has no actual requested export");
        if (!export->forwarder) {
            if (!export->rva || export->rva >= image->bytes.size)
                return guest_fail(error, QA_ERROR_FORMAT, export->rva, "PE export has no actual image address");
            uint64_t address = image->base + export->rva;
            if (!target(guest, address, error)) return false;
            *out_image = pe; *out_address = address; return true;
        }
        const char *dot = strrchr(export->forwarder, '.');
        if (!dot || dot == export->forwarder || !dot[1] || !lookup)
            return guest_fail(error, QA_ERROR_FORMAT, export->rva, "PE forwarder lacks its actual library and symbol resolver");
        size_t bytes = (size_t)(dot - export->forwarder);
        char *library = malloc(bytes + 1);
        if (!library) return guest_fail(error, QA_ERROR_MEMORY, export->rva, "owning PE forwarder library name");
        memcpy(library, export->forwarder, bytes); library[bytes] = 0;
        const guest_pe *dependency = NULL; bool okay = lookup(context, library, pe, &dependency, error);
        free(library);
        if (!okay) return false;
        const guest_pe_view *next = guest_pe_describe(dependency);
        if (!next || next->image.target.os != origin->image.target.os ||
            next->image.target.arch != origin->image.target.arch || next->image.target.abi != origin->image.target.abi ||
            next->image.target.pointer_bytes != origin->image.target.pointer_bytes)
            return guest_fail(error, QA_ERROR_FORMAT, 0, "PE forwarder provider differs from the actual origin ABI");
        name = dot + 1; by_ordinal = name[0] == '#'; ordinal = 0;
        if (by_ordinal) {
            const char *digit = name + 1;
            if (!*digit) return guest_fail(error, QA_ERROR_FORMAT, export->rva, "PE ordinal forwarder has no actual number");
            for (; *digit; ++digit) {
                if (*digit < '0' || *digit > '9' || ordinal > (UINT32_MAX - (unsigned)(*digit - '0')) / 10)
                    return guest_fail(error, QA_ERROR_FORMAT, export->rva, "PE ordinal forwarder exceeds its actual width");
                ordinal = ordinal * 10 + (unsigned)(*digit - '0');
            }
        }
        pe = dependency;
    }
    return guest_fail(error, QA_ERROR_FORMAT, 0, "PE forwarder exceeds its bounded actual provider chain");
}

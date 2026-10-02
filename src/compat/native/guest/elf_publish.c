#include "internal.h"
#include "elf_publish.h"

struct guest_elf_publication {
    guest_sysv_provider provider;
    guest_sysv_export *exports;
    uint64_t *preinitializers, *initializers, *finalizers;
    uint8_t *tls;
    size_t tls_bytes;
};

static bool executable(const guest_elf_binding *binding, uint64_t target, qa_error *error)
{ return target && guest_range(binding->guest, target, 1, QA_NATIVE_GUEST_EXECUTE, error); }

static bool array(const guest_elf_binding *binding, uint64_t address_tag, uint64_t size_tag,
    uint64_t extra_tag, bool reverse, uint64_t **out, size_t *count, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    uint64_t address = 0, bytes = 0, extra = 0;
    bool has_address = guest_elf_dynamic_value(binding->image, address_tag, &address);
    bool has_size = guest_elf_dynamic_value(binding->image, size_tag, &bytes);
    bool has_extra = extra_tag && guest_elf_dynamic_value(binding->image, extra_tag, &extra) && extra;
    unsigned width = image->image.target.pointer_bytes;
    if (has_address != has_size || bytes % width || bytes > SIZE_MAX ||
        bytes / width > (SIZE_MAX / sizeof(uint64_t)) - (has_extra ? 1u : 0u) ||
        address > UINT64_MAX - image->bias || extra > UINT64_MAX - image->bias)
        return guest_fail(error, QA_ERROR_FORMAT, address, "ELF lifecycle array has incomplete or overflowing actual tags");
    qa_bytes file = {0};
    if (bytes && !guest_elf_file_range(binding->image, address, (size_t)bytes, &file, error)) return false;
    size_t capacity = (size_t)(bytes / width) + (has_extra ? 1u : 0u);
    uint64_t *targets = capacity ? malloc(capacity * sizeof(*targets)) : NULL;
    if (capacity && !targets) return guest_fail(error, QA_ERROR_MEMORY, address, "owning relocated ELF lifecycle entries");
    size_t used = 0; bool okay = true;
    if (has_extra && !reverse) targets[used++] = image->bias + extra;
    for (uint64_t offset = 0; okay && offset < bytes; offset += width) {
        uint8_t encoded[8];
        okay = qa_native_guest_read(binding->guest, image->bias + address + offset, encoded, width, error);
        if (!okay) break;
        uint64_t value = width == 4 ? qa_load_u32le(encoded) : qa_load_u64le(encoded);
        if (value && value != (width == 4 ? UINT32_MAX : UINT64_MAX)) targets[used++] = value;
    }
    if (reverse) for (size_t i = 0; i < used / 2; ++i) {
        uint64_t swap = targets[i]; targets[i] = targets[used - i - 1]; targets[used - i - 1] = swap;
    }
    if (has_extra && reverse) targets[used++] = image->bias + extra;
    for (size_t i = 0; okay && i < used; ++i)
        if (!executable(binding, targets[i], error))
            okay = guest_fail(error, QA_ERROR_FORMAT, targets[i], "ELF lifecycle target has no actual executable guest storage");
    if (!okay) { free(targets); return false; }
    *out = targets; *count = used; return true;
}

void guest_elf_publication_close(guest_elf_publication **owner)
{
    if (!owner || !*owner) return;
    guest_elf_publication *publication = *owner;
    free(publication->exports); free(publication->preinitializers);
    free(publication->initializers); free(publication->finalizers); free(publication->tls);
    free(publication); *owner = NULL;
}

static bool append_export(const guest_elf_binding *binding, guest_elf_publication *publication,
    size_t index, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(binding->image);
    const guest_elf_symbol *symbol = image->symbols + index;
    guest_sysv_symbol resolved;
    if (!guest_elf_bind_export(binding, index, &resolved, error)) return false;
    guest_sysv_export export = {.name = symbol->name,
        .version = symbol->version ? symbol->version->name : NULL,
        .address = resolved.address, .bytes = symbol->bytes,
        .tls_offset = resolved.offset, .tls = resolved.tls};
    publication->exports[publication->provider.export_count++] = export;
    if (symbol->version && !symbol->hidden) {
        export.version = NULL;
        publication->exports[publication->provider.export_count++] = export;
    }
    return true;
}

bool guest_elf_publication_open(const guest_elf_binding *binding,
    guest_elf_publication **out, qa_error *error)
{
    const guest_elf_view *image = binding ? guest_elf_describe(binding->image) : NULL;
    if (!image || !out || *out || !binding->runtime || !binding->provider ||
        guest_sysv_guest(binding->runtime) != binding->guest || !qa_native_guest_idle(binding->guest) ||
        image->image.target.os != binding->guest->options.image.target.os ||
        image->image.target.arch != binding->guest->options.image.target.arch ||
        image->image.target.abi != binding->guest->options.image.target.abi ||
        image->image.target.pointer_bytes != binding->guest->options.image.target.pointer_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF publication requires its actual idle relocated process and empty output");
    size_t exports = 0;
    for (size_t i = 0; i < image->symbol_count; ++i) {
        const guest_elf_symbol *symbol = image->symbols + i;
        if (!symbol->name[0] || !symbol->section ||
            (symbol->binding != 1 && symbol->binding != 2 && symbol->binding != 10) ||
            symbol->visibility == 1 || symbol->visibility == 2) continue;
        size_t added = symbol->version && !symbol->hidden ? 2 : 1;
        if (exports > SIZE_MAX / sizeof(guest_sysv_export) - added)
            return guest_fail(error, QA_ERROR_MEMORY, i, "ELF provider export ownership extent overflows");
        exports += added;
    }
    guest_elf_publication *publication = calloc(1, sizeof(*publication));
    if (!publication) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning relocated ELF provider");
    publication->exports = exports ? malloc(exports * sizeof(*publication->exports)) : NULL;
    if (exports && !publication->exports) {
        guest_elf_publication_close(&publication);
        return guest_fail(error, QA_ERROR_MEMORY, 0, "owning relocated ELF provider exports");
    }
    publication->provider = (guest_sysv_provider){.id = binding->provider, .image = image->image,
        .soname = image->soname, .exports = publication->exports,
        .needed = image->needed, .needed_count = image->needed_count};
    bool okay = true;
    for (unsigned pass = 0; okay && pass < 3; ++pass)
        for (size_t i = 0; okay && i < image->symbol_count; ++i) {
            const guest_elf_symbol *symbol = image->symbols + i;
            if (!symbol->name[0] || !symbol->section ||
                (symbol->binding != 1 && symbol->binding != 2 && symbol->binding != 10) ||
                symbol->visibility == 1 || symbol->visibility == 2) continue;
            unsigned phase = symbol->binding == 10 ? 1 : symbol->type == 10 ? 2 : 0;
            if (phase == pass) okay = append_export(binding, publication, i, error);
        }
    if (okay) okay = array(binding, 32, 33, 0, false, &publication->preinitializers,
        &publication->provider.preinitializer_count, error);
    if (okay && image->role != GUEST_ELF_PROGRAM && publication->provider.preinitializer_count)
        okay = guest_fail(error, QA_ERROR_FORMAT, image->bias, "ELF preinitializers require the actual main-program role");
    if (okay) okay = array(binding, 25, 27, 12, false, &publication->initializers,
        &publication->provider.initializer_count, error);
    if (okay) okay = array(binding, 26, 28, 13, true, &publication->finalizers,
        &publication->provider.finalizer_count, error);
    publication->provider.preinitializers = publication->preinitializers;
    publication->provider.initializers = publication->initializers;
    publication->provider.finalizers = publication->finalizers;
    if (okay && image->entry && (image->entry > UINT64_MAX - image->bias ||
        !executable(binding, image->bias + image->entry, error)))
        okay = guest_fail(error, QA_ERROR_FORMAT, image->entry, "ELF entry has no actual executable guest storage");
    if (okay && image->tls) {
        if (!binding->tls.module_id || binding->tls.bytes != image->tls->memory_bytes ||
            image->tls->file_bytes > SIZE_MAX || image->tls->address > UINT64_MAX - image->bias)
            okay = guest_fail(error, QA_ERROR_FORMAT, image->tls->address, "ELF TLS publication differs from its actual allocated template");
        else {
            publication->tls_bytes = (size_t)image->tls->file_bytes;
            publication->tls = publication->tls_bytes ? malloc(publication->tls_bytes) : NULL;
            if (publication->tls_bytes && !publication->tls)
                okay = guest_fail(error, QA_ERROR_MEMORY, image->tls->address, "owning relocated ELF TLS template");
            else if (publication->tls_bytes) okay = qa_native_guest_read(binding->guest,
                image->bias + image->tls->address, publication->tls, publication->tls_bytes, error);
        }
    }
    if (!okay || !guest_ready(binding->guest, error)) {
        binding->guest->failed = true; guest_elf_publication_close(&publication); return false;
    }
    *out = publication; return true;
}

const guest_sysv_provider *guest_elf_publication_provider(const guest_elf_publication *publication)
{ return publication ? &publication->provider : NULL; }

qa_bytes guest_elf_publication_tls(const guest_elf_publication *publication)
{ return publication ? (qa_bytes){publication->tls, publication->tls_bytes} : (qa_bytes){0}; }

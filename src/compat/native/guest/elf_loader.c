#include "internal.h"
#include "elf_loader.h"

struct guest_elf_loaded {
    const guest_elf *image;
    guest_sysv_runtime *runtime;
    guest_elf_memory *memory;
    guest_elf_unwind *unwind;
    uint64_t provider;
    bool committed;
};

void guest_elf_loaded_abandon(guest_elf_loaded **owner)
{
    if (!owner || !*owner) return;
    guest_elf_memory_abandon(&(*owner)->memory);
    guest_elf_unwind_close(&(*owner)->unwind);
    free(*owner); *owner = NULL;
}

bool guest_elf_loaded_unmap(guest_elf_loaded *owner, qa_error *error)
{
    if (!owner || !owner->committed || !guest_sysv_idle(owner->runtime))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF reload requires its finalized stopped module attachment");
    return guest_elf_memory_close(&owner->memory, error);
}

bool guest_elf_load(const guest_elf *elf, guest_sysv_runtime *runtime,
    const guest_elf_load_options *options, guest_elf_loaded **out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(elf);
    qa_native_guest *guest = guest_sysv_guest(runtime);
    bool native = qa_native_guest_execution(guest) == QA_NATIVE_GUEST_HOST_X86_64;
    if (!image || !options || !options->provider || !options->return_trap ||
        (native ? options->instruction_budget != 0 : !options->instruction_budget) ||
        !out || *out || !guest_sysv_idle(runtime) ||
        !qa_native_guest_idle(guest) ||
        !guest_range(guest, options->return_trap, 1, QA_NATIVE_GUEST_EXECUTE, error))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF fresh loading requires its actual idle runtime, provider, stack continuation and empty output");
    guest_elf_loaded *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning actual ELF loader record");
    owner->image = elf; owner->runtime = runtime; owner->provider = options->provider;
    if (!guest_elf_memory_attach(elf, guest, &options->memory, &owner->memory, error)) {
        if (owner->memory || guest->failed) *out = owner;
        else guest_elf_loaded_abandon(&owner);
        return false;
    }
    guest_sysv_load *ticket = NULL;
    guest_elf_binding binding = {.image = elf, .guest = guest, .runtime = runtime,
        .provider = options->provider, .return_trap = options->return_trap,
        .instruction_budget = options->instruction_budget};
    bool okay = guest_elf_memory_write_begin(owner->memory, error) &&
        (options->replacing ? guest_sysv_reload_begin(runtime, options->provider,
            &ticket, &binding.tls, error) :
        guest_sysv_load_begin(runtime, options->provider, image->tls != NULL,
            image->tls ? image->tls->memory_bytes : 0,
            image->tls && image->tls->alignment ? image->tls->alignment : 1,
            &ticket, &binding.tls, error));
    for (size_t i = 0; okay && i < image->symbol_count; ++i) {
        const guest_elf_symbol *symbol = image->symbols + i;
        if (!symbol->name[0] || !symbol->section || symbol->binding != 10 ||
            symbol->visibility == 1 || symbol->visibility == 2) continue;
        guest_sysv_symbol definition;
        okay = guest_elf_bind_export(&binding, i, &definition, error);
    }
    if (okay) okay = guest_elf_bind_relocations(&binding, error);
    guest_elf_publication *publication = NULL;
    if (okay) okay = guest_elf_publication_open(&binding, &publication, error);
    if (okay) okay = guest_elf_unwind_open(elf, guest, SIZE_MAX, &owner->unwind, error);
    if (okay) okay = guest_elf_memory_finish(owner->memory, error);
    if (okay) okay = guest_sysv_load_commit(&ticket,
        guest_elf_publication_provider(publication), guest_elf_publication_tls(publication), error);
    guest_elf_publication_close(&publication);
    if (!okay || !guest_ready(guest, error)) {
        /* The runtime owns any outstanding ticket. Whole terminal teardown,
         * then runtime abandon, frees it without an allocating abort/replay. */
        guest->failed = true; *out = owner; return false;
    }
    owner->committed = true; *out = owner; return true;
}

bool guest_elf_loaded_initialize(guest_elf_loaded *owner, size_t budget, qa_error *error)
{
    qa_native_guest *guest = owner ? guest_sysv_guest(owner->runtime) : NULL;
    bool native = qa_native_guest_execution(guest) == QA_NATIVE_GUEST_HOST_X86_64;
    if (!owner || !owner->committed || (native ? budget != 0 : !budget) || !qa_native_guest_idle(guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF initialization requires its genuinely committed provider");
    bool okay = guest_sysv_initialize(owner->runtime, owner->provider, budget, error);
    if (!okay) guest->failed = true;
    return okay;
}

bool guest_elf_loaded_finalize(guest_elf_loaded *owner, size_t budget, qa_error *error)
{
    qa_native_guest *guest = owner ? guest_sysv_guest(owner->runtime) : NULL;
    bool native = qa_native_guest_execution(guest) == QA_NATIVE_GUEST_HOST_X86_64;
    if (!owner || !owner->committed || (native ? budget != 0 : !budget) || !qa_native_guest_idle(guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF finalization requires its genuinely committed provider");
    bool okay = guest_sysv_finalize(owner->runtime, owner->provider, budget, error);
    if (!okay) guest->failed = true;
    return okay;
}

guest_elf_memory *guest_elf_loaded_memory(guest_elf_loaded *owner)
{ return owner && owner->committed ? owner->memory : NULL; }
uint64_t guest_elf_loaded_provider(const guest_elf_loaded *owner)
{ return owner ? owner->provider : 0; }
const guest_elf_unwind *guest_elf_loaded_unwind(const guest_elf_loaded *owner)
{ return owner && owner->committed ? owner->unwind : NULL; }

static bool text_equal(const char *a, const char *b)
{ return a == b || (a && b && !strcmp(a, b)); }

static bool provider_retained(const guest_elf_loaded *owner, qa_error *error)
{
    const guest_elf_view *artifact = guest_elf_describe(owner->image);
    const guest_sysv_provider *provider = guest_sysv_provider_read(owner->runtime, owner->provider);
    const qa_native_image_info *actual = artifact ? &artifact->image : NULL;
    const qa_native_image_info *saved = provider ? &provider->image : NULL;
    if (!actual || !saved || !owner->provider || saved->format != actual->format ||
        saved->target.os != actual->target.os || saved->target.arch != actual->target.arch ||
        saved->target.abi != actual->target.abi || saved->target.pointer_bytes != actual->target.pointer_bytes ||
        saved->preferred_base != actual->preferred_base || saved->image_bytes != actual->image_bytes ||
        !qa_sha256_equal(&saved->digest, &actual->digest) ||
        !text_equal(provider->soname, artifact->soname) || provider->needed_count != artifact->needed_count)
        return guest_fail(error, QA_ERROR_FORMAT, owner->provider, "ELF loaded provider differs from its actual retained artifact");
    for (size_t i = 0; i < artifact->needed_count; ++i)
        if (!text_equal(provider->needed[i], artifact->needed[i]))
            return guest_fail(error, QA_ERROR_FORMAT, i, "ELF loaded provider dependency order differs from its actual artifact");
    guest_sysv_provider_tls tls = {0};
    if (!guest_sysv_provider_tls_read(owner->runtime, owner->provider, &tls))
        return guest_fail(error, QA_ERROR_FORMAT, owner->provider, "ELF loaded provider has no actual committed TLS receipt");
    if (artifact->tls) {
        uint64_t alignment = artifact->tls->alignment ? artifact->tls->alignment : 1;
        uint64_t extent = artifact->tls->memory_bytes;
        if (alignment > 4096 || (alignment & (alignment - 1)) ||
            extent > 0x10000 || tls.prior_used > 0x10000 - extent || tls.prior_module >= 1024)
            return guest_fail(error, QA_ERROR_FORMAT, owner->provider, "ELF loaded TLS differs from its actual static module domain");
        uint64_t next = (tls.prior_used + extent + alignment - 1) & ~(alignment - 1);
        uint64_t thread_pointer = guest_sysv_thread_pointer(owner->runtime);
        if (next > 0x10000 || thread_pointer < next || tls.block.module_id != tls.prior_module + 1 ||
            tls.block.bytes != extent || tls.block.address != thread_pointer - next ||
            tls.block.thread_pointer_offset != -(int64_t)next)
            return guest_fail(error, QA_ERROR_FORMAT, owner->provider, "ELF loaded TLS topology differs from its immutable PT_TLS allocation");
    } else if (tls.block.module_id || tls.block.address || tls.block.bytes || tls.block.thread_pointer_offset)
        return guest_fail(error, QA_ERROR_FORMAT, owner->provider, "ELF loaded provider invents a TLS module absent from its artifact");
    /* Compare declarations and deterministic ordinary addresses in the
     * publication's actual three phase order. IFUNC/GNU-unique resolutions
     * and mutable relocated lifecycle arrays remain runtime-owned. */
    size_t row = 0;
    for (unsigned pass = 0; pass < 3; ++pass)
        for (size_t i = 0; i < artifact->symbol_count; ++i) {
            const guest_elf_symbol *symbol = artifact->symbols + i;
            if (!symbol->name[0] || !symbol->section ||
                (symbol->binding != 1 && symbol->binding != 2 && symbol->binding != 10) ||
                symbol->visibility == 1 || symbol->visibility == 2) continue;
            unsigned phase = symbol->binding == 10 ? 1 : symbol->type == 10 ? 2 : 0;
            if (phase != pass) continue;
            size_t aliases = symbol->version && !symbol->hidden ? 2 : 1;
            for (size_t alias = 0; alias < aliases; ++alias) {
                const char *version = !alias && symbol->version ? symbol->version->name : NULL;
                if (row >= provider->export_count)
                    return guest_fail(error, QA_ERROR_FORMAT, row, "ELF loaded provider omits an actual export declaration");
                const guest_sysv_export *entry = provider->exports + row++;
                if (!text_equal(entry->name, symbol->name) || !text_equal(entry->version, version) ||
                    entry->bytes != symbol->bytes || entry->tls != (symbol->type == 6) ||
                    (entry->tls && entry->tls_offset != symbol->value))
                    return guest_fail(error, QA_ERROR_FORMAT, row - 1, "ELF loaded export differs from its immutable source declaration");
                if (!entry->tls && symbol->binding != 10 && symbol->type != 10) {
                    uint64_t bias = symbol->section == 0xfff1 ? 0 : artifact->bias;
                    if (symbol->value > UINT64_MAX - bias ||
                        entry->address != symbol->value + bias ||
                        (artifact->image.target.pointer_bytes == 4 && entry->address > UINT32_MAX))
                        return guest_fail(error, QA_ERROR_FORMAT, row - 1,
                            "ELF loaded ordinary address differs from its immutable definition");
                }
            }
        }
    return row == provider->export_count ||
        guest_fail(error, QA_ERROR_FORMAT, row, "ELF loaded provider invents an export declaration");
}

enum { ELF_LOADED_HEADER = 40 };

bool guest_elf_loaded_checkpoint(const guest_elf_loaded *owner,
    qa_buffer *out, qa_error *error)
{
    if (!owner || !owner->committed || !out || out->data || out->size ||
        !guest_sysv_idle(owner->runtime) ||
        guest_elf_memory_guest(owner->memory) != guest_sysv_guest(owner->runtime))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF loaded capture requires its actual committed idle process");
    if (!provider_retained(owner, error)) return false;
    qa_buffer memory = {0}, unwind = {0};
    if (!guest_elf_memory_checkpoint(owner->memory, &memory, error)) return false;
    if (!guest_elf_unwind_checkpoint(owner->unwind, &unwind, error)) {
        qa_buffer_free(&memory); return false;
    }
    if (memory.size > SIZE_MAX - ELF_LOADED_HEADER ||
        unwind.size > SIZE_MAX - ELF_LOADED_HEADER - memory.size) {
        qa_buffer_free(&memory); qa_buffer_free(&unwind);
        return guest_fail(error, QA_ERROR_MEMORY, 0, "ELF loaded ownership checkpoint overflows");
    }
    size_t bytes = ELF_LOADED_HEADER + memory.size + unwind.size;
    uint8_t *data = calloc(1, bytes);
    if (!data) {
        qa_buffer_free(&memory); qa_buffer_free(&unwind);
        return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF loaded checkpoint");
    }
    memcpy(data, "QALL", 4); qa_store_u32le(data + 4, 2);
    qa_store_u64le(data + 8, owner->provider); qa_store_u32le(data + 16, 1);
    qa_store_u64le(data + 24, memory.size);
    qa_store_u64le(data + 32, unwind.size);
    memcpy(data + ELF_LOADED_HEADER, memory.data, memory.size);
    memcpy(data + ELF_LOADED_HEADER + memory.size, unwind.data, unwind.size);
    qa_buffer_free(&memory); qa_buffer_free(&unwind);
    *out = (qa_buffer){data, bytes}; return true;
}

bool guest_elf_loaded_adopt(const guest_elf *artifact, guest_sysv_runtime *runtime,
    qa_bytes encoded, guest_elf_loaded **out, qa_error *error)
{
    if (!guest_elf_describe(artifact) || !guest_sysv_idle(runtime) || !out || *out ||
        !encoded.data || encoded.size < ELF_LOADED_HEADER || memcmp(encoded.data, "QALL", 4) ||
        qa_load_u32le(encoded.data + 4) != 2 || !qa_load_u64le(encoded.data + 8) ||
        qa_load_u32le(encoded.data + 16) != 1 || qa_load_u32le(encoded.data + 20) ||
        qa_load_u64le(encoded.data + 24) > encoded.size - ELF_LOADED_HEADER ||
        qa_load_u64le(encoded.data + 32) != encoded.size - ELF_LOADED_HEADER - qa_load_u64le(encoded.data + 24))
        return guest_fail(error, QA_ERROR_FORMAT, 0, "ELF cold loaded adoption requires its exact committed record and attached runtime");
    guest_elf_loaded *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF cold loaded image");
    owner->image = artifact; owner->runtime = runtime; owner->provider = qa_load_u64le(encoded.data + 8);
    size_t memory_bytes = (size_t)qa_load_u64le(encoded.data + 24);
    size_t unwind_bytes = (size_t)qa_load_u64le(encoded.data + 32);
    bool okay = provider_retained(owner, error) && guest_elf_memory_adopt(artifact,
        guest_sysv_guest(runtime), (qa_bytes){encoded.data + ELF_LOADED_HEADER,
            memory_bytes}, &owner->memory, error) &&
        guest_elf_unwind_restore(artifact, guest_sysv_guest(runtime),
            (qa_bytes){encoded.data + ELF_LOADED_HEADER + memory_bytes, unwind_bytes},
            unwind_bytes, &owner->unwind, error);
    if (!okay) { guest_elf_loaded_abandon(&owner); return false; }
    owner->committed = true; *out = owner; return true;
}

#include "sysv_program_private.h"
#include "qa/native_sysv_program_save.h"
#include "qa/source_save.h"

static bool image_fields(qa_source_save_io *io, qa_native_image_info *image)
{
    uint32_t format = image->format, os = image->target.os, arch = image->target.arch, abi = image->target.abi;
    if (!qa_source_save_u32(io, &format) || !qa_source_save_u32(io, &os) ||
        !qa_source_save_u32(io, &arch) || !qa_source_save_u32(io, &abi) ||
        !qa_source_save_u8(io, &image->target.pointer_bytes) ||
        !qa_source_save_u64(io, &image->preferred_base) || !qa_source_save_u64(io, &image->image_bytes) ||
        !qa_source_save_bytes(io, image->digest.bytes, 32)) return false;
    image->format = (qa_native_image_format)format;
    image->target = (qa_native_target){(qa_native_os)os, (qa_native_arch)arch, (qa_native_abi)abi, image->target.pointer_bytes};
    return (format == QA_NATIVE_IMAGE_ELF64 && os == QA_NATIVE_OS_LINUX &&
        arch == QA_NATIVE_ARCH_X86_64 && abi == QA_NATIVE_ABI_SYSTEM_V_X64 && image->target.pointer_bytes == 8) ||
        guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "saved Linux program lost its actual ELF64 ABI");
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = bytes->size;
    if (!qa_source_save_count(io, &count, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || count > io->input.size - io->offset)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Linux program capsule blob is truncated");
        *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count; return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, count);
}
static bool fields(qa_source_save_io *io, qa_native_sysv_program *owner)
{
    uint8_t magic[5] = {'Q','S','P','G',1}; const uint8_t expected[5] = {'Q','S','P','G',1};
    qa_native_sysv_program_options *o = &owner->options; uint32_t backend = o->guest.backend;
    bool opener = o->services.open_file != NULL;
    if (!qa_source_save_bytes(io, magic, 5) || memcmp(magic, expected, 5) ||
        !image_fields(io, &o->guest.image) || !qa_source_save_u32(io, &backend) || backend > QA_NATIVE_GUEST_HOST_X86_64 ||
        !qa_source_save_u64(io, &o->guest.allocation_base) ||
        !qa_source_save_count(io, &o->guest.maximum_backing_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->stack_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->instruction_budget, SIZE_MAX) ||
        !qa_source_save_u32(io, &o->anonymous_permissions) ||
        !qa_source_save_bool(io, &o->read_implies_execute) || !qa_source_save_u64(io, &o->services.id) ||
        !qa_source_save_bool(io, &opener)) return false;
    o->guest.backend = (qa_native_guest_backend)backend;
    if (io->direction == QA_SOURCE_SAVE_READ && opener != (o->services.open_file != NULL))
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Linux restored opener differs from the saved authority");
    uint64_t *words[] = {&owner->stack, &owner->returned, &owner->mapping_cursor, &owner->break_base,
        &owner->current_break, &owner->status.process_id, &owner->status.thread_id,
        &owner->status.clear_child_tid, &owner->status.robust_list};
    for (size_t i = 0; i < sizeof(words) / sizeof(*words); ++i)
        if (!qa_source_save_u64(io, words[i])) return false;
    if (!qa_source_save_u32(io, &owner->status.exit_code) || !qa_source_save_bool(io, &owner->status.exited)) return false;
    if (!o->guest.allocation_base || o->guest.allocation_base % 4096 || !o->guest.maximum_backing_bytes ||
        !o->stack_bytes || o->stack_bytes % 4096 || o->anonymous_permissions > 7 || !o->services.id ||
        (backend == QA_NATIVE_GUEST_HOST_X86_64 ? o->instruction_budget != 0 : !o->instruction_budget) ||
        !owner->stack || owner->returned ||
        !owner->mapping_cursor || owner->mapping_cursor % 4096 || !owner->break_base ||
        owner->break_base % 4096 || owner->current_break < owner->break_base ||
        owner->current_break >= UINT64_C(0x0000800000000000) ||
        !owner->status.process_id || owner->status.process_id > INT32_MAX ||
        !owner->status.thread_id || owner->status.thread_id > INT32_MAX || owner->status.exit_code > 255)
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Linux saved kernel ownership fields are invalid");
    return true;
}
static bool files(qa_source_save_io *io, qa_native_sysv_program *owner)
{
    size_t count = owner->file_count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(*owner->files))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (count > (io->input.size - io->offset) / 31)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Linux file descriptions exceed the actual capsule");
        owner->files = count ? calloc(count, sizeof(*owner->files)) : NULL;
        if (count && !owner->files) return guest_fail(io->error, QA_ERROR_MEMORY, 0, "owning cold Linux file descriptions");
        owner->file_count = owner->file_capacity = count;
    }
    for (size_t i = 0; i < count; ++i) {
        program_file *entry = owner->files + i;
        if (!qa_source_save_u64(io, &entry->capability.capability) ||
            !qa_source_save_u32(io, &entry->capability.mode) || !qa_source_save_u64(io, &entry->offset) ||
            !qa_source_save_u32(io, &entry->flags) || !qa_source_save_count(io, &entry->references, SIZE_MAX) ||
            !qa_source_save_bool(io, &entry->closed) || !qa_source_save_bool(io, &entry->closing)) return false;
        if (!entry->capability.capability || entry->capability.mode > 3 || entry->offset > INT64_MAX ||
            (entry->closed && (!entry->closing || entry->references)) || (entry->closing && entry->references))
            return guest_fail(io->error, QA_ERROR_FORMAT, i, "Linux saved open-description fields are invalid");
        for (size_t j = 0; j < i; ++j)
            if (owner->files[j].capability.capability == entry->capability.capability)
                return guest_fail(io->error, QA_ERROR_FORMAT, i, "Linux descriptions duplicate an actual capability close owner");
    }
    count = owner->descriptor_count;
    if (!qa_source_save_count(io, &count, INT32_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (count > (io->input.size - io->offset) / 13)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Linux descriptor rows exceed the capsule");
        owner->descriptors = count ? calloc(count, sizeof(*owner->descriptors)) : NULL;
        if (count && !owner->descriptors) return guest_fail(io->error, QA_ERROR_MEMORY, 0, "owning cold Linux descriptors");
        owner->descriptor_count = owner->descriptor_capacity = count;
    }
    for (size_t i = 0; i < count; ++i) {
        program_descriptor *fd = owner->descriptors + i;
        if (!qa_source_save_i32(io, &fd->number) || !qa_source_save_count(io, &fd->file, owner->file_count) ||
            !qa_source_save_bool(io, &fd->close_on_exec)) return false;
        if (fd->number < 0 || fd->file >= owner->file_count || owner->files[fd->file].closed || owner->files[fd->file].closing)
            return guest_fail(io->error, QA_ERROR_FORMAT, i, "Linux descriptor lost its actual live open description");
        for (size_t j = 0; j < i; ++j)
            if (owner->descriptors[j].number == fd->number)
                return guest_fail(io->error, QA_ERROR_FORMAT, i, "Linux saved descriptor number repeats");
    }
    for (size_t i = 0; i < owner->file_count; ++i) {
        size_t references = 0;
        for (size_t j = 0; j < owner->descriptor_count; ++j) references += owner->descriptors[j].file == i;
        if (references != owner->files[i].references)
            return guest_fail(io->error, QA_ERROR_FORMAT, i, "Linux saved descriptor aliases lost their shared offset owner");
    }
    return true;
}
static bool storage(qa_native_sysv_program *owner, qa_error *error)
{
    qa_native_allocation_info stack;
    return qa_native_guest_allocation(owner->guest, owner->stack, &stack, error) &&
        ((stack.base == owner->stack && stack.bytes == owner->options.stack_bytes && stack.tag == INT32_C(0x4b535441) &&
          !owner->returned &&
          qa_native_guest_execution(owner->guest) == owner->options.guest.backend) ||
         guest_fail(error, QA_ERROR_FORMAT, owner->stack, "Linux kernel stack/stop lost their actual lower owner"));
}
bool qa_native_sysv_program_checkpoint(qa_native_sysv_program *owner, qa_buffer *out, qa_error *error)
{
    if (!owner || !owner->complete || owner->busy || !out || out->data || out->size ||
        !qa_native_guest_idle(owner->guest) || !program_current(owner, error) || !storage(owner, error)) return false;
    qa_buffer parts[5] = {0}; qa_source_save_io io = {0};
    bool okay = guest_profile_artifacts_checkpoint(owner->provenance, parts, error) &&
        guest_elf_program_checkpoint(owner->startup, parts + 1, error) &&
        guest_elf_memory_checkpoint(owner->memory[0], parts + 2, error) &&
        (!owner->memory[1] || guest_elf_memory_checkpoint(owner->memory[1], parts + 3, error)) &&
        qa_native_guest_checkpoint(owner->guest, parts + 4, error) &&
        qa_source_save_writer(&io, NULL, error) && fields(&io, owner) && files(&io, owner);
    size_t count = owner->options.interpreter.provider ? 2 : 1;
    if (okay) okay = qa_source_save_count(&io, &count, 2);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_sysv_artifact row = i ? owner->options.interpreter : owner->options.program;
        const guest_elf_view *view = guest_elf_describe(owner->artifacts[i]); row.bytes = view->artifact;
        okay = qa_source_save_u64(&io, &row.provider) && qa_source_save_u64(&io, &row.load_bias) &&
            image_fields(&io, &row.image) && qa_source_save_count(&io, &row.maximum_image_bytes, SIZE_MAX) && blob(&io, &row.bytes);
    }
    for (size_t i = 0; okay && i < 5; ++i) {
        qa_bytes part = {parts[i].data, parts[i].size}; okay = blob(&io, &part);
    }
    if (okay) okay = owner->options.services.current(owner->options.services.context, error) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    for (size_t i = 0; i < 5; ++i) qa_buffer_free(parts + i);
    return okay;
}
static bool no_callback(void *context, uint64_t id, uint64_t address,
    qa_native_guest_callback *out, qa_error *error)
{
    (void)context; (void)out;
    qa_error_set(error, QA_ERROR_FORMAT, (size_t)address, "raw Linux program has no source import callback ID %llu", (unsigned long long)id);
    return false;
}
bool qa_native_sysv_program_restore(qa_bytes bytes, const qa_native_sysv_program_restore_bindings *bindings,
    qa_native_sysv_program **out, qa_error *error)
{
    if (!out || *out || !bindings || !bindings->maximum_backing_bytes || !bindings->maximum_image_bytes ||
        !bindings->services.id || !bindings->services.current || !bindings->services.resolve_file ||
        !bindings->services.file_status || !bindings->services.identity || !bindings->services.entropy ||
        !bindings->services.clock || !bindings->services.native_error)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux restore requires actual retained kernel services and empty output");
    qa_native_sysv_program *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning cold Linux program kernel");
    *out = owner; owner->provisional = true; owner->options.services = bindings->services;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, owner) && files(&io, owner);
    if (okay && (owner->options.services.id != bindings->services.id ||
        owner->options.guest.backend != bindings->backend ||
        owner->options.guest.maximum_backing_bytes > bindings->maximum_backing_bytes))
        okay = guest_fail(error, QA_ERROR_FORMAT, 0, "Linux continuation differs from its actual backend/resource authority");
    size_t count = 0;
    if (okay) okay = qa_source_save_count(&io, &count, 2) && count > 0;
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_sysv_artifact *row = i ? &owner->options.interpreter : &owner->options.program;
        row->role = QA_NATIVE_SYSV_PROGRAM;
        okay = qa_source_save_u64(&io, &row->provider) && qa_source_save_u64(&io, &row->load_bias) &&
            image_fields(&io, &row->image) && qa_source_save_count(&io, &row->maximum_image_bytes, bindings->maximum_image_bytes) &&
            blob(&io, &row->bytes) && row->provider && row->maximum_image_bytes &&
            guest_elf_open(row->bytes, &row->image, GUEST_ELF_PROGRAM, row->load_bias,
                row->maximum_image_bytes, &owner->artifacts[i], error);
    }
    qa_bytes parts[5] = {{0}};
    for (size_t i = 0; okay && i < 5; ++i) okay = blob(&io, parts + i);
    if (okay) okay = qa_source_save_finish(&io, NULL) &&
        guest_profile_artifacts_decode(parts[0], bindings->maximum_image_bytes, &owner->provenance, error);
    guest_profile_image images[2] = {
        {.provider = owner->options.program.provider, .kind = GUEST_PROFILE_ELF, .owner.elf = owner->artifacts[0]},
        {.provider = owner->options.interpreter.provider, .kind = GUEST_PROFILE_ELF, .owner.elf = owner->artifacts[1]}};
    if (okay) okay = guest_profile_artifacts_match(owner->provenance, images, count, error) &&
        ((count == 2) == (parts[3].size != 0));
    for (size_t i = 0; okay && i < owner->file_count; ++i) {
        program_file *entry = owner->files + i;
        if (entry->closed) continue;
        qa_native_sysv_file capability = {0};
        okay = bindings->services.resolve_file(bindings->services.context, entry->capability.capability, &capability, error);
        if (okay && (capability.capability != entry->capability.capability || capability.mode != entry->capability.mode ||
            !capability.close || !capability.size || ((capability.mode & 1u) && !capability.read) ||
            ((capability.mode & 2u) && !capability.write)))
            okay = guest_fail(error, QA_ERROR_FORMAT, i, "Linux saved file differs from its retained actual capability");
        if (okay) entry->capability = capability;
    }
    owner->options.guest.host_executable = bindings->host_executable;
    owner->options.guest.profile_guard = bindings->profile_guard;
    if (okay) okay = bindings->services.current(bindings->services.context, error) &&
        qa_native_guest_restore(parts[4], &owner->options.guest, no_callback, owner, &owner->guest, error);
    for (size_t i = 0; okay && i < count; ++i)
        okay = guest_elf_memory_adopt(owner->artifacts[i], owner->guest, parts[i + 2], &owner->memory[i], error);
    guest_elf_program_images startup = {
        .program = {owner->options.program.provider, owner->artifacts[0], owner->memory[0]},
        .interpreter = {owner->options.interpreter.provider, owner->artifacts[1], owner->memory[1]},
        .interpreter_path = guest_elf_describe(owner->artifacts[0]) ? guest_elf_describe(owner->artifacts[0])->interpreter : NULL};
    if (okay) okay = guest_elf_program_adopt(&startup, parts[1], &owner->startup, error) && storage(owner, error);
    const guest_elf_program_view *view = guest_elf_program_describe(owner->startup);
    if (okay && (!view || view->stack != owner->stack || view->stack_bytes != owner->options.stack_bytes ||
        owner->options.program.provider == owner->options.interpreter.provider))
        okay = guest_fail(error, QA_ERROR_FORMAT, 0, "Linux kernel differs from its immutable startup owner");
    owner->options.program.bytes = owner->options.interpreter.bytes = (qa_bytes){0};
    owner->options.guest.host_executable = NULL; owner->options.guest.profile_guard = NULL;
    owner->complete = okay; owner->failed = !okay; qa_source_save_dispose(&io); return okay;
}
bool qa_native_sysv_program_adopt_owned(qa_native_sysv_program *candidate,
    qa_native_sysv_program *previous, qa_error *error)
{
    if (!candidate || !candidate->complete || !candidate->provisional || candidate->busy || candidate->failed ||
        candidate->disposing || !qa_native_guest_idle(candidate->guest) || candidate == previous ||
        (previous && (previous->busy || previous->disposing || !qa_native_guest_idle(previous->guest))))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux publication requires complete stopped independent kernel owners");
    if (!candidate->options.services.current(candidate->options.services.context, error)) return false;
    candidate->provisional = false;
    if (previous) previous->disposing = true;
    return true;
}

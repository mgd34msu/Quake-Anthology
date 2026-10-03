#include "sysv_process_private.h"
#include "sysv_libc_private.h"
#include "qa/native_sysv_process_save.h"

typedef struct saved_process_image {
    qa_native_image_info image;
    uint64_t provider, bias;
    uint32_t role;
    size_t maximum;
    qa_bytes artifact, loaded;
} saved_process_image;

static bool image_fields(qa_source_save_io *io, qa_native_image_info *image)
{
    uint32_t format = image->format, os = image->target.os;
    uint32_t arch = image->target.arch, abi = image->target.abi;
    if (!qa_source_save_u32(io, &format) || !qa_source_save_u32(io, &os) ||
        !qa_source_save_u32(io, &arch) || !qa_source_save_u32(io, &abi) ||
        !qa_source_save_u8(io, &image->target.pointer_bytes) ||
        !qa_source_save_u64(io, &image->preferred_base) ||
        !qa_source_save_u64(io, &image->image_bytes) ||
        !qa_source_save_bytes(io, image->digest.bytes, sizeof(image->digest.bytes))) return false;
    image->format = (qa_native_image_format)format;
    image->target = (qa_native_target){(qa_native_os)os, (qa_native_arch)arch,
        (qa_native_abi)abi, image->target.pointer_bytes};
    bool valid = os == QA_NATIVE_OS_LINUX &&
        ((format == QA_NATIVE_IMAGE_ELF32 && arch == QA_NATIVE_ARCH_I386 &&
          abi == QA_NATIVE_ABI_SYSTEM_V_I386 && image->target.pointer_bytes == 4) ||
         (format == QA_NATIVE_IMAGE_ELF64 && arch == QA_NATIVE_ARCH_X86_64 &&
          abi == QA_NATIVE_ABI_SYSTEM_V_X64 && image->target.pointer_bytes == 8));
    return valid || guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "saved System V process target is not its actual Linux x86 ABI");
}

static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = bytes->size;
    if (!qa_source_save_count(io, &count, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || count > io->input.size - io->offset)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "saved System V process blob is truncated");
        *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count;
        return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, count);
}

static bool process_fields(qa_source_save_io *io, qa_native_sysv_process *owner)
{
    uint8_t magic[] = {'Q','S','V','P'};
    const uint8_t expected[] = {'Q','S','V','P'};
    qa_native_sysv_process_options *o = &owner->options;
    uint64_t *fields[] = {&o->guest.allocation_base, &o->scope, &o->first_function,
        &o->trap_base, &owner->stack, &owner->returned, &o->clock_id,
        &o->standard_handles[0], &o->standard_handles[1], &o->standard_handles[2]};
    uint32_t execution = o->guest.backend;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) ||
        memcmp(magic, expected, sizeof(magic)) || !image_fields(io, &o->guest.image)) return false;
    if (!qa_source_save_u32(io, &execution) || execution > QA_NATIVE_GUEST_HOST_X86_64) return false;
    o->guest.backend = (qa_native_guest_backend)execution;
    for (size_t i = 0; i < sizeof(fields)/sizeof(*fields); ++i)
        if (!qa_source_save_u64(io, fields[i])) return false;
    bool clock = o->time != NULL;
    if (!qa_source_save_count(io, &o->guest.maximum_backing_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->trap_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->stack_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->instruction_budget, SIZE_MAX) ||
        !qa_source_save_count(io, &o->argc, INT32_MAX) ||
        !qa_source_save_count(io, &o->environment_count, SIZE_MAX) ||
        !qa_source_save_u32(io, &o->anonymous_permissions) ||
        !qa_source_save_bool(io, &o->read_implies_execute) ||
        !qa_source_save_bool(io, &o->output_is_terminal) ||
        !qa_source_save_bool(io, &clock)) return false;
    bool native = o->guest.backend == QA_NATIVE_GUEST_HOST_X86_64;
    if (!o->scope || !o->first_function || (native ? o->instruction_budget != 0 : !o->instruction_budget) ||
        (native && (o->guest.image.target.arch != QA_NATIVE_ARCH_X86_64 ||
         o->guest.image.target.abi != QA_NATIVE_ABI_SYSTEM_V_X64 || o->guest.image.target.pointer_bytes != 8)) ||
        !o->stack_bytes || o->stack_bytes % 16 || !owner->stack || !owner->returned ||
        !o->guest.maximum_backing_bytes || !o->guest.allocation_base ||
        o->guest.allocation_base % QA_NATIVE_GUEST_PAGE ||
        !o->trap_base || o->trap_base % QA_NATIVE_GUEST_PAGE || !o->trap_bytes ||
        o->trap_bytes % QA_NATIVE_GUEST_PAGE || o->anonymous_permissions > 7 ||
        !!o->clock_id != clock || owner->stack > UINT64_MAX - o->stack_bytes ||
        (o->guest.image.target.pointer_bytes == 4 && owner->stack + o->stack_bytes > UINT32_MAX))
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "saved System V process storage or capability identities are invalid");
    return true;
}

static bool runtime_matches(const qa_native_sysv_process *owner, qa_error *error)
{
    const qa_native_sysv_process_options *o = &owner->options;
    const guest_sysv_runtime *r = owner->runtime;
    if (!r || r->execution != o->guest.backend || r->options.scope != o->scope || r->options.first_function != o->first_function ||
        r->options.trap_base != o->trap_base || r->options.trap_bytes != o->trap_bytes ||
        r->options.return_trap != owner->returned || r->options.instruction_budget != o->instruction_budget ||
        r->options.argc != o->argc || r->options.environment_count != o->environment_count ||
        r->image_count != owner->image_count)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "System V process differs from its actual runtime construction and provider inventory");
    for (size_t i = 0; i < owner->image_count; ++i)
        if (r->images[i].provider.id != owner->images[i].provider)
            return guest_fail(error, QA_ERROR_FORMAT, i, "System V process provider order differs from its actual runtime");
    return true;
}

static bool allocations_match(const qa_native_sysv_process *owner, qa_error *error)
{
    const uint64_t addresses[] = {owner->stack, owner->returned};
    const size_t sizes[] = {owner->options.stack_bytes, 16};
    const int32_t tags[] = {INT32_C(0x53595053), INT32_C(0x53595052)};
    for (size_t i = 0; i < 2; ++i) {
        qa_native_allocation_info allocation;
        if (!qa_native_guest_allocation(owner->guest, addresses[i], &allocation, error) ||
            allocation.base != addresses[i] || allocation.bytes != sizes[i] || allocation.tag != tags[i] ||
            !guest_range(owner->guest, addresses[i], sizes[i], 0, error))
            return guest_fail(error, QA_ERROR_FORMAT, addresses[i], "System V process lost its actual stack or return allocation");
    }
    uint8_t returned[16];
    if (qa_native_guest_execution(owner->guest) != owner->options.guest.backend ||
        !guest_range(owner->guest, owner->returned, sizeof(returned), QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, error) ||
        !qa_native_guest_read(owner->guest, owner->returned, returned, sizeof(returned), error)) return false;
    for (size_t i = 0; i < sizeof(returned); ++i)
        if (returned[i] != 0xcc)
            return guest_fail(error, QA_ERROR_FORMAT, owner->returned + i, "System V owned return trap changed");
    return true;
}

static bool streams_match(const qa_native_sysv_process *owner, qa_error *error)
{
    for (size_t i = 0; i < 3; ++i) {
        uint64_t handle = owner->options.standard_handles[i];
        if (!handle) continue;
        guest_runtime_file_view view;
        uint32_t mode = i == 0 ? GUEST_RUNTIME_FILE_READ : GUEST_RUNTIME_FILE_WRITE;
        if (!guest_runtime_resources_find(owner->resources, handle, &view, error) ||
            view.closed || (view.mode & mode) != mode ||
            (i && handle == owner->options.standard_handles[0]))
            return guest_fail(error, QA_ERROR_FORMAT, handle, "System V saved standard stream has no actual live file authority");
    }
    return true;
}

bool qa_native_sysv_process_checkpoint(qa_native_sysv_process *owner, qa_buffer *out, qa_error *error)
{
    if (!qa_native_sysv_process_idle(owner) || !out || out->data || out->size)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V process capture requires complete idle ownership and empty output");
    owner->busy = true;
    qa_source_save_io io;
    qa_buffer runtime = {0}, resources = {0}, guest = {0}, profile = {0};
    bool okay = sysv_process_current(owner, error) && runtime_matches(owner, error) &&
        allocations_match(owner, error) && streams_match(owner, error) &&
        sysv_process_profile(owner, false, error) &&
        guest_profile_artifacts_checkpoint(owner->profile, &profile, error) &&
        guest_sysv_checkpoint(owner->runtime, &runtime, error) &&
        guest_runtime_resources_checkpoint(owner->resources, &resources, error) &&
        qa_native_guest_checkpoint(owner->guest, &guest, error);
    bool writer = okay && qa_source_save_writer(&io, NULL, error);
    if (writer) {
        qa_bytes runtime_bytes = {runtime.data, runtime.size}, resource_bytes = {resources.data, resources.size};
        qa_bytes guest_bytes = {guest.data, guest.size};
        qa_bytes profile_bytes = {profile.data, profile.size};
        size_t images = owner->image_count;
        okay = process_fields(&io, owner) && qa_source_save_count(&io, &images, SIZE_MAX) &&
            blob(&io, &runtime_bytes) && blob(&io, &resource_bytes) && blob(&io, &guest_bytes) &&
            blob(&io, &profile_bytes);
        for (size_t i = 0; okay && i < owner->image_count; ++i) {
            sysv_process_image *row = owner->images + i;
            const guest_elf_view *actual = guest_elf_describe(row->artifact);
            qa_buffer loaded = {0};
            okay = actual && guest_elf_loaded_checkpoint(row->loaded, &loaded, error);
            if (okay) {
                saved_process_image record = {actual->image, row->provider, actual->bias,
                    (uint32_t)actual->role, actual->bytes.size, actual->artifact, {loaded.data, loaded.size}};
                okay = image_fields(&io, &record.image) && qa_source_save_u64(&io, &record.provider) &&
                    qa_source_save_u64(&io, &record.bias) && qa_source_save_u32(&io, &record.role) &&
                    qa_source_save_count(&io, &record.maximum, SIZE_MAX) && blob(&io, &record.artifact) && blob(&io, &record.loaded);
            }
            qa_buffer_free(&loaded);
        }
        if (okay) okay = qa_source_save_finish(&io, out);
        qa_source_save_dispose(&io);
    } else okay = false;
    qa_buffer_free(&runtime); qa_buffer_free(&resources); qa_buffer_free(&guest); qa_buffer_free(&profile);
    owner->busy = false; return okay;
}

typedef struct file_resolver {
    const qa_native_sysv_process_restore_bindings *bindings;
} file_resolver;
typedef struct callback_resolver {
    guest_sysv_runtime *runtime;
    const qa_native_sysv_process_restore_bindings *bindings;
} callback_resolver;
static bool resolve_callback(void *context, uint64_t id, uint64_t address,
    qa_native_guest_callback *out, qa_error *error)
{
    callback_resolver *resolver = context;
    qa_error runtime_error = {0};
    if (guest_sysv_callback(resolver->runtime, id, address, out, &runtime_error)) return true;
    if (runtime_error.code != QA_ERROR_NOT_FOUND || !resolver->bindings->external_callback) {
        if (error) *error = runtime_error;
        return false;
    }
    return resolver->bindings->external_callback(resolver->bindings->external_context,
        id, address, out, error);
}
static bool resolve_file(void *context, uint64_t id, guest_runtime_file_capability *out, qa_error *error)
{
    file_resolver *resolver = context;
    qa_native_sysv_file actual = {0};
    if (!resolver->bindings->file ||
        !resolver->bindings->file(resolver->bindings->file_context, id, &actual, error)) return false;
    *out = (guest_runtime_file_capability){actual.capability, actual.mode, actual.read,
        actual.write, actual.size, actual.truncate, actual.flush, actual.close, actual.context};
    return true;
}

bool qa_native_sysv_process_restore(qa_bytes encoded,
    const qa_native_sysv_process_restore_bindings *bindings, qa_native_sysv_process **out, qa_error *error)
{
    if (!bindings || !bindings->current || !bindings->maximum_backing_bytes ||
        bindings->backend > QA_NATIVE_GUEST_HOST_X86_64 ||
        (bindings->backend == QA_NATIVE_GUEST_HOST_X86_64 && (!bindings->profile_guard ||
         !bindings->host_executable || !*bindings->host_executable)) ||
        !!bindings->clock_id != (bindings->time != NULL) || !out || *out)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V process restore needs actual prepared capability bindings and empty output");
    qa_native_sysv_process *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning cold System V process");
    owner->busy = true; owner->provisional = true;
    qa_source_save_io io;
    bool reader = qa_source_save_reader(&io, NULL, encoded, error);
    size_t count = 0;
    qa_bytes runtime = {0}, resources = {0}, guest = {0}, profile = {0};
    saved_process_image *records = NULL;
    bool okay = reader && process_fields(&io, owner) &&
        qa_source_save_count(&io, &count, SIZE_MAX / sizeof(*records)) && count &&
        blob(&io, &runtime) && blob(&io, &resources) && blob(&io, &guest) && blob(&io, &profile);
    if (okay && count > (io.input.size - io.offset) / 109)
        okay = guest_fail(error, QA_ERROR_FORMAT, count, "System V artifact count exceeds its actual complete envelope");
    if (okay) {
        records = calloc(count, sizeof(*records));
        if (!records) okay = guest_fail(error, QA_ERROR_MEMORY, count, "owning cold System V artifact records");
    }
    for (size_t i = 0; okay && i < count; ++i) {
        saved_process_image *record = records + i;
        okay = image_fields(&io, &record->image) && qa_source_save_u64(&io, &record->provider) &&
            qa_source_save_u64(&io, &record->bias) && qa_source_save_u32(&io, &record->role) &&
            qa_source_save_count(&io, &record->maximum, SIZE_MAX) && blob(&io, &record->artifact) && blob(&io, &record->loaded);
        if (okay && (!record->provider || record->role > GUEST_ELF_PROGRAM || !record->maximum))
            okay = guest_fail(error, QA_ERROR_FORMAT, i, "System V saved artifact identity is invalid");
        for (size_t j = 0; okay && j < i; ++j)
            if (records[j].provider == record->provider)
                okay = guest_fail(error, QA_ERROR_FORMAT, i, "System V saved artifact provider is repeated");
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    if (reader) qa_source_save_dispose(&io);
    if (okay && (owner->options.guest.backend != bindings->backend ||
        owner->options.guest.maximum_backing_bytes != bindings->maximum_backing_bytes ||
        owner->options.clock_id != bindings->clock_id ||
        owner->options.output_is_terminal != bindings->output_is_terminal))
        okay = guest_fail(error, QA_ERROR_FORMAT, 0, "System V saved process policy differs from its actual external authority");
    owner->options.time = bindings->time; owner->options.current = bindings->current;
    owner->options.open_file = bindings->open_file;
    owner->options.file_context = bindings->file_context;
    owner->options.guest.host_executable = bindings->host_executable;
    owner->options.guest.profile_guard = bindings->profile_guard;
    owner->options.context = bindings->context;
    if (okay) {
        owner->images = calloc(count, sizeof(*owner->images));
        if (!owner->images) okay = guest_fail(error, QA_ERROR_MEMORY, count, "owning cold System V artifact graph");
    }
    bool primary_present = false;
    for (size_t i = 0; okay && i < count; ++i) {
        saved_process_image *record = records + i;
        owner->images[i].provider = record->provider; owner->image_count = i + 1;
        okay = guest_elf_open(record->artifact, &record->image, (guest_elf_role)record->role,
            record->bias, record->maximum, &owner->images[i].artifact, error);
        const qa_native_image_info *a = &record->image, *b = &owner->options.guest.image;
        if (a->format == b->format && a->target.os == b->target.os && a->target.arch == b->target.arch &&
            a->target.abi == b->target.abi && a->target.pointer_bytes == b->target.pointer_bytes &&
            a->preferred_base == b->preferred_base && a->image_bytes == b->image_bytes &&
            qa_sha256_equal(&a->digest, &b->digest)) primary_present = true;
    }
    if (okay && !primary_present)
        okay = guest_fail(error, QA_ERROR_FORMAT, 0, "System V cold process image witness lacks its actual source artifact");
    if (okay) {
        size_t maximum = 0;
        for (size_t i = 0; i < count; ++i)
            if (records[i].maximum > maximum) maximum = records[i].maximum;
        okay = guest_profile_artifacts_decode(profile, maximum, &owner->profile, error) &&
            sysv_process_profile(owner, false, error);
    }
    if (okay) okay = guest_runtime_resources_decode(resources, &owner->resources, error) &&
        streams_match(owner, error);
    guest_sysv_bindings actual_bindings = sysv_process_bindings(owner);
    if (okay) okay = guest_sysv_decode(runtime, &owner->options.guest.image.target,
        &actual_bindings, &owner->runtime, error) && runtime_matches(owner, error);
    bool rebound = false;
    if (okay) {
        file_resolver resolver = {bindings};
        okay = guest_runtime_resources_rebind(owner->resources, resolve_file, &resolver, error);
        rebound = okay;
    }
    if (okay) {
        callback_resolver resolver = {owner->runtime, bindings};
        okay = qa_native_guest_restore(guest, &owner->options.guest,
            resolve_callback, &resolver, &owner->guest, error);
    }
    if (okay) okay = allocations_match(owner, error) && guest_sysv_attach(owner->runtime, owner->guest, error);
    for (size_t i = 0; okay && i < owner->image_count; ++i) {
        okay = guest_elf_loaded_adopt(owner->images[i].artifact, owner->runtime,
            records[i].loaded, &owner->images[i].loaded, error);
        if (okay && guest_elf_loaded_provider(owner->images[i].loaded) != owner->images[i].provider)
            okay = guest_fail(error, QA_ERROR_FORMAT, i, "System V cold loaded image changed its actual provider identity");
    }
    free(records);
    owner->options.artifact_count = owner->image_count;
    owner->options.file_count = guest_runtime_resources_count(owner->resources);
    owner->options.guest.host_executable = NULL;
    owner->options.guest.profile_guard = NULL;
    owner->complete = okay; owner->failed = !okay; owner->busy = false;
    if (okay) { *out = owner; return true; }
    if (!rebound) guest_runtime_resources_abandon(&owner->resources);
    qa_error cleanup = {0};
    if (!qa_native_sysv_process_dispose(&owner, &cleanup)) *out = owner;
    if (error && error->code == QA_OK)
        guest_fail(error, QA_ERROR_FORMAT, 0, "System V process capsule could not be admitted completely");
    return false;
}

static bool process_adopt(qa_native_sysv_process *candidate,
    qa_native_sysv_process *previous, bool independent, qa_error *error)
{
    if (!candidate || candidate == previous || !candidate->provisional || !candidate->complete ||
        candidate->failed || candidate->busy || candidate->disposing ||
        !guest_sysv_idle(candidate->runtime) || !qa_native_guest_idle(candidate->guest) ||
        !guest_runtime_resources_idle(candidate->resources) ||
        (previous && !qa_native_sysv_process_idle(previous)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V publication requires complete quiescent candidate and previous owners");
    if (!guest_runtime_resources_adopt(candidate->resources,
            !independent && previous ? previous->resources : NULL, error)) return false;
    candidate->provisional = false;
    if (previous) previous->disposing = true;
    return true;
}
bool qa_native_sysv_process_adopt(qa_native_sysv_process *candidate,
    qa_native_sysv_process *previous, qa_error *error)
{ return process_adopt(candidate, previous, false, error); }
bool qa_native_sysv_process_adopt_owned(qa_native_sysv_process *candidate,
    qa_native_sysv_process *previous, qa_error *error)
{ return process_adopt(candidate, previous, true, error); }

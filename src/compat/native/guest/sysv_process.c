#include "sysv_process_private.h"

bool sysv_process_current(qa_native_sysv_process *owner, qa_error *error)
{
    if (!owner || !owner->guest || owner->failed || owner->disposing || !owner->options.current)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V process has no actual current resource authority");
    if (!guest_ready(owner->guest, error)) return false;
    return owner->options.current(owner->options.context, error);
}

static bool runtime_current(void *context, const qa_native_guest *guest, qa_error *error)
{
    qa_native_sysv_process *owner = context;
    if (!owner || guest != owner->guest)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V runtime belongs to a different process");
    return sysv_process_current(owner, error);
}

static bool stream_read(void *context, void *data, size_t bytes, size_t *completed, qa_error *error)
{
    sysv_process_stream *stream = context;
    return sysv_process_current(stream->process, error) &&
        guest_runtime_resources_read(stream->process->resources, stream->handle, data, bytes, completed, error);
}
static bool stream_write(void *context, qa_bytes bytes, size_t *completed, qa_error *error)
{
    sysv_process_stream *stream = context;
    return sysv_process_current(stream->process, error) &&
        guest_runtime_resources_write(stream->process->resources, stream->handle, bytes, completed, error);
}
static bool stream_flush(void *context, qa_error *error)
{
    sysv_process_stream *stream = context;
    return sysv_process_current(stream->process, error) &&
        guest_runtime_resources_flush(stream->process->resources, stream->handle, error);
}
static bool clock_read(void *context, int64_t *seconds, qa_error *error)
{
    qa_native_sysv_process *owner = context;
    return sysv_process_current(owner, error) &&
        owner->options.time(owner->options.context, seconds, error);
}
static bool open_file(void *context, const char *name, uint32_t mode, uint32_t creation,
    uint64_t *handle, bool *opened, qa_error *error)
{
    qa_native_sysv_process *owner = context;
    qa_native_sysv_file actual = {0};
    *opened = false;
    if (!sysv_process_current(owner, error)) return false;
    bool okay = owner->options.open_file(owner->options.file_context, name,
        mode, creation, &actual, opened, error);
    qa_error first = error ? *error : (qa_error){0};
    if (*opened) {
        guest_runtime_file_capability capability = {actual.capability, actual.mode,
            actual.read, actual.write, actual.size, actual.truncate, actual.flush,
            actual.close, actual.context};
        if (!guest_runtime_resources_file(owner->resources, actual.handle,
                actual.name, actual.creation, &capability, error)) return false;
        owner->options.file_count = guest_runtime_resources_count(owner->resources);
        *handle = actual.handle;
    }
    if (!okay) { if (error) *error = first; return false; }
    if (!*opened)
        return guest_fail(error, QA_ERROR_NOT_FOUND, 0, "System V opener found no contained file");
    return sysv_process_current(owner, error);
}

guest_sysv_bindings sysv_process_bindings(qa_native_sysv_process *owner)
{
    guest_sysv_bindings bindings = {.clock_id = owner->options.clock_id,
        .resources = owner->resources,
        .open_file = owner->options.open_file ? open_file : NULL,
        .time = owner->options.time ? clock_read : NULL,
        .output_is_terminal = owner->options.output_is_terminal,
        .current = runtime_current, .context = owner};
    for (size_t i = 0; i < 3; ++i) {
        owner->streams[i] = (sysv_process_stream){owner, owner->options.standard_handles[i]};
        if (!owner->streams[i].handle) continue;
        bindings.streams[i] = (guest_sysv_stream){owner->streams[i].handle,
            i == 0 ? stream_read : NULL, i == 0 ? NULL : stream_write,
            stream_flush, &owner->streams[i]};
        for (size_t j = 1; j < i; ++j)
            if (owner->streams[j].handle == owner->streams[i].handle)
                bindings.streams[i].context = &owner->streams[j];
    }
    return bindings;
}

static bool same_target(const qa_native_target *a, const qa_native_target *b)
{
    return a->os == b->os && a->arch == b->arch && a->abi == b->abi &&
        a->pointer_bytes == b->pointer_bytes;
}
static bool same_image(const qa_native_image_info *a, const qa_native_image_info *b)
{
    return same_target(&a->target, &b->target) && a->format == b->format &&
        a->preferred_base == b->preferred_base && a->image_bytes == b->image_bytes &&
        qa_sha256_equal(&a->digest, &b->digest);
}
static bool options_valid(const qa_native_sysv_process_options *options, qa_error *error)
{
    if (!options || !options->current || !options->scope || !options->first_function ||
        !options->stack_bytes || options->stack_bytes % 16 ||
        !options->artifact_count || !options->artifacts ||
        options->artifact_count > SIZE_MAX / sizeof(sysv_process_image) ||
        (options->file_count && !options->files) || options->anonymous_permissions > 7 ||
        !!options->clock_id != (options->time != NULL) || options->guest.image.target.os != QA_NATIVE_OS_LINUX)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V process requires actual artifacts, storage and prepared capabilities");
    bool emulated = options->guest.backend == QA_NATIVE_GUEST_EMULATED;
    bool native = options->guest.backend == QA_NATIVE_GUEST_HOST_X86_64;
    if ((!emulated && !native) || (emulated && !options->instruction_budget) ||
        (native && (options->instruction_budget || !options->guest.profile_guard || !options->guest.host_executable ||
         !*options->guest.host_executable || options->guest.image.target.arch != QA_NATIVE_ARCH_X86_64 ||
         options->guest.image.target.abi != QA_NATIVE_ABI_SYSTEM_V_X64 ||
         options->guest.image.target.pointer_bytes != 8)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V process requires its explicit backend and actual call policy");
    bool primary_present = false;
    for (size_t i = 0; i < options->artifact_count; ++i) {
        const qa_native_sysv_artifact *image = options->artifacts + i;
        if (!image->provider || image->role < QA_NATIVE_SYSV_LIBRARY ||
            image->role > QA_NATIVE_SYSV_PROGRAM || !image->maximum_image_bytes ||
            !same_target(&image->image.target, &options->guest.image.target))
            return guest_fail(error, QA_ERROR_ARGUMENT, i, "System V artifact belongs to a different actual process target");
        for (size_t j = 0; j < i; ++j)
            if (options->artifacts[j].provider == image->provider)
                return guest_fail(error, QA_ERROR_ARGUMENT, i, "System V artifact provider identity is repeated");
        if (same_image(&image->image, &options->guest.image)) primary_present = true;
    }
    if (!primary_present)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V process image witness has no actual source artifact");
    for (size_t i = 0; i < 3; ++i) {
        if (!options->standard_handles[i]) continue;
        const qa_native_sysv_file *file = NULL;
        for (size_t j = 0; j < options->file_count; ++j)
            if (options->files[j].handle == options->standard_handles[i]) file = options->files + j;
        uint32_t mode = i == 0 ? QA_NATIVE_SYSV_FILE_READ : QA_NATIVE_SYSV_FILE_WRITE;
        if (!file || (file->mode & mode) != mode)
            return guest_fail(error, QA_ERROR_ARGUMENT, i, "System V standard stream lacks its actual file mode");
        if (i && options->standard_handles[0] == options->standard_handles[i])
            return guest_fail(error, QA_ERROR_ARGUMENT, i, "System V input and output handles have different actual stream operations");
    }
    return true;
}

bool sysv_process_profile(qa_native_sysv_process *owner, bool create, qa_error *error)
{
    if (!owner || !owner->image_count || owner->image_count > SIZE_MAX / sizeof(guest_profile_image))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V profile requires its actual ordered artifacts");
    guest_profile_image *images = calloc(owner->image_count, sizeof(*images));
    if (!images) return guest_fail(error, QA_ERROR_MEMORY, 0, "qualifying System V artifact provenance");
    for (size_t i = 0; i < owner->image_count; ++i) {
        images[i].provider = owner->images[i].provider;
        images[i].kind = GUEST_PROFILE_ELF;
        images[i].owner.elf = owner->images[i].artifact;
    }
    bool okay = create ? guest_profile_artifacts_create(images, owner->image_count, &owner->profile, error) :
        guest_profile_artifacts_match(owner->profile, images, owner->image_count, error);
    free(images); return okay;
}

bool qa_native_sysv_process_create(const qa_native_sysv_process_options *options,
    qa_native_sysv_process **out, qa_error *error)
{
    if (!out || *out || !options_valid(options, error)) return false;
    qa_native_sysv_process *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning System V process");
    owner->options = *options; owner->busy = true;
    owner->images = calloc(options->artifact_count, sizeof(*owner->images));
    if (!owner->images) { free(owner); return guest_fail(error, QA_ERROR_MEMORY, 0, "owning System V artifact graph"); }
    *out = owner;
    bool okay = true;
    for (size_t i = 0; okay && i < options->artifact_count; ++i) {
        const qa_native_sysv_artifact *image = options->artifacts + i;
        sysv_process_image *row = owner->images + i;
        row->provider = image->provider; owner->image_count = i + 1;
        okay = guest_elf_open(image->bytes, &image->image,
            image->role == QA_NATIVE_SYSV_LIBRARY ? GUEST_ELF_LIBRARY : GUEST_ELF_PROGRAM,
            image->load_bias, image->maximum_image_bytes, &row->artifact, error);
    }
    if (okay) okay = sysv_process_profile(owner, true, error) &&
        sysv_process_profile(owner, false, error);
    if (okay) okay = guest_runtime_resources_create(&owner->resources, error);
    for (size_t i = 0; okay && i < options->file_count; ++i) {
        const qa_native_sysv_file *file = options->files + i;
        guest_runtime_file_capability capability = {file->capability, file->mode,
            file->read, file->write, file->size, file->truncate, file->flush, file->close, file->context};
        okay = guest_runtime_resources_file(owner->resources, file->handle,
            file->name, file->creation, &capability, error);
    }
    if (okay) okay = qa_native_guest_create(&options->guest, &owner->guest, error);
    if (okay) okay = qa_native_guest_allocate_aligned(owner->guest, options->stack_bytes,
        16, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, INT32_C(0x53595053), &owner->stack, error) &&
        qa_native_guest_allocate_aligned(owner->guest, 16, 16,
            QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, INT32_C(0x53595052), &owner->returned, error);
    if (okay) {
        uint8_t returned[16]; memset(returned, 0xcc, sizeof(returned));
        okay = qa_native_guest_write(owner->guest, owner->returned,
            (qa_bytes){returned, sizeof(returned)}, error) &&
            qa_native_guest_protect_range(owner->guest, owner->returned, QA_NATIVE_GUEST_PAGE,
                QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, error);
    }
    if (okay && (owner->stack > UINT64_MAX - options->stack_bytes ||
        (options->guest.image.target.pointer_bytes == 4 && owner->stack + options->stack_bytes > UINT32_MAX)))
        okay = guest_fail(error, QA_ERROR_ARGUMENT, owner->stack, "System V fresh stack continuation exceeds its actual processor width");
    qa_native_guest_cpu cpu;
    if (okay) okay = qa_native_guest_cpu_read(owner->guest, &cpu, error);
    if (okay) {
        cpu.instruction = 0;
        if (options->guest.backend == QA_NATIVE_GUEST_EMULATED) cpu.flags = 2;
        memset(cpu.registers, 0, sizeof(cpu.registers));
        memset(cpu.fp_mantissa, 0, sizeof(cpu.fp_mantissa));
        memset(cpu.fp_exponent, 0, sizeof(cpu.fp_exponent));
        memset(cpu.xmm, 0, sizeof(cpu.xmm));
        cpu.registers[QA_NATIVE_RSP] = owner->stack + options->stack_bytes;
        cpu.fp_control = 0x37f; cpu.fp_status = 0; cpu.fp_tags = 0xffff; cpu.mxcsr = 0x1f80;
        cpu.fp_instruction = cpu.fp_operand = 0;
        cpu.fp_code_selector = cpu.fp_data_selector = cpu.fp_opcode = 0;
        okay = qa_native_guest_cpu_write(owner->guest, &cpu, error);
    }
    guest_sysv_options runtime = {.scope = options->scope, .first_function = options->first_function,
        .trap_base = options->trap_base, .trap_bytes = options->trap_bytes,
        .return_trap = owner->returned, .instruction_budget = options->instruction_budget,
        .argv = options->argv, .argc = options->argc, .environment = options->environment,
        .environment_count = options->environment_count, .bindings = sysv_process_bindings(owner)};
    if (okay) okay = guest_sysv_create(owner->guest, &runtime, &owner->runtime, error);
    for (size_t i = 0; okay && i < owner->image_count; ++i) {
        guest_elf_load_options load = {.provider = owner->images[i].provider,
            .return_trap = owner->returned, .instruction_budget = options->instruction_budget,
            .memory = {options->anonymous_permissions, options->read_implies_execute}};
        okay = guest_elf_load(owner->images[i].artifact, owner->runtime, &load,
            &owner->images[i].loaded, error);
    }
    owner->options.artifacts = NULL; owner->options.files = NULL;
    owner->options.argv = NULL; owner->options.environment = NULL;
    owner->options.guest.host_executable = NULL;
    owner->options.guest.profile_guard = NULL;
    owner->failed = !okay; owner->complete = okay; owner->busy = false;
    return okay;
}

bool qa_native_sysv_process_idle(const qa_native_sysv_process *owner)
{
    return owner && owner->complete && !owner->busy && !owner->failed && !owner->disposing &&
        !owner->provisional && guest_sysv_idle(owner->runtime) &&
        guest_runtime_resources_idle(owner->resources) && qa_native_guest_idle(owner->guest);
}
qa_native_guest *qa_native_sysv_process_guest(const qa_native_sysv_process *owner)
{ return owner && !owner->failed && !owner->disposing ? owner->guest : NULL; }

static bool begin(qa_native_sysv_process *owner, qa_error *error)
{
    if (!qa_native_sysv_process_idle(owner))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V operation requires its complete idle process");
    owner->busy = true;
    if (!sysv_process_current(owner, error)) { owner->busy = false; return false; }
    return true;
}
static bool end(qa_native_sysv_process *owner, bool okay)
{
    owner->busy = false;
    if (!okay && owner->guest->failed) owner->failed = true;
    return okay;
}

bool qa_native_sysv_process_initialize(qa_native_sysv_process *owner, uint64_t provider, qa_error *error)
{
    if (!begin(owner, error)) return false;
    return end(owner, guest_sysv_initialize(owner->runtime, provider, owner->options.instruction_budget, error));
}
bool qa_native_sysv_process_reload(qa_native_sysv_process *owner, uint64_t provider, qa_error *error)
{
    sysv_process_image *row = NULL;
    for (size_t i = 0; owner && i < owner->image_count; ++i)
        if (owner->images[i].provider == provider) row = owner->images + i;
    const guest_elf_view *image = row ? guest_elf_describe(row->artifact) : NULL;
    if (!image || image->role != GUEST_ELF_LIBRARY || !row->loaded)
        return guest_fail(error, QA_ERROR_ARGUMENT, provider, "System V reload requires its actual retained library attachment");
    if (!begin(owner, error)) return false;
    bool okay = guest_sysv_finalize(owner->runtime, provider, owner->options.instruction_budget, error) &&
        guest_sysv_finalize_image_destructors(owner->runtime, provider, image->bias + image->first,
            image->end - image->first, error) && guest_elf_loaded_unmap(row->loaded, error);
    if (okay) {
        guest_elf_loaded_abandon(&row->loaded);
        guest_elf_load_options load = {.provider = provider, .return_trap = owner->returned,
            .instruction_budget = owner->options.instruction_budget,
            .memory = {owner->options.anonymous_permissions, owner->options.read_implies_execute},
            .replacing = true};
        okay = guest_elf_load(row->artifact, owner->runtime, &load, &row->loaded, error) &&
            guest_sysv_initialize(owner->runtime, provider, owner->options.instruction_budget, error);
    }
    if (!okay) owner->guest->failed = true;
    return end(owner, okay);
}
bool qa_native_sysv_process_finalize(qa_native_sysv_process *owner, uint64_t provider, qa_error *error)
{
    if (!begin(owner, error)) return false;
    return end(owner, guest_sysv_finalize(owner->runtime, provider, owner->options.instruction_budget, error));
}
bool qa_native_sysv_process_finalize_destructors(qa_native_sysv_process *owner, uint64_t dso, qa_error *error)
{
    if (!begin(owner, error)) return false;
    return end(owner, guest_sysv_finalize_destructors(owner->runtime, dso, error));
}
bool qa_native_sysv_process_finalize_all(qa_native_sysv_process *owner, qa_error *error)
{
    if (!begin(owner, error)) return false;
    return end(owner, guest_sysv_finalize_all(owner->runtime, owner->options.instruction_budget, error));
}
bool qa_native_sysv_process_file_add(qa_native_sysv_process *owner, const qa_native_sysv_file *file, qa_error *error)
{
    if (!file) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V file registration needs its actual acquired capability");
    if (!begin(owner, error)) return false;
    guest_runtime_file_capability capability = {file->capability, file->mode,
        file->read, file->write, file->size, file->truncate, file->flush, file->close, file->context};
    bool okay = guest_runtime_resources_file(owner->resources, file->handle, file->name,
        file->creation, &capability, error);
    if (okay) owner->options.file_count = guest_runtime_resources_count(owner->resources);
    return end(owner, okay);
}
bool qa_native_sysv_process_file_close(qa_native_sysv_process *owner, uint64_t handle, qa_error *error)
{
    if (!owner || !handle) return guest_fail(error, QA_ERROR_ARGUMENT, handle, "System V close requires its actual file handle");
    for (size_t i = 0; i < 3; ++i)
        if (owner->options.standard_handles[i] == handle)
            return guest_fail(error, QA_ERROR_ARGUMENT, handle, "temporary file retirement cannot remove a standard stream");
    if (!begin(owner, error)) return false;
    if (guest_sysv_file_in_use(owner->runtime, handle)) {
        guest_fail(error, QA_ERROR_ARGUMENT, handle, "System V FILE still owns this capability");
        return end(owner, false);
    }
    return end(owner, guest_runtime_resources_close(owner->resources, handle, error));
}
bool qa_native_sysv_process_file_remove(qa_native_sysv_process *owner, uint64_t handle, qa_error *error)
{
    if (!owner || !handle) return guest_fail(error, QA_ERROR_ARGUMENT, handle, "System V removal requires its actual file handle");
    for (size_t i = 0; i < 3; ++i)
        if (owner->options.standard_handles[i] == handle)
            return guest_fail(error, QA_ERROR_ARGUMENT, handle, "temporary file removal cannot erase a standard stream");
    if (!begin(owner, error)) return false;
    if (guest_sysv_file_in_use(owner->runtime, handle)) {
        guest_fail(error, QA_ERROR_ARGUMENT, handle, "System V FILE still owns this capability");
        return end(owner, false);
    }
    bool okay = guest_runtime_resources_remove_closed(owner->resources, handle, error);
    if (okay) owner->options.file_count = guest_runtime_resources_count(owner->resources);
    return end(owner, okay);
}
bool qa_native_sysv_process_artifact_read(const qa_native_sysv_process *owner, uint64_t provider,
    qa_native_sysv_artifact *out, qa_error *error)
{
    if (!owner || !provider || !out)
        return guest_fail(error, QA_ERROR_ARGUMENT, provider, "System V artifact lookup requires its retained owner and provider");
    for (size_t i = 0; i < owner->image_count; ++i) {
        const sysv_process_image *image = owner->images + i;
        if (image->provider != provider) continue;
        const guest_elf_view *view = guest_elf_describe(image->artifact);
        if (!view) break;
        *out = (qa_native_sysv_artifact){.provider = provider, .load_bias = view->bias,
            .role = view->role == GUEST_ELF_LIBRARY ? QA_NATIVE_SYSV_LIBRARY : QA_NATIVE_SYSV_PROGRAM,
            .image = view->image, .bytes = view->artifact, .maximum_image_bytes = view->bytes.size};
        return true;
    }
    return guest_fail(error, QA_ERROR_NOT_FOUND, provider, "System V retained artifact provider is absent");
}

static bool process_invoke(qa_native_sysv_process *owner, uint64_t original, uint64_t target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    if (!owner || !signature || signature->variadic ||
        signature->abi != owner->options.guest.image.target.abi)
        return guest_fail(error, QA_ERROR_ARGUMENT, target, "System V invocation needs the actual fixed source ABI");
    guest_abi_plan *plan = NULL;
    if (!guest_abi_plan_native(signature, NULL, 0, &plan, error)) return false;
    bool enclosing_busy = owner->busy;
    bool nested = owner->guest &&
        ((enclosing_busy&&owner->guest->run&&owner->guest->callback_depth)||
         (owner->guest->publication_depth&&owner->guest->stopped_write_calls)) &&
        (!owner->guest->publication_depth || owner->guest->stopped_write_calls);
    bool okay;
    if (nested) {
        okay = !owner->failed && !owner->disposing && !owner->provisional &&
            guest_mutable(owner->guest, error) && sysv_process_current(owner, error);
        if (!okay && error && error->code == QA_OK)
            guest_fail(error, QA_ERROR_ARGUMENT, target, "System V nested invocation has no actual stopped process scope");
    } else if(!enclosing_busy&&owner->complete&&!owner->failed&&!owner->disposing&&!owner->provisional&&
        guest_call_prepared(owner->guest)&&guest_sysv_idle(owner->runtime)&&
        guest_runtime_resources_idle(owner->resources)) {
        okay=sysv_process_current(owner,error);
        if(okay)owner->busy=true;
    } else okay = begin(owner, error);
    if (okay) {
        owner->busy=true;
        bool invoked = owner->options.guest.backend == QA_NATIVE_GUEST_HOST_X86_64 ?
            guest_abi_invoke_native(plan, owner->guest, target, owner->returned,
                arguments, count, result, error) :
            original ? guest_abi_invoke_original(plan, owner->guest, original, target, owner->returned,
                arguments, count, result, owner->options.instruction_budget, error) :
            guest_abi_invoke(plan, owner->guest, target, owner->returned,
                arguments, count, result, owner->options.instruction_budget, error);
        owner->busy = enclosing_busy;
        if (!invoked && owner->guest->failed) owner->failed = true;
        okay = invoked;
    }
    guest_abi_plan_destroy(plan); return okay;
}
bool qa_native_sysv_process_invoke(qa_native_sysv_process *owner, uint64_t target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{ return process_invoke(owner, 0, target, signature, arguments, count, result, error); }
bool qa_native_sysv_process_invoke_original(qa_native_sysv_process *owner, uint64_t id, uint64_t target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    if (!owner || !id || owner->options.guest.backend != QA_NATIVE_GUEST_EMULATED)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, target, "original observed entry requires its actual emulated store scope");
    return process_invoke(owner, id, target, signature, arguments, count, result, error);
}

bool qa_native_sysv_process_export(const qa_native_sysv_process *owner, uint64_t provider,
    const char *name, const char *version, uint64_t *out, qa_error *error)
{
    if (!owner || !owner->complete || !name || !out || owner->disposing)
        return guest_fail(error, QA_ERROR_ARGUMENT, provider, "System V export needs its retained artifact identity");
    const guest_sysv_provider *image = guest_sysv_provider_read(owner->runtime, provider);
    for (size_t i = 0; image && i < image->export_count; ++i) {
        const guest_sysv_export *symbol = image->exports + i;
        bool same_version = version == symbol->version ||
            (version && symbol->version && !strcmp(version, symbol->version));
        if (!symbol->tls && !strcmp(name, symbol->name) && same_version) {
            *out = symbol->address; return true;
        }
    }
    return guest_fail(error, QA_ERROR_NOT_FOUND, provider, "System V actual provider has no matching address export");
}

bool qa_native_sysv_process_dispose(qa_native_sysv_process **pointer, qa_error *error)
{
    if (!pointer) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V disposal needs its owning pointer");
    qa_native_sysv_process *owner = *pointer;
    if (!owner) return true;
    if (owner->busy || (owner->guest && (owner->guest->run || owner->guest->stepping ||
        owner->guest->callback_depth || owner->guest->publication_depth)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "System V disposal requires drained source and capability callbacks");
    if (owner->runtime && owner->guest && !owner->disposing && !owner->failed &&
        !owner->guest->failed && !owner->provisional) {
        owner->busy = true;
        bool retired = guest_sysv_retire(owner->runtime, error);
        owner->busy = false;
        if (!retired) return false;
    }
    owner->disposing = true; owner->busy = true;
    if (!qa_native_guest_destroy(&owner->guest, error)) { owner->busy = false; return false; }
    /* Runtime callbacks and loaded-image metadata survive a rejected CPU
     * close. Once the actual CPU is gone no callback can borrow them. */
    for (size_t i = 0; i < owner->image_count; ++i) guest_elf_loaded_abandon(&owner->images[i].loaded);
    guest_sysv_abandon(&owner->runtime);
    if (!guest_runtime_resources_destroy(&owner->resources, error)) { owner->busy = false; return false; }
    for (size_t i = 0; i < owner->image_count; ++i) guest_elf_close(&owner->images[i].artifact);
    guest_profile_artifacts_destroy(&owner->profile);
    free(owner->images); free(owner); *pointer = NULL; return true;
}

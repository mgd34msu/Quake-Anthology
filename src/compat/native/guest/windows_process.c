#include "windows_process_private.h"
#include "windows_kernel.h"
#include "windows_stdio.h"

uint64_t qa_native_windows_process_callback_minimum(void)
{ return GUEST_WINDOWS_CALLBACK_MINIMUM; }

bool windows_process_current(qa_native_windows_process *owner, qa_error *error)
{
    if (!owner || !owner->guest || owner->failed || owner->disposing ||
        !owner->options.capabilities.current)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows process lost its actual resource authority");
    return guest_ready(owner->guest, error) &&
        owner->options.capabilities.current(owner->options.capabilities.context, error);
}

static guest_runtime_file_capability file_capability(const qa_native_windows_file *file)
{
    return (guest_runtime_file_capability){file->id, file->mode, file->read, file->write,
        file->size, file->truncate, file->flush, file->close, file->context};
}
static bool entropy(void *context, void *data, size_t bytes, qa_error *error)
{
    qa_native_windows_process *owner = context;
    return windows_process_current(owner, error) &&
        owner->options.capabilities.entropy(owner->options.capabilities.context, data, bytes, error);
}
static bool milliseconds(void *context, int64_t *out, qa_error *error)
{
    qa_native_windows_process *owner = context;
    return windows_process_current(owner, error) &&
        owner->options.capabilities.milliseconds(owner->options.capabilities.context, out, error);
}
static bool performance(void *context, int64_t *out, qa_error *error)
{
    qa_native_windows_process *owner = context;
    return windows_process_current(owner, error) &&
        owner->options.capabilities.performance(owner->options.capabilities.context, out, error);
}
static bool calendar(void *context, int64_t when, bool local, guest_windows_calendar *out, qa_error *error)
{
    qa_native_windows_process *owner = context; qa_native_windows_calendar actual = {0};
    if (!windows_process_current(owner, error) ||
        !owner->options.capabilities.calendar(owner->options.capabilities.context, when, local, &actual, error)) return false;
    *out = (guest_windows_calendar){actual.year, actual.month, actual.weekday, actual.day,
        actual.hour, actual.minute, actual.second, actual.millisecond, actual.year_day,
        actual.daylight, actual.timezone_minutes};
    return true;
}
static bool compare_string(void *context, uint32_t locale, uint32_t flags, bool wide,
    const uint16_t *first, size_t first_count, const uint16_t *second, size_t second_count,
    int32_t *out, uint32_t *source_error, qa_error *error)
{
    qa_native_windows_process *owner = context;
    return windows_process_current(owner,error) &&
        owner->options.capabilities.compare_string(owner->options.capabilities.context,locale,flags,wide,
            first,first_count,second,second_count,out,source_error,error);
}
static bool open_file(void *context, const char *path, uint32_t mode, uint32_t creation,
    guest_runtime_file_capability *out, bool *opened, qa_error *error)
{
    qa_native_windows_process *owner = context; qa_native_windows_file actual = {0};
    *opened = false;
    if (!windows_process_current(owner, error)) return false;
    bool okay = owner->options.capabilities.open_file(owner->options.capabilities.context,
        path, mode, creation, &actual, opened, error);
    /* A failed open can still transfer a real close owner. */
    *out = file_capability(&actual); return okay;
}
static bool resolve_file(void *context, uint64_t id, guest_runtime_file_capability *out, qa_error *error)
{
    qa_native_windows_process *owner = context; qa_native_windows_file actual = {0};
    /* Detached decode has no CPU yet. This is a pure borrow from the prepared
     * external graph, not a process operation or file opening. */
    if (!owner->options.capabilities.resolve_file ||
        !owner->options.capabilities.resolve_file(owner->options.capabilities.context, id, &actual, error)) return false;
    *out = file_capability(&actual); return true;
}
static bool stream_read(void *context, void *data, size_t bytes, size_t *done, qa_error *error)
{
    windows_process_stream *stream = context; qa_native_windows_process *owner = stream->process;
    const qa_native_windows_stream *source = owner->options.capabilities.streams + stream->index;
    return windows_process_current(owner, error) && source->read(source->context, data, bytes, done, error);
}
static bool stream_write(void *context, qa_bytes bytes, qa_error *error)
{
    windows_process_stream *stream = context; qa_native_windows_process *owner = stream->process;
    const qa_native_windows_stream *source = owner->options.capabilities.streams + stream->index;
    return windows_process_current(owner, error) && source->write(source->context, bytes, error);
}
guest_windows_capabilities windows_process_capabilities(qa_native_windows_process *owner)
{
    const qa_native_windows_capabilities *source = &owner->options.capabilities;
    guest_windows_capabilities out = {.id = source->id, .entropy = entropy,
        .locale = source->locale, .compare_string = source->compare_string ? compare_string : NULL,
        .milliseconds = milliseconds, .performance = performance,
        .performance_frequency = source->performance_frequency, .calendar = calendar,
        .open_file = source->open_file ? open_file : NULL, .resolve_file = resolve_file, .context = owner};
    guest_windows_stream_capability *streams[] = {&out.standard_input, &out.standard_output, &out.standard_error};
    for (size_t i = 0; i < 3; ++i) {
        owner->streams[i] = (windows_process_stream){owner, i};
        *streams[i] = (guest_windows_stream_capability){source->streams[i].id,
            source->streams[i].read ? stream_read : NULL,
            source->streams[i].write ? stream_write : NULL, &owner->streams[i]};
    }
    return out;
}

bool windows_process_same_image(const qa_native_image_info *a, const qa_native_image_info *b)
{
    return a->format == b->format && a->target.os == b->target.os &&
        a->target.arch == b->target.arch && a->target.abi == b->target.abi &&
        a->target.pointer_bytes == b->target.pointer_bytes && a->preferred_base == b->preferred_base &&
        a->image_bytes == b->image_bytes && qa_sha256_equal(&a->digest, &b->digest);
}
bool windows_process_artifacts(qa_native_windows_process *owner, bool fresh, qa_error *error)
{
    if (!owner->image_count || owner->image_count > SIZE_MAX / sizeof(guest_profile_image))
        return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows profile lacks its actual artifact graph");
    guest_profile_image *images = calloc(owner->image_count, sizeof(*images));
    if (!images) return guest_fail(error, QA_ERROR_MEMORY, 0, "qualifying Windows ordered artifact provenance");
    for (size_t i = 0; i < owner->image_count; ++i)
        images[i] = (guest_profile_image){.provider = owner->images[i].id,
            .kind = GUEST_PROFILE_PE, .owner.pe = owner->images[i].artifact};
    bool okay = (!fresh || guest_profile_artifacts_create(images, owner->image_count, &owner->provenance, error)) &&
        guest_profile_artifacts_match(owner->provenance, images, owner->image_count, error);
    free(images); return okay;
}
static bool options_valid(const qa_native_windows_process_options *o, qa_error *error)
{
    if (!o || !o->artifact_count || !o->artifacts || !o->primary_image ||
        o->artifact_count > SIZE_MAX / sizeof(windows_process_image) || !o->stack_bytes ||
        o->stack_bytes % QA_NATIVE_GUEST_PAGE || o->guest.image.target.os != QA_NATIVE_OS_WINDOWS ||
        !o->capabilities.id || !qa_native_windows_locale_profile_valid(&o->capabilities.locale) ||
        (o->capabilities.locale.source == 2) != (o->capabilities.compare_string != NULL) ||
        !o->capabilities.entropy || !o->capabilities.milliseconds ||
        !o->capabilities.performance || !o->capabilities.calendar || !o->capabilities.current ||
        o->capabilities.performance_frequency <= 0 ||
        (o->command_line_units && !o->command_line) || (o->environment_units && !o->environment))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows process needs its actual artifacts, stack and capability graph");
    bool native = o->guest.backend == QA_NATIVE_GUEST_HOST_X86_64;
    if ((o->guest.backend != QA_NATIVE_GUEST_EMULATED && !native) ||
        (native ? o->instruction_budget != 0 : !o->instruction_budget) ||
        (native && (!o->guest.host_executable || !*o->guest.host_executable || !o->guest.profile_guard ||
         o->guest.image.target.arch != QA_NATIVE_ARCH_X86_64 ||
         o->guest.image.target.abi != QA_NATIVE_ABI_MICROSOFT_X64 || o->guest.image.target.pointer_bytes != 8)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows process requires its declared actual execution policy");
    bool primary = false;
    for (size_t i = 0; i < o->artifact_count; ++i) {
        const qa_native_windows_artifact *a = o->artifacts + i;
        const qa_native_target *x = &a->image.target, *y = &o->guest.image.target;
        if (!a->id || !a->path || !*a->path || !a->maximum_image_bytes || x->os != y->os ||
            x->arch != y->arch || x->abi != y->abi || x->pointer_bytes != y->pointer_bytes)
            return guest_fail(error, QA_ERROR_ARGUMENT, i, "Windows artifact has a different actual process target");
        for (size_t j = 0; j < i; ++j)
            if (o->artifacts[j].id == a->id)
                return guest_fail(error, QA_ERROR_ARGUMENT, a->id, "Windows image identity repeats");
        if (a->id == o->primary_image) primary = windows_process_same_image(&a->image, &o->guest.image);
    }
    if (!primary) return guest_fail(error, QA_ERROR_ARGUMENT, o->primary_image, "Windows primary ID lacks its exact source witness");
    for (size_t i = 0; i < 3; ++i)
        if (!!o->capabilities.streams[i].id !=
            (i ? o->capabilities.streams[i].write != NULL : o->capabilities.streams[i].read != NULL))
            return guest_fail(error, QA_ERROR_ARGUMENT, i, "Windows standard stream lacks its actual operation");
    return true;
}

bool qa_native_windows_process_create(const qa_native_windows_process_options *options,
    qa_native_windows_process **out, qa_error *error)
{
    if (!out || *out || !options_valid(options, error)) return false;
    qa_native_windows_process *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning Windows process");
    owner->options = *options; owner->busy = true; *out = owner;
    owner->images = calloc(options->artifact_count, sizeof(*owner->images));
    bool okay = owner->images != NULL;
    if (!okay) guest_fail(error, QA_ERROR_MEMORY, 0, "owning Windows artifact graph");
    for (size_t i = 0; okay && i < options->artifact_count; ++i) {
        const qa_native_windows_artifact *source = options->artifacts + i;
        windows_process_image *row = owner->images + i;
        row->id = source->id; owner->image_count = i + 1;
        size_t bytes = strlen(source->path);
        row->path = bytes < SIZE_MAX ? malloc(bytes + 1) : NULL;
        if (!row->path) { okay = guest_fail(error, QA_ERROR_MEMORY, i, "owning Windows image path"); break; }
        memcpy(row->path, source->path, bytes + 1);
        okay = guest_pe_open(source->bytes, &source->image, source->load_base,
            source->maximum_image_bytes, &row->artifact, error);
    }
    if (okay) okay = windows_process_artifacts(owner, true, error) &&
        options->capabilities.current(options->capabilities.context, error) &&
        qa_native_guest_create(&options->guest, &owner->guest, error);
    for (size_t i = 0; okay && i < owner->image_count; ++i)
        okay = guest_pe_memory_attach(owner->images[i].artifact, owner->guest, &owner->images[i].memory, error);
    if (okay) okay = qa_native_guest_allocate_aligned(owner->guest, options->stack_bytes, 16,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, INT32_C(0x57505354), &owner->stack, error);
    if (okay && (owner->stack > UINT64_MAX - options->stack_bytes ||
        (options->guest.image.target.pointer_bytes == 4 && owner->stack + options->stack_bytes > UINT32_MAX)))
        okay = guest_fail(error, QA_ERROR_ARGUMENT, owner->stack, "Windows process stack exceeds its actual ABI");
    qa_native_guest_cpu cpu;
    if (okay) okay = qa_native_guest_cpu_read(owner->guest, &cpu, error);
    if (okay) {
        cpu.instruction = 0;
        if (options->guest.backend == QA_NATIVE_GUEST_EMULATED) cpu.flags = 2;
        memset(cpu.registers, 0, sizeof(cpu.registers));
        memset(cpu.fp_mantissa, 0, sizeof(cpu.fp_mantissa)); memset(cpu.fp_exponent, 0, sizeof(cpu.fp_exponent));
        memset(cpu.xmm, 0, sizeof(cpu.xmm));
        cpu.registers[QA_NATIVE_RSP] = owner->stack + options->stack_bytes;
        cpu.fp_control = 0x37f; cpu.fp_status = 0; cpu.fp_tags = 0xffff; cpu.mxcsr = 0x1f80;
        cpu.fp_instruction = cpu.fp_operand = 0;
        cpu.fp_code_selector = cpu.fp_data_selector = cpu.fp_opcode = 0;
        okay = qa_native_guest_cpu_write(owner->guest, &cpu, error);
    }
    guest_windows_options runtime = {.guest = owner->guest, .capabilities = windows_process_capabilities(owner),
        .process_id = options->process_id, .thread_id = options->thread_id,
        .has_process_id = options->has_process_id, .has_thread_id = options->has_thread_id,
        .command_line = options->command_line, .command_line_units = options->command_line_units,
        .environment = options->environment, .environment_units = options->environment_units,
        .stack_base = owner->stack, .stack_bytes = options->stack_bytes,
        .instruction_budget = options->instruction_budget, .primary_image = options->primary_image};
    if (okay) okay = guest_windows_create(&runtime, &owner->runtime, error);
    guest_windows_image *inventory = okay ? calloc(owner->image_count, sizeof(*inventory)) : NULL;
    if (okay && !inventory) okay = guest_fail(error, QA_ERROR_MEMORY, 0, "staging Windows process image inventory");
    for (size_t i = 0; okay && i < owner->image_count; ++i)
        inventory[i] = (guest_windows_image){owner->images[i].id, owner->images[i].artifact, owner->images[i].path};
    if (okay) okay = guest_windows_inventory(owner->runtime, inventory, owner->image_count, error);
    free(inventory);
    for (size_t i = 0; okay && i < owner->image_count; ++i) {
        guest_windows_image image = {owner->images[i].id, owner->images[i].artifact, owner->images[i].path};
        okay = guest_windows_prepare(owner->runtime, &image, error);
    }
    owner->options.artifacts = NULL; owner->options.command_line = owner->options.environment = NULL;
    owner->options.guest.host_executable = NULL;
    owner->options.guest.profile_guard = NULL;
    owner->complete = okay; owner->failed = !okay; owner->busy = false;
    return okay;
}

bool qa_native_windows_process_idle(const qa_native_windows_process *owner)
{
    return owner && owner->complete && !owner->busy && !owner->failed && !owner->disposing &&
        !owner->provisional && guest_windows_idle(owner->runtime) && qa_native_guest_idle(owner->guest);
}
qa_native_guest *qa_native_windows_process_guest(const qa_native_windows_process *owner)
{ return owner && !owner->failed && !owner->disposing ? owner->guest : NULL; }
bool qa_native_windows_process_artifact_read(const qa_native_windows_process *owner,
    uint64_t id, qa_native_windows_artifact *out, qa_error *error)
{
    if (!owner || !id || !out) return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows artifact read requires its retained owner");
    for (size_t i = 0; i < owner->image_count; ++i) {
        const windows_process_image *row = owner->images + i;
        const guest_pe_view *image = guest_pe_describe(row->artifact);
        if (row->id == id && image) {
            *out = (qa_native_windows_artifact){id, image->base, image->image,
                image->artifact, image->bytes.size, row->path}; return true;
        }
    }
    return guest_fail(error, QA_ERROR_NOT_FOUND, id, "Windows retained source artifact is absent");
}
static bool begin(qa_native_windows_process *owner, qa_error *error)
{
    if (!qa_native_windows_process_idle(owner))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows operation requires its complete idle process");
    owner->busy = true;
    if (!windows_process_current(owner, error)) { owner->busy = false; return false; }
    return true;
}
static bool end(qa_native_windows_process *owner, bool okay)
{
    owner->busy = false;
    if (!okay && owner->guest->failed) owner->failed = true;
    return okay;
}
bool qa_native_windows_process_initialize(qa_native_windows_process *owner, uint64_t image, qa_error *error)
{
    if (!begin(owner, error)) return false;
    return end(owner, guest_windows_initialize(owner->runtime, image, owner->options.instruction_budget, error));
}
bool qa_native_windows_process_reload(qa_native_windows_process *owner, uint64_t id, qa_error *error)
{
    windows_process_image *row = NULL;
    for (size_t i = 0; owner && i < owner->image_count; ++i)
        if (owner->images[i].id == id) row = owner->images + i;
    if (!row || !row->memory)
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "Windows reload requires its actual retained DLL attachment");
    if (!begin(owner, error)) return false;
    bool okay = guest_windows_finalize(owner->runtime, id, owner->options.instruction_budget, error) &&
        guest_windows_reload_begin(owner->runtime, id, error) && guest_pe_memory_close(&row->memory, error) &&
        guest_pe_memory_attach(row->artifact, owner->guest, &row->memory, error);
    if (okay) {
        guest_windows_image image = {id, row->artifact, row->path};
        okay = guest_windows_prepare(owner->runtime, &image, error) &&
            guest_windows_initialize(owner->runtime, id, owner->options.instruction_budget, error);
    }
    if (!okay) owner->guest->failed = true;
    return end(owner, okay);
}
bool qa_native_windows_process_finalize(qa_native_windows_process *owner, uint64_t image, qa_error *error)
{
    if (!begin(owner, error)) return false;
    return end(owner, guest_windows_finalize(owner->runtime, image, owner->options.instruction_budget, error));
}
bool qa_native_windows_process_finalize_all(qa_native_windows_process *owner, qa_error *error)
{
    if (!begin(owner, error)) return false;
    bool okay = true;
    while (okay && owner->runtime->initialized_count) {
        uint64_t image = owner->runtime->initialized_ids[owner->runtime->initialized_count - 1];
        okay = guest_windows_finalize(owner->runtime, image, owner->options.instruction_budget, error);
    }
    return end(owner, okay);
}
bool qa_native_windows_process_file_add(qa_native_windows_process *owner, const char *name,
    uint32_t creation, const qa_native_windows_file *file, qa_error *error)
{
    if (!name || !file)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows file registration needs its actual acquired capability and source name");
    if (!begin(owner, error)) return false;
    guest_runtime_file_capability capability = file_capability(file);
    return end(owner, guest_runtime_resources_file(owner->runtime->resources, file->id,
        name, creation, &capability, error));
}
static bool temporary_file(qa_native_windows_process *owner, uint64_t handle, qa_error *error)
{
    if (!owner || !handle)
        return guest_fail(error, QA_ERROR_ARGUMENT, handle, "Windows retirement requires its actual file handle");
    if (!begin(owner, error)) return false;
    for (size_t i = 0; i < owner->runtime->kernel->standard_count; ++i)
        if (owner->runtime->kernel->standards[i].handle == handle) {
            guest_fail(error, QA_ERROR_ARGUMENT, handle, "temporary retirement cannot remove a Windows standard stream");
            return end(owner, false);
        }
    if (windows_stdio_file_in_use(owner->runtime, handle)) {
        guest_fail(error, QA_ERROR_ARGUMENT, handle, "Windows FILE still owns this capability");
        return end(owner, false);
    }
    return true;
}
bool qa_native_windows_process_file_close(qa_native_windows_process *owner, uint64_t handle, qa_error *error)
{
    if (!temporary_file(owner, handle, error)) return false;
    return end(owner, guest_runtime_resources_close(owner->runtime->resources, handle, error));
}
bool qa_native_windows_process_file_remove(qa_native_windows_process *owner, uint64_t handle, qa_error *error)
{
    if (!temporary_file(owner, handle, error)) return false;
    return end(owner, guest_runtime_resources_remove_closed(owner->runtime->resources, handle, error));
}
static bool process_invoke(qa_native_windows_process *owner, uint64_t original, uint64_t target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    if (!owner || !signature || signature->variadic || signature->abi != owner->options.guest.image.target.abi)
        return guest_fail(error, QA_ERROR_ARGUMENT, target, "Windows invocation requires the actual fixed source ABI");
    guest_abi_plan *plan = NULL;
    if (!guest_abi_plan_native(signature, NULL, 0, &plan, error)) return false;
    bool outer_busy = owner->busy;
    bool nested = owner->guest &&
        ((owner->guest->run&&owner->guest->callback_depth)||
         (owner->guest->publication_depth&&owner->guest->stopped_write_calls)) &&
        (!owner->guest->publication_depth || owner->guest->stopped_write_calls) && !owner->failed && !owner->disposing &&
        !owner->provisional && guest_mutable(owner->guest, error);
    bool prepared=!outer_busy&&owner->complete&&!owner->failed&&!owner->disposing&&!owner->provisional&&
        guest_call_prepared(owner->guest)&&guest_windows_idle(owner->runtime);
    bool okay = nested||prepared ? windows_process_current(owner, error) : begin(owner, error);
    if (okay) {
        owner->busy = true;
        okay = owner->options.guest.backend == QA_NATIVE_GUEST_HOST_X86_64 && !original ?
            guest_abi_invoke_native(plan, owner->guest, target, owner->runtime->return_trap, arguments, count, result, error) :
            original ? guest_abi_invoke_original(plan, owner->guest, original, target, owner->runtime->return_trap,
                arguments, count, result, owner->options.instruction_budget, error) :
            guest_abi_invoke(plan, owner->guest, target, owner->runtime->return_trap, arguments, count, result,
                owner->options.instruction_budget, error);
        okay = end(owner, okay);
        owner->busy = outer_busy;
    }
    guest_abi_plan_destroy(plan); return okay;
}
bool qa_native_windows_process_invoke(qa_native_windows_process *owner, uint64_t target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{ return process_invoke(owner, 0, target, signature, arguments, count, result, error); }
bool qa_native_windows_process_invoke_original(qa_native_windows_process *owner, uint64_t id, uint64_t target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    if (!owner || !id || owner->options.guest.backend != QA_NATIVE_GUEST_EMULATED)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, target, "original observed entry requires its actual emulated store scope");
    return process_invoke(owner, id, target, signature, arguments, count, result, error);
}
bool qa_native_windows_process_export(qa_native_windows_process *owner,
    const char *library, const char *name, uint64_t *out, qa_error *error)
{
    if (!owner || !library || !name || !out) return false;
    bool outer_busy = owner->busy;
    bool nested = owner->guest && owner->guest->run && owner->guest->callback_depth &&
        !owner->guest->publication_depth && !owner->failed && !owner->disposing &&
        !owner->provisional && guest_mutable(owner->guest, error);
    if (!(nested ? windows_process_current(owner, error) : begin(owner, error))) return false;
    owner->busy = true;
    bool okay = guest_windows_resolve(owner->runtime, library, name, out, error);
    if (okay && !*out) okay = guest_fail(error, QA_ERROR_NOT_FOUND, 0, "Windows actual provider has no matching export");
    okay = end(owner, okay); owner->busy = outer_busy; return okay;
}
bool windows_process_storage(const qa_native_windows_process *owner, qa_error *error)
{
    qa_native_allocation_info stack;
    if (!qa_native_guest_allocation(owner->guest, owner->stack, &stack, error)) return false;
    return (stack.base == owner->stack && stack.bytes == owner->options.stack_bytes &&
        stack.tag == INT32_C(0x57505354) &&
        qa_native_guest_execution(owner->guest) == owner->options.guest.backend) ||
        guest_fail(error, QA_ERROR_FORMAT, owner->stack, "Windows stack lost its actual allocation or backend ownership");
}

bool qa_native_windows_process_dispose(qa_native_windows_process **pointer, qa_error *error)
{
    if (!pointer) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows disposal needs its owning pointer");
    qa_native_windows_process *owner = *pointer;
    if (!owner) return true;
    if (owner->busy || (owner->guest && (owner->guest->run || owner->guest->stepping ||
        owner->guest->callback_depth || owner->guest->publication_depth)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows disposal requires drained source and capability callbacks");
    owner->disposing = true; owner->busy = true;
    /* Real open capabilities retire while their actual lower/context owner is
     * retained. Detached/provisional candidates only borrow close ownership. */
    if (owner->runtime && !owner->runtime->detached && !owner->runtime->publication_pending &&
        !owner->runtime->files_closed && !guest_windows_close_files(owner->runtime, error)) {
        owner->busy = false; return false;
    }
    if (!qa_native_guest_destroy(&owner->guest, error)) { owner->busy = false; return false; }
    guest_windows_abandon(&owner->runtime);
    for (size_t i = 0; i < owner->image_count; ++i) {
        guest_pe_memory_abandon(&owner->images[i].memory);
        guest_pe_close(&owner->images[i].artifact); free(owner->images[i].path);
    }
    guest_profile_artifacts_destroy(&owner->provenance);
    free(owner->images); free(owner); *pointer = NULL; return true;
}

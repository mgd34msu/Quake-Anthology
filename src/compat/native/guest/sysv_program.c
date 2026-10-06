#include "sysv_program_private.h"
#include <time.h>

static bool physical_clock(qa_native_sysv_program *owner, int32_t clock_id,
    int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{
    if (owner->options.guest.backend == QA_NATIVE_GUEST_HOST_X86_64)
        return guest_host_child_cpu_clock_read(owner->guest->child, clock_id, seconds, nanoseconds, error);
#if defined(__linux__)
    /* The emulated task executes synchronously on this exclusive strand.
     * Count only its entered execution interval, including its kernel work;
     * other controller threads and stopped construction are not source tasks. */
    struct timespec value;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value))
        return guest_fail(error, QA_ERROR_IO, 0, "observing the actual emulated task execution clock");
    *seconds = (int64_t)value.tv_sec; *nanoseconds = (int32_t)value.tv_nsec; return true;
#else
    (void)clock_id; (void)seconds; (void)nanoseconds;
    return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "emulated task CPU accounting requires its actual platform execution clock");
#endif
}
bool program_clock_read(qa_native_sysv_program *owner, int32_t clock_id,
    int64_t *seconds, int32_t *nanoseconds, qa_error *error)
{
    if (!owner || (clock_id != 2 && clock_id != 3) || !seconds || !nanoseconds)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "source CPU clock needs its actual task owner");
    const program_clock *saved = owner->clocks + (clock_id - 2);
    uint64_t total = saved->seconds; int64_t part = saved->nanoseconds;
    if (owner->clock_active) {
        int64_t now; int32_t nanos;
        if (!physical_clock(owner, clock_id, &now, &nanos, error)) return false;
        if (now < saved->baseline_seconds || nanos < 0 || nanos >= 1000000000 ||
            (now == saved->baseline_seconds && nanos < saved->baseline_nanoseconds))
            return guest_fail(error, QA_ERROR_FORMAT, 0, "physical source CPU clock moved backwards");
        uint64_t elapsed = (uint64_t)(now - saved->baseline_seconds);
        if (elapsed > (uint64_t)INT64_MAX - total)
            return guest_fail(error, QA_ERROR_FORMAT, 0, "source CPU accounting overflowed its timespec domain");
        total += elapsed; part += (int64_t)nanos - saved->baseline_nanoseconds;
        if (part < 0) { --total; part += 1000000000; }
        if (part >= 1000000000) {
            if (total == INT64_MAX) return guest_fail(error, QA_ERROR_FORMAT, 0, "source CPU accounting seconds overflow");
            ++total; part -= 1000000000;
        }
    }
    *seconds = (int64_t)total; *nanoseconds = (int32_t)part; return true;
}
static bool clocks_start(qa_native_sysv_program *owner, qa_error *error)
{
    for (int32_t clock_id = 2; clock_id <= 3; ++clock_id) {
        program_clock *saved = owner->clocks + (clock_id - 2);
        if (!physical_clock(owner, clock_id, &saved->baseline_seconds, &saved->baseline_nanoseconds, error)) return false;
        if (saved->baseline_seconds < 0 || saved->baseline_nanoseconds < 0 || saved->baseline_nanoseconds >= 1000000000)
            return guest_fail(error, QA_ERROR_FORMAT, 0, "source physical CPU clock has invalid fields");
    }
    owner->clock_active = true; return true;
}
static bool clocks_finish(qa_native_sysv_program *owner, qa_error *error)
{
    for (int32_t clock_id = 2; clock_id <= 3; ++clock_id) {
        int64_t seconds; int32_t nanos;
        if (!program_clock_read(owner, clock_id, &seconds, &nanos, error)) return false;
        owner->clocks[clock_id - 2].seconds = (uint64_t)seconds;
        owner->clocks[clock_id - 2].nanoseconds = (uint32_t)nanos;
    }
    owner->clock_active = false; return true;
}

bool program_current(qa_native_sysv_program *owner, qa_error *error)
{
    return owner && !owner->failed && !owner->disposing && !owner->provisional &&
        owner->options.services.current && guest_ready(owner->guest, error) &&
        owner->options.services.current(owner->options.services.context, error);
}
static bool options_valid(const qa_native_sysv_program_options *o, qa_error *error)
{
    if (!o || !o->program.provider || o->program.role != QA_NATIVE_SYSV_PROGRAM ||
        o->guest.image.format != QA_NATIVE_IMAGE_ELF64 || o->guest.image.target.os != QA_NATIVE_OS_LINUX ||
        o->guest.image.target.arch != QA_NATIVE_ARCH_X86_64 ||
        o->guest.image.target.abi != QA_NATIVE_ABI_SYSTEM_V_X64 || o->guest.image.target.pointer_bytes != 8 ||
        !o->stack_bytes || o->stack_bytes % QA_NATIVE_GUEST_PAGE || o->anonymous_permissions > 7 ||
        !o->services.id || !o->services.current || !o->services.resolve_file || !o->services.file_status ||
        !o->services.descriptor_status || !o->services.descriptor_flags ||
        !o->services.identity || !o->services.entropy || !o->services.clock || !o->services.native_error ||
        (o->auxiliary_count && !o->auxiliary) ||
        (o->guest.backend == QA_NATIVE_GUEST_HOST_X86_64 ? o->instruction_budget != 0 :
         o->guest.backend != QA_NATIVE_GUEST_EMULATED || !o->instruction_budget || !o->process_id || !o->thread_id) ||
        o->process_id > INT32_MAX || o->thread_id > INT32_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux program needs its actual ELF64 kernel authority and execution policy");
    if (o->program.image.format != o->guest.image.format ||
        o->program.image.target.os != o->guest.image.target.os ||
        o->program.image.target.arch != o->guest.image.target.arch ||
        o->program.image.target.abi != o->guest.image.target.abi ||
        o->program.image.target.pointer_bytes != o->guest.image.target.pointer_bytes ||
        o->program.image.preferred_base != o->guest.image.preferred_base ||
        o->program.image.image_bytes != o->guest.image.image_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, o->program.provider, "Linux program primary differs from its source image");
    if (o->interpreter.provider && (o->interpreter.role != QA_NATIVE_SYSV_PROGRAM ||
        o->interpreter.provider == o->program.provider))
        return guest_fail(error, QA_ERROR_ARGUMENT, o->interpreter.provider, "Linux interpreter needs its distinct actual PROGRAM artifact");
    return true;
}
bool qa_native_sysv_program_create(const qa_native_sysv_program_options *options,
    qa_native_sysv_program **out, qa_error *error)
{
    if (!out || *out || !options_valid(options, error)) return false;
    qa_native_sysv_program *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning Linux program kernel");
    owner->options = *options; owner->busy = true; *out = owner;
    const qa_native_sysv_artifact *rows[] = {&options->program, &options->interpreter};
    size_t count = options->interpreter.provider ? 2 : 1;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i)
        okay = guest_elf_open(rows[i]->bytes, &rows[i]->image, GUEST_ELF_PROGRAM,
            rows[i]->load_bias, rows[i]->maximum_image_bytes, &owner->artifacts[i], error);
    guest_profile_image provenance[2] = {
        {.provider = options->program.provider, .kind = GUEST_PROFILE_ELF, .owner.elf = owner->artifacts[0]},
        {.provider = options->interpreter.provider, .kind = GUEST_PROFILE_ELF, .owner.elf = owner->artifacts[1]}};
    if (okay) okay = guest_profile_artifacts_create(provenance, count, &owner->provenance, error) &&
        guest_profile_artifacts_match(owner->provenance, provenance, count, error) &&
        options->services.current(options->services.context, error) &&
        qa_native_guest_create(&options->guest, &owner->guest, error);
    guest_elf_memory_options memory = {options->anonymous_permissions, options->read_implies_execute};
    for (size_t i = 0; okay && i < count; ++i)
        okay = guest_elf_memory_attach(owner->artifacts[i], owner->guest, &memory, &owner->memory[i], error);
    uint32_t stack_rights = QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE;
    const guest_elf_view *image = guest_elf_describe(owner->artifacts[0]);
    for (size_t i = 0; image && i < image->segment_count; ++i)
        if (image->segments[i].type == UINT32_C(0x6474e551) && (image->segments[i].flags & 1))
            stack_rights |= QA_NATIVE_GUEST_EXECUTE;
    if (options->read_implies_execute) stack_rights |= QA_NATIVE_GUEST_EXECUTE;
    if (okay) okay = qa_native_guest_allocate_aligned(owner->guest, options->stack_bytes, 16,
        stack_rights, INT32_C(0x4b535441), &owner->stack, error);
    qa_native_guest_cpu cpu;
    if (okay) okay = qa_native_guest_cpu_read(owner->guest, &cpu, error);
    if (okay) {
        if (options->guest.backend == QA_NATIVE_GUEST_EMULATED) cpu.flags = 2;
        memset(cpu.fp_mantissa, 0, sizeof(cpu.fp_mantissa)); memset(cpu.fp_exponent, 0, sizeof(cpu.fp_exponent));
        memset(cpu.xmm, 0, sizeof(cpu.xmm)); cpu.fp_control = 0x37f; cpu.fp_status = 0;
        cpu.fp_tags = 0xffff; cpu.mxcsr = 0x1f80; cpu.fp_instruction = cpu.fp_operand = 0;
        cpu.fp_code_selector = cpu.fp_data_selector = cpu.fp_opcode = 0;
        okay = qa_native_guest_cpu_write(owner->guest, &cpu, error);
    }
    guest_elf_program_options startup = {.images = {
        .program = {options->program.provider, owner->artifacts[0], owner->memory[0]},
        .interpreter = {options->interpreter.provider, owner->artifacts[1], owner->memory[1]},
        .interpreter_path = options->interpreter_path},
        .stack = owner->stack, .stack_bytes = options->stack_bytes, .argv = options->argv,
        .environment = options->environment, .argc = options->argc, .environment_count = options->environment_count,
        .executed_path = options->executed_path, .platform = options->platform, .base_platform = options->base_platform,
        .random = options->random};
    guest_elf_program_aux *aux = NULL;
    if (okay && options->auxiliary_count) {
        if (options->auxiliary_count > SIZE_MAX / sizeof(*aux)) okay = guest_fail(error, QA_ERROR_MEMORY, 0, "Linux auxiliary inventory overflows");
        else aux = calloc(options->auxiliary_count, sizeof(*aux));
        if (okay && !aux) okay = guest_fail(error, QA_ERROR_MEMORY, 0, "owning Linux auxiliary words");
        for (size_t i = 0; okay && i < options->auxiliary_count; ++i)
            aux[i] = (guest_elf_program_aux){options->auxiliary[i].tag, options->auxiliary[i].value};
    }
    startup.auxiliary = aux; startup.auxiliary_count = options->auxiliary_count;
    if (okay) okay = guest_elf_program_prepare(&startup, &owner->startup, error);
    if (okay) okay = guest_elf_program_transfer_stack(owner->startup, error);
    free(aux);
    owner->status.process_id = options->process_id; owner->status.thread_id = options->thread_id;
    if (okay && options->guest.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
        (!owner->status.process_id || !owner->status.thread_id)) {
        uint64_t pid, tid;
        okay = guest_host_child_process_read(owner->guest->child, &pid, &tid, error);
        if (okay && (!pid || pid > INT32_MAX || !tid || tid > INT32_MAX))
            okay = guest_fail(error, QA_ERROR_FORMAT, pid, "physical Linux task identity exceeds the source PID domain");
        if (okay) {
            if (!owner->status.process_id) owner->status.process_id = pid;
            if (!owner->status.thread_id) owner->status.thread_id = tid;
        }
    }
    if (okay) {
        owner->mapping_cursor = owner->guest->allocation_cursor;
        uint64_t end = image->bias + image->end;
        if (end > UINT64_MAX - (QA_NATIVE_GUEST_PAGE - 1)) okay = guest_fail(error, QA_ERROR_FORMAT, end, "Linux initial brk overflows");
        else owner->break_base = owner->current_break = (end + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1);
    }
    for (size_t i = 0; okay && i < 3; ++i) {
        const qa_native_sysv_file *source = options->standard_files + i;
        if (!source->capability) continue;
        if (!source->close || !source->size || (source->mode & ~3u) ||
            ((source->mode & 1u) && !source->read) || ((source->mode & 2u) && !source->write)) {
            okay = guest_fail(error, QA_ERROR_ARGUMENT, i, "Linux standard descriptor lacks its actual capability"); break;
        }
        okay = guest_grow((void **)&owner->files, &owner->file_capacity, owner->file_count + 1, sizeof(*owner->files), error) &&
            guest_grow((void **)&owner->descriptors, &owner->descriptor_capacity, owner->descriptor_count + 1, sizeof(*owner->descriptors), error);
        if (okay) {
            owner->files[owner->file_count] = (program_file){.capability = *source, .references = 1,
                .flags = source->mode == 2 ? 1u : source->mode == 3 ? 2u : 0u};
            size_t file = owner->file_count++;
            owner->descriptors[owner->descriptor_count++] = (program_descriptor){(int32_t)i, file, false};
            qa_native_sysv_program_descriptor_status status;
            okay = options->services.descriptor_status(options->services.context, source->capability, &status, error);
            if (okay && (status.offset < 0 || (!status.seekable && status.offset)))
                okay = guest_fail(error, QA_ERROR_FORMAT, i, "Linux inherited descriptor has invalid position ownership");
            if (okay) {
                owner->files[file].offset = (uint64_t)status.offset;
                owner->files[file].flags = status.flags;
                owner->files[file].seekable = status.seekable;
            }
        }
    }
    owner->options.program.bytes = owner->options.interpreter.bytes = (qa_bytes){0};
    owner->options.argv = owner->options.environment = NULL; owner->options.auxiliary = NULL;
    owner->options.executed_path = owner->options.platform = owner->options.base_platform = owner->options.interpreter_path = NULL;
    owner->options.random = (qa_bytes){0}; owner->options.guest.host_executable = NULL; owner->options.guest.profile_guard = NULL;
    owner->complete = okay; owner->failed = !okay; owner->busy = false;
    return okay;
}
qa_native_guest *qa_native_sysv_program_guest(const qa_native_sysv_program *owner)
{ return owner && !owner->failed && !owner->disposing ? owner->guest : NULL; }
bool qa_native_sysv_program_status_read(const qa_native_sysv_program *owner,
    qa_native_sysv_program_status *out, qa_error *error)
{
    if (!owner || !out) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux status requires its actual kernel owner");
    *out = owner->status; return true;
}
bool qa_native_sysv_program_run(qa_native_sysv_program *owner, qa_error *error)
{
    if (!owner || !owner->complete || owner->busy || owner->status.exited ||
        !qa_native_guest_idle(owner->guest) || !program_current(owner, error))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux scheduling requires its live stopped program");
    owner->busy = true; qa_native_guest_cpu cpu; bool stopped = false;
    bool okay = qa_native_guest_cpu_read(owner->guest, &cpu, error) && clocks_start(owner, error) &&
        qa_native_guest_run_program(owner->guest, cpu.instruction, owner->returned,
            owner->options.instruction_budget, program_syscall, owner, &stopped, error);
    if (okay) okay = clocks_finish(owner, error);
    if (okay && (!stopped || !owner->status.exited))
        okay = guest_fail(error, QA_ERROR_FORMAT, cpu.instruction, "Linux program reached a controller return without a kernel exit");
    if (!okay) { owner->failed = true; owner->guest->failed = true; }
    owner->clock_active = false; owner->busy = false; return okay;
}
bool qa_native_sysv_program_dispose(qa_native_sysv_program **slot, qa_error *error)
{
    if (!slot) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux disposal requires its actual owner slot");
    qa_native_sysv_program *owner = *slot; if (!owner) return true;
    if (owner->busy || (owner->guest && (owner->guest->run || owner->guest->callback_depth || owner->guest->publication_depth)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Linux disposal requires drained kernel/source callbacks");
    owner->disposing = true;
    if (!program_files_close(owner, error) || !qa_native_guest_destroy(&owner->guest, error)) return false;
    guest_elf_program_abandon(&owner->startup);
    for (size_t i = 0; i < 2; ++i) { guest_elf_memory_abandon(&owner->memory[i]); guest_elf_close(&owner->artifacts[i]); }
    guest_profile_artifacts_destroy(&owner->provenance);
    free(owner->files); free(owner->descriptors); free(owner); *slot = NULL; return true;
}

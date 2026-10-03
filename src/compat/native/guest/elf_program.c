#include "internal.h"
#include "elf_program.h"

struct guest_elf_program {
    guest_elf_program_view view;
    guest_elf_program_images images;
    qa_native_guest *guest;
    uint64_t phdr, phent, phnum, main_entry, interpreter_base;
    uint64_t stack_mapping, stack_backing;
    bool complete, stack_transferred, stack_changed;
};

static bool target_equal(const qa_native_target *a, const qa_native_target *b)
{
    return a->os == b->os && a->arch == b->arch && a->abi == b->abi &&
        a->pointer_bytes == b->pointer_bytes;
}

static bool image_read(const guest_elf_program_image *row, qa_native_guest *guest,
    const guest_elf_view **out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(row->artifact);
    const guest_elf_memory_view *memory = guest_elf_memory_describe(row->memory);
    if (!row->provider || !image || !memory || image->role != GUEST_ELF_PROGRAM ||
        memory->role != GUEST_ELF_PROGRAM || guest_elf_memory_guest(row->memory) != guest ||
        !target_equal(&image->image.target, &guest->options.image.target) ||
        !target_equal(&memory->image.target, &image->image.target) ||
        memory->base != image->bias + image->first || memory->bytes != image->end - image->first ||
        memory->image.format != image->image.format ||
        memory->image.preferred_base != image->image.preferred_base ||
        memory->image.image_bytes != image->image.image_bytes ||
        !qa_sha256_equal(&memory->image.digest, &image->image.digest))
        return guest_fail(error, QA_ERROR_ARGUMENT, row->provider,
            "ELF program image differs from its actual raw process attachment");
    unsigned width = image->image.target.pointer_bytes;
    uint16_t count = qa_load_u16le(image->artifact.data + (width == 8 ? 56 : 44));
    uint16_t stride = qa_load_u16le(image->artifact.data + (width == 8 ? 54 : 42));
    /* Linux load_elf_phdrs reads the literal header count and permits at most
     * 64 KiB of records. Extended counts remain supported by inert/library
     * owners; they do not become an invented kernel exec admission here. */
    if (count != image->segment_count || !count || (size_t)count * stride > 65536)
        return guest_fail(error, QA_ERROR_FORMAT, row->provider,
            "ELF program header table exceeds the actual kernel exec contract");
    *out = image;
    return true;
}

static bool images_read(guest_elf_program *owner, bool fresh, qa_error *error)
{
    const guest_elf_view *program = NULL, *interpreter = NULL;
    owner->guest = guest_elf_memory_guest(owner->images.program.memory);
    if (!qa_native_guest_idle(owner->guest) ||
        !image_read(&owner->images.program, owner->guest, &program, error)) return false;
    if (program->image.format != owner->guest->options.image.format ||
        program->image.preferred_base != owner->guest->options.image.preferred_base ||
        program->image.image_bytes != owner->guest->options.image.image_bytes ||
        !qa_sha256_equal(&program->image.digest, &owner->guest->options.image.digest))
        return guest_fail(error, QA_ERROR_ARGUMENT, owner->images.program.provider,
            "ELF startup program differs from the actual primary process artifact");
    if (program->interpreter) {
        if (!owner->images.interpreter_path ||
            strcmp(program->interpreter, owner->images.interpreter_path) ||
            owner->images.interpreter.provider == owner->images.program.provider ||
            !image_read(&owner->images.interpreter, owner->guest, &interpreter, error))
            return guest_fail(error, QA_ERROR_ARGUMENT, owner->images.interpreter.provider,
                "ELF program lacks its actual selected interpreter artifact and path");
    } else if (owner->images.interpreter.provider || owner->images.interpreter.artifact ||
        owner->images.interpreter.memory || owner->images.interpreter_path)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0,
            "ELF program without PT_INTERP cannot attach an invented interpreter");
    owner->images.interpreter_path = program->interpreter;
    if (fresh && (!guest_elf_memory_program_ready(owner->images.program.memory, error) ||
        (interpreter && !guest_elf_memory_program_ready(owner->images.interpreter.memory, error)))) return false;
    if (program->entry > UINT64_MAX - program->bias ||
        (interpreter && interpreter->entry > UINT64_MAX - interpreter->bias))
        return guest_fail(error, QA_ERROR_FORMAT, program->entry, "ELF initial entry address overflows");
    owner->main_entry = program->bias + program->entry;
    owner->view.entry = interpreter ? interpreter->bias + interpreter->entry : owner->main_entry;
    owner->interpreter_base = interpreter ? interpreter->bias : 0;
    owner->view.program = owner->images.program.provider;
    owner->view.interpreter = interpreter ? owner->images.interpreter.provider : 0;
    unsigned width = program->image.target.pointer_bytes;
    uint64_t offset = width == 8 ? qa_load_u64le(program->artifact.data + 32) :
        qa_load_u32le(program->artifact.data + 28);
    owner->phent = qa_load_u16le(program->artifact.data + (width == 8 ? 54 : 42));
    owner->phnum = program->segment_count;
    owner->phdr = 0;
    for (size_t i = 0; i < program->segment_count; ++i) {
        const guest_elf_segment *segment = program->segments + i;
        if (segment->type == 1 && offset >= segment->offset &&
            offset - segment->offset < segment->file_bytes) {
            uint64_t relative = offset - segment->offset;
            if (relative > UINT64_MAX - segment->address ||
                segment->address + relative > UINT64_MAX - program->bias)
                return guest_fail(error, QA_ERROR_FORMAT, offset, "ELF loaded program-header address overflows");
            owner->phdr = program->bias + segment->address + relative;
        }
    }
    /* binfmt_elf retains bias+zero when no LOAD covers e_phoff. Keep that
     * actual metadata value instead of synthesizing a PT_PHDR mapping. */
    if (!owner->phdr) owner->phdr = program->bias;
    return !fresh || guest_range(owner->guest, owner->view.entry, 1,
        QA_NATIVE_GUEST_EXECUTE, error);
}

static bool stack_read(guest_elf_program *owner, bool fresh, qa_error *error)
{
    qa_native_allocation_info allocation;
    if (!owner->view.stack_bytes || owner->view.stack % 16 ||
        owner->view.stack_bytes % 16 || owner->view.stack > UINT64_MAX - owner->view.stack_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, owner->view.stack,
            "ELF initial stack has an invalid retained address extent");
    uint64_t top = owner->view.stack + owner->view.stack_bytes;
    if (owner->guest->options.image.target.pointer_bytes == 4 && top > UINT32_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, top, "ELF initial stack exceeds its actual i386 address domain");
    if (owner->stack_transferred) {
        if (fresh || !owner->stack_mapping || owner->stack_mapping >= owner->guest->next_mapping ||
            !owner->stack_backing || owner->stack_backing >= owner->guest->next_backing ||
            owner->view.stack % QA_NATIVE_GUEST_PAGE || owner->view.stack_bytes % QA_NATIVE_GUEST_PAGE)
            return guest_fail(error, QA_ERROR_FORMAT, owner->view.stack, "ELF kernel stack lost its original VM ownership receipt");
        for (size_t i = 0; i < owner->guest->allocation_count; ++i) {
            const guest_allocation *row = owner->guest->allocations + i;
            size_t bytes = 0; uint64_t backing = 0;
            if (!guest_allocation_storage(owner->guest, row, &backing, &bytes, error)) return false;
            if (backing == owner->stack_backing ||
                (row->address < top && owner->view.stack < row->address + bytes))
                return guest_fail(error, QA_ERROR_FORMAT, row->address, "ELF kernel stack retains a conflicting allocator claim");
        }
        guest_backing *backing = guest_backing_at(owner->guest, owner->stack_backing);
        if (backing && (backing->file || backing->bytes != owner->view.stack_bytes))
            return guest_fail(error, QA_ERROR_FORMAT, owner->stack_backing, "ELF historical stack backing differs from its original extent");
        if (owner->stack_changed) return true;
        if (!backing) return guest_fail(error, QA_ERROR_FORMAT, owner->stack_backing, "ELF unmodified kernel stack backing is absent");
        size_t offset = 0;
        while (offset < owner->view.stack_bytes) {
            qa_native_guest_mapping *row = guest_mapping(owner->guest, owner->view.stack + offset);
            if (!row || row->base != owner->view.stack + offset || row->backing != owner->stack_backing ||
                row->backing_offset != offset || row->bytes > owner->view.stack_bytes - offset ||
                (!offset && row->id != owner->stack_mapping))
                return guest_fail(error, QA_ERROR_FORMAT, owner->view.stack + offset, "ELF kernel stack fragments differ from their original backing");
            offset += (size_t)row->bytes;
        }
        return true;
    }
    if (owner->stack_changed || owner->stack_mapping || owner->stack_backing ||
        !qa_native_guest_allocation(owner->guest, owner->view.stack, &allocation, error) ||
        allocation.base != owner->view.stack || allocation.bytes != owner->view.stack_bytes ||
        (!fresh && allocation.tag != owner->view.stack_tag))
        return guest_fail(error, QA_ERROR_ARGUMENT, owner->view.stack, "ELF initial stack differs from its actual allocator ownership");
    if (fresh) owner->view.stack_tag = allocation.tag;
    return guest_range(owner->guest, owner->view.stack, owner->view.stack_bytes,
        fresh ? QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE : 0, error);
}

static bool derived(uint64_t tag)
{
    return tag == 0 || tag == 3 || tag == 4 || tag == 5 || tag == 6 || tag == 7 ||
        tag == 9 || tag == 15 || tag == 24 || tag == 25 || tag == 31;
}

static const guest_elf_program_aux *aux_at(const guest_elf_program_options *options, uint64_t tag)
{
    for (size_t i = 0; i < options->auxiliary_count; ++i)
        if (options->auxiliary[i].tag == tag) return options->auxiliary + i;
    return NULL;
}

static bool options_read(const guest_elf_program_options *options, qa_error *error)
{
    if (!options || !options->executed_path || !*options->executed_path ||
        options->argc > UINT32_C(0x7fffffff) || options->environment_count > UINT32_C(0x7fffffff) ||
        (options->argc && !options->argv) || (options->environment_count && !options->environment) ||
        !options->random.data || options->random.size != 16 || !options->auxiliary ||
        options->auxiliary_count > SIZE_MAX / sizeof(guest_elf_program_aux) - 10)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF startup requires actual arguments, entropy and kernel auxiliary authority");
    static const uint64_t required[] = {8, 11, 12, 13, 14, 16, 17, 23};
    for (size_t i = 0; i < sizeof(required) / sizeof(*required); ++i)
        if (!aux_at(options, required[i]))
            return guest_fail(error, QA_ERROR_ARGUMENT, required[i], "ELF startup omits a required actual kernel scalar");
    for (size_t i = 0; i < options->auxiliary_count; ++i) {
        uint64_t tag = options->auxiliary[i].tag;
        if (derived(tag)) return guest_fail(error, QA_ERROR_ARGUMENT, tag, "ELF derived auxiliary entry cannot be supplied as a scalar");
        for (size_t j = 0; j < i; ++j)
            if (options->auxiliary[j].tag == tag)
                return guest_fail(error, QA_ERROR_ARGUMENT, tag, "ELF auxiliary scalar identity is repeated");
    }
    return true;
}

static bool push(uint8_t *data, size_t *cursor, qa_bytes bytes, uint64_t base,
    uint64_t *address, qa_error *error)
{
    if (bytes.size > *cursor)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "ELF initial stack payload exceeds its actual owned extent");
    *cursor -= bytes.size;
    memcpy(data + *cursor, bytes.data, bytes.size);
    *address = base + *cursor;
    return true;
}

static bool text_push(uint8_t *data, size_t *cursor, const char *text, uint64_t base,
    uint64_t *address, qa_error *error)
{
    if (!text) return guest_fail(error, QA_ERROR_ARGUMENT, base, "ELF startup argument has no actual string");
    size_t bytes = strlen(text);
    if (bytes >= 32 * QA_NATIVE_GUEST_PAGE)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "ELF startup string exceeds the actual kernel exec string limit");
    return push(data, cursor, (qa_bytes){(const uint8_t *)text, bytes + 1}, base, address, error);
}

static bool word_write(uint8_t *data, size_t *cursor, unsigned width, uint64_t value, qa_error *error)
{
    if (width == 4 && value > UINT32_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, value, "ELF startup word exceeds its actual i386 width");
    if (width == 4) qa_store_u32le(data + *cursor, (uint32_t)value);
    else qa_store_u64le(data + *cursor, value);
    *cursor += width;
    return true;
}

static bool stage(guest_elf_program *owner, const guest_elf_program_options *options,
    qa_buffer *out, qa_error *error)
{
    size_t count = options->argc;
    if (options->environment_count > SIZE_MAX - count ||
        count + options->environment_count > SIZE_MAX / sizeof(uint64_t))
        return guest_fail(error, QA_ERROR_MEMORY, count, "ELF initial argument pointer inventory overflows");
    count += options->environment_count;
    uint64_t *pointers = count ? calloc(count, sizeof(*pointers)) : NULL;
    uint8_t *data = calloc(1, owner->view.stack_bytes);
    guest_elf_program_aux *aux = calloc(options->auxiliary_count + 10, sizeof(*aux));
    if ((count && !pointers) || !data || !aux) {
        free(pointers); free(data); free(aux);
        return guest_fail(error, QA_ERROR_MEMORY, 0, "staging actual ELF initial stack");
    }
    size_t cursor = owner->view.stack_bytes, entries = 0;
    uint64_t executed = 0, platform = 0, base_platform = 0, random = 0;
    bool okay = text_push(data, &cursor, options->executed_path, owner->view.stack, &executed, error);
    for (size_t i = options->environment_count; okay && i; --i)
        okay = text_push(data, &cursor, options->environment[i - 1], owner->view.stack,
            pointers + options->argc + i - 1, error);
    for (size_t i = options->argc; okay && i; --i)
        okay = text_push(data, &cursor, options->argv[i - 1], owner->view.stack, pointers + i - 1, error);
    if (okay && options->platform)
        okay = text_push(data, &cursor, options->platform, owner->view.stack, &platform, error);
    if (okay && options->base_platform)
        okay = text_push(data, &cursor, options->base_platform, owner->view.stack, &base_platform, error);
    if (okay) okay = push(data, &cursor, options->random, owner->view.stack, &random, error);
    /* Architecture entries precede the generic Linux sequence. Their actual
     * caller order is preserved, including x86 SYSINFO/EHDR/MINSIGSTKSZ. */
    for (size_t i = 0; i < options->auxiliary_count; ++i) {
        uint64_t tag = options->auxiliary[i].tag;
        if (tag == 32 || tag == 33 || tag == 51) aux[entries++] = options->auxiliary[i];
    }
    aux[entries++] = *aux_at(options, 16);
    aux[entries++] = (guest_elf_program_aux){6, QA_NATIVE_GUEST_PAGE};
    aux[entries++] = *aux_at(options, 17);
    aux[entries++] = (guest_elf_program_aux){3, owner->phdr};
    aux[entries++] = (guest_elf_program_aux){4, owner->phent};
    aux[entries++] = (guest_elf_program_aux){5, owner->phnum};
    aux[entries++] = (guest_elf_program_aux){7, owner->interpreter_base};
    aux[entries++] = *aux_at(options, 8);
    aux[entries++] = (guest_elf_program_aux){9, owner->main_entry};
    for (uint64_t tag = 11; tag <= 14; ++tag) aux[entries++] = *aux_at(options, tag);
    aux[entries++] = *aux_at(options, 23);
    aux[entries++] = (guest_elf_program_aux){25, random};
    static const uint64_t hwcap[] = {26, 29, 30};
    for (size_t i = 0; i < sizeof(hwcap) / sizeof(*hwcap); ++i) {
        const guest_elf_program_aux *row = aux_at(options, hwcap[i]);
        if (row) aux[entries++] = *row;
    }
    aux[entries++] = (guest_elf_program_aux){31, executed};
    if (platform) aux[entries++] = (guest_elf_program_aux){15, platform};
    if (base_platform) aux[entries++] = (guest_elf_program_aux){24, base_platform};
    for (size_t i = 0; i < options->auxiliary_count; ++i) {
        uint64_t tag = options->auxiliary[i].tag;
        if (tag != 8 && (tag < 11 || tag > 14) && tag != 16 && tag != 17 && tag != 23 &&
            tag != 26 && tag != 29 && tag != 30 && tag != 32 && tag != 33 && tag != 51)
            aux[entries++] = options->auxiliary[i];
    }
    unsigned width = owner->guest->options.image.target.pointer_bytes;
    size_t words = 0;
    if (entries > (SIZE_MAX - 5) / 2 || count > SIZE_MAX - (entries * 2 + 5) ||
        (words = count + entries * 2 + 5) > SIZE_MAX / width || words * width > cursor)
        okay = guest_fail(error, QA_ERROR_ARGUMENT, owner->view.stack, "ELF initial stack table exceeds its actual extent");
    size_t begin = okay ? (cursor - words * width) & ~(size_t)15 : 0;
    cursor = begin;
    if (okay) okay = word_write(data, &cursor, width, options->argc, error);
    for (size_t i = 0; okay && i < options->argc; ++i) okay = word_write(data, &cursor, width, pointers[i], error);
    if (okay) okay = word_write(data, &cursor, width, 0, error);
    for (size_t i = 0; okay && i < options->environment_count; ++i)
        okay = word_write(data, &cursor, width, pointers[options->argc + i], error);
    if (okay) okay = word_write(data, &cursor, width, 0, error);
    for (size_t i = 0; okay && i < entries; ++i)
        okay = word_write(data, &cursor, width, aux[i].tag, error) &&
            word_write(data, &cursor, width, aux[i].value, error);
    if (okay) okay = word_write(data, &cursor, width, 0, error) && word_write(data, &cursor, width, 0, error);
    free(pointers); free(aux);
    if (!okay) { free(data); return false; }
    owner->view.initial_stack = owner->view.stack + begin;
    *out = (qa_buffer){data, owner->view.stack_bytes};
    return true;
}

bool guest_elf_program_prepare(const guest_elf_program_options *options,
    guest_elf_program **out, qa_error *error)
{
    if (!out || *out || !options_read(options, error)) return false;
    guest_elf_program_options actual = *options;
    const char *empty_argv[] = {""};
    /* Linux execve normalizes an empty argv before entering binfmt_elf. */
    if (!actual.argc) { actual.argc = 1; actual.argv = empty_argv; }
    options = &actual;
    guest_elf_program *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF program startup receipt");
    owner->images = options->images;
    owner->view.stack = options->stack; owner->view.stack_bytes = options->stack_bytes;
    owner->view.argc = options->argc; owner->view.environment_count = options->environment_count;
    qa_buffer staged = {0}; qa_native_guest_cpu cpu;
    bool okay = images_read(owner, true, error) && stack_read(owner, true, error) &&
        !owner->guest->observe && stage(owner, options, &staged, error) &&
        qa_native_guest_cpu_read(owner->guest, &cpu, error);
    if (!okay) {
        if (owner->guest && owner->guest->observe)
            guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF startup cannot run a CPU store observer");
        qa_buffer_free(&staged); free(owner); return false;
    }
    memset(cpu.registers, 0, sizeof(cpu.registers));
    cpu.registers[QA_NATIVE_RSP] = owner->view.initial_stack;
    cpu.instruction = owner->view.entry;
    cpu.segments[4].base = cpu.segments[5].base = 0;
    size_t offset = (size_t)(owner->view.initial_stack - owner->view.stack);
    *out = owner;
    okay = qa_native_guest_write(owner->guest, owner->view.initial_stack,
        (qa_bytes){staged.data + offset, staged.size - offset}, error) &&
        qa_native_guest_cpu_write(owner->guest, &cpu, error) &&
        guest_elf_memory_program_seal(owner->images.program.memory, error) &&
        (!owner->view.interpreter || guest_elf_memory_program_seal(owner->images.interpreter.memory, error));
    qa_buffer_free(&staged);
    if (!okay) { owner->guest->failed = true; return false; }
    owner->complete = true;
    return true;
}

const guest_elf_program_view *guest_elf_program_describe(const guest_elf_program *owner)
{ return owner && owner->complete ? &owner->view : NULL; }

bool guest_elf_program_transfer_stack(guest_elf_program *owner, qa_error *error)
{
    if (!owner || !owner->complete || owner->stack_transferred || !qa_native_guest_idle(owner->guest) ||
        owner->view.stack % QA_NATIVE_GUEST_PAGE || owner->view.stack_bytes % QA_NATIVE_GUEST_PAGE ||
        !stack_read(owner, false, error))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF stack transfer requires its completed idle initial allocation");
    guest_allocation allocation; uint64_t backing = 0;
    if (!guest_allocation_transfer(owner->guest, owner->view.stack, &allocation, &backing, error)) return false;
    owner->stack_mapping = allocation.mapping; owner->stack_backing = backing;
    owner->stack_transferred = true;
    return true;
}

bool guest_elf_program_stack_changed(guest_elf_program *owner, uint64_t base, size_t bytes, qa_error *error)
{
    if (!owner || !owner->complete || !owner->stack_transferred || !guest_mutable(owner->guest, error) ||
        !bytes || base > UINT64_MAX - bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "ELF stack mutation requires actual kernel VM ownership");
    if (base < owner->view.stack + owner->view.stack_bytes && owner->view.stack < base + bytes)
        owner->stack_changed = true;
    return true;
}

bool guest_elf_program_stack_owned(const guest_elf_program *owner)
{ return owner && owner->complete && owner->stack_transferred; }

enum { PROGRAM_RECORD = 204 };

static bool receipt_read(const guest_elf_program *owner, qa_error *error)
{
    uint64_t top = owner->view.stack + owner->view.stack_bytes;
    size_t available = owner->guest->options.image.target.pointer_bytes;
    if (!owner->view.argc || owner->view.argc > UINT32_C(0x7fffffff) ||
        owner->view.environment_count > UINT32_C(0x7fffffff) ||
        owner->view.initial_stack < owner->view.stack ||
        owner->view.initial_stack >= top || owner->view.initial_stack % 16)
        return guest_fail(error, QA_ERROR_FORMAT, owner->view.initial_stack,
            "ELF cold initial stack is outside its retained allocation");
    available = (size_t)((top - owner->view.initial_stack) / available);
    if (available < 37 || owner->view.argc > available - 37 ||
        owner->view.environment_count > available - 37 - owner->view.argc)
        return guest_fail(error, QA_ERROR_FORMAT, owner->view.initial_stack,
            "ELF cold argument receipt exceeds its actual startup table extent");
    qa_buffer program = {0}, interpreter = {0};
    bool okay = guest_elf_memory_checkpoint(owner->images.program.memory, &program, error) &&
        (!owner->view.interpreter || guest_elf_memory_checkpoint(owner->images.interpreter.memory,
            &interpreter, error));
    qa_buffer_free(&program); qa_buffer_free(&interpreter);
    return okay;
}

static void record(uint8_t data[PROGRAM_RECORD], const guest_elf_program *owner)
{
    const guest_elf_view *program = guest_elf_describe(owner->images.program.artifact);
    const guest_elf_view *interpreter = guest_elf_describe(owner->images.interpreter.artifact);
    memset(data, 0, PROGRAM_RECORD); memcpy(data, "QEPG", 4);
    qa_store_u64le(data + 4, owner->view.program); qa_store_u64le(data + 12, owner->view.interpreter);
    qa_store_u64le(data + 20, owner->view.stack); qa_store_u64le(data + 28, owner->view.stack_bytes);
    qa_store_u64le(data + 36, owner->view.initial_stack); qa_store_u64le(data + 44, owner->view.entry);
    qa_store_u64le(data + 52, owner->view.argc); qa_store_u64le(data + 60, owner->view.environment_count);
    qa_store_u64le(data + 68, owner->phdr); qa_store_u64le(data + 76, owner->phent);
    qa_store_u64le(data + 84, owner->phnum); qa_store_u64le(data + 92, owner->main_entry);
    qa_store_u64le(data + 100, owner->interpreter_base); qa_store_u32le(data + 108, (uint32_t)owner->view.stack_tag);
    qa_store_u32le(data + 112, program->image.target.arch); data[116] = program->image.target.pointer_bytes;
    data[117] = owner->stack_transferred; data[118] = owner->stack_changed;
    memcpy(data + 124, program->image.digest.bytes, 32);
    if (interpreter) memcpy(data + 156, interpreter->image.digest.bytes, 32);
    qa_store_u64le(data + 188, owner->stack_mapping); qa_store_u64le(data + 196, owner->stack_backing);
}

bool guest_elf_program_checkpoint(const guest_elf_program *owner, qa_buffer *out, qa_error *error)
{
    if (!owner || !owner->complete || !out || out->data || out->size ||
        !qa_native_guest_idle(owner->guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF startup receipt capture requires its actual idle completed owner");
    guest_elf_program retained = *owner;
    if (!images_read(&retained, false, error) || !stack_read(&retained, false, error) ||
        !receipt_read(&retained, error)) return false;
    uint8_t *data = malloc(PROGRAM_RECORD);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF startup checkpoint");
    record(data, owner); *out = (qa_buffer){data, PROGRAM_RECORD}; return true;
}

bool guest_elf_program_adopt(const guest_elf_program_images *images, qa_bytes encoded,
    guest_elf_program **out, qa_error *error)
{
    if (!images || !out || *out || !encoded.data || encoded.size != PROGRAM_RECORD ||
        memcmp(encoded.data, "QEPG", 4) ||
        encoded.data[117] > 1 || encoded.data[118] > 1 ||
        qa_load_u64le(encoded.data + 28) > SIZE_MAX || qa_load_u64le(encoded.data + 52) > SIZE_MAX ||
        qa_load_u64le(encoded.data + 60) > SIZE_MAX)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "ELF cold startup receipt has an invalid typed extent");
    guest_elf_program *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning cold ELF startup receipt");
    owner->images = *images;
    owner->view.stack = qa_load_u64le(encoded.data + 20);
    owner->view.stack_bytes = (size_t)qa_load_u64le(encoded.data + 28);
    owner->view.initial_stack = qa_load_u64le(encoded.data + 36);
    owner->view.argc = (size_t)qa_load_u64le(encoded.data + 52);
    owner->view.environment_count = (size_t)qa_load_u64le(encoded.data + 60);
    owner->view.stack_tag = (int32_t)qa_load_u32le(encoded.data + 108);
    owner->stack_transferred = encoded.data[117] != 0; owner->stack_changed = encoded.data[118] != 0;
    owner->stack_mapping = qa_load_u64le(encoded.data + 188); owner->stack_backing = qa_load_u64le(encoded.data + 196);
    bool okay = images_read(owner, false, error) && stack_read(owner, false, error);
    if (okay) okay = receipt_read(owner, error);
    uint8_t expected[PROGRAM_RECORD];
    if (okay) {
        record(expected, owner);
        if (memcmp(expected, encoded.data, PROGRAM_RECORD))
            okay = guest_fail(error, QA_ERROR_FORMAT, 0, "ELF cold startup receipt differs from its immutable artifact graph");
    }
    if (!okay) { free(owner); return false; }
    owner->complete = true; *out = owner; return true;
}

void guest_elf_program_abandon(guest_elf_program **owner)
{
    if (!owner || !*owner) return;
    free(*owner); *owner = NULL;
}

#include "internal.h"

typedef struct guest_codec {
    qa_buffer output;
    qa_bytes input;
    size_t offset, capacity;
    qa_error *error;
    bool reading;
} guest_codec;

static bool bytes(guest_codec *io, void *data, size_t amount)
{
    if (io->reading) {
        if (amount > io->input.size - io->offset)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest checkpoint is truncated");
        if (amount) memcpy(data, io->input.data + io->offset, amount);
        io->offset += amount;
        return true;
    }
    if (amount > SIZE_MAX - io->output.size)
        return guest_fail(io->error, QA_ERROR_MEMORY, io->offset, "native guest checkpoint size overflows");
    size_t required = io->output.size + amount;
    if (!guest_grow((void **)&io->output.data, &io->capacity, required, 1, io->error)) return false;
    if (amount) memcpy(io->output.data + io->output.size, data, amount);
    io->output.size = required;
    return true;
}

static bool u16(guest_codec *io, uint16_t *value)
{
    uint8_t encoded[2];
    if (!io->reading) qa_store_u16le(encoded, *value);
    if (!bytes(io, encoded, sizeof(encoded))) return false;
    if (io->reading) *value = qa_load_u16le(encoded);
    return true;
}

static bool u32(guest_codec *io, uint32_t *value)
{
    uint8_t encoded[4];
    if (!io->reading) qa_store_u32le(encoded, *value);
    if (!bytes(io, encoded, sizeof(encoded))) return false;
    if (io->reading) *value = qa_load_u32le(encoded);
    return true;
}

static bool u64(guest_codec *io, uint64_t *value)
{
    uint8_t encoded[8];
    if (!io->reading) qa_store_u64le(encoded, *value);
    if (!bytes(io, encoded, sizeof(encoded))) return false;
    if (io->reading) *value = qa_load_u64le(encoded);
    return true;
}

static bool count(guest_codec *io, size_t *value, size_t minimum_bytes)
{
    uint64_t saved = *value;
    if (!u64(io, &saved)) return false;
    if (io->reading) {
        if (saved > SIZE_MAX || saved > (io->input.size - io->offset) / minimum_bytes)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest checkpoint table extent is invalid");
        *value = (size_t)saved;
    }
    return true;
}

static bool image(guest_codec *io, qa_native_image_info *saved)
{
    uint32_t format = saved->format, os = saved->target.os, arch = saved->target.arch, abi = saved->target.abi;
    if (!u32(io, &format) || !u32(io, &os) || !u32(io, &arch) || !u32(io, &abi) ||
        !bytes(io, &saved->target.pointer_bytes, 1) || !u64(io, &saved->preferred_base) ||
        !u64(io, &saved->image_bytes) || !bytes(io, saved->digest.bytes, sizeof(saved->digest.bytes))) return false;
    saved->format = (qa_native_image_format)format;
    saved->target.os = (qa_native_os)os;
    saved->target.arch = (qa_native_arch)arch;
    saved->target.abi = (qa_native_abi)abi;
    return true;
}

static bool same_image(const qa_native_image_info *saved, const qa_native_image_info *expected)
{
    return saved->format == expected->format && saved->target.os == expected->target.os &&
        saved->target.arch == expected->target.arch && saved->target.abi == expected->target.abi &&
        saved->target.pointer_bytes == expected->target.pointer_bytes &&
        saved->preferred_base == expected->preferred_base && saved->image_bytes == expected->image_bytes &&
        qa_sha256_equal(&saved->digest, &expected->digest);
}

static bool cpu(guest_codec *io, qa_native_guest_cpu *state)
{
    for (size_t i = 0; i < QA_NATIVE_REGISTER_COUNT; ++i) if (!u64(io, &state->registers[i])) return false;
    if (!u64(io, &state->instruction) || !u64(io, &state->flags)) return false;
    for (size_t i = 0; i < 10; ++i) {
        qa_native_guest_table *table = i < 6 ? &state->segments[i] : &state->tables[i - 6];
        if (!u16(io, &table->selector) || !u64(io, &table->base) ||
            !u32(io, &table->limit) || !u32(io, &table->flags)) return false;
    }
    for (size_t i = 0; i < 9; ++i) if (!u64(io, &state->control[i])) return false;
    for (size_t i = 0; i < 8; ++i) if (!u64(io, &state->debug[i])) return false;
    if (!u64(io, &state->efer) || !u64(io, &state->xcr0) || !u64(io, &state->xstate_bv) ||
        !u32(io, &state->execution_flags) || !u32(io, &state->execution_flags2) ||
        !u32(io, &state->a20_mask)) return false;
    for (size_t i = 0; i < 8; ++i)
        if (!u64(io, &state->fp_mantissa[i]) || !u16(io, &state->fp_exponent[i])) return false;
    if (!u16(io, &state->fp_control) || !u16(io, &state->fp_status) || !u16(io, &state->fp_tags) ||
        !u64(io, &state->fp_instruction) || !u64(io, &state->fp_operand) ||
        !u16(io, &state->fp_code_selector) || !u16(io, &state->fp_data_selector) ||
        !u16(io, &state->fp_opcode) || !u32(io, &state->mxcsr)) return false;
    for (size_t i = 0; i < 16; ++i)
        if (!u64(io, &state->xmm[i][0]) || !u64(io, &state->xmm[i][1])) return false;
    return true;
}

static bool mapping(guest_codec *io, qa_native_guest_mapping *saved)
{
    return u64(io, &saved->id) && u64(io, &saved->base) && u64(io, &saved->bytes) &&
        u64(io, &saved->backing) && u64(io, &saved->backing_offset) && u32(io, &saved->permissions);
}

bool qa_native_guest_checkpoint(qa_native_guest *guest, qa_buffer *out, qa_error *error)
{
    if (!qa_native_guest_idle(guest) || !out || out->data || out->size)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest checkpoint requires an idle owner and empty output");
    qa_native_guest_cpu state;
    if (!qa_native_guest_cpu_read(guest, &state, error)) return false;
    guest_codec io = {.error = error};
    uint8_t magic[4] = {'Q','A','N','G'};
    uint32_t version = 2;
    qa_native_image_info saved_image = guest->options.image;
    bool okay = bytes(&io, magic, sizeof(magic)) && u32(&io, &version) && image(&io, &saved_image) &&
        u64(&io, &guest->options.allocation_base) && u64(&io, &guest->allocation_cursor) &&
        u64(&io, &guest->next_mapping) && u64(&io, &guest->next_backing) && cpu(&io, &state);
    size_t total = guest->backing_count;
    if (okay) okay = count(&io, &total, 16);
    for (size_t i = 0; okay && i < guest->backing_count; ++i) {
        guest_backing *backing = &guest->backings[i]; uint64_t length = backing->bytes;
        okay = u64(&io, &backing->id) && u64(&io, &length) && bytes(&io, backing->data, backing->bytes);
    }
    total = guest->mapping_count;
    if (okay) okay = count(&io, &total, 44);
    for (size_t i = 0; okay && i < guest->mapping_count; ++i) okay = mapping(&io, &guest->mappings[i]);
    total = guest->allocation_count;
    if (okay) okay = count(&io, &total, 28);
    for (size_t i = 0; okay && i < guest->allocation_count; ++i) {
        guest_allocation *allocation = &guest->allocations[i];
        uint64_t length = allocation->bytes; uint32_t tag; memcpy(&tag, &allocation->tag, sizeof(tag));
        okay = u64(&io, &allocation->address) && u64(&io, &allocation->mapping) &&
            u64(&io, &length) && u32(&io, &tag);
    }
    total = guest->callback_count;
    if (okay) okay = count(&io, &total, 16);
    for (size_t i = 0; okay && i < guest->callback_count; ++i)
        okay = u64(&io, &guest->callbacks[i].id) && u64(&io, &guest->callbacks[i].address);
    if (okay) *out = io.output;
    else qa_buffer_free(&io.output);
    return okay;
}

static bool restore_backings(guest_codec *io, qa_native_guest *guest)
{
    size_t total = 0;
    if (!count(io, &total, 16) || !guest_grow((void **)&guest->backings,
        &guest->backing_capacity, total, sizeof(*guest->backings), io->error)) return false;
    for (size_t i = 0; i < total; ++i) {
        uint64_t id = 0, length = 0;
        if (!u64(io, &id) || !u64(io, &length)) return false;
        if (!id || id >= guest->next_backing || guest_backing_at(guest, id) ||
            !length || length > SIZE_MAX || length % QA_NATIVE_GUEST_PAGE ||
            length > io->input.size - io->offset ||
            length > guest->options.maximum_backing_bytes - guest->backing_bytes)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved backing is invalid");
        uint8_t *data = malloc((size_t)length);
        if (!data) return guest_fail(io->error, QA_ERROR_MEMORY, io->offset, "restoring native guest backing");
        if (!bytes(io, data, (size_t)length)) { free(data); return false; }
        guest->backings[guest->backing_count++] = (guest_backing){id, data, (size_t)length, 0};
        guest->backing_bytes += (size_t)length;
    }
    return true;
}

static bool restore_mappings(guest_codec *io, qa_native_guest *guest)
{
    size_t total = 0;
    if (!count(io, &total, 44)) return false;
    for (size_t i = 0; i < total; ++i) {
        qa_native_guest_mapping saved = {0};
        if (!mapping(io, &saved)) return false;
        if (saved.id >= guest->next_mapping)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved mapping exceeds its identity cursor");
        if (!guest_install_mapping(guest, &saved, io->error)) return false;
    }
    for (size_t i = 0; i < guest->backing_count; ++i)
        if (!guest->backings[i].references)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved backing has no mapping owner");
    return true;
}

static bool restore_allocations(guest_codec *io, qa_native_guest *guest)
{
    size_t total = 0;
    if (!count(io, &total, 28) || !guest_grow((void **)&guest->allocations,
        &guest->allocation_capacity, total, sizeof(*guest->allocations), io->error)) return false;
    for (size_t i = 0; i < total; ++i) {
        uint64_t address = 0, id = 0, length = 0; uint32_t tag = 0;
        if (!u64(io, &address) || !u64(io, &id) || !u64(io, &length) || !u32(io, &tag)) return false;
        qa_native_guest_mapping *saved = guest_mapping(guest, address);
        if (!saved || saved->id != id || saved->base != address || saved->backing_offset ||
            !length || length > SIZE_MAX || length > SIZE_MAX - (QA_NATIVE_GUEST_PAGE - 1) ||
            ((length + QA_NATIVE_GUEST_PAGE - 1) & ~(uint64_t)(QA_NATIVE_GUEST_PAGE - 1)) != saved->bytes ||
            address < guest->options.allocation_base || address + saved->bytes > guest->allocation_cursor)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved allocation has no exact storage owner");
        for (size_t j = 0; j < guest->allocation_count; ++j)
            if (guest->allocations[j].mapping == id)
                return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved allocation repeats a mapping owner");
        int32_t signed_tag; memcpy(&signed_tag, &tag, sizeof(tag));
        guest->allocations[guest->allocation_count++] = (guest_allocation){address, id, (size_t)length, signed_tag};
    }
    return true;
}

static bool restore_callbacks(guest_codec *io, qa_native_guest *guest,
    qa_native_guest_callback_resolve_fn resolve, void *context)
{
    size_t total = 0;
    if (!count(io, &total, 16)) return false;
    if (total && !resolve)
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved callbacks require their real resolver");
    if (!guest_grow((void **)&guest->callbacks, &guest->callback_capacity,
        total, sizeof(*guest->callbacks), io->error)) return false;
    for (size_t i = 0; i < total; ++i) {
        uint64_t id = 0, address = 0;
        if (!u64(io, &id) || !u64(io, &address)) return false;
        if (!id) return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved callback has no identity");
        if (!guest_range(guest, address, 1, QA_NATIVE_GUEST_EXECUTE, io->error)) return false;
        for (size_t j = 0; j < guest->callback_count; ++j)
            if (guest->callbacks[j].id == id || guest->callbacks[j].address == address)
                return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved callback identity repeats");
        qa_native_guest_callback callback = {0};
        if (!resolve(context, id, address, &callback, io->error)) return false;
        if (callback.id != id || callback.address != address || !callback.invoke)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest callback resolver changed saved identity");
        guest->callbacks[guest->callback_count++] = callback;
    }
    return true;
}

bool qa_native_guest_restore(qa_bytes encoded, const qa_native_guest_options *options,
    qa_native_guest_callback_resolve_fn resolve, void *context,
    qa_native_guest **out, qa_error *error)
{
    if (!options || !out || *out || (!encoded.data && encoded.size))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest restore requires an inert output and actual artifact identity");
    guest_codec io = {.input = encoded, .reading = true, .error = error};
    uint8_t magic[4]; uint32_t version = 0;
    qa_native_image_info saved_image = {0};
    uint64_t allocation_base = 0, cursor = 0, next_mapping = 0, next_backing = 0;
    qa_native_guest_cpu state = {0};
    bool okay = bytes(&io, magic, sizeof(magic)) && !memcmp(magic, "QANG", sizeof(magic)) &&
        u32(&io, &version) && version == 2 && image(&io, &saved_image) &&
        same_image(&saved_image, &options->image) && u64(&io, &allocation_base) &&
        allocation_base == options->allocation_base && u64(&io, &cursor) &&
        u64(&io, &next_mapping) && next_mapping && u64(&io, &next_backing) && next_backing &&
        cursor >= allocation_base && !(cursor % QA_NATIVE_GUEST_PAGE) &&
        (saved_image.target.pointer_bytes != 4 || cursor <= UINT64_C(0x100000000)) && cpu(&io, &state);
    qa_native_guest *candidate = NULL;
    if (okay) okay = qa_native_guest_create(options, &candidate, error);
    if (okay) {
        candidate->restoring = true;
        candidate->allocation_cursor = cursor;
        candidate->next_mapping = next_mapping; candidate->next_backing = next_backing;
        okay = restore_backings(&io, candidate) && restore_mappings(&io, candidate) &&
            restore_allocations(&io, candidate) && restore_callbacks(&io, candidate, resolve, context) &&
            io.offset == io.input.size;
        candidate->restoring = false;
        if (okay) okay = qa_native_guest_cpu_write(candidate, &state, error);
    }
    if (okay) { *out = candidate; return true; }
    if (error && error->code == QA_OK)
        guest_fail(error, QA_ERROR_FORMAT, io.offset, "native guest checkpoint identity or structure is invalid");
    if (candidate) {
        /* A rejected CPU close retains the actual failed candidate, including
         * all callback contexts, for the caller's destruction retry. */
        qa_error cleanup = {0};
        if (!qa_native_guest_destroy(&candidate, &cleanup)) {
            *out = candidate;
            if (error && error->code == QA_OK) *error = cleanup;
        }
    }
    return false;
}

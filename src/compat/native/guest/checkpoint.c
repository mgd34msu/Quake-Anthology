#include "internal.h"
#include "native_cpu_codec.h"
#include "qa/source_save.h"

static bool count(qa_source_save_io *io, size_t *value, size_t minimum_bytes)
{
    size_t remaining = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ?
        (remaining < 8 ? 0 : (remaining - 8) / minimum_bytes) : SIZE_MAX;
    return qa_source_save_count(io, value, maximum);
}

static bool baseline_read(const qa_native_guest_baseline *baseline, uint64_t backing,
    size_t extent, const qa_native_guest_file *file, qa_bytes *out, qa_error *error)
{
    *out = (qa_bytes){0};
    if (baseline && baseline->read)
        return baseline->read(baseline->context, backing, extent, file, out, error);
    return !file || guest_fail(error, QA_ERROR_UNSUPPORTED, backing,
        "native file pages require their actual resource baseline");
}

static bool image(qa_source_save_io *io, qa_native_image_info *saved)
{
    uint32_t format = saved->format, os = saved->target.os, arch = saved->target.arch, abi = saved->target.abi;
    if (!qa_source_save_u32(io, &format) || !qa_source_save_u32(io, &os) || !qa_source_save_u32(io, &arch) || !qa_source_save_u32(io, &abi) ||
        !qa_source_save_bytes(io, &saved->target.pointer_bytes, 1) || !qa_source_save_u64(io, &saved->preferred_base) ||
        !qa_source_save_u64(io, &saved->image_bytes) || !qa_source_save_bytes(io, saved->digest.bytes, sizeof(saved->digest.bytes))) return false;
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

static bool cpu(qa_source_save_io *io, qa_native_guest_cpu *state)
{
    for (size_t i = 0; i < QA_NATIVE_REGISTER_COUNT; ++i) if (!qa_source_save_u64(io, &state->registers[i])) return false;
    if (!qa_source_save_u64(io, &state->instruction) || !qa_source_save_u64(io, &state->flags)) return false;
    for (size_t i = 0; i < 10; ++i) {
        qa_native_guest_table *table = i < 6 ? &state->segments[i] : &state->tables[i - 6];
        if (!qa_source_save_u16(io, &table->selector) || !qa_source_save_u64(io, &table->base) ||
            !qa_source_save_u32(io, &table->limit) || !qa_source_save_u32(io, &table->flags)) return false;
    }
    for (size_t i = 0; i < 9; ++i) if (!qa_source_save_u64(io, &state->control[i])) return false;
    for (size_t i = 0; i < 8; ++i) if (!qa_source_save_u64(io, &state->debug[i])) return false;
    if (!qa_source_save_u64(io, &state->efer) || !qa_source_save_u64(io, &state->xcr0) || !qa_source_save_u64(io, &state->xstate_bv) ||
        !qa_source_save_u32(io, &state->execution_flags) || !qa_source_save_u32(io, &state->execution_flags2) ||
        !qa_source_save_u32(io, &state->a20_mask)) return false;
    for (size_t i = 0; i < 8; ++i)
        if (!qa_source_save_u64(io, &state->fp_mantissa[i]) || !qa_source_save_u16(io, &state->fp_exponent[i])) return false;
    if (!qa_source_save_u16(io, &state->fp_control) || !qa_source_save_u16(io, &state->fp_status) || !qa_source_save_u16(io, &state->fp_tags) ||
        !qa_source_save_u64(io, &state->fp_instruction) || !qa_source_save_u64(io, &state->fp_operand) ||
        !qa_source_save_u16(io, &state->fp_code_selector) || !qa_source_save_u16(io, &state->fp_data_selector) ||
        !qa_source_save_u16(io, &state->fp_opcode) || !qa_source_save_u32(io, &state->mxcsr)) return false;
    for (size_t i = 0; i < 16; ++i)
        if (!qa_source_save_u64(io, &state->xmm[i][0]) || !qa_source_save_u64(io, &state->xmm[i][1])) return false;
    return true;
}

static bool mapping(qa_source_save_io *io, qa_native_guest_mapping *saved)
{
    return qa_source_save_u64(io, &saved->id) && qa_source_save_u64(io, &saved->base) && qa_source_save_u64(io, &saved->bytes) &&
        qa_source_save_u64(io, &saved->backing) && qa_source_save_u64(io, &saved->backing_offset) && qa_source_save_u32(io, &saved->permissions);
}

static bool source_profile(qa_source_save_io *io, guest_profile_guard_receipt *profile,
    guest_profile_cpu_domain *domain)
{
    uint32_t unused = 0, has_pkru = domain->has_pkru ? 1 : 0;
    if (!qa_source_save_u32(io, &profile->policy) || !qa_source_save_u32(io, &unused) ||
        !qa_source_save_u64(io, &domain->xfeatures) || !qa_source_save_u32(io, &domain->pkru) ||
        !qa_source_save_u32(io, &has_pkru)) return false;
    if (profile->policy != GUEST_PROFILE_GUARD_SOURCE_X64 ||
        has_pkru > 1 ||
        (domain->xfeatures & 3) != 3 ||
        has_pkru != ((domain->xfeatures & (UINT64_C(1) << 9)) != 0) ||
        (!has_pkru && domain->pkru) || (domain->pkru & 3))
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset,
            "native saved monitor or named source CPU domain is invalid");
    domain->has_pkru = has_pkru != 0;
    return true;
}

static bool same_profile(const guest_profile_guard_receipt *a,
    const guest_profile_cpu_domain *x, const guest_profile_guard_receipt *b,
    const guest_profile_cpu_domain *y)
{
    return a->policy == b->policy &&
        x->xfeatures == y->xfeatures && x->pkru == y->pkru && x->has_pkru == y->has_pkru;
}

bool qa_native_guest_checkpoint(qa_native_guest *guest, const qa_native_guest_baseline *baseline,
    qa_buffer *out, qa_error *error)
{
    if (!qa_native_guest_idle(guest) || !out || out->data || out->size)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest checkpoint requires an idle owner and empty output");
    qa_native_guest_cpu state = {0}; qa_buffer hardware = {0};
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64) {
        guest_host_x86_64_state physical = {0};
        guest_profile_guard_receipt profile = {0}; guest_profile_cpu_domain domain = {0};
        bool okay = guest_host_child_profile_read(guest->child, &profile, error) &&
            guest_host_child_source_domain(guest->child, &domain, error) &&
            same_profile(&profile, &domain, &guest->profile, &guest->source_domain);
        if (!okay && error && error->code == QA_OK)
            guest_fail(error, QA_ERROR_FORMAT, 0, "native child lost its actual source monitor or CPU domain");
        if (okay) okay = guest_host_child_cpu_read(guest->child, &physical, error) &&
            guest_profile_cpu_current(&domain, guest_host_child_capability(guest->child), &physical, error);
        if (!okay) guest->failed = true;
        okay = okay &&
            guest_native_cpu_checkpoint(&physical, guest_host_child_capability(guest->child), &hardware, error);
        guest_host_x86_64_state_free(&physical);
        if (!okay) return false;
    } else if (!qa_native_guest_cpu_read(guest, &state, error)) return false;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) { qa_buffer_free(&hardware); return false; }
    uint8_t magic[4] = {'Q','A','N','G'};
    uint32_t backend = guest->options.backend;
    qa_native_image_info saved_image = guest->options.image;
    bool okay = qa_source_save_bytes(&io, magic, sizeof(magic)) && qa_source_save_u32(&io, &backend) && image(&io, &saved_image) &&
        qa_source_save_u64(&io, &guest->options.allocation_base) && qa_source_save_u64(&io, &guest->allocation_cursor) &&
        qa_source_save_u64(&io, &guest->next_mapping) && qa_source_save_u64(&io, &guest->next_backing);
    if (okay && backend == QA_NATIVE_GUEST_HOST_X86_64) {
        uint64_t length = hardware.size;
        okay = source_profile(&io, &guest->profile, &guest->source_domain) &&
            qa_source_save_u64(&io, &length) && qa_source_save_bytes(&io, hardware.data, hardware.size);
    } else if (okay) okay = cpu(&io, &state);
    qa_buffer_free(&hardware);
    size_t total = guest->backing_count;
    if (okay) okay = count(&io, &total, 28);
    for (size_t i = 0; okay && i < guest->backing_count; ++i) {
        guest_backing *backing = &guest->backings[i]; uint64_t length = backing->bytes;
        uint32_t file = backing->file ? 1 : 0;
        okay = qa_source_save_u64(&io, &backing->id) && qa_source_save_u64(&io, &length) && qa_source_save_u32(&io, &file);
        if (okay && file) okay = qa_source_save_bytes(&io, backing->source.digest.bytes, 32) &&
            qa_source_save_u64(&io, &backing->source.bytes) && qa_source_save_u64(&io, &backing->source.offset) &&
            qa_source_save_u64(&io, &backing->source.accessible_bytes);
        qa_bytes pristine = {0};
        if (okay) okay = baseline_read(baseline, backing->id, backing->bytes,
            file ? &backing->source : NULL, &pristine, error) &&
            qa_source_save_memory_delta(&io, backing->data, backing->bytes, pristine);
    }
    total = guest->mapping_count;
    if (okay) okay = count(&io, &total, 44);
    for (size_t i = 0; okay && i < guest->mapping_count; ++i) okay = mapping(&io, &guest->mappings[i]);
    total = guest->allocation_count;
    if (okay) okay = count(&io, &total, 28);
    for (size_t i = 0; okay && i < guest->allocation_count; ++i) {
        guest_allocation *allocation = &guest->allocations[i];
        uint64_t length = allocation->bytes; uint32_t tag; memcpy(&tag, &allocation->tag, sizeof(tag));
        okay = qa_source_save_u64(&io, &allocation->address) && qa_source_save_u64(&io, &allocation->mapping) &&
            qa_source_save_u64(&io, &length) && qa_source_save_u32(&io, &tag);
    }
    total = guest->callback_count;
    if (okay) okay = count(&io, &total, 16);
    for (size_t i = 0; okay && i < guest->callback_count; ++i)
        okay = qa_source_save_u64(&io, &guest->callbacks[i].id) && qa_source_save_u64(&io, &guest->callbacks[i].address);
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay;
}

static bool restore_backings(qa_source_save_io *io, qa_native_guest *guest,
    const qa_native_guest_baseline *baseline)
{
    size_t total = 0;
    if (!count(io, &total, 28) || !guest_grow((void **)&guest->backings,
        &guest->backing_capacity, total, sizeof(*guest->backings), io->error)) return false;
    for (size_t i = 0; i < total; ++i) {
        uint64_t id = 0, length = 0; uint32_t file = 0;
        qa_native_guest_file source = {0};
        if (!qa_source_save_u64(io, &id) || !qa_source_save_u64(io, &length) || !qa_source_save_u32(io, &file) || file > 1) return false;
        if (file && (!qa_source_save_bytes(io, source.digest.bytes, 32) || !qa_source_save_u64(io, &source.bytes) ||
            !qa_source_save_u64(io, &source.offset) || !qa_source_save_u64(io, &source.accessible_bytes))) return false;
        if (!id || id >= guest->next_backing || guest_backing_at(guest, id) ||
            !length || length > SIZE_MAX || length % QA_NATIVE_GUEST_PAGE ||
            length > guest->options.maximum_backing_bytes - guest->backing_bytes)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved backing is invalid");
        if (file) {
            uint64_t copied = source.offset < source.bytes ? source.bytes - source.offset : 0;
            if (copied > length) copied = length;
            uint64_t admitted = copied;
            if (admitted % QA_NATIVE_GUEST_PAGE)
                admitted += QA_NATIVE_GUEST_PAGE - admitted % QA_NATIVE_GUEST_PAGE;
            if (source.offset % QA_NATIVE_GUEST_PAGE || source.accessible_bytes != admitted)
                return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native saved file pages differ from their actual EOF boundary");
        }
        uint8_t *data = malloc((size_t)length);
        if (!data) return guest_fail(io->error, QA_ERROR_MEMORY, io->offset, "restoring native guest backing");
        qa_bytes pristine = {0};
        if (!baseline_read(baseline, id, (size_t)length, file ? &source : NULL,
            &pristine, io->error) ||
            !qa_source_save_memory_delta(io, data, (size_t)length, pristine)) {
            free(data); return false;
        }
        guest->backings[guest->backing_count++] = (guest_backing){.id = id, .data = data,
            .bytes = (size_t)length, .file = file != 0, .source = source};
        guest->backing_bytes += (size_t)length;
    }
    return true;
}

static bool restore_mappings(qa_source_save_io *io, qa_native_guest *guest)
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

static bool restore_allocations(qa_source_save_io *io, qa_native_guest *guest)
{
    size_t total = 0;
    if (!count(io, &total, 28) || !guest_grow((void **)&guest->allocations,
        &guest->allocation_capacity, total, sizeof(*guest->allocations), io->error)) return false;
    for (size_t i = 0; i < total; ++i) {
        uint64_t address = 0, id = 0, length = 0; uint32_t tag = 0;
        if (!qa_source_save_u64(io, &address) || !qa_source_save_u64(io, &id) || !qa_source_save_u64(io, &length) || !qa_source_save_u32(io, &tag)) return false;
        if (!length || length > SIZE_MAX)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved allocation has no exact storage owner");
        int32_t signed_tag; memcpy(&signed_tag, &tag, sizeof(tag));
        guest_allocation allocation = {address, id, (size_t)length, signed_tag};
        uint64_t backing = 0; size_t rounded = 0;
        if (!guest_allocation_storage(guest, &allocation, &backing, &rounded, io->error)) return false;
        if (address < guest->options.allocation_base || address + rounded > guest->allocation_cursor)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved allocation exceeds its actual allocator cursor");
        for (size_t j = 0; j < guest->allocation_count; ++j)
            if (guest_mapping(guest, guest->allocations[j].address)->backing == backing)
                return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved allocation repeats a mapping owner");
        guest->allocations[guest->allocation_count++] = allocation;
    }
    return true;
}

static bool restore_callbacks(qa_source_save_io *io, qa_native_guest *guest,
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
        if (!qa_source_save_u64(io, &id) || !qa_source_save_u64(io, &address)) return false;
        if (!id) return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved callback has no identity");
        if (!guest_range(guest, address, 1, QA_NATIVE_GUEST_EXECUTE, io->error)) return false;
        for (size_t j = 0; j < guest->callback_count; ++j)
            if (guest->callbacks[j].id == id || guest->callbacks[j].address == address)
                return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest saved callback identity repeats");
        qa_native_guest_callback callback = {0};
        if (!resolve(context, id, address, &callback, io->error)) return false;
        if (callback.id != id || callback.address != address || !callback.invoke)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "native guest callback resolver changed saved identity");
        if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
            !guest_native_result(guest, guest_host_child_bind(guest->child, id, address, io->error), io->error)) return false;
        guest->callbacks[guest->callback_count++] = callback;
    }
    return true;
}

bool qa_native_guest_restore(qa_bytes encoded, const qa_native_guest_options *options,
    qa_native_guest_callback_resolve_fn resolve, void *context,
    const qa_native_guest_baseline *baseline, qa_native_guest **out, qa_error *error)
{
    if (!options || !out || *out || (!encoded.data && encoded.size))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest restore requires an inert output and actual artifact identity");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, encoded, error)) return false;
    uint8_t magic[4]; uint32_t backend = 0;
    qa_native_image_info saved_image = {0};
    uint64_t allocation_base = 0, cursor = 0, next_mapping = 0, next_backing = 0;
    qa_native_guest_cpu state = {0};
    bool okay = qa_source_save_bytes(&io, magic, sizeof(magic)) && !memcmp(magic, "QANG", sizeof(magic)) &&
        qa_source_save_u32(&io, &backend);
    okay = okay && backend == (uint32_t)options->backend && image(&io, &saved_image) &&
        same_image(&saved_image, &options->image) && qa_source_save_u64(&io, &allocation_base) &&
        allocation_base == options->allocation_base && qa_source_save_u64(&io, &cursor) &&
        qa_source_save_u64(&io, &next_mapping) && next_mapping && qa_source_save_u64(&io, &next_backing) && next_backing &&
        cursor >= allocation_base && !(cursor % QA_NATIVE_GUEST_PAGE) &&
        (saved_image.target.pointer_bytes != 4 || cursor <= UINT64_C(0x100000000));
    qa_bytes hardware = {0};
    guest_profile_guard_receipt profile = {0}; guest_profile_cpu_domain domain = {0};
    if (okay && backend == QA_NATIVE_GUEST_HOST_X86_64) {
        uint64_t length = 0;
        /* Unguarded hardware capsules have no actual monitor/domain witness.
         * They cannot be upgraded by inventing one during cold construction. */
        okay = source_profile(&io, &profile, &domain) &&
            qa_source_save_u64(&io, &length) && length <= io.input.size - io.offset;
        if (okay) okay = qa_source_save_span(&io, (size_t)length, &hardware);
    } else if (okay) okay = cpu(&io, &state);
    qa_native_guest *candidate = NULL;
    if (okay) okay = guest_create(options, false, &candidate, error);
    guest_host_x86_64_state physical = {0};
    if (okay && backend == QA_NATIVE_GUEST_HOST_X86_64) {
        okay = same_profile(&profile, &domain, &candidate->profile, &candidate->source_domain);
        if (!okay) guest_fail(error, QA_ERROR_UNSUPPORTED, 0,
            "saved native monitor or CPU domain differs from the actual candidate child");
        if (okay) okay = guest_native_cpu_restore(hardware,
            guest_host_child_capability(candidate->child), &physical, error) &&
            guest_profile_cpu_current(&domain, guest_host_child_capability(candidate->child), &physical, error);
    }
    if (okay) {
        candidate->restoring = true;
        candidate->allocation_cursor = cursor;
        candidate->next_mapping = next_mapping; candidate->next_backing = next_backing;
        okay = restore_backings(&io, candidate, baseline) && restore_mappings(&io, candidate) &&
            restore_allocations(&io, candidate) && restore_callbacks(&io, candidate, resolve, context) &&
            qa_source_save_finish(&io, NULL);
        candidate->restoring = false;
        if (okay) okay = backend == QA_NATIVE_GUEST_HOST_X86_64 ?
            guest_host_child_cpu_write(candidate->child, &physical, error) :
            qa_native_guest_cpu_write(candidate, &state, error);
    }
    guest_host_x86_64_state_free(&physical);
    if (okay) { *out = candidate; qa_source_save_dispose(&io); return true; }
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
    qa_source_save_dispose(&io);
    return false;
}

#include "windows_process_private.h"
#include "qa/native_windows_locale_save.h"
#include "qa/native_windows_process_save.h"
#include "qa/source_save.h"

typedef struct saved_windows_image {
    qa_native_image_info image;
    uint64_t id, base;
    size_t maximum;
    qa_bytes path, attachment;
} saved_windows_image;

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
    return (os == QA_NATIVE_OS_WINDOWS &&
        ((format == QA_NATIVE_IMAGE_PE32 && arch == QA_NATIVE_ARCH_I386 &&
          abi == QA_NATIVE_ABI_CDECL_I386 && image->target.pointer_bytes == 4) ||
         (format == QA_NATIVE_IMAGE_PE32_PLUS && arch == QA_NATIVE_ARCH_X86_64 &&
          abi == QA_NATIVE_ABI_MICROSOFT_X64 && image->target.pointer_bytes == 8))) ||
        guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows saved artifact has a different actual ABI");
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = bytes->size;
    if (!qa_source_save_count(io, &count, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) return qa_source_save_span(io, count, bytes);
    return qa_source_save_bytes(io, (void *)bytes->data, count);
}
static bool process_fields(qa_source_save_io *io, qa_native_windows_process *owner)
{
    uint8_t magic[] = {'Q','W','P','R'}, expected[] = {'Q','W','P','R'};
    qa_native_windows_process_options *o = &owner->options;
    qa_native_windows_capabilities *c = &o->capabilities;
    uint32_t backend = o->guest.backend;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)))
        return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows process capsule signature differs");
    if (!image_fields(io, &o->guest.image) || !qa_source_save_u32(io, &backend)) return false;
    o->guest.backend = (qa_native_guest_backend)backend;
    uint64_t *fields[] = {&o->guest.allocation_base, &o->primary_image, &owner->stack,
        &c->id, &c->streams[0].id, &c->streams[1].id, &c->streams[2].id};
    for (size_t i = 0; i < sizeof(fields)/sizeof(*fields); ++i)
        if (!qa_source_save_u64(io, fields[i])) return false;
    if (!qa_source_save_count(io, &o->guest.maximum_backing_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->stack_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &o->instruction_budget, SIZE_MAX) ||
        !qa_source_save_i64(io, &c->performance_frequency) ||
        !qa_native_windows_locale_profile_save(io,&c->locale)) return false;
    bool native = backend == QA_NATIVE_GUEST_HOST_X86_64;
    return ((backend == QA_NATIVE_GUEST_EMULATED || native) &&
        (native ? o->instruction_budget == 0 : o->instruction_budget != 0) &&
        (!native || (o->guest.image.target.arch == QA_NATIVE_ARCH_X86_64 &&
         o->guest.image.target.abi == QA_NATIVE_ABI_MICROSOFT_X64 && o->guest.image.target.pointer_bytes == 8)) &&
        o->guest.maximum_backing_bytes && o->guest.allocation_base &&
        !(o->guest.allocation_base % QA_NATIVE_GUEST_PAGE) && o->primary_image && c->id &&
        c->performance_frequency > 0 && owner->stack && o->stack_bytes &&
        !(o->stack_bytes % QA_NATIVE_GUEST_PAGE) && o->stack_bytes <= UINT64_MAX - owner->stack &&
        (o->guest.image.target.pointer_bytes != 4 || owner->stack + o->stack_bytes <= UINT32_MAX)) ||
        guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "Windows saved process construction is invalid");
}
static bool runtime_matches(const qa_native_windows_process *owner, qa_error *error)
{
    const guest_windows *r = owner->runtime;
    const qa_native_windows_process_options *o = &owner->options;
    if (!r || r->primary_image != o->primary_image || r->execution != o->guest.backend || r->target.os != o->guest.image.target.os ||
        r->target.arch != o->guest.image.target.arch || r->target.abi != o->guest.image.target.abi ||
        r->target.pointer_bytes != o->guest.image.target.pointer_bytes || r->stack_base != owner->stack ||
        r->stack_bytes != o->stack_bytes || r->instruction_budget != o->instruction_budget ||
        r->capability_id != o->capabilities.id ||
        r->capabilities.performance_frequency != o->capabilities.performance_frequency ||
        !qa_native_windows_locale_profile_equal(&r->capabilities.locale,&o->capabilities.locale) ||
        r->image_count != owner->image_count)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows process differs from its actual runtime owner");
    for (size_t i = 0; i < 3; ++i)
        if (r->stream_ids[i] != o->capabilities.streams[i].id)
            return guest_fail(error, QA_ERROR_FORMAT, i, "Windows process standard capability identity differs");
    bool primary = false;
    for (size_t i = 0; i < owner->image_count; ++i) {
        const windows_process_image *row = owner->images + i;
        const guest_pe_view *pe = guest_pe_describe(row->artifact);
        const windows_image_record *image = r->images + i;
        if (!pe || image->id != row->id || image->image != row->artifact || !image->prepared ||
            strcmp(image->path, row->path) || image->base != pe->base ||
            !qa_sha256_equal(&image->digest, &pe->image.digest))
            return guest_fail(error, QA_ERROR_FORMAT, i, "Windows process source graph differs from its prepared runtime inventory");
        size_t alignment = pe->tls.alignment < 16 ? 16 : pe->tls.alignment;
        if (pe->tls.present && (!image->tls_block || image->tls_block % alignment))
            return guest_fail(error, QA_ERROR_FORMAT, i, "Windows saved static TLS has a different actual artifact alignment");
        if (row->id == o->primary_image) primary = windows_process_same_image(&pe->image, &o->guest.image);
    }
    return primary || guest_fail(error, QA_ERROR_FORMAT, o->primary_image, "Windows process primary witness is absent");
}

bool qa_native_windows_process_checkpoint(qa_native_windows_process *owner, qa_buffer *out, qa_error *error)
{
    if (!qa_native_windows_process_idle(owner) || !out || out->data || out->size)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows process capture requires complete idle ownership");
    owner->busy = true;
    qa_buffer runtime = {0}, guest = {0}, provenance = {0}; qa_source_save_io io;
    bool okay = windows_process_current(owner, error) && runtime_matches(owner, error) &&
        windows_process_storage(owner, error) && windows_process_artifacts(owner, false, error) &&
        guest_profile_artifacts_checkpoint(owner->provenance, &provenance, error) &&
        guest_windows_checkpoint(owner->runtime, &runtime, error) &&
        qa_native_guest_checkpoint(owner->guest, &guest, error);
    bool writer = okay && qa_source_save_writer(&io, NULL, error);
    if (writer) {
        qa_bytes runtime_bytes = {runtime.data, runtime.size}, guest_bytes = {guest.data, guest.size};
        qa_bytes provenance_bytes = {provenance.data, provenance.size};
        size_t count = owner->image_count;
        okay = process_fields(&io, owner) && qa_source_save_count(&io, &count, SIZE_MAX) &&
            blob(&io, &runtime_bytes) && blob(&io, &guest_bytes) && blob(&io, &provenance_bytes);
        for (size_t i = 0; okay && i < count; ++i) {
            windows_process_image *row = owner->images + i;
            const guest_pe_view *pe = guest_pe_describe(row->artifact); qa_buffer attachment = {0};
            okay = pe && guest_pe_memory_checkpoint(row->memory, &attachment, error);
            if (okay) {
                saved_windows_image record = {pe->image, row->id, pe->base, pe->bytes.size,
                    {(const uint8_t *)row->path, strlen(row->path)}, {attachment.data, attachment.size}};
                okay = image_fields(&io, &record.image) && qa_source_save_u64(&io, &record.id) &&
                    qa_source_save_u64(&io, &record.base) && qa_source_save_count(&io, &record.maximum, SIZE_MAX) &&
                    blob(&io, &record.path) && blob(&io, &record.attachment);
            }
            qa_buffer_free(&attachment);
        }
        if (okay) okay = qa_source_save_finish(&io, out);
        qa_source_save_dispose(&io);
    } else okay = false;
    qa_buffer_free(&runtime); qa_buffer_free(&guest); qa_buffer_free(&provenance);
    owner->busy = false; return okay;
}

static bool image_resolve(void *context, uint64_t id, const qa_sha256_digest *digest,
    uint64_t base, guest_windows_image *out, qa_error *error)
{
    qa_native_windows_process *owner = context;
    for (size_t i = 0; i < owner->image_count; ++i) {
        const windows_process_image *row = owner->images + i;
        const guest_pe_view *pe = guest_pe_describe(row->artifact);
        if (row->id == id && pe && pe->base == base && qa_sha256_equal(digest, &pe->image.digest)) {
            *out = (guest_windows_image){id, row->artifact, row->path}; return true;
        }
    }
    return guest_fail(error, QA_ERROR_FORMAT, id, "Windows cold runtime requested a different actual source artifact");
}
static bool capabilities_match(const qa_native_windows_process *owner,
    const qa_native_windows_process_restore_bindings *bindings, qa_error *error)
{
    const qa_native_windows_capabilities *a = &owner->options.capabilities, *b = &bindings->capabilities;
    if (owner->options.guest.backend != bindings->backend ||
        owner->options.guest.maximum_backing_bytes != bindings->maximum_backing_bytes ||
        a->id != b->id || a->performance_frequency != b->performance_frequency ||
        !qa_native_windows_locale_profile_equal(&a->locale,&b->locale))
        return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows cold process differs from actual external authority");
    for (size_t i = 0; i < 3; ++i)
        if (a->streams[i].id != b->streams[i].id ||
            (!!b->streams[i].id != (i ? b->streams[i].write != NULL : b->streams[i].read != NULL)))
            return guest_fail(error, QA_ERROR_FORMAT, i, "Windows cold standard stream differs from its actual capability");
    return true;
}
typedef struct windows_restore_callbacks {
    guest_windows *runtime;
    const qa_native_windows_process_restore_bindings *bindings;
} windows_restore_callbacks;

static bool restore_callback(void *context, uint64_t id, uint64_t address,
    qa_native_guest_callback *out, qa_error *error)
{
    windows_restore_callbacks *callbacks = context;
    qa_error local = {0};
    if (guest_windows_callback(callbacks->runtime, id, address, out, &local)) return true;
    if (local.code == QA_ERROR_NOT_FOUND && callbacks->bindings->external_callback)
        return callbacks->bindings->external_callback(callbacks->bindings->external_context,
            id, address, out, error);
    if (error) *error = local;
    return false;
}

bool qa_native_windows_process_restore(qa_bytes encoded,
    const qa_native_windows_process_restore_bindings *bindings, qa_native_windows_process **out, qa_error *error)
{
    if (!bindings || !out || *out || !bindings->maximum_backing_bytes ||
        !bindings->artifacts || !bindings->artifact_count ||
        bindings->backend > QA_NATIVE_GUEST_HOST_X86_64 ||
        (bindings->backend == QA_NATIVE_GUEST_HOST_X86_64 &&
            (!bindings->host_executable || !*bindings->host_executable || !bindings->profile_guard)) ||
        !bindings->capabilities.id || !bindings->capabilities.current || !bindings->capabilities.entropy ||
        !bindings->capabilities.milliseconds || !bindings->capabilities.performance || !bindings->capabilities.calendar ||
        bindings->capabilities.performance_frequency <= 0 ||
        !qa_native_windows_locale_profile_valid(&bindings->capabilities.locale) ||
        (bindings->capabilities.locale.source == 2) != (bindings->capabilities.compare_string != NULL))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows restore needs its actual prepared capability graph");
    qa_native_windows_process *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning cold Windows process");
    owner->busy = true; owner->provisional = true;
    qa_source_save_io io; bool reader = qa_source_save_reader(&io, NULL, encoded, error);
    size_t count = 0; qa_bytes runtime = {0}, guest = {0}, provenance = {0}; saved_windows_image *records = NULL;
    bool okay = reader && process_fields(&io, owner) &&
        qa_source_save_count(&io, &count, SIZE_MAX / sizeof(*records)) && count &&
        blob(&io, &runtime) && blob(&io, &guest) && blob(&io, &provenance);
    if (okay && (count != bindings->artifact_count || count > (io.input.size - io.offset) / 105))
        okay = guest_fail(error, QA_ERROR_FORMAT, count, "Windows source count exceeds its complete outer envelope");
    if (okay) {
        records = calloc(count, sizeof(*records));
        if (!records) okay = guest_fail(error, QA_ERROR_MEMORY, count, "owning Windows cold artifact records");
    }
    for (size_t i = 0; okay && i < count; ++i) {
        saved_windows_image *record = records + i;
        okay = image_fields(&io, &record->image) && qa_source_save_u64(&io, &record->id) &&
            qa_source_save_u64(&io, &record->base) && qa_source_save_count(&io, &record->maximum, SIZE_MAX) &&
            blob(&io, &record->path) && blob(&io, &record->attachment);
        if (okay && (!record->id || !record->maximum || !record->path.size ||
            record->path.size == SIZE_MAX || memchr(record->path.data, 0, record->path.size)))
            okay = guest_fail(error, QA_ERROR_FORMAT, i, "Windows saved source identity or path is invalid");
        for (size_t j = 0; okay && j < i; ++j)
            if (records[j].id == record->id) okay = guest_fail(error, QA_ERROR_FORMAT, i, "Windows saved image identity repeats");
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    if (reader) qa_source_save_dispose(&io);
    if (okay) okay = capabilities_match(owner, bindings, error);
    owner->options.capabilities = bindings->capabilities;
    owner->options.guest.host_executable = bindings->host_executable;
    owner->options.guest.profile_guard = bindings->profile_guard;
    if (okay) {
        owner->images = calloc(count, sizeof(*owner->images));
        if (!owner->images) okay = guest_fail(error, QA_ERROR_MEMORY, count, "owning cold Windows source graph");
    }
    for (size_t i = 0; okay && i < count; ++i) {
        const saved_windows_image *record = records + i; windows_process_image *row = owner->images + i;
        const qa_native_windows_artifact *source = NULL;
        for (size_t j = 0; okay && j < bindings->artifact_count; ++j) {
            const qa_native_windows_artifact *actual = bindings->artifacts + j;
            if (actual->id != record->id) continue;
            if (source || actual->load_base != record->base ||
                !windows_process_same_image(&actual->image, &record->image) ||
                record->maximum > actual->maximum_image_bytes || !actual->path ||
                strlen(actual->path) != record->path.size ||
                memcmp(actual->path, record->path.data, record->path.size)) {
                okay = guest_fail(error, QA_ERROR_FORMAT, i, "Windows saved image differs from its installed source");
                break;
            }
            source = actual;
        }
        if (!okay) break;
        if (!source) { okay = guest_fail(error, QA_ERROR_FORMAT, i, "Windows saved image is not installed"); break; }
        row->id = record->id; owner->image_count = i + 1;
        row->path = malloc(record->path.size + 1);
        if (!row->path) { okay = guest_fail(error, QA_ERROR_MEMORY, i, "owning cold Windows image path"); break; }
        memcpy(row->path, record->path.data, record->path.size); row->path[record->path.size] = 0;
        okay = guest_pe_open(source->bytes, &record->image, record->base,
            record->maximum, &row->artifact, error);
    }
    if (okay) okay = guest_profile_artifacts_decode(provenance, owner->options.guest.maximum_backing_bytes,
        &owner->provenance, error) && windows_process_artifacts(owner, false, error) &&
        guest_windows_decode(runtime, image_resolve, owner, &owner->runtime, error) &&
        runtime_matches(owner, error) && bindings->capabilities.current(bindings->capabilities.context, error);
    windows_restore_callbacks callbacks = {owner->runtime, bindings};
    if (okay) okay = qa_native_guest_restore(guest, &owner->options.guest,
        restore_callback, &callbacks, &owner->guest, error);
    guest_windows_capabilities actual = windows_process_capabilities(owner);
    if (okay) okay = windows_process_storage(owner, error) &&
        guest_windows_attach(owner->runtime, owner->guest, &actual, error);
    for (size_t i = 0; okay && i < owner->image_count; ++i)
        okay = guest_pe_memory_adopt(owner->images[i].artifact, owner->guest,
            records[i].attachment, false, &owner->images[i].memory, error);
    free(records); owner->options.artifact_count = owner->image_count;
    owner->options.guest.host_executable = NULL;
    owner->options.guest.profile_guard = NULL;
    owner->complete = okay; owner->failed = !okay; owner->busy = false;
    if (okay) { *out = owner; return true; }
    qa_error cleanup = {0};
    if (!qa_native_windows_process_dispose(&owner, &cleanup)) *out = owner;
    if (error && error->code == QA_OK)
        guest_fail(error, QA_ERROR_FORMAT, 0, "Windows process capsule could not be admitted completely");
    return false;
}
static bool adopt(qa_native_windows_process *candidate,
    qa_native_windows_process *previous, bool owned, qa_error *error)
{
    if (!candidate || candidate == previous || !candidate->provisional || !candidate->complete ||
        candidate->failed || candidate->busy || candidate->disposing || !qa_native_guest_idle(candidate->guest) ||
        (previous && !qa_native_windows_process_idle(previous)))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows publication requires complete quiescent candidate and previous owners");
    if (!guest_windows_adopt(candidate->runtime,
            previous && !owned ? previous->runtime : NULL, error)) return false;
    candidate->provisional = false;
    if (previous) {
        previous->runtime->retired = true;
        previous->disposing = true;
    }
    return true;
}

bool qa_native_windows_process_adopt(qa_native_windows_process *candidate,
    qa_native_windows_process *previous, qa_error *error)
{ return adopt(candidate, previous, false, error); }

bool qa_native_windows_process_adopt_owned(qa_native_windows_process *candidate,
    qa_native_windows_process *previous, qa_error *error)
{ return adopt(candidate, previous, true, error); }

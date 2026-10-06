#include "artifact.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>

typedef struct profile_image {
    uint64_t provider;
    guest_profile_image_kind kind;
    union { guest_elf *elf; guest_pe *pe; } owner;
} profile_image;
struct guest_profile_artifacts { profile_image *images; size_t count; };

static bool fail(qa_error *error, qa_status status, size_t at, const char *message)
{ qa_error_set(error, status, at, "%s", message); return false; }

static bool copy_image(const guest_profile_image *source, profile_image *out,
    qa_error *error)
{
    out->provider = source->provider; out->kind = source->kind;
    if (source->kind == GUEST_PROFILE_ELF) {
        const guest_elf_view *view = guest_elf_describe(source->owner.elf);
        if (!view) return fail(error, QA_ERROR_ARGUMENT, 0, "profile ELF owner is absent");
        return guest_elf_open(view->artifact, &view->image, view->role, view->bias,
            view->bytes.size, &out->owner.elf, error);
    }
    if (source->kind == GUEST_PROFILE_PE) {
        const guest_pe_view *view = guest_pe_describe(source->owner.pe);
        if (!view) return fail(error, QA_ERROR_ARGUMENT, 0, "profile PE owner is absent");
        return guest_pe_open(view->artifact, &view->image, view->base,
            view->bytes.size, &out->owner.pe, error);
    }
    return fail(error, QA_ERROR_ARGUMENT, 0, "profile image kind is unknown");
}

void guest_profile_artifacts_destroy(guest_profile_artifacts **pointer)
{
    if (!pointer || !*pointer) return;
    guest_profile_artifacts *owner = *pointer;
    for (size_t i = 0; i < owner->count; ++i) {
        if (owner->images[i].kind == GUEST_PROFILE_ELF) guest_elf_close(&owner->images[i].owner.elf);
        else guest_pe_close(&owner->images[i].owner.pe);
    }
    free(owner->images); free(owner); *pointer = NULL;
}

bool guest_profile_artifacts_create(const guest_profile_image *images, size_t count,
    guest_profile_artifacts **out, qa_error *error)
{
    if (!out || *out || !images || !count || count > SIZE_MAX / sizeof(profile_image))
        return fail(error, QA_ERROR_ARGUMENT, 0, "profile needs actual ordered images and empty output");
    guest_profile_artifacts *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, 0, "owning profile artifacts");
    owner->images = calloc(count, sizeof(*owner->images));
    if (!owner->images) { free(owner); return fail(error, QA_ERROR_MEMORY, count, "owning profile image order"); }
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        for (size_t j = 0; j < i; ++j)
            if (images[j].provider == images[i].provider) {
                okay = fail(error, QA_ERROR_ARGUMENT, i, "profile repeats a physical provider"); break;
            }
        if (!okay) break;
        ++owner->count;
        okay = copy_image(images + i, owner->images + i, error);
    }
    if (!okay) { guest_profile_artifacts_destroy(&owner); return false; }
    *out = owner; return true;
}

size_t guest_profile_artifacts_count(const guest_profile_artifacts *owner)
{ return owner ? owner->count : 0; }

bool guest_profile_artifacts_at(const guest_profile_artifacts *owner, size_t index,
    guest_profile_image_view *out, qa_error *error)
{
    if (!owner || !out || index >= owner->count)
        return fail(error, QA_ERROR_ARGUMENT, index, "profile image ordinal is absent");
    const profile_image *row = owner->images + index;
    *out = (guest_profile_image_view){.provider = row->provider, .kind = row->kind};
    if (row->kind == GUEST_PROFILE_ELF) out->image.elf = guest_elf_describe(row->owner.elf);
    else out->image.pe = guest_pe_describe(row->owner.pe);
    return true;
}

static bool same_bytes(qa_bytes left, qa_bytes right)
{ return left.size == right.size && (!left.size || !memcmp(left.data, right.data, left.size)); }

bool guest_profile_artifacts_match(const guest_profile_artifacts *owner,
    const guest_profile_image *images, size_t count, qa_error *error)
{
    if (!owner || !images || count != owner->count)
        return fail(error, QA_ERROR_FORMAT, count, "profile dependency order differs");
    for (size_t i = 0; i < count; ++i) {
        const profile_image *row = owner->images + i;
        if (row->provider != images[i].provider || row->kind != images[i].kind)
            return fail(error, QA_ERROR_FORMAT, i, "profile physical provider or image kind differs");
        if (row->kind == GUEST_PROFILE_ELF) {
            const guest_elf_view *a = guest_elf_describe(row->owner.elf), *b = guest_elf_describe(images[i].owner.elf);
            if (!b || a->role != b->role || a->bias != b->bias || !same_bytes(a->artifact, b->artifact))
                return fail(error, QA_ERROR_FORMAT, i, "profile ELF original bytes, role or load bias differs");
        } else {
            const guest_pe_view *a = guest_pe_describe(row->owner.pe), *b = guest_pe_describe(images[i].owner.pe);
            if (!b || a->base != b->base || !same_bytes(a->artifact, b->artifact))
                return fail(error, QA_ERROR_FORMAT, i, "profile PE original bytes or load base differs");
        }
    }
    return true;
}

/* Wire records own original artifacts, not relocated image buffers. Image
 * target/metadata are derived again by the genuine inert parser. */
bool guest_profile_artifacts_checkpoint(const guest_profile_artifacts *owner,
    qa_buffer *out, qa_error *error)
{
    if (!owner || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, 0, "profile capture needs owner and empty output");
    size_t bytes = 12;
    for (size_t i = 0; i < owner->count; ++i) {
        const profile_image *row = owner->images + i;
        qa_bytes artifact = row->kind == GUEST_PROFILE_ELF ? guest_elf_describe(row->owner.elf)->artifact : guest_pe_describe(row->owner.pe)->artifact;
        if (bytes > SIZE_MAX - 32 || artifact.size > SIZE_MAX - bytes - 32)
            return fail(error, QA_ERROR_MEMORY, i, "profile capture extent overflows");
        bytes += 32 + artifact.size;
    }
    uint8_t *data = malloc(bytes);
    if (!data) return fail(error, QA_ERROR_MEMORY, bytes, "capturing profile artifacts");
    memcpy(data, "QAPA", 4); qa_store_u64le(data + 4, owner->count);
    size_t offset = 12;
    for (size_t i = 0; i < owner->count; ++i) {
        const profile_image *row = owner->images + i;
        qa_bytes artifact; uint64_t address; uint32_t role = 0;
        if (row->kind == GUEST_PROFILE_ELF) {
            const guest_elf_view *view = guest_elf_describe(row->owner.elf);
            artifact = view->artifact; address = view->bias; role = (uint32_t)view->role;
        } else {
            const guest_pe_view *view = guest_pe_describe(row->owner.pe);
            artifact = view->artifact; address = view->base;
        }
        qa_store_u64le(data + offset, row->provider); qa_store_u32le(data + offset + 8, (uint32_t)row->kind);
        qa_store_u32le(data + offset + 12, role); qa_store_u64le(data + offset + 16, address);
        qa_store_u64le(data + offset + 24, artifact.size);
        memcpy(data + offset + 32, artifact.data, artifact.size); offset += 32 + artifact.size;
    }
    *out = (qa_buffer){data, bytes}; return true;
}

bool guest_profile_artifacts_decode(qa_bytes encoded, size_t maximum_image_bytes,
    guest_profile_artifacts **out, qa_error *error)
{
    if (!out || *out || !maximum_image_bytes || !encoded.data || encoded.size < 12 ||
        memcmp(encoded.data, "QAPA", 4))
        return fail(error, QA_ERROR_FORMAT, 0, "profile artifact record is invalid");
    uint64_t count = qa_load_u64le(encoded.data + 4);
    if (!count || count > (encoded.size - 12) / 32 || count > SIZE_MAX / sizeof(profile_image))
        return fail(error, QA_ERROR_FORMAT, 4, "profile image count exceeds real record storage");
    guest_profile_artifacts *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, 0, "decoding profile owner");
    owner->images = calloc((size_t)count, sizeof(*owner->images));
    if (!owner->images) { free(owner); return fail(error, QA_ERROR_MEMORY, 0, "decoding profile dependency order"); }
    size_t offset = 12; bool okay = true;
    for (size_t i = 0; okay && i < (size_t)count; ++i) {
        if (offset > encoded.size || encoded.size - offset < 32) {
            okay = fail(error, QA_ERROR_FORMAT, offset, "profile image record is truncated"); break;
        }
        const uint8_t *record = encoded.data + offset;
        uint32_t kind = qa_load_u32le(record + 8), role = qa_load_u32le(record + 12);
        uint64_t address = qa_load_u64le(record + 16), bytes = qa_load_u64le(record + 24);
        if (kind > GUEST_PROFILE_PE || bytes > encoded.size - offset - 32 ||
            (kind == GUEST_PROFILE_PE ? role != 0 : role > GUEST_ELF_PROGRAM)) {
            okay = fail(error, QA_ERROR_FORMAT, offset, "profile image kind, role or bytes are invalid"); break;
        }
        profile_image *row = owner->images + i;
        row->provider = qa_load_u64le(record); row->kind = (guest_profile_image_kind)kind;
        for (size_t j = 0; j < i; ++j) if (owner->images[j].provider == row->provider) {
            okay = fail(error, QA_ERROR_FORMAT, i, "profile repeats a retained provider"); break;
        }
        if (!okay) break;
        ++owner->count;
        qa_bytes artifact = {record + 32, (size_t)bytes}; qa_native_image_info image;
        okay = kind == GUEST_PROFILE_ELF && role == GUEST_ELF_PROGRAM ?
            qa_native_inspect_program(artifact, &image, error) : qa_native_inspect(artifact, &image, error);
        if (okay) okay = kind == GUEST_PROFILE_ELF ?
            guest_elf_open(artifact, &image, (guest_elf_role)role, address, maximum_image_bytes, &row->owner.elf, error) :
            guest_pe_open(artifact, &image, address, maximum_image_bytes, &row->owner.pe, error);
        offset += 32 + (size_t)bytes;
    }
    if (okay && offset != encoded.size) okay = fail(error, QA_ERROR_FORMAT, offset, "profile record has trailing bytes");
    if (!okay) { guest_profile_artifacts_destroy(&owner); return false; }
    *out = owner; return true;
}

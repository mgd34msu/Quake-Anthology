#include "elf_unwind_private.h"
#include "qa/source_save.h"

static bool header(qa_source_save_io *io, const guest_elf *artifact)
{
    const guest_elf_view *image = guest_elf_describe(artifact);
    uint8_t magic[5] = {'Q','E','U','W',1};
    const uint8_t expected[5] = {'Q','E','U','W',1};
    qa_sha256_digest digest = image->image.digest;
    uint64_t bias = image->bias; uint32_t role = image->role;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) ||
        !qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) ||
        !qa_source_save_u64(io, &bias) || !qa_source_save_u32(io, &role)) return false;
    return (!memcmp(magic, expected, sizeof(magic)) &&
        qa_sha256_equal(&digest, &image->image.digest) && bias == image->bias &&
        role == (uint32_t)image->role) ||
        guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "unwind capsule differs from its actual source artifact identity");
}

static bool shape(const guest_elf_unwind *owner, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(owner->artifact);
    size_t block = 0; bool eh = false;
    for (size_t i = 0; i < image->section_count; ++i) {
        const guest_elf_section *section = image->sections + i;
        bool debug = !strcmp(section->name, ".debug_frame");
        if (!debug && strcmp(section->name, ".eh_frame")) continue;
        if (!debug) eh = true;
        if (block >= owner->block_count || owner->blocks[block].fallback ||
            owner->blocks[block].source != i)
            return guest_fail(error, QA_ERROR_FORMAT, i, "unwind continuation omits or reorders an actual source section");
        if (!guest_elf_unwind_source(owner, owner->blocks + block, error)) return false;
        ++block;
    }
    if (!eh) for (size_t i = 0; i < image->segment_count; ++i) {
        if (image->segments[i].type != 0x6474e550) continue;
        if (block >= owner->block_count || !owner->blocks[block].fallback ||
            owner->blocks[block].source != i)
            return guest_fail(error, QA_ERROR_FORMAT, i, "unwind continuation omits its actual stripped header source");
        if (!guest_elf_unwind_source(owner, owner->blocks + block, error)) return false;
        ++block; break;
    }
    bool fallback = block && owner->blocks[block - 1].fallback;
    return (block == owner->block_count && fallback == (owner->header.size != 0) &&
        (fallback || (!owner->header.data && !owner->header_address && !owner->header_source))) ||
        guest_fail(error, QA_ERROR_FORMAT, block, "unwind continuation invents an actual metadata source");
}

static bool blob(qa_source_save_io *io, qa_buffer *out, size_t maximum)
{
    if (!qa_source_save_count(io, &out->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || out->size > io->input.size - io->offset)
            return guest_fail(io->error, QA_ERROR_FORMAT, io->offset, "unwind snapshot bytes exceed their actual capsule");
        out->data = out->size ? malloc(out->size) : NULL;
        if (out->size && !out->data)
            return guest_fail(io->error, QA_ERROR_MEMORY, io->offset, "owning unwind snapshot bytes");
    }
    return qa_source_save_bytes(io, out->data, out->size);
}

bool guest_elf_unwind_checkpoint(const guest_elf_unwind *owner, qa_buffer *out, qa_error *error)
{
    if (!owner || !out || out->data || out->size)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "unwind capture requires actual retained metadata and empty output");
    if (!shape(owner, error)) return false;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool present = owner->header.size != 0;
    bool okay = header(&io, owner->artifact) && qa_source_save_bool(&io, &present);
    if (okay && present) {
        qa_buffer data = owner->header; uint64_t address = owner->header_address;
        size_t source = owner->header_source;
        okay = qa_source_save_count(&io, &source, SIZE_MAX) && qa_source_save_u64(&io, &address) &&
            blob(&io, &data, owner->maximum);
    }
    size_t count = owner->block_count;
    if (okay) okay = qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; okay && i < owner->block_count; ++i) {
        const unwind_block *block = owner->blocks + i;
        uint64_t address = block->address; size_t source = block->source;
        bool debug = block->debug, fallback = block->fallback;
        qa_buffer data = {block->data, block->bytes};
        okay = qa_source_save_count(&io, &source, SIZE_MAX) && qa_source_save_u64(&io, &address) &&
            qa_source_save_bool(&io, &debug) && qa_source_save_bool(&io, &fallback) && blob(&io, &data, owner->maximum);
    }
    count = owner->count;
    if (okay) okay = qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; okay && i < owner->count; ++i) {
        const guest_elf_unwind_region *region = owner->regions + i;
        size_t source = owner->block_count;
        for (size_t j = 0; j < owner->block_count; ++j)
            if (owner->blocks[j].data == region->metadata.data &&
                owner->blocks[j].bytes == region->metadata.size) source = j;
        uint64_t first = region->first, end = region->end;
        size_t cie = region->cie_offset, fde = region->fde_offset;
        if (source >= owner->block_count) {
            okay = guest_fail(error, QA_ERROR_FORMAT, i, "unwind index has no actual owned metadata block"); break;
        }
        okay = qa_source_save_count(&io, &source, SIZE_MAX) &&
            qa_source_save_u64(&io, &first) && qa_source_save_u64(&io, &end) &&
            qa_source_save_count(&io, &cie, SIZE_MAX) && qa_source_save_count(&io, &fde, SIZE_MAX);
    }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}

bool guest_elf_unwind_restore(const guest_elf *artifact, const qa_native_guest *guest,
    qa_bytes bytes, size_t maximum, guest_elf_unwind **out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(artifact);
    if (!image || !maximum || !out || *out || !guest_ready(guest, error) ||
        image->image.target.os != guest->options.image.target.os ||
        image->image.target.arch != guest->options.image.target.arch ||
        image->image.target.abi != guest->options.image.target.abi ||
        image->image.target.pointer_bytes != guest->options.image.target.pointer_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "cold unwind owner requires its actual retained artifact and stopped ABI identity");
    guest_elf_unwind *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning cold unwind inventory");
    owner->artifact = artifact; owner->maximum = maximum;
    qa_source_save_io io;
    bool initialized = qa_source_save_reader(&io, NULL, bytes, error);
    bool present = false;
    bool okay = initialized && header(&io, artifact) && qa_source_save_bool(&io, &present);
    if (okay && present) {
        okay = qa_source_save_count(&io, &owner->header_source, SIZE_MAX) &&
            qa_source_save_u64(&io, &owner->header_address) && blob(&io, &owner->header, maximum);
        owner->owned_bytes = owner->header.size;
    }
    size_t count = 0;
    if (okay) okay = qa_source_save_count(&io, &count, SIZE_MAX) &&
        count <= (io.input.size - io.offset) / 26 &&
        guest_grow((void **)&owner->blocks, &owner->block_capacity, count, sizeof(*owner->blocks), error);
    for (size_t i = 0; okay && i < count; ++i) {
        unwind_block *block = owner->blocks + owner->block_count++;
        memset(block, 0, sizeof(*block)); qa_buffer data = {0};
        okay = qa_source_save_count(&io, &block->source, SIZE_MAX) && qa_source_save_u64(&io, &block->address) &&
            qa_source_save_bool(&io, &block->debug) && qa_source_save_bool(&io, &block->fallback) &&
            blob(&io, &data, maximum - owner->owned_bytes);
        block->data = data.data; block->bytes = data.size;
        if (okay) owner->owned_bytes += block->bytes;
    }
    if (okay) okay = shape(owner, error);
    size_t saved_count = 0;
    if (okay) okay = qa_source_save_count(&io, &saved_count, SIZE_MAX) &&
        saved_count <= (io.input.size - io.offset) / 40 && saved_count <= SIZE_MAX / sizeof(guest_elf_unwind_region);
    guest_elf_unwind_region *saved = okay && saved_count ? calloc(saved_count, sizeof(*saved)) : NULL;
    if (okay && saved_count && !saved) okay = guest_fail(error, QA_ERROR_MEMORY, 0, "owning cold unwind frame receipts");
    for (size_t i = 0; okay && i < saved_count; ++i) {
        size_t source = 0; guest_elf_unwind_region *region = saved + i;
        okay = qa_source_save_count(&io, &source, owner->block_count) && source < owner->block_count &&
            qa_source_save_u64(&io, &region->first) && qa_source_save_u64(&io, &region->end) &&
            qa_source_save_count(&io, &region->cie_offset, SIZE_MAX) &&
            qa_source_save_count(&io, &region->fde_offset, SIZE_MAX);
        if (okay) {
            const unwind_block *block = owner->blocks + source;
            region->metadata_address = block->address; region->metadata = (qa_bytes){block->data, block->bytes};
            region->format = block->debug ? GUEST_ELF_DEBUG_FRAME : GUEST_ELF_EH_FRAME;
        }
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    size_t used = 0;
    for (size_t i = 0; okay && i < owner->block_count; ++i)
        okay = guest_elf_unwind_index(owner, owner->blocks + i, guest, saved, saved_count, &used, error);
    if (okay && used != saved_count) okay = guest_fail(error, QA_ERROR_FORMAT, used, "cold unwind index invents a source frame receipt");
    free(saved);
    if (initialized) qa_source_save_dispose(&io);
    if (!okay) {
        if (error && error->code == QA_OK) guest_fail(error, QA_ERROR_FORMAT, 0, "cold unwind ownership record has invalid identity or structure");
        guest_elf_unwind_close(&owner); return false;
    }
    *out = owner; return true;
}

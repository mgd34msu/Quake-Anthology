#include "elf_unwind_private.h"

typedef struct unwind_cie { size_t offset; uint8_t encoding; bool augmentation; } unwind_cie;
typedef struct unwind_reader {
    const uint8_t *data;
    size_t bytes, cursor, end;
    uint64_t address, data_base;
    unsigned width;
    bool has_data_base;
    qa_error *error;
} unwind_reader;
typedef struct unwind_number { uint64_t magnitude; bool negative; } unwind_number;

static bool fail(unwind_reader *r, const char *message)
{
    guest_fail(r->error, QA_ERROR_FORMAT, r->cursor, message);
    return false;
}

static bool take(unwind_reader *r, size_t count, const uint8_t **out)
{
    if (r->cursor > r->end || count > r->end - r->cursor)
        return fail(r, "DWARF frame field exceeds its actual record");
    *out = r->data + r->cursor; r->cursor += count; return true;
}

static bool fixed(unwind_reader *r, unsigned bytes, bool signed_value,
    unwind_number *out)
{
    const uint8_t *data;
    if (!take(r, bytes, &data)) return false;
    uint64_t bits = 0;
    for (unsigned i = 0; i < bytes; ++i) bits |= (uint64_t)data[i] << (i * 8);
    bool negative = signed_value && (bits >> (bytes * 8 - 1));
    uint64_t mask = bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (bytes * 8)) - 1;
    *out = (unwind_number){negative ? ((~bits) & mask) + 1 : bits, negative};
    return true;
}

/* Ten bytes retain the donor's entire 70-bit LEB field. Discarded code/data
 * factors and register IDs need no invented narrow range; actual addresses
 * and byte extents require a representable 64-bit value. */
static bool leb(unwind_reader *r, bool signed_value, unwind_number *out)
{
    uint64_t bits = 0; uint8_t final = 0; unsigned used = 0;
    for (unsigned i = 0; i < 10; ++i) {
        const uint8_t *data;
        if (!take(r, 1, &data)) return false;
        final = *data; used = i + 1;
        if (i < 9) bits |= (uint64_t)(final & 127) << (i * 7);
        else bits |= (uint64_t)(final & 1) << 63;
        if (!(final & 128)) break;
    }
    if (final & 128) return fail(r, "DWARF LEB field exceeds its actual format");
    if (!out) return true;
    bool negative = signed_value && (final & 64);
    if (used == 10 && (negative ? (final & 126) != 126 || !bits : (final & 127) > 1))
        return fail(r, "DWARF numeric address or extent exceeds 64 bits");
    if (negative && used < 10) bits |= UINT64_MAX << (used * 7);
    *out = (unwind_number){negative ? ~bits + 1 : bits, negative};
    return true;
}

static bool byte(unwind_reader *r, uint8_t *out)
{
    const uint8_t *data;
    if (!take(r, 1, &data)) return false;
    *out = *data; return true;
}

static bool encoded(unwind_reader *r, uint8_t encoding, bool relative,
    unwind_number *out)
{
    if (encoding == 255) return fail(r, "DWARF omitted pointer was used as an address");
    if ((encoding & 0x70) == 0x50) {
        if (r->cursor > UINT64_MAX - r->address)
            return fail(r, "DWARF aligned pointer address overflows");
        uint64_t address = r->address + r->cursor;
        size_t padding = (size_t)((r->width - address % r->width) % r->width);
        const uint8_t *ignored;
        if (!take(r, padding, &ignored)) return false;
    }
    uint64_t base = 0;
    if (r->cursor > UINT64_MAX - r->address)
        return fail(r, "DWARF field address overflows");
    uint64_t address = r->address + r->cursor;
    unwind_number number = {0}; bool okay;
    switch (encoding & 15) {
    case 0: okay = fixed(r, r->width, false, &number); break;
    case 1: okay = leb(r, false, &number); break;
    case 2: okay = fixed(r, 2, false, &number); break;
    case 3: okay = fixed(r, 4, false, &number); break;
    case 4: okay = fixed(r, 8, false, &number); break;
    case 9: okay = leb(r, true, &number); break;
    case 10: okay = fixed(r, 2, true, &number); break;
    case 11: okay = fixed(r, 4, true, &number); break;
    case 12: okay = fixed(r, 8, true, &number); break;
    default: return fail(r, "DWARF pointer has an unsupported numeric encoding");
    }
    if (!okay) return false;
    if (relative) {
        bool based = true;
        switch (encoding & 0x70) {
        case 0: case 0x50: based = false; break;
        case 0x10: base = address; break;
        case 0x30:
            if (!r->has_data_base) return fail(r, "DWARF data-relative pointer has no actual base");
            base = r->data_base; break;
        default: return fail(r, "DWARF relative pointer has an unsupported actual base");
        }
        if (based && number.negative) {
            if (base < number.magnitude) return fail(r, "DWARF relative pointer underflows its address domain");
            number = (unwind_number){base - number.magnitude, false};
        } else if (based) {
            if (number.magnitude > UINT64_MAX - base) return fail(r, "DWARF relative pointer overflows its address domain");
            number.magnitude += base;
        }
    }
    *out = number; return true;
}

static bool extent(unwind_reader *r, size_t *out)
{
    unwind_number value;
    if (!leb(r, false, &value)) return false;
    if (value.magnitude > SIZE_MAX || value.magnitude > r->end - r->cursor)
        return fail(r, "DWARF augmentation bytes exceed their actual record");
    *out = (size_t)value.magnitude; return true;
}

bool guest_elf_unwind_index(guest_elf_unwind *owner, unwind_block *block,
    const qa_native_guest *guest, const guest_elf_unwind_region *saved,
    size_t saved_count, size_t *saved_used, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(owner->artifact);
    unwind_reader r = {.data = block->data, .bytes = block->bytes, .end = block->bytes,
        .address = block->address, .width = image->image.target.pointer_bytes, .error = error};
    unwind_cie *cies = NULL; size_t count = 0, capacity = 0; bool okay = true;
    while (okay && r.cursor < r.bytes) {
        size_t start = r.cursor;
        if (r.bytes - start < 4) {
            for (size_t i = start; i < r.bytes; ++i)
                if (r.data[i]) { okay = fail(&r, "DWARF short terminator contains nonzero bytes"); break; }
            break;
        }
        r.end = r.bytes;
        unwind_number value;
        okay = fixed(&r, 4, false, &value);
        if (!okay || !value.magnitude) break;
        bool wide = value.magnitude == UINT32_MAX;
        if (wide) okay = fixed(&r, 8, false, &value);
        /* GNU EH IDs/displacements remain four bytes with an extended
         * length. DWARF64 debug frames use eight-byte section offsets. */
        unsigned id_bytes = wide && block->debug ? 8 : 4;
        if (!okay) break;
        if (value.magnitude < id_bytes || value.magnitude > r.bytes - r.cursor) {
            okay = fail(&r, "DWARF frame length has invalid actual bounds"); break;
        }
        r.end = r.cursor + (size_t)value.magnitude;
        size_t id_position = r.cursor;
        okay = fixed(&r, id_bytes, false, &value);
        bool cie = block->debug ? value.magnitude == (wide ? UINT64_MAX : UINT32_MAX) : !value.magnitude;
        if (!okay) break;
        if (cie) {
            uint8_t version;
            okay = byte(&r, &version);
            if (okay && version != 1 && version != 3 && version != 4)
                okay = fail(&r, "DWARF CIE version is unsupported");
            size_t augmentation = r.cursor; uint8_t c = 1;
            while (okay && c) okay = byte(&r, &c);
            size_t augmentation_bytes = okay ? r.cursor - augmentation - 1 : 0;
            if (okay && version == 4) {
                uint8_t width, segment;
                okay = byte(&r, &width) && byte(&r, &segment);
                if (okay && (width != r.width || segment)) okay = fail(&r, "DWARF CIE uses a different address or segment domain");
            }
            if (okay) okay = leb(&r, false, NULL) && leb(&r, true, NULL);
            if (okay) okay = version == 1 ? byte(&r, &c) : leb(&r, false, NULL);
            uint8_t encoding = 0;
            bool augmented = augmentation_bytes && r.data[augmentation] == 'z';
            if (okay && augmented) {
                size_t bytes;
                okay = extent(&r, &bytes);
                size_t end = okay ? r.cursor + bytes : r.cursor;
                size_t record_end = r.end; r.end = end;
                for (size_t i = 1; okay && i < augmentation_bytes; ++i) {
                    switch (r.data[augmentation + i]) {
                    case 'R': okay = byte(&r, &encoding); break;
                    case 'L': okay = byte(&r, &c); break;
                    case 'P': okay = byte(&r, &c) && encoded(&r, c, false, &value); break;
                    case 'S': break;
                    default: okay = fail(&r, "DWARF CIE augmentation is unsupported"); break;
                    }
                }
                r.cursor = end; r.end = record_end;
            } else if (okay && augmentation_bytes) okay = fail(&r, "DWARF nonempty CIE augmentation lacks its actual schema");
            if (okay) okay = guest_grow((void **)&cies, &capacity, count + 1, sizeof(*cies), error);
            if (okay) cies[count++] = (unwind_cie){start, encoding, augmented};
        } else {
            uint64_t cie_offset = value.magnitude;
            if (!block->debug) {
                if (cie_offset > id_position) { okay = fail(&r, "DWARF FDE CIE displacement underflows"); break; }
                cie_offset = id_position - cie_offset;
            }
            unwind_cie *record = NULL;
            for (size_t i = 0; i < count; ++i) if (cies[i].offset == cie_offset) record = cies + i;
            if (!record) { okay = fail(&r, "DWARF FDE references an unavailable actual CIE"); break; }
            unwind_number pc, range;
            okay = encoded(&r, record->encoding, true, &pc);
            bool indirect = (record->encoding & 0x80) != 0;
            if (okay && indirect) {
                uint8_t slot[8];
                okay = !pc.negative || fail(&r, "DWARF indirect PC slot is outside its address domain");
                if (okay && !saved_used) {
                    okay = qa_native_guest_read(guest, pc.magnitude, slot, r.width, error);
                    if (okay) pc.magnitude = r.width == 8 ? qa_load_u64le(slot) : qa_load_u32le(slot);
                }
            }
            if (okay) okay = encoded(&r, record->encoding & 15, false, &range);
            if (okay && range.negative) okay = fail(&r, "DWARF FDE covers a negative PC range");
            if (okay && indirect && saved_used) {
                if (range.magnitude && *saved_used >= saved_count)
                    okay = fail(&r, "saved DWARF indirect frame has no actual retained receipt");
                else if (range.magnitude) {
                    uint64_t first = saved[*saved_used].first;
                    if (block->debug && first < image->bias) okay = fail(&r, "saved indirect debug frame bias underflows");
                    else pc = (unwind_number){first - (block->debug ? image->bias : 0), false};
                } else pc = (unwind_number){0};
            }
            if (okay && block->debug) {
                if (pc.negative) {
                    if (pc.magnitude > image->bias) okay = fail(&r, "DWARF debug PC bias underflows");
                    else pc = (unwind_number){image->bias - pc.magnitude, false};
                } else if (pc.magnitude > UINT64_MAX - image->bias) okay = fail(&r, "DWARF debug PC bias overflows");
                else pc.magnitude += image->bias;
            }
            if (okay && record->augmentation) {
                size_t bytes; const uint8_t *ignored;
                okay = extent(&r, &bytes) && take(&r, bytes, &ignored);
            }
            if (okay && range.magnitude) {
                uint64_t limit = r.width == 4 ? UINT32_MAX : UINT64_MAX;
                if (pc.negative || !pc.magnitude || pc.magnitude > limit || range.magnitude > limit - pc.magnitude)
                    okay = fail(&r, "DWARF FDE covered PC extent exceeds its actual ABI");
                if (okay) okay = guest_grow((void **)&owner->regions, &owner->capacity,
                    owner->count + 1, sizeof(*owner->regions), error);
                guest_elf_unwind_region row = {
                    .first = pc.magnitude, .end = pc.magnitude + range.magnitude,
                    .metadata_address = block->address, .cie_offset = record->offset,
                    .fde_offset = start, .format = block->debug ? GUEST_ELF_DEBUG_FRAME : GUEST_ELF_EH_FRAME,
                    .metadata = {block->data, block->bytes}};
                if (okay && saved_used) {
                    if (*saved_used >= saved_count) okay = fail(&r, "saved DWARF index omits an actual frame");
                    else {
                        const guest_elf_unwind_region *actual = saved + *saved_used;
                        if (row.first != actual->first || row.end != actual->end ||
                            row.metadata_address != actual->metadata_address ||
                            row.cie_offset != actual->cie_offset || row.fde_offset != actual->fde_offset ||
                            row.format != actual->format || row.metadata.data != actual->metadata.data ||
                            row.metadata.size != actual->metadata.size)
                            okay = fail(&r, "saved DWARF index differs from its actual metadata record");
                    }
                    if (okay) ++*saved_used;
                }
                if (okay) owner->regions[owner->count++] = row;
            }
        }
        r.cursor = r.end;
    }
    free(cies); return okay;
}

static bool append_block(guest_elf_unwind *owner, const qa_native_guest *guest,
    uint64_t address, bool debug, qa_bytes original, size_t bytes, bool live,
    size_t source, bool fallback, qa_error *error)
{
    if (bytes > owner->maximum - owner->owned_bytes)
        return guest_fail(error, QA_ERROR_MEMORY, address, "ELF unwind metadata exceeds its actual admitted ownership");
    if (!guest_grow((void **)&owner->blocks, &owner->block_capacity,
        owner->block_count + 1, sizeof(*owner->blocks), error)) return false;
    uint8_t *data = bytes ? malloc(bytes) : NULL;
    if (bytes && !data) return guest_fail(error, QA_ERROR_MEMORY, address, "owning actual ELF unwind bytes");
    bool okay = !live || !bytes || qa_native_guest_read(guest, address, data, bytes, error);
    if (okay && !live && bytes) memcpy(data, original.data, bytes);
    if (!okay) { free(data); return false; }
    unwind_block *block = owner->blocks + owner->block_count++;
    *block = (unwind_block){.data = data, .bytes = bytes, .source = source,
        .address = address, .debug = debug, .fallback = fallback}; owner->owned_bytes += bytes;
    return guest_elf_unwind_index(owner, block, guest, NULL, 0, NULL, error);
}

void guest_elf_unwind_close(guest_elf_unwind **pointer)
{
    if (!pointer || !*pointer) return;
    guest_elf_unwind *owner = *pointer;
    for (size_t i = 0; i < owner->block_count; ++i) free(owner->blocks[i].data);
    qa_buffer_free(&owner->header);
    free(owner->blocks); free(owner->regions); free(owner); *pointer = NULL;
}

bool guest_elf_unwind_open(const guest_elf *artifact, const qa_native_guest *guest,
    size_t maximum, guest_elf_unwind **out, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(artifact);
    if (!image || !maximum || !out || *out || !guest_ready(guest, error) ||
        image->image.target.os != guest->options.image.target.os ||
        image->image.target.arch != guest->options.image.target.arch ||
        image->image.target.abi != guest->options.image.target.abi ||
        image->image.target.pointer_bytes != guest->options.image.target.pointer_bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "ELF unwind indexing requires its actual artifact and stopped ABI owner");
    guest_elf_unwind *owner = calloc(1, sizeof(*owner));
    if (!owner) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning ELF unwind inventory");
    owner->artifact = artifact; owner->maximum = maximum;
    bool has_eh = false, okay = true;
    for (size_t i = 0; okay && i < image->section_count; ++i) {
        const guest_elf_section *section = image->sections + i;
        bool debug = !strcmp(section->name, ".debug_frame");
        if (!debug && strcmp(section->name, ".eh_frame")) continue;
        if (!debug) has_eh = true;
        bool live = (section->flags & 2) != 0;
        if (section->bytes > SIZE_MAX || (live && section->address > UINT64_MAX - image->bias) ||
            (!live && (section->offset > image->artifact.size || section->bytes > image->artifact.size - section->offset))) {
            okay = guest_fail(error, QA_ERROR_FORMAT, section->address, "ELF unwind section has no actual source extent"); break;
        }
        qa_bytes original = live ? (qa_bytes){0} :
            (qa_bytes){image->artifact.data + (size_t)section->offset, (size_t)section->bytes};
        okay = append_block(owner, guest, section->address + (live ? image->bias : 0),
            debug, original, (size_t)section->bytes, live, i, false, error);
    }
    if (okay && !has_eh) for (size_t i = 0; i < image->segment_count; ++i) {
        const guest_elf_segment *header = image->segments + i;
        if (header->type != 0x6474e550) continue;
        if (header->file_bytes > SIZE_MAX || header->file_bytes > maximum - owner->owned_bytes ||
            header->address > UINT64_MAX - image->bias) {
            okay = guest_fail(error, QA_ERROR_FORMAT, header->address, "GNU EH header has no bounded actual address"); break;
        }
        size_t size = (size_t)header->file_bytes; uint8_t *data = size ? malloc(size) : NULL;
        if (size && !data) { okay = guest_fail(error, QA_ERROR_MEMORY, header->address, "owning GNU EH header bytes"); break; }
        uint64_t address = header->address + image->bias;
        okay = qa_native_guest_read(guest, address, data, size, error);
        unwind_reader r = {.data = data, .bytes = size, .end = size, .address = address,
            .data_base = address, .has_data_base = true, .width = image->image.target.pointer_bytes, .error = error};
        uint8_t version, encoding, ignored; unwind_number pointer;
        if (okay) okay = byte(&r, &version) && byte(&r, &encoding) && byte(&r, &ignored) && byte(&r, &ignored);
        if (okay && (version != 1 || (encoding & 0x80))) okay = fail(&r, "GNU EH header version or indirect section pointer is unsupported");
        if (okay) okay = encoded(&r, encoding, true, &pointer);
        owner->header = (qa_buffer){data, size}; owner->header_address = address;
        owner->header_source = i; owner->owned_bytes += size;
        const guest_elf_segment *segment = NULL;
        if (okay && !pointer.negative && pointer.magnitude >= image->bias) {
            uint64_t original = pointer.magnitude - image->bias;
            for (size_t j = 0; j < image->segment_count; ++j) {
                const guest_elf_segment *candidate = image->segments + j;
                if (candidate->type == 1 && original >= candidate->address &&
                    original - candidate->address < candidate->file_bytes) { segment = candidate; break; }
            }
        }
        if (okay && !segment) okay = guest_fail(error, QA_ERROR_FORMAT, address, "GNU EH pointer has no actual file-backed load segment");
        if (okay) {
            uint64_t original = pointer.magnitude - image->bias;
            uint64_t bytes = segment->file_bytes - (original - segment->address); qa_bytes proof;
            okay = bytes <= SIZE_MAX && guest_elf_file_range(artifact, original, (size_t)bytes, &proof, error) &&
                append_block(owner, guest, pointer.magnitude, false, (qa_bytes){0}, (size_t)bytes, true, i, true, error);
        }
        break;
    }
    if (!okay) { guest_elf_unwind_close(&owner); return false; }
    *out = owner; return true;
}

size_t guest_elf_unwind_count(const guest_elf_unwind *owner)
{ return owner ? owner->count : 0; }
bool guest_elf_unwind_at(const guest_elf_unwind *owner, size_t index,
    guest_elf_unwind_region *out, qa_error *error)
{
    if (!owner || !out || index >= owner->count)
        return guest_fail(error, QA_ERROR_ARGUMENT, index, "ELF unwind index is unavailable");
    *out = owner->regions[index]; return true;
}
const guest_elf_unwind_region *guest_elf_unwind_find(const guest_elf_unwind *owner, uint64_t pc)
{
    for (size_t i = 0; owner && i < owner->count; ++i)
        if (pc >= owner->regions[i].first && pc < owner->regions[i].end) return owner->regions + i;
    return NULL;
}

bool guest_elf_unwind_source(const guest_elf_unwind *owner,
    const unwind_block *block, qa_error *error)
{
    const guest_elf_view *image = guest_elf_describe(owner->artifact);
    if (!block->fallback) {
        if (block->source >= image->section_count)
            return guest_fail(error, QA_ERROR_FORMAT, block->source, "saved unwind section identity is absent");
        const guest_elf_section *section = image->sections + block->source;
        bool live = (section->flags & 2) != 0;
        if (strcmp(section->name, block->debug ? ".debug_frame" : ".eh_frame") ||
            section->bytes != block->bytes || (live && section->address > UINT64_MAX - image->bias) ||
            block->address != section->address + (live ? image->bias : 0) ||
            (!live && (section->offset > image->artifact.size || section->bytes > image->artifact.size - section->offset)))
            return guest_fail(error, QA_ERROR_FORMAT, block->source, "saved unwind section differs from its actual artifact declaration");
        if (!live && block->bytes && memcmp(block->data,
            image->artifact.data + (size_t)section->offset, block->bytes))
            return guest_fail(error, QA_ERROR_FORMAT, block->source, "saved nonallocated frame bytes differ from their immutable artifact");
        return true;
    }
    if (block->debug || block->source >= image->segment_count ||
        block->source != owner->header_source || !owner->header.data)
        return guest_fail(error, QA_ERROR_FORMAT, block->source, "saved stripped unwind source has no actual retained header");
    const guest_elf_segment *header = image->segments + block->source;
    if (header->type != 0x6474e550 || header->file_bytes != owner->header.size ||
        header->address > UINT64_MAX - image->bias ||
        owner->header_address != header->address + image->bias)
        return guest_fail(error, QA_ERROR_FORMAT, block->source, "saved GNU EH header differs from its actual artifact declaration");
    unwind_reader r = {.data = owner->header.data, .bytes = owner->header.size,
        .end = owner->header.size, .address = owner->header_address,
        .data_base = owner->header_address, .has_data_base = true,
        .width = image->image.target.pointer_bytes, .error = error};
    uint8_t version, encoding, ignored; unwind_number pointer;
    if (!byte(&r, &version) || !byte(&r, &encoding) || !byte(&r, &ignored) ||
        !byte(&r, &ignored)) return false;
    if (version != 1 || (encoding & 0x80))
        return fail(&r, "saved GNU EH header pointer has a different actual schema");
    if (!encoded(&r, encoding, true, &pointer)) return false;
    if (pointer.negative || pointer.magnitude < image->bias || pointer.magnitude != block->address)
        return fail(&r, "saved stripped unwind address differs from its retained header pointer");
    uint64_t original = pointer.magnitude - image->bias;
    for (size_t i = 0; i < image->segment_count; ++i) {
        const guest_elf_segment *segment = image->segments + i;
        if (segment->type != 1 || original < segment->address ||
            original - segment->address >= segment->file_bytes) continue;
        uint64_t bytes = segment->file_bytes - (original - segment->address); qa_bytes proof;
        if (bytes != block->bytes)
            return guest_fail(error, QA_ERROR_FORMAT, block->address, "saved stripped unwind extent differs from its actual file-backed load segment");
        return guest_elf_file_range(owner->artifact, original, block->bytes, &proof, error);
    }
    return fail(&r, "saved stripped unwind metadata has no actual file-backed segment");
}

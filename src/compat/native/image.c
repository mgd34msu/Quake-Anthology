#include "internal.h"

static bool span(qa_bytes bytes, size_t offset, size_t length) {
    return offset <= bytes.size && length <= bytes.size - offset;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if (right > UINT64_MAX - left)
        return false;
    *out = left + right;
    return true;
}

static bool target_for(qa_native_os os, uint16_t machine, qa_native_target *out, qa_error *error) {
    qa_native_target target = {.os = os};
    if ((os == QA_NATIVE_OS_WINDOWS && machine == 0x014c) ||
        (os != QA_NATIVE_OS_WINDOWS && machine == 3)) {
        target.arch = QA_NATIVE_ARCH_I386;
        target.pointer_bytes = 4;
        target.abi =
            os == QA_NATIVE_OS_WINDOWS ? QA_NATIVE_ABI_CDECL_I386 : QA_NATIVE_ABI_SYSTEM_V_I386;
    } else if ((os == QA_NATIVE_OS_WINDOWS && machine == 0x8664) ||
               (os != QA_NATIVE_OS_WINDOWS && machine == 62)) {
        target.arch = QA_NATIVE_ARCH_X86_64;
        target.pointer_bytes = 8;
        target.abi =
            os == QA_NATIVE_OS_WINDOWS ? QA_NATIVE_ABI_MICROSOFT_X64 : QA_NATIVE_ABI_SYSTEM_V_X64;
    } else if ((os == QA_NATIVE_OS_WINDOWS && machine == 0xaa64) ||
               (os != QA_NATIVE_OS_WINDOWS && machine == 183)) {
        target.arch = QA_NATIVE_ARCH_AARCH64;
        target.pointer_bytes = 8;
        target.abi = QA_NATIVE_ABI_AAPCS64;
    } else {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "unsupported native image machine 0x%04x",
                     machine);
        return false;
    }
    *out = target;
    return true;
}

static bool elf_program_table(qa_bytes bytes, bool elf32, size_t *offset, size_t *stride,
                              size_t *count, qa_error *error) {
    size_t header = elf32 ? 52u : 64u;
    if (!span(bytes, 0, header) || bytes.data[4] != (elf32 ? 1 : 2) ||
        bytes.data[5] != 1 || bytes.data[6] != 1 || qa_load_u32le(bytes.data + 20) != 1 ||
        qa_load_u16le(bytes.data + (elf32 ? 40 : 52)) != header)
        return native_fail(error, QA_ERROR_FORMAT, 0, "invalid original ELF header class/version/size");
    uint64_t phoff = elf32 ? qa_load_u32le(bytes.data + 28) : qa_load_u64le(bytes.data + 32);
    *stride = qa_load_u16le(bytes.data + (elf32 ? 42 : 54));
    *count = qa_load_u16le(bytes.data + (elf32 ? 44 : 56));
    if (*count == 0xffff) {
        uint64_t shoff = elf32 ? qa_load_u32le(bytes.data + 32) : qa_load_u64le(bytes.data + 40);
        size_t shstride = qa_load_u16le(bytes.data + (elf32 ? 46 : 58));
        if (!shoff || shstride < (elf32 ? 40u : 64u) || !native_u64_fits_size(shoff) ||
            !span(bytes, (size_t)shoff, shstride) || qa_load_u32le(bytes.data + (size_t)shoff + 4))
            return native_fail(error, QA_ERROR_FORMAT, 0, "ELF PN_XNUM lacks its actual null section header");
        *count = qa_load_u32le(bytes.data + (size_t)shoff + (elf32 ? 28 : 44));
        if (*count < 0xffff)
            return native_fail(error, QA_ERROR_FORMAT, (size_t)shoff, "ELF PN_XNUM has an invalid actual extended count");
    }
    size_t table_bytes;
    if (*stride < (elf32 ? 32u : 56u) || !native_u64_fits_size(phoff) ||
        !native_size_multiply(*stride, *count, &table_bytes) ||
        !span(bytes, (size_t)phoff, table_bytes))
        return native_fail(error, QA_ERROR_FORMAT, 0, "truncated original ELF program table");
    *offset = (size_t)phoff;
    return true;
}

bool qa_native_module_mutable_range(const qa_native_module *module, uint64_t rva,
                                    uint64_t length, qa_error *error) {
    if (!module || !length || rva > module->info.image.image_bytes || length > module->info.image.image_bytes - rva)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native mutable source range exceeds its qualified image");
    const uint8_t *bytes = module->bytes;
    bool writable = false;
    if (module->info.image.format == QA_NATIVE_IMAGE_PE32 || module->info.image.format == QA_NATIVE_IMAGE_PE32_PLUS) {
        uint32_t pe = qa_load_u32le(bytes + 0x3c);
        const uint8_t *coff = bytes + pe + 4;
        uint16_t count = qa_load_u16le(coff + 2), optional = qa_load_u16le(coff + 16);
        const uint8_t *table = bytes + (size_t)pe + 24u + optional;
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t *section = table + (size_t)i * 40;
            uint64_t start = qa_load_u32le(section + 12), size = qa_load_u32le(section + 8);
            uint32_t raw = qa_load_u32le(section + 16), flags = qa_load_u32le(section + 36);
            if (raw > size) size = raw;
            if (size && rva < start + size && start < rva + length &&
                (!(flags & UINT32_C(0x80000000)) || (flags & UINT32_C(0x20000000))))
                return native_fail(error, QA_ERROR_UNSUPPORTED, 0, "native mutable source range overlaps non-mutable PE storage");
            if ((flags & UINT32_C(0x80000000)) && !(flags & UINT32_C(0x20000000)) &&
                rva >= start && rva - start <= size && length <= size - (rva - start)) writable = true;
        }
    } else {
        bool elf32 = module->info.image.format == QA_NATIVE_IMAGE_ELF32;
        size_t offset, stride, count;
        if (!elf_program_table((qa_bytes){bytes, module->size}, elf32, &offset, &stride, &count, error)) return false;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t *program = bytes + offset + i * stride;
            uint32_t type = qa_load_u32le(program), flags = qa_load_u32le(program + (elf32 ? 24 : 4));
            uint64_t start = elf32 ? qa_load_u32le(program + 8) : qa_load_u64le(program + 16);
            uint64_t size = elf32 ? qa_load_u32le(program + 20) : qa_load_u64le(program + 40);
            if (type == 1 && size && start <= UINT64_MAX - size && rva < start + size && start < rva + length &&
                (!(flags & 2u) || (flags & 1u)))
                return native_fail(error, QA_ERROR_UNSUPPORTED, 0, "native mutable source range overlaps non-mutable ELF storage");
            if (type == 1 && (flags & 2u) && !(flags & 1u) && rva >= start &&
                rva - start <= size && length <= size - (rva - start)) writable = true;
            if (type == UINT32_C(0x6474e552) && size && start <= UINT64_MAX - size &&
                rva < start + size && start < rva + length)
                return native_fail(error, QA_ERROR_UNSUPPORTED, 0, "native mutable source range overlaps ELF RELRO");
        }
    }
    return writable || native_fail(error, QA_ERROR_UNSUPPORTED, 0, "native private state is not an original mutable image span");
}

static bool inspect_pe(qa_bytes bytes, bool program, qa_native_image_info *out, qa_error *error) {
    if (!span(bytes, 0x3c, 4))
        return native_fail(error, QA_ERROR_FORMAT, bytes.size, "truncated PE DOS header");
    uint32_t pe = qa_load_u32le(bytes.data + 0x3c);
    if (!span(bytes, pe, 24) || memcmp(bytes.data + pe, "PE\0\0", 4))
        return native_fail(error, QA_ERROR_FORMAT, pe, "invalid PE signature or COFF header");
    const uint8_t *coff = bytes.data + pe + 4;
    uint16_t machine = qa_load_u16le(coff);
    uint16_t sections = qa_load_u16le(coff + 2);
    uint16_t optional_bytes = qa_load_u16le(coff + 16);
    uint16_t characteristics = qa_load_u16le(coff + 18);
    size_t optional = (size_t)pe + 24u;
    if (!span(bytes, optional, optional_bytes) || optional_bytes < 60)
        return native_fail(error, QA_ERROR_FORMAT, optional, "truncated PE optional header");
    uint16_t magic = qa_load_u16le(bytes.data + optional);
    qa_native_image_format format;
    uint64_t preferred;
    if (magic == 0x010b && optional_bytes >= 60) {
        format = QA_NATIVE_IMAGE_PE32;
        preferred = qa_load_u32le(bytes.data + optional + 28);
    } else if (magic == 0x020b && optional_bytes >= 64) {
        format = QA_NATIVE_IMAGE_PE32_PLUS;
        preferred = qa_load_u64le(bytes.data + optional + 24);
    } else {
        return native_fail(error, QA_ERROR_UNSUPPORTED, optional,
                           "unsupported PE optional-header format");
    }
    qa_native_target target;
    if (!target_for(QA_NATIVE_OS_WINDOWS, machine, &target, error))
        return false;
    if ((format == QA_NATIVE_IMAGE_PE32) != (target.pointer_bytes == 4))
        return native_fail(error, QA_ERROR_FORMAT, optional, "PE class does not match its machine");
    if (program && ((characteristics & 0x2000u) || !(characteristics & 2u)))
        return native_fail(error, QA_ERROR_FORMAT, pe + 22, "PE artifact is not an executable program");
    if (!program && !(characteristics & 0x2000u))
        return native_fail(error, QA_ERROR_FORMAT, pe + 22, "PE artifact is not a dynamic library");
    uint32_t image_bytes = qa_load_u32le(bytes.data + optional + 56);
    if (!image_bytes)
        return native_fail(error, QA_ERROR_FORMAT, optional + 56, "PE image has zero virtual size");
    uint32_t entry = qa_load_u32le(bytes.data + optional + 16);
    if (program && (!entry || entry >= image_bytes))
        return native_fail(error, QA_ERROR_FORMAT, optional + 16, "PE program has no admitted entry point");
    size_t table = optional + optional_bytes;
    size_t section_bytes;
    bool executable_entry = false;
    if (!native_size_multiply(sections, 40, &section_bytes) || !span(bytes, table, section_bytes))
        return native_fail(error, QA_ERROR_FORMAT, table, "truncated PE section table");
    for (uint16_t index = 0; index < sections; ++index) {
        const uint8_t *section = bytes.data + table + (size_t)index * 40u;
        uint32_t raw_size = qa_load_u32le(section + 16);
        uint32_t raw_offset = qa_load_u32le(section + 20);
        if (raw_size && !span(bytes, raw_offset, raw_size)) {
            qa_error_set(error, QA_ERROR_FORMAT, table + (size_t)index * 40u,
                         "PE section %u exceeds the artifact", index);
            return false;
        }
        uint32_t start = qa_load_u32le(section + 12);
        if ((qa_load_u32le(section + 36) & UINT32_C(0x20000000)) &&
            entry >= start && (uint64_t)entry - start < raw_size)
            executable_entry = true;
    }
    if (program && !executable_entry)
        return native_fail(error, QA_ERROR_FORMAT, optional + 16, "PE entry is outside executable file storage");
    *out = (qa_native_image_info){.format = format,
                                  .target = target,
                                  .preferred_base = preferred,
                                  .image_bytes = image_bytes};
    qa_sha256(bytes, &out->digest);
    return true;
}

static bool inspect_elf(qa_bytes bytes, bool executable, qa_native_image_info *out, qa_error *error) {
    if (!span(bytes, 0, 16))
        return native_fail(error, QA_ERROR_FORMAT, bytes.size, "truncated ELF identification");
    uint8_t class_id = bytes.data[4], encoding = bytes.data[5];
    if (class_id != 1 && class_id != 2)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 4, "unsupported ELF class");
    if (encoding != 1)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 5,
                           "big-endian native modules are unsupported");
    size_t header_size = class_id == 1 ? 52u : 64u;
    if (!span(bytes, 0, header_size))
        return native_fail(error, QA_ERROR_FORMAT, bytes.size, "truncated ELF header");
    uint16_t type = qa_load_u16le(bytes.data + 16);
    if ((!executable && type != 3) || (executable && type != 2 && type != 3))
        return native_fail(error, QA_ERROR_FORMAT, 16,
                           "ELF native artifact is not a shared object");
    uint16_t machine = qa_load_u16le(bytes.data + 18);
    qa_native_target target;
    if (!target_for(QA_NATIVE_OS_LINUX, machine, &target, error))
        return false;
    if ((class_id == 1) != (target.pointer_bytes == 4))
        return native_fail(error, QA_ERROR_FORMAT, 4, "ELF class does not match its machine");
    size_t program_offset, program_size, program_count;
    if (!elf_program_table(bytes, class_id == 1, &program_offset, &program_size, &program_count, error)) return false;
    uint64_t first = UINT64_MAX, end = 0;
    bool loadable = false, executable_entry = false;
    uint64_t entry = class_id == 1 ? qa_load_u32le(bytes.data + 24) : qa_load_u64le(bytes.data + 24);
    for (size_t index = 0; index < program_count; ++index) {
        const uint8_t *program = bytes.data + program_offset + index * program_size;
        if (qa_load_u32le(program) != 1)
            continue;
        uint64_t file_offset, virtual_address, file_size, memory_size;
        if (class_id == 1) {
            file_offset = qa_load_u32le(program + 4);
            virtual_address = qa_load_u32le(program + 8);
            file_size = qa_load_u32le(program + 16);
            memory_size = qa_load_u32le(program + 20);
        } else {
            file_offset = qa_load_u64le(program + 8);
            virtual_address = qa_load_u64le(program + 16);
            file_size = qa_load_u64le(program + 32);
            memory_size = qa_load_u64le(program + 40);
        }
        uint64_t file_end, virtual_end;
        if (memory_size < file_size || !add_u64(file_offset, file_size, &file_end) ||
            file_end > bytes.size || !add_u64(virtual_address, memory_size, &virtual_end)) {
            qa_error_set(error, QA_ERROR_FORMAT,
                         (size_t)program_offset + (size_t)index * program_size,
                         "invalid ELF load segment %zu", index);
            return false;
        }
        if (virtual_address < first)
            first = virtual_address;
        if (virtual_end > end)
            end = virtual_end;
        uint32_t flags = qa_load_u32le(program + (class_id == 1 ? 24 : 4));
        if ((flags & 1u) && entry && entry >= virtual_address && entry - virtual_address < file_size)
            executable_entry = true;
        loadable = true;
    }
    if (!loadable || end <= first)
        return native_fail(error, QA_ERROR_FORMAT, (size_t)program_offset,
                           "ELF image has no nonempty load segments");
    if (executable && !executable_entry)
        return native_fail(error, QA_ERROR_FORMAT, 24, "ELF program has no executable entry point");
    *out = (qa_native_image_info){.format =
                                      class_id == 1 ? QA_NATIVE_IMAGE_ELF32 : QA_NATIVE_IMAGE_ELF64,
                                  .target = target,
                                  .preferred_base = first,
                                  .image_bytes = end};
    qa_sha256(bytes, &out->digest);
    return true;
}

static bool inspect(qa_bytes image, bool program, qa_native_image_info *out, qa_error *error) {
    if (!out || (!image.data && image.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native image bytes and output are required");
    qa_native_image_info inspected;
    if (image.size >= 2 && image.data[0] == 'M' && image.data[1] == 'Z') {
        if (!inspect_pe(image, program, &inspected, error))
            return false;
    } else if (image.size >= 4 && !memcmp(image.data,
                                          "\x7f"
                                          "ELF",
                                          4)) {
        if (!inspect_elf(image, program, &inspected, error))
            return false;
    } else {
        return native_fail(error, QA_ERROR_FORMAT, 0, "native artifact is neither PE nor ELF");
    }
    *out = inspected;
    return true;
}

bool qa_native_inspect(qa_bytes image, qa_native_image_info *out, qa_error *error) {
    return inspect(image, false, out, error);
}

bool qa_native_inspect_program(qa_bytes image, qa_native_image_info *out, qa_error *error) {
    return inspect(image, true, out, error);
}

bool native_image_soname(qa_bytes image, qa_bytes *out, qa_error *error) {
    qa_native_image_info info;
    if (!out || !qa_native_inspect(image, &info, error)) return false;
    *out = (qa_bytes){0};
    if (info.format != QA_NATIVE_IMAGE_ELF32 && info.format != QA_NATIVE_IMAGE_ELF64) return true;
    bool elf32 = info.format == QA_NATIVE_IMAGE_ELF32;
    const uint8_t *bytes = image.data;
    size_t program_offset, stride, count;
    if (!elf_program_table(image, elf32, &program_offset, &stride, &count, error)) return false;
    uint64_t strings = 0, string_bytes = 0, soname = 0;
    bool has_soname = false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *program = bytes + program_offset + (size_t)i * stride;
        if (qa_load_u32le(program) != 2) continue;
        uint64_t offset = elf32 ? qa_load_u32le(program + 4) : qa_load_u64le(program + 8);
        uint64_t length = elf32 ? qa_load_u32le(program + 16) : qa_load_u64le(program + 32);
        size_t item_bytes = elf32 ? 8u : 16u;
        if (!native_u64_fits_size(offset) || !native_u64_fits_size(length) ||
            length % item_bytes || !span(image, (size_t)offset, (size_t)length))
            return native_fail(error, QA_ERROR_FORMAT, program_offset + (size_t)i * stride,
                "invalid original dependency dynamic table");
        bool ended = false;
        for (size_t n = 0; n < (size_t)length; n += item_bytes) {
            const uint8_t *entry = bytes + (size_t)offset + n;
            uint64_t tag = elf32 ? qa_load_u32le(entry) : qa_load_u64le(entry);
            uint64_t value = elf32 ? qa_load_u32le(entry + 4) : qa_load_u64le(entry + 8);
            if (!tag) { ended = true; break; }
            if (tag == 5) strings = value;
            else if (tag == 10) string_bytes = value;
            else if (tag == 14) { soname = value; has_soname = true; }
        }
        if (!ended) return native_fail(error, QA_ERROR_FORMAT, (size_t)offset,
            "unterminated original dependency dynamic table");
    }
    if (!has_soname) return true;
    if (!strings || !string_bytes || soname >= string_bytes || !native_u64_fits_size(string_bytes))
        return native_fail(error, QA_ERROR_FORMAT, 0, "invalid original dependency SONAME strings");
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *program = bytes + program_offset + (size_t)i * stride;
        if (qa_load_u32le(program) != 1) continue;
        uint64_t offset = elf32 ? qa_load_u32le(program + 4) : qa_load_u64le(program + 8);
        uint64_t address = elf32 ? qa_load_u32le(program + 8) : qa_load_u64le(program + 16);
        uint64_t length = elf32 ? qa_load_u32le(program + 16) : qa_load_u64le(program + 32);
        if (strings < address || strings - address > length || string_bytes > length - (strings - address)) continue;
        uint64_t translated;
        if (!add_u64(offset, strings - address, &translated) || !native_u64_fits_size(translated) ||
            !span(image, (size_t)translated, (size_t)string_bytes)) continue;
        const uint8_t *name = bytes + (size_t)translated + (size_t)soname;
        const uint8_t *end = memchr(name, 0, (size_t)(string_bytes - soname));
        if (!end || end == name) return native_fail(error, QA_ERROR_FORMAT, (size_t)translated,
            "original dependency SONAME is empty or unterminated");
        *out = (qa_bytes){name, (size_t)(end - name)};
        return true;
    }
    return native_fail(error, QA_ERROR_FORMAT, 0, "original dependency SONAME has no admitted file segment");
}

qa_native_target qa_native_host_target(void) {
    qa_native_target target = {0};
#if defined(_WIN32)
    target.os = QA_NATIVE_OS_WINDOWS;
#elif defined(__APPLE__)
    target.os = QA_NATIVE_OS_MACOS;
#else
    target.os = QA_NATIVE_OS_LINUX;
#endif
#if defined(__i386__) || defined(_M_IX86)
    target.arch = QA_NATIVE_ARCH_I386;
    target.pointer_bytes = 4;
    target.abi =
        target.os == QA_NATIVE_OS_WINDOWS ? QA_NATIVE_ABI_CDECL_I386 : QA_NATIVE_ABI_SYSTEM_V_I386;
#elif defined(__x86_64__) || defined(_M_X64)
    target.arch = QA_NATIVE_ARCH_X86_64;
    target.pointer_bytes = 8;
    target.abi = target.os == QA_NATIVE_OS_WINDOWS ? QA_NATIVE_ABI_MICROSOFT_X64
                                                   : QA_NATIVE_ABI_SYSTEM_V_X64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    target.arch = QA_NATIVE_ARCH_AARCH64;
    target.pointer_bytes = 8;
    target.abi = QA_NATIVE_ABI_AAPCS64;
#else
#error Unsupported native module host architecture
#endif
    return target;
}

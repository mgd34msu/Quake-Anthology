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

static bool inspect_pe(qa_bytes bytes, qa_native_image_info *out, qa_error *error) {
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
    if (!(characteristics & 0x2000u))
        return native_fail(error, QA_ERROR_FORMAT, pe + 22, "PE artifact is not a dynamic library");
    uint32_t image_bytes = qa_load_u32le(bytes.data + optional + 56);
    if (!image_bytes)
        return native_fail(error, QA_ERROR_FORMAT, optional + 56, "PE image has zero virtual size");
    size_t table = optional + optional_bytes;
    size_t section_bytes;
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
    }
    *out = (qa_native_image_info){.format = format,
                                  .target = target,
                                  .preferred_base = preferred,
                                  .image_bytes = image_bytes};
    qa_sha256(bytes, &out->digest);
    return true;
}

static bool inspect_elf(qa_bytes bytes, qa_native_image_info *out, qa_error *error) {
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
    if (qa_load_u16le(bytes.data + 16) != 3)
        return native_fail(error, QA_ERROR_FORMAT, 16,
                           "ELF native artifact is not a shared object");
    uint16_t machine = qa_load_u16le(bytes.data + 18);
    qa_native_target target;
    if (!target_for(QA_NATIVE_OS_LINUX, machine, &target, error))
        return false;
    if ((class_id == 1) != (target.pointer_bytes == 4))
        return native_fail(error, QA_ERROR_FORMAT, 4, "ELF class does not match its machine");
    uint64_t program_offset =
        class_id == 1 ? qa_load_u32le(bytes.data + 28) : qa_load_u64le(bytes.data + 32);
    uint16_t program_size = qa_load_u16le(bytes.data + (class_id == 1 ? 42 : 54));
    uint16_t program_count = qa_load_u16le(bytes.data + (class_id == 1 ? 44 : 56));
    size_t expected_size = class_id == 1 ? 32u : 56u;
    uint64_t table_bytes;
    if (program_size < expected_size ||
        !add_u64(0, (uint64_t)program_size * program_count, &table_bytes) ||
        !native_u64_fits_size(program_offset) || !native_u64_fits_size(table_bytes) ||
        !span(bytes, (size_t)program_offset, (size_t)table_bytes))
        return native_fail(error, QA_ERROR_FORMAT, (size_t)program_offset,
                           "truncated ELF program table");
    uint64_t first = UINT64_MAX, end = 0;
    bool loadable = false;
    for (uint16_t index = 0; index < program_count; ++index) {
        const uint8_t *program = bytes.data + (size_t)program_offset + (size_t)index * program_size;
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
                         "invalid ELF load segment %u", index);
            return false;
        }
        if (virtual_address < first)
            first = virtual_address;
        if (virtual_end > end)
            end = virtual_end;
        loadable = true;
    }
    if (!loadable || end <= first)
        return native_fail(error, QA_ERROR_FORMAT, (size_t)program_offset,
                           "ELF image has no nonempty load segments");
    *out = (qa_native_image_info){.format =
                                      class_id == 1 ? QA_NATIVE_IMAGE_ELF32 : QA_NATIVE_IMAGE_ELF64,
                                  .target = target,
                                  .preferred_base = first,
                                  .image_bytes = end};
    qa_sha256(bytes, &out->digest);
    return true;
}

bool qa_native_inspect(qa_bytes image, qa_native_image_info *out, qa_error *error) {
    if (!out || (!image.data && image.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native image bytes and output are required");
    qa_native_image_info inspected;
    if (image.size >= 2 && image.data[0] == 'M' && image.data[1] == 'Z') {
        if (!inspect_pe(image, &inspected, error))
            return false;
    } else if (image.size >= 4 && !memcmp(image.data,
                                          "\x7f"
                                          "ELF",
                                          4)) {
        if (!inspect_elf(image, &inspected, error))
            return false;
    } else {
        return native_fail(error, QA_ERROR_FORMAT, 0, "native artifact is neither PE nor ELF");
    }
    *out = inspected;
    return true;
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

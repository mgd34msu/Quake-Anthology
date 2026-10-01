#include "internal.h"
#include "qa/native_module_save.h"
#include "qa/source_save.h"

static bool image_equal(const qa_native_image_info *left, const qa_native_image_info *right)
{
    return left->format == right->format && left->target.os == right->target.os &&
        left->target.arch == right->target.arch && left->target.abi == right->target.abi &&
        left->target.pointer_bytes == right->target.pointer_bytes &&
        left->preferred_base == right->preferred_base && left->image_bytes == right->image_bytes &&
        qa_sha256_equal(&left->digest, &right->digest);
}
static bool metadata(qa_source_save_io *io, qa_native_module_info *info)
{
    uint8_t magic[4] = {'Q','A','N','M'};
    uint32_t version = 1, profile = info->profile, format = info->image.format;
    uint32_t os = info->image.target.os, arch = info->image.target.arch, abi = info->image.target.abi;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QANM", 4) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_u32(io, &profile) || profile > QA_NATIVE_QUAKE_LIVE_GAME_API10 ||
        !qa_source_save_u32(io, &format) || format > QA_NATIVE_IMAGE_ELF64 ||
        !qa_source_save_u32(io, &os) || os > QA_NATIVE_OS_MACOS ||
        !qa_source_save_u32(io, &arch) || arch > QA_NATIVE_ARCH_AARCH64 ||
        !qa_source_save_u32(io, &abi) || abi > QA_NATIVE_ABI_AAPCS64 ||
        !qa_source_save_u8(io, &info->image.target.pointer_bytes) ||
        (info->image.target.pointer_bytes != 4 && info->image.target.pointer_bytes != 8) ||
        !qa_source_save_u64(io, &info->image.preferred_base) ||
        !qa_source_save_u64(io, &info->image.image_bytes) ||
        !qa_source_save_bytes(io, info->image.digest.bytes, sizeof(info->image.digest.bytes))) return false;
    info->profile = (qa_native_profile)profile;
    info->image.format = (qa_native_image_format)format;
    info->image.target.os = (qa_native_os)os;
    info->image.target.arch = (qa_native_arch)arch;
    info->image.target.abi = (qa_native_abi)abi;
    return true;
}
bool qa_native_module_checkpoint(const qa_native_module *module, qa_bytes artifact,
    qa_buffer *out, qa_error *error)
{
    if (!module || !out || out->data || out->size || (!artifact.data && artifact.size) ||
        artifact.size != module->size || (artifact.size && memcmp(artifact.data, module->bytes, artifact.size)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
            "native module checkpoint requires its actual immutable artifact and empty output");
    qa_native_image_info image;
    if (!qa_native_inspect(artifact, &image, error)) return false;
    if (!image_equal(&image, &module->info.image))
        return native_fail(error, QA_ERROR_FORMAT, 0, "native module cache image identity changed");
    qa_native_module_info info = module->info;
    size_t name = strlen(module->source);
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && metadata(&io, &info) &&
        qa_source_save_count(&io, &name, SIZE_MAX - 1) &&
        qa_source_save_bytes(&io, module->source, name) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK)
        native_fail(error, QA_ERROR_FORMAT, 0, "native module cache record is invalid");
    return okay;
}
bool qa_native_module_restore(qa_bytes state, qa_bytes artifact,
    qa_native_module **out, qa_error *error)
{
    if (!out || *out || (!state.data && state.size) || (!artifact.data && artifact.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
            "native module restore requires held artifact bytes and an empty cache owner");
    qa_native_module_info saved = {0};
    qa_source_save_io io = {0}; size_t length = 0; char *name = NULL;
    bool okay = qa_source_save_reader(&io, NULL, state, error) && metadata(&io, &saved) &&
        qa_source_save_count(&io, &length, io.input.size - io.offset) && length < SIZE_MAX;
    if (okay) {
        name = malloc(length + 1);
        if (!name) okay = native_fail(error, QA_ERROR_MEMORY, 0, "retaining native module cache opening name");
    }
    if (okay) {
        okay = qa_source_save_bytes(&io, name, length) && !memchr(name, 0, length) &&
            qa_source_save_finish(&io, NULL);
        name[length] = 0;
    }
    qa_source_save_dispose(&io);
    qa_native_image_info actual;
    if (okay) okay = qa_native_inspect(artifact, &actual, error);
    if (okay && !image_equal(&saved.image, &actual))
        okay = native_fail(error, QA_ERROR_FORMAT, 0, "saved native cache differs from its held artifact ABI");
    if (okay) okay = qa_native_module_load(artifact, name, saved.profile, &saved.image.digest, out, error);
    free(name);
    if (!okay && error && error->code == QA_OK)
        native_fail(error, QA_ERROR_FORMAT, 0, "saved native module cache record is invalid");
    return okay;
}

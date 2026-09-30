#include "qa/network_q1_download_save.h"
#include "qa/hash.h"
#include <stdlib.h>
#include <string.h>

typedef struct qw_file_download {
    qa_fs_root *root;
    qa_fs_file *file;
    qa_fs_identity identity;
    char *name;
    qa_buffer bytes;
    qa_sha256_digest digest;
    uint64_t maximum;
} qw_file_download;

static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool read_file(void *context, uint64_t offset, uint8_t *out, size_t count, qa_error *error)
{
    const qw_file_download *file = context;
    if (!file || (count && !out) || offset > file->bytes.size || count > file->bytes.size - offset)
        return fail(error, QA_ERROR_ARGUMENT, "QW hosted download read exceeds its immutable source");
    if (count) memcpy(out, file->bytes.data + (size_t)offset, count);
    return true;
}
static void close_file(void *context)
{
    qw_file_download *file = context;
    if (!file) return;
    qa_fs_file_close(file->file); qa_fs_root_close(file->root);
    qa_buffer_free(&file->bytes); free(file->name); free(file);
}
static bool empty(const qa_qw_download *out)
{ return out && !out->state && !out->size && !out->read && !out->close; }
static bool ready(const qw_file_download *file, qa_error *error)
{
    bool unchanged = false;
    return file && file->root && file->file && file->name && file->bytes.size <= file->maximum &&
        qa_fs_file_path_unchanged(file->file, &file->identity, &unchanged, error) && unchanged;
}
bool qa_qw_file_download_open(const qa_qw_download_admission *admission, const char *name,
    bool *found, qa_qw_download *out, qa_error *error)
{
    if (!admission || !admission->root || !found || !empty(out) || !qa_qw_download_path_valid(name))
        return fail(error, QA_ERROR_ARGUMENT, "QW hosted download requires an admitted contained root and empty output");
    qa_fs_entry_kind kind; qa_fs_identity identity;
    if (!qa_fs_root_status(admission->root, name, &kind, &identity, error)) return false;
    if (kind == QA_FS_MISSING) { *found = false; return true; }
    if (kind != QA_FS_REGULAR || qa_fs_identity_size(&identity) > admission->maximum_bytes)
        return fail(error, QA_ERROR_ARGUMENT, "QW hosted download exceeds its admitted file policy");
    qw_file_download *file = calloc(1, sizeof(*file));
    if (!file) return fail(error, QA_ERROR_MEMORY, "Allocating QW hosted source download");
    file->root = admission->root; qa_fs_root_retain(file->root); file->maximum = admission->maximum_bytes;
    size_t length = strlen(name);
    file->name = malloc(length + 1);
    if (!file->name) { close_file(file); return fail(error, QA_ERROR_MEMORY, "Retaining QW hosted download name"); }
    memcpy(file->name, name, length + 1);
    bool ok = qa_fs_root_file_open(file->root, name, &file->file, &file->identity, error) &&
        qa_fs_identity_equal(&identity, &file->identity) &&
        qa_fs_file_read_snapshot(file->file, &file->identity, &file->bytes, error) && ready(file, error);
    if (!ok) { close_file(file); return fail(error, error && error->code ? error->code : QA_ERROR_IO, "QW hosted file changed during admission"); }
    qa_sha256((qa_bytes){file->bytes.data, file->bytes.size}, &file->digest);
    *out = (qa_qw_download){file, file->bytes.size, read_file, close_file}; *found = true; return true;
}
bool qa_qw_file_download_checkpoint(const qa_qw_download *download, qa_buffer *out, qa_error *error)
{
    if (!download || !out || download->read != read_file || download->close != close_file || !download->state)
        return fail(error, QA_ERROR_UNSUPPORTED, "QW source download has no actual hosted file continuation owner");
    const qw_file_download *file = download->state;
    if (download->size != file->bytes.size || !ready(file, error))
        return fail(error, QA_ERROR_FORMAT, "QW hosted download no longer owns its admitted source file");
    size_t length = strlen(file->name);
    if (length > SIZE_MAX - 57) return fail(error, QA_ERROR_MEMORY, "QW download identity extent overflows");
    qa_buffer bytes = {malloc(57 + length), 0};
    if (!bytes.data) return fail(error, QA_ERROR_MEMORY, "Capturing QW hosted download identity");
    qa_net_writer writer; qa_net_writer_init(&writer, bytes.data, 57 + length, error);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x44465751)) && qa_net_write_u32(&writer, 1) &&
        qa_net_write_u64(&writer, file->maximum) && qa_net_write_u64(&writer, download->size) &&
        qa_net_write_data(&writer, file->digest.bytes, sizeof(file->digest.bytes)) && qa_net_write_string(&writer, file->name);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&writer); *out = bytes; return true;
}
bool qa_qw_file_download_restore_checkpoint(qa_bytes bytes, const qa_qw_download_admission *admission,
    qa_qw_download *out, qa_error *error)
{
    if (!admission || !admission->root || !empty(out) || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "QW file continuation requires an admitted empty resource owner");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint32_t magic = qa_net_read_u32(&reader), schema = qa_net_read_u32(&reader);
    uint64_t maximum = qa_net_read_u64(&reader), size = qa_net_read_u64(&reader);
    qa_bytes digest; const char *name;
    if (magic != UINT32_C(0x44465751) || schema != 1 || maximum != admission->maximum_bytes || size > maximum ||
        !qa_net_read_bytes(&reader, 32, &digest) || !qa_q1_read_cstring(&reader, &name) ||
        !qa_qw_download_path_valid(name) || !qa_net_reader_finish(&reader))
        return fail(error, QA_ERROR_FORMAT, "QW hosted download identity differs from candidate admission");
    qa_qw_download candidate = {0}; bool found = false;
    if (!qa_qw_file_download_open(admission, name, &found, &candidate, error)) return false;
    if (!found) return fail(error, QA_ERROR_NOT_FOUND, "Saved QW hosted source file is missing");
    const qw_file_download *file = candidate.state;
    if (candidate.size != size || memcmp(file->digest.bytes, digest.data, 32)) {
        candidate.close(candidate.state); return fail(error, QA_ERROR_FORMAT, "QW hosted source file content differs from checkpoint");
    }
    *out = candidate; return true;
}

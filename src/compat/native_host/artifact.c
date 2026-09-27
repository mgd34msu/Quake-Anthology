#include "qa/native_host.h"

bool qa_native_host_module_open(qa_vfs *vfs, const char *path, qa_native_profile profile,
                                const qa_sha256_digest *expected_digest,
                                qa_native_module **out, qa_error *error)
{
    if (!vfs || !path || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "native host VFS, artifact path and output are required");
        return false;
    }
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(vfs, path, &resource, NULL, error))
        return false;
    qa_bytes bytes = qa_resource_bytes(resource);
    bool ok = qa_native_module_load(bytes, path, profile, expected_digest, out, error);
    qa_resource_release(resource);
    return ok;
}

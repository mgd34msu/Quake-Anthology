#include "native_q3_client_modules_private.h"
#include <stdlib.h>
#include <string.h>

bool native_client_module_body_open(native_client_module *role, qa_error *error)
{
    if (!role || !role->owner || role->kind != QA_QVM_CGAME || !role->image ||
        !role->artifact.resource || !role->artifact.path || role->body_profile.artifact_path ||
        role->body_declaration.resource || role->body_declaration.path)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body metadata requires its actual empty artifact holder");
    qa_vfs *view = role->owner->source.descriptor->content;
    if (!qa_vfs_acquisition_retained(view, &role->artifact.acquisition, error)) return false;
    bool found = false;
    uint64_t extent;
    if (!qa_vfs_probe(view, "cgame-presentation.json", &found, &extent, error)) return false;
    if (found) {
        static const char path[] = "cgame-presentation.json";
        role->body_declaration.path = malloc(sizeof(path));
        if (!role->body_declaration.path)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining actual CGAME body declaration path");
        memcpy(role->body_declaration.path, path, sizeof(path));
        if (!qa_vfs_acquire_receipt(view, path, &role->body_declaration.resource,
            &role->body_declaration.acquisition, error)) return false;
    }
    qa_bytes declaration = found ? qa_resource_bytes(role->body_declaration.resource) : (qa_bytes){0};
    return application_q3_body_profile_read(role->image, role->kind, role->abi,
        role->artifact.path, found ? &declaration : NULL, &role->body_profile, error);
}

#include "native_process_owner.h"
#include <stdlib.h>

bool application_native_process_prepare(qa_application *application,
    const qa_launch_instance *descriptor, qa_actor_owner receiver, uint64_t service_owner,
    const qa_native_process_resource_artifact *artifacts, size_t count, size_t primary,
    const qa_native_image_info *image,
    bool (*current)(void *, const qa_launch_instance *, qa_actor_owner, uint64_t, qa_error *),
    void *context, const qa_native_process_resources *capture, qa_bytes recipe,
    application_native_process_owner *owner, qa_error *error)
{
    if (!application || !descriptor || !receiver || !service_owner || !image ||
        !owner || !current || !artifacts || !count || primary >= count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native process requires its prepared source graph");
    if (owner->resources)
        return qa_native_process_resources_options_read(owner->resources, &owner->process, error);
    if (capture) {
        qa_native_process_resources_options bindings = {.descriptor = descriptor,
            .receiver = receiver, .service_owner = service_owner, .current = current, .context = context};
        return qa_native_process_resources_rebind(capture, &bindings, recipe, &owner->resources, error) &&
            qa_native_process_resources_options_read(owner->resources, &owner->process, error);
    }
    qa_native_process_resource_policy policy = application->native_process_policy;
    bool native_target = image->target.arch == QA_NATIVE_ARCH_X86_64 &&
        image->target.pointer_bytes == 8 &&
        (image->target.os == QA_NATIVE_OS_LINUX || image->target.os == QA_NATIVE_OS_WINDOWS);
    if (policy.backend == QA_NATIVE_GUEST_HOST_X86_64 && !native_target) {
        policy.backend = QA_NATIVE_GUEST_EMULATED;
        policy.instruction_budget = 50000000u;
    }
    if (!owner->platform) {
        qa_native_process_platform_options platform = {.id = service_owner, .standard_ids = {1, 2, 3}};
        if (!qa_native_process_platform_create(&platform, &owner->platform, error)) return false;
    }
    qa_native_process_resources_options options = {.descriptor = descriptor,
        .artifacts = artifacts, .artifact_count = count, .primary = primary,
        .receiver = receiver, .service_owner = service_owner, .policy = policy,
        .runtime = application->native_runtime, .bootstrap = application->native_bootstrap,
        .platform = owner->platform, .current = current, .context = context};
    size_t mounts = qa_vfs_mount_count(descriptor->content);
    if (mounts == SIZE_MAX || mounts + 1 > SIZE_MAX / sizeof(qa_native_process_resource_root))
        return application_fail(error, QA_ERROR_MEMORY, "Native directory authority inventory overflows");
    qa_native_process_resource_root *roots = calloc(mounts + 1, sizeof(*roots));
    if (!roots) return application_fail(error, QA_ERROR_MEMORY, "Holding actual native directory authorities");
    qa_catalog *catalog = qa_launch_instance_catalog(descriptor);
    qa_fs_root *writable = application->baseline_write_root ? application->baseline_write_root :
        qa_catalog_product_write_root(catalog, descriptor->selection.product);
    bool selected = false;
    for (size_t i = 0; i < mounts; ++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(descriptor->content, i, &mount) || mount.is_archive) continue;
        qa_fs_root *root = qa_vfs_mount_root(descriptor->content, mount.id);
        if (!root) continue;
        bool write = writable && qa_fs_root_same_object(root, writable);
        roots[options.root_count++] = (qa_native_process_resource_root){"", root,
            QA_FS_OPENED_READ | (write ? QA_FS_OPENED_WRITE : 0u)};
        selected |= write;
    }
    if (writable && !selected)
        roots[options.root_count++] = (qa_native_process_resource_root){"", writable,
            QA_FS_OPENED_READ | QA_FS_OPENED_WRITE};
    options.roots = roots;
    bool created = recipe.size ?
        qa_native_process_resources_restore(&options, recipe, &owner->resources, error) :
        qa_native_process_resources_create(&options, &owner->resources, error);
    free(roots);
    if (!created) return false;
    return qa_native_process_resources_options_read(owner->resources, &owner->process, error);
}

bool application_native_process_release(application_native_process_owner *owner, qa_error *error)
{
    if (!owner) return true;
    if (!qa_native_process_resources_release(&owner->resources, error)) return false;
    if (!qa_native_process_platform_release(&owner->platform, error)) return false;
    owner->process = (qa_native_process_options){0};
    return true;
}

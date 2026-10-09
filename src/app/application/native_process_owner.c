#include "native_process_owner.h"
#include <stdlib.h>

typedef struct application_process_binding {
    size_t references;
    qa_launch_instance_lease *lease;
    const qa_launch_instance *descriptor;
    qa_resource *resource;
    qa_vfs_acquisition acquisition;
    qa_actor_owner receiver;
    uint64_t service_owner;
    bool (*current)(void *, const qa_launch_instance *, qa_actor_owner, uint64_t, qa_error *);
    void *context;
    bool receipt;
} application_process_binding;
static void binding_retain(void *context)
{ ++((application_process_binding *)context)->references; }
static void binding_release(void *context)
{
    application_process_binding *binding = context;
    if (!binding || --binding->references) return;
    qa_vfs_acquisition_dispose(&binding->acquisition);
    qa_resource_release(binding->resource);
    qa_launch_instance_lease_release(binding->lease);
    free(binding);
}
static bool binding_current(void *context, qa_error *error)
{
    application_process_binding *binding = context;
    return binding->current(binding->context, binding->descriptor,
        binding->receiver, binding->service_owner, error);
}
static bool binding_retained(void *context, qa_error *error)
{
    const application_process_binding *binding = context;
    return qa_vfs_acquisition_retained(binding->descriptor->content, &binding->acquisition, error);
}
static qa_native_process_resource_owner binding_owner(application_process_binding *binding)
{
    return (qa_native_process_resource_owner){.context = binding, .retain = binding_retain,
        .release = binding_release, .retained = binding->receipt ? binding_retained : NULL};
}
static bool binding_create(const qa_launch_instance *descriptor, qa_actor_owner receiver,
    uint64_t service_owner, const application_native_process_artifact *artifact,
    bool (*current)(void *, const qa_launch_instance *, qa_actor_owner, uint64_t, qa_error *),
    void *context, application_process_binding **out, qa_error *error)
{
    application_process_binding *binding = calloc(1, sizeof(*binding));
    if (!binding) return application_fail(error, QA_ERROR_MEMORY, "Holding native source authority");
    binding->references = 1;
    binding->receiver = receiver; binding->service_owner = service_owner;
    binding->current = current; binding->context = context;
    if (!qa_launch_instance_retain_metadata(descriptor, &binding->lease, error)) goto failed;
    binding->descriptor = qa_launch_instance_lease_view(binding->lease);
    if (artifact) {
        if (!artifact->resource || !qa_resource_id(artifact->resource)) {
            application_fail(error, QA_ERROR_ARGUMENT, "Native artifact has no genuine acquired identity"); goto failed;
        }
        binding->resource = (qa_resource *)artifact->resource;
        qa_resource_retain(binding->resource);
        if (artifact->acquisition) {
            if (artifact->acquisition->resource_id != qa_resource_id(binding->resource) ||
                !qa_vfs_acquisition_copy(artifact->acquisition, &binding->acquisition, error) ||
                !qa_vfs_acquisition_retained(binding->descriptor->content, &binding->acquisition, error)) goto failed;
            binding->receipt = true;
        }
    }
    *out = binding; return true;
failed:
    binding_release(binding); return false;
}

bool application_native_process_prepare(qa_application *application,
    const qa_launch_instance *descriptor, qa_actor_owner receiver, uint64_t service_owner,
    const application_native_process_artifact *artifacts, size_t count, size_t primary,
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
    application_process_binding *authority = NULL;
    if (!binding_create(descriptor, receiver, service_owner, NULL, current, context, &authority, error)) return false;
    if (capture) {
        qa_native_process_resources_options bindings = {.identity = descriptor->identity,
            .authority = binding_owner(authority), .receiver = receiver,
            .service_owner = service_owner, .current = binding_current};
        bool rebound = qa_native_process_resources_rebind(capture, &bindings, recipe, &owner->resources, error);
        binding_release(authority);
        return rebound && qa_native_process_resources_options_read(owner->resources, &owner->process, error);
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
        if (!qa_native_process_platform_create(&platform, &owner->platform, error)) { binding_release(authority); return false; }
    }
    qa_native_process_resource_artifact *admitted = calloc(count, sizeof(*admitted));
    if (!admitted) {
        binding_release(authority);
        return application_fail(error, QA_ERROR_MEMORY, "Holding actual native artifacts");
    }
    size_t admitted_count = 0;
    for (; admitted_count < count; ++admitted_count) {
        application_process_binding *artifact = NULL;
        if (!binding_create(descriptor, receiver, service_owner, artifacts + admitted_count,
            NULL, NULL, &artifact, error)) break;
        admitted[admitted_count] = (qa_native_process_resource_artifact){
            .bytes = qa_resource_bytes(artifact->resource), .path = artifacts[admitted_count].path,
            .owner = binding_owner(artifact)};
    }
    qa_native_process_resources_options options = {.identity = descriptor->identity,
        .authority = binding_owner(authority),
        .artifacts = admitted, .artifact_count = count, .primary = primary,
        .receiver = receiver, .service_owner = service_owner, .policy = policy,
        .runtime = application->native_runtime, .bootstrap = application->native_bootstrap,
        .platform = owner->platform, .temporary_root = qa_native_process_platform_temporary_root,
        .current = binding_current};
    qa_native_process_resource_root *roots = NULL;
    bool created = false;
    if (admitted_count != count) goto cleanup;
    size_t mounts = qa_vfs_mount_count(descriptor->content);
    if (mounts == SIZE_MAX || mounts + 1 > SIZE_MAX / sizeof(qa_native_process_resource_root)) {
        application_fail(error, QA_ERROR_MEMORY, "Native directory authority inventory overflows"); goto cleanup;
    }
    roots = calloc(mounts + 1, sizeof(*roots));
    if (!roots) { application_fail(error, QA_ERROR_MEMORY, "Holding actual native directory authorities"); goto cleanup; }
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
    created = recipe.size ?
        qa_native_process_resources_restore(&options, recipe, &owner->resources, error) :
        qa_native_process_resources_create(&options, &owner->resources, error);
cleanup:
    free(roots);
    for (size_t i = 0; i < admitted_count; ++i) binding_release(admitted[i].owner.context);
    free(admitted); binding_release(authority);
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

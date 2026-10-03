#include "native_q3_client_modules_private.h"
#include "guest_q3_body_save.h"
#include "qa/q3_host_save.h"
#include "qa/source_save.h"
#include "qa/script_defines_save.h"
#include <stdlib.h>
#include <string.h>

typedef struct saved_opening {
    const char *path;
    uint64_t pool, resource;
    qa_vfs_acquisition acquisition;
} saved_opening;

typedef struct saved_module {
    bool present, initialized, succeeded;
    uint32_t role, abi;
    uint64_t sequence;
    qa_string_id service_owner;
    saved_opening artifact, declaration, body_declaration;
    qa_buffer executor, functions, services, body_profile;
} saved_module;

typedef struct saved_modules {
    uint64_t catalog, view, generation, epoch, physical_owner;
    uint32_t receiver, seat;
    qa_sha256_digest identity;
    bool pure;
    qa_buffer script_globals;
    saved_module ui, cgame;
} saved_modules;

static bool signature(qa_source_save_io *io)
{
    uint8_t value[8] = {'Q','A','N','C','M',0,0,0};
    const uint8_t expected[8] = {'Q','A','N','C','M',0,0,0};
    return qa_source_save_bytes(io, value, sizeof(value)) &&
        (!memcmp(value, expected, sizeof(value)) ||
            application_fail(io->error, QA_ERROR_FORMAT, "Invalid acquired CLIENT module continuation"));
}

bool qa_application_native_q3_client_modules_checkpoint_format(qa_bytes bytes, qa_error *error)
{
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io);
    qa_source_save_dispose(&io);
    return okay;
}

static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    size_t size = value->size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (size > io->input.size - io->offset)
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated acquired CLIENT continuation");
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data)
            return application_fail(io->error, QA_ERROR_MEMORY, "Retaining acquired CLIENT continuation");
    }
    return qa_source_save_bytes(io, value->data, size);
}

static bool opening_fields(qa_source_save_io *io, saved_opening *value)
{
    const char *path = value->acquisition.path, *lookup = value->acquisition.lookup_path;
    const char *link_source = value->acquisition.link_source, *link_target = value->acquisition.link_target;
    bool okay = qa_source_save_text(io, &value->path) && qa_source_save_u64(io, &value->pool) &&
        qa_source_save_u64(io, &value->resource) && qa_source_save_u64(io, &value->acquisition.mount) &&
        qa_source_save_u64(io, &value->acquisition.resource_id) && qa_source_save_text(io, &path) &&
        qa_source_save_text(io, &lookup) && qa_source_save_text(io, &link_source) && qa_source_save_text(io, &link_target);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        value->acquisition.path = (char *)path; value->acquisition.lookup_path = (char *)lookup;
        value->acquisition.link_source = (char *)link_source; value->acquisition.link_target = (char *)link_target;
    }
    return okay;
}

static bool module_fields(qa_source_save_io *io, saved_module *role)
{
    if (!qa_source_save_bool(io, &role->present)) return false;
    if (!role->present) return true;
    if (!qa_source_save_u32(io, &role->role) || !qa_source_save_u32(io, &role->abi) ||
        !qa_source_save_u64(io, &role->sequence) || !qa_source_save_string(io, &role->service_owner) ||
        !qa_source_save_bool(io, &role->initialized) || !qa_source_save_bool(io, &role->succeeded) ||
        !opening_fields(io, &role->artifact) || !opening_fields(io, &role->declaration) ||
        (role->role == QA_QVM_CGAME && (!opening_fields(io, &role->body_declaration) ||
            !blob(io, &role->body_profile))) ||
        !blob(io, &role->executor) || !blob(io, &role->functions) || !blob(io, &role->services)) return false;
    return ((role->role == QA_QVM_UI || role->role == QA_QVM_CGAME) && role->abi <= QA_QVM_Q3_116N &&
        role->sequence && role->service_owner && (!role->succeeded || role->initialized) &&
        role->artifact.path && *role->artifact.path && role->artifact.pool && role->artifact.resource &&
        role->executor.size && role->services.size &&
        (role->role != QA_QVM_CGAME || role->body_profile.size)) ||
        application_fail(io->error, QA_ERROR_FORMAT, "Acquired CLIENT continuation leaves its genuine role inventory");
}

static bool fields(qa_source_save_io *io, saved_modules *saved)
{
    return signature(io) && qa_source_save_u64(io, &saved->catalog) && qa_source_save_u64(io, &saved->view) &&
        qa_source_save_u64(io, &saved->generation) && qa_source_save_u64(io, &saved->epoch) &&
        qa_source_save_u64(io, &saved->physical_owner) && qa_source_save_u32(io, &saved->receiver) &&
        qa_source_save_u32(io, &saved->seat) && qa_source_save_bytes(io, saved->identity.bytes, sizeof(saved->identity.bytes)) &&
        qa_source_save_bool(io, &saved->pure) && blob(io, &saved->script_globals) &&
        module_fields(io, &saved->ui) && module_fields(io, &saved->cgame) &&
        ((saved->catalog && saved->view && saved->generation && saved->epoch && saved->physical_owner &&
          saved->receiver && saved->script_globals.size && saved->ui.present && saved->ui.role == QA_QVM_UI &&
          saved->pure == saved->cgame.present && (!saved->cgame.present ||
            (saved->cgame.role == QA_QVM_CGAME && saved->cgame.sequence != saved->ui.sequence &&
             saved->cgame.service_owner != saved->ui.service_owner)) &&
          (!saved->cgame.present || !saved->cgame.initialized || saved->ui.initialized) &&
          (!saved->cgame.present || !saved->cgame.succeeded || saved->ui.succeeded)) ||
            application_fail(io->error, QA_ERROR_FORMAT, "Invalid acquired CLIENT physical inventory"));
}

static void saved_dispose(saved_modules *saved)
{
    qa_buffer_free(&saved->script_globals);
    saved_module *roles[] = {&saved->ui, &saved->cgame};
    for (size_t i = 0; i < 2; ++i) {
        qa_buffer_free(&roles[i]->executor); qa_buffer_free(&roles[i]->functions); qa_buffer_free(&roles[i]->services);
        qa_buffer_free(&roles[i]->body_profile);
    }
}

static bool capture_opening(const qa_application_content_graph *graph, qa_vfs *view,
    const native_client_opening *actual, saved_opening *out, qa_error *error)
{
    if (!actual->resource) return true;
    *out = (saved_opening){.path = actual->path, .acquisition = actual->acquisition};
    return (qa_application_content_resource_id(graph, actual->resource, &out->pool, &out->resource) &&
        qa_application_content_pool(graph, out->pool) == qa_vfs_resources(view) &&
        actual->acquisition.resource_id == qa_resource_id(actual->resource)) ||
        application_fail(error, QA_ERROR_FORMAT, "Acquired CLIENT opening is absent from the actual content graph");
}

static bool capture_module(const qa_application_content_graph *graph, native_client_module *actual,
    saved_module *out, qa_error *error)
{
    if (!actual->ready) {
        if (actual->host || actual->artifact.path || actual->artifact.resource ||
            actual->declaration.path || actual->declaration.resource ||
            actual->body_declaration.path || actual->body_declaration.resource ||
            actual->image || actual->module || actual->native_declaration ||
            actual->native || actual->vm || actual->equipment || actual->body ||
            actual->body_profile.artifact_path || actual->body_profile.submissions)
            return application_fail(error, QA_ERROR_ARGUMENT, "Incomplete acquired CLIENT constructor cannot be captured");
        return true;
    }
    if (!actual->vm || actual->native || actual->module)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Native CLIENT private RAM requires its actual executor continuation owner");
    *out = (saved_module){.present = true, .role = actual->kind, .abi = actual->abi, .sequence = actual->sequence,
        .service_owner = actual->service_owner, .initialized = actual->initialized, .succeeded = actual->init_succeeded};
    qa_vfs *view = actual->owner->source.descriptor->content;
    bool okay = capture_opening(graph, view, &actual->artifact, &out->artifact, error) &&
        capture_opening(graph, view, &actual->declaration, &out->declaration, error) &&
        capture_opening(graph, view, &actual->body_declaration, &out->body_declaration, error) &&
        qa_vfs_acquisition_retained(actual->owner->source.descriptor->content, &actual->artifact.acquisition, error) &&
        (!actual->declaration.resource || qa_vfs_acquisition_retained(actual->owner->source.descriptor->content,
            &actual->declaration.acquisition, error)) &&
        (!actual->body_declaration.resource ||
            (actual->kind == QA_QVM_CGAME && actual->body_declaration.path &&
             !strcmp(actual->body_declaration.path, "cgame-presentation.json") &&
             qa_vfs_acquisition_retained(view, &actual->body_declaration.acquisition, error))) &&
        qa_q3_host_checkpoint_portable_ready(actual->host, error) &&
        qa_q3_host_checkpoint_services(actual->host, &out->services, error);
    if (okay && actual->kind == QA_QVM_CGAME)
        okay = application_q3_body_profile_checkpoint(actual->image, actual->abi, actual->artifact.path,
            &actual->body_profile, &out->body_profile, error);
    if (okay) okay = actual->kind == QA_QVM_CGAME ? native_client_module_functions_checkpoint(
        actual, &out->functions, error) :
        qa_qvm_checkpoint_functions(actual->vm, NULL, 0, error);
    return okay && qa_qvm_checkpoint(actual->vm, &out->executor, error);
}

static bool capture(application_native_q3_client_modules *owner, qa_buffer *out, qa_error *error)
{
    qa_application_q3_remote_source source;
    if (!owner || !owner->prepared || !out || out->data || out->size || owner->retiring ||
        !qa_application_native_q3_client_modules_idle(owner) ||
        !native_client_modules_physical(owner, &source, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT capture requires its idle actual physical owner");
    qa_application_content_graph *graph = qa_application_content_graph_read(owner->app);
    saved_modules saved = {.generation = source.configuration_generation, .epoch = source.connection_epoch,
        .physical_owner = source.receiver.service_owner, .receiver = source.receiver.receiver,
        .seat = source.receiver.seat, .identity = source.descriptor->identity, .pure = owner->pure};
    if (!graph || !(saved.catalog = qa_application_content_catalog_id(graph, qa_launch_instance_catalog(source.descriptor))) ||
        !(saved.view = qa_application_content_view_id(graph, source.descriptor->content)))
        return application_fail(error, QA_ERROR_FORMAT, "Acquired CLIENT descriptor lacks its real content graph");
    qa_source_save_io io = {0};
    bool okay = qa_script_defines_save_capture(owner->script_globals, &saved.script_globals, error) &&
        capture_module(graph, &owner->ui, &saved.ui, error) &&
        capture_module(graph, &owner->cgame, &saved.cgame, error) &&
        qa_source_save_writer(&io, owner->app->session, error) && fields(&io, &saved) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); saved_dispose(&saved); return okay;
}

bool qa_application_native_q3_client_modules_checkpoint(const application_native_q3_client_modules *owner,
    qa_buffer *out, qa_error *error)
{
    qa_application_q3_remote_source actual;
    if (!owner || owner->restore_pending || owner->retiring ||
        (owner->app->operation != APPLICATION_PERSISTING &&
            (owner->app->operation != APPLICATION_IDLE || !owner->app->content_graph ||
             owner->app->capture_content_graph || !native_client_modules_physical(owner, &actual, error) ||
             !qa_application_native_q3_client_modules_current(owner, &actual))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT checkpoint requires its application capture lease");
    return capture((application_native_q3_client_modules *)owner, out, error);
}

static bool opening_valid(qa_application_content_graph *graph, qa_vfs *view,
    const saved_opening *saved, bool required, qa_error *error)
{
    if (!saved->resource && !saved->pool && !saved->path) {
        return (!required && !saved->acquisition.mount && !saved->acquisition.resource_id &&
            !saved->acquisition.path && !saved->acquisition.lookup_path &&
            !saved->acquisition.link_source && !saved->acquisition.link_target) ||
                application_fail(error, QA_ERROR_FORMAT, "Absent acquired CLIENT opening retains a false recipe");
    }
    const qa_resource *resource = qa_application_content_resource(graph, saved->pool, saved->resource);
    return (saved->path && *saved->path && saved->pool && saved->resource && resource &&
        qa_application_content_pool(graph, saved->pool) == qa_vfs_resources(view) &&
        saved->acquisition.path && !strcmp(saved->path, saved->acquisition.path) &&
        saved->acquisition.resource_id == qa_resource_id(resource) &&
        qa_vfs_acquisition_retained(view, &saved->acquisition, error)) ||
            application_fail(error, QA_ERROR_FORMAT, "Acquired CLIENT opening leaves its actual retained VFS journal");
}

static bool copy_text(const char *text, char **out, qa_error *error)
{
    if (!text) return true;
    size_t length = strlen(text);
    *out = malloc(length + 1);
    if (!*out) return application_fail(error, QA_ERROR_MEMORY, "Retaining restored acquired CLIENT recipe");
    memcpy(*out, text, length + 1); return true;
}

static bool restore_opening(qa_application_content_graph *graph,
    const saved_opening *saved, native_client_opening *out, qa_error *error)
{
    if (!saved->resource) return true;
    out->resource = (qa_resource *)qa_application_content_resource(graph, saved->pool, saved->resource);
    qa_resource_retain(out->resource);
    out->acquisition.mount = saved->acquisition.mount;
    out->acquisition.resource_id = saved->acquisition.resource_id;
    return copy_text(saved->path, &out->path, error) &&
        copy_text(saved->acquisition.path, &out->acquisition.path, error) &&
        copy_text(saved->acquisition.lookup_path, &out->acquisition.lookup_path, error) &&
        copy_text(saved->acquisition.link_source, &out->acquisition.link_source, error) &&
        copy_text(saved->acquisition.link_target, &out->acquisition.link_target, error);
}

static bool restore_artifact(qa_application_content_graph *graph, native_client_module *actual,
    const saved_module *saved, qa_error *error)
{
    if (!saved->present) return true;
    actual->sequence = saved->sequence; actual->service_owner = saved->service_owner;
    if (!restore_opening(graph, &saved->artifact, &actual->artifact, error) ||
        !restore_opening(graph, &saved->declaration, &actual->declaration, error) ||
        !restore_opening(graph, &saved->body_declaration, &actual->body_declaration, error) ||
        !qa_qvm_image_load(qa_resource_bytes(actual->artifact.resource), &actual->image, error) ||
        !native_client_module_qualify(actual, error)) return false;
    if (actual->abi != (qa_qvm_abi)saved->abi)
        return application_fail(error, QA_ERROR_FORMAT, "Restored acquired CLIENT ABI differs from its exact artifact");
    return actual->kind != QA_QVM_CGAME || application_q3_body_profile_restore(actual->image,
        actual->abi, actual->artifact.path, (qa_bytes){saved->body_profile.data, saved->body_profile.size},
        &actual->body_profile, error);
}

static bool restore_module(native_client_module *actual, saved_module *saved, qa_error *error)
{
    if (!saved->present) return true;
    if (!native_client_module_construct(actual, true, error)) return false;
    qa_bytes executor = {saved->executor.data, saved->executor.size};
    bool okay = actual->kind == QA_QVM_CGAME ? native_client_module_functions_restore(actual,
        (qa_bytes){saved->functions.data, saved->functions.size}, executor, error) :
        !saved->functions.size && qa_qvm_restore_candidate_bindings(actual->vm, executor, NULL, NULL, 0, error);
    if (okay) okay = qa_qvm_restore_candidate(actual->vm, executor, error);
    if (!okay) return false;
    actual->initialized = saved->initialized; actual->init_succeeded = saved->succeeded;
    actual->saved_services = saved->services; saved->services = (qa_buffer){0};
    return true;
}

bool qa_application_native_q3_client_modules_restore(qa_application *app,
    const qa_application_native_q3_client_modules_options *options, qa_bytes bytes,
    application_native_q3_client_modules **out, qa_error *error)
{
    if (!app || !options || !out || *out || app->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT import requires its isolated physical candidate");
    saved_modules saved = {0}; qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, app->session, bytes, error) && fields(&io, &saved) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    qa_application_content_graph *graph = qa_application_content_graph_read(app);
    const qa_application_q3_remote_source *source = &options->source;
    if (okay) okay = graph && source->descriptor && saved.receiver == source->receiver.receiver &&
        saved.seat == source->receiver.seat && saved.physical_owner == source->receiver.service_owner &&
        saved.generation == source->configuration_generation && saved.epoch == source->connection_epoch &&
        qa_sha256_equal(&saved.identity, &source->descriptor->identity) &&
        qa_application_content_catalog(graph, saved.catalog) == qa_launch_instance_catalog(source->descriptor) &&
        qa_application_content_view(graph, saved.view) == source->descriptor->content;
    if (okay) okay = native_client_modules_policy(options->gamestate, saved.pure, error);
    if (okay) okay = opening_valid(graph, source->descriptor->content, &saved.ui.artifact, true, error) &&
        opening_valid(graph, source->descriptor->content, &saved.ui.declaration, false, error) &&
        (!saved.cgame.present || (opening_valid(graph, source->descriptor->content, &saved.cgame.artifact, true, error) &&
            opening_valid(graph, source->descriptor->content, &saved.cgame.declaration, false, error) &&
            opening_valid(graph, source->descriptor->content, &saved.cgame.body_declaration, false, error) &&
            (!saved.cgame.body_declaration.resource ||
                !strcmp(saved.cgame.body_declaration.path, "cgame-presentation.json"))));
    if (okay && saved.pure && (strcmp(saved.ui.artifact.path, "vm/ui.qvm") ||
        strcmp(saved.cgame.artifact.path, "vm/cgame.qvm"))) okay = false;
    if (!okay) {
        saved_dispose(&saved);
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT,
            "Acquired CLIENT continuation differs from its actual physical descriptor");
        return false;
    }
    qa_script_defines *script_globals = NULL;
    okay = qa_script_defines_save_restore((qa_bytes){saved.script_globals.data, saved.script_globals.size},
        &script_globals, error) && native_client_modules_allocate(app, options, true, out, error);
    if (okay) {
        application_native_q3_client_modules *owner = *out;
        owner->script_globals = script_globals; script_globals = NULL;
        owner->pure = saved.pure;
        okay = restore_artifact(graph, &owner->ui, &saved.ui, error) &&
            restore_artifact(graph, &owner->cgame, &saved.cgame, error);
        if (okay) okay = native_client_module_namespace(&owner->ui, true, error) &&
            (!saved.cgame.present || native_client_module_namespace(&owner->cgame, true, error));
        /* Both held artifacts and immutable profiles precede all hosts. */
        if (okay) okay = restore_module(&owner->ui, &saved.ui, error) && restore_module(&owner->cgame, &saved.cgame, error);
        if (okay) {
            owner->saved.data = bytes.size ? malloc(bytes.size) : NULL; owner->saved.size = bytes.size;
            if (bytes.size && !owner->saved.data) okay = application_fail(error, QA_ERROR_MEMORY,
                "Retaining complete acquired CLIENT restoration witness");
            else if (bytes.size) memcpy(owner->saved.data, bytes.data, bytes.size);
        }
        if (okay) owner->prepared = true;
    }
    qa_script_defines_release(script_globals);
    saved_dispose(&saved); return okay;
}

bool qa_application_native_q3_client_modules_finish_restore(application_native_q3_client_modules *owner, qa_error *error)
{
    if (!owner || !owner->restore_pending || !owner->saved.data || owner->app->operation != APPLICATION_PERSISTING ||
        !qa_application_native_q3_client_modules_idle(owner) ||
        !native_client_modules_policy(owner->options.gamestate, owner->pure, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CLIENT finish requires its restored actual owners");
    native_client_module *roles[] = {&owner->ui, &owner->cgame};
    for (size_t i = 0; i < 2; ++i) if (roles[i]->ready) {
        qa_buffer actual = {0};
        bool okay = qa_q3_host_checkpoint_services(roles[i]->host, &actual, error) &&
            actual.size == roles[i]->saved_services.size && !memcmp(actual.data, roles[i]->saved_services.data, actual.size);
        qa_buffer_free(&actual);
        if (!okay) return application_fail(error, QA_ERROR_FORMAT, "Acquired CLIENT restored service inventory differs");
    }
    for (size_t i = 0; i < 2; ++i) if (roles[i]->ready && !qa_q3_host_finish_restore(roles[i]->host, error)) return false;
    qa_buffer actual = {0};
    bool okay = capture(owner, &actual, error) && actual.size == owner->saved.size &&
        !memcmp(actual.data, owner->saved.data, actual.size);
    qa_buffer_free(&actual);
    if (!okay) return application_fail(error, QA_ERROR_FORMAT, "Acquired CLIENT differs after true owner reconnection");
    owner->restore_pending = false;
    if (owner->cgame.ready && owner->cgame.init_succeeded) {
        qa_application_q3_remote_source source;
        if (!native_client_modules_physical(owner, &source, error) ||
            !application_native_q3_remote_role_modules_initialized(owner->provider, &source, owner, error)) {
            owner->restore_pending = true;
            return false;
        }
    }
    qa_buffer_free(&owner->saved);
    for (size_t i = 0; i < 2; ++i) qa_buffer_free(&roles[i]->saved_services);
    return true;
}

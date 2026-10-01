#include "guest_q3_factory.h"
#include "guest_q3_private.h"

static void artifact_free(q3g_artifact *artifact)
{
    qa_native_module_release(artifact->module);
    qa_resource_release(artifact->resource);
    qa_vfs_acquisition_dispose(&artifact->acquisition);
    qa_launch_instance_lease_release(artifact->descriptor);
    free(artifact->path);
    free(artifact);
}

/* SDK Sys_LoadDll authors these names for its admitted i386 platforms. The
 * physical source module supplies the target when a native CGAME is retained;
 * a QVM client uses the actual process target. Other targets fall through to
 * VM_Create's bytecode fallback without inventing a foreign loader target. */
static qa_native_target native_ui_target(q3g_role *cgame)
{
    return cgame->module ? qa_native_module_describe(cgame->module).image.target : qa_native_host_target();
}

static const char *native_ui_path(q3g_role *cgame)
{
    qa_native_target target = native_ui_target(cgame);
    if (target.arch != QA_NATIVE_ARCH_I386) return NULL;
    if (target.os == QA_NATIVE_OS_WINDOWS) return "uix86.dll";
    if (target.os == QA_NATIVE_OS_LINUX) return "uii386.so";
    return NULL;
}

static bool native_artifact(struct application_q3_guest *engine, q3g_role *cgame,
    const char *path, bool *loaded, qa_error *error)
{
    *loaded = false;
    for (q3g_artifact *row = engine->artifacts; row; row = row->next)
        if (row->view == cgame->descriptor->content && row->kind == QA_QVM_UI &&
            !row->qvm && row->module && !strcmp(row->path, path)) {
            *loaded = true; return true;
        }
    bool found = false;
    uint64_t extent = 0;
    if (!qa_vfs_probe(cgame->descriptor->content, path, &found, &extent, error)) return false;
    if (!found) return true;
    q3g_artifact *artifact = calloc(1, sizeof(*artifact));
    if (!artifact) return application_fail(error, QA_ERROR_MEMORY, "Retaining source UI native opening");
    artifact->path = q3g_copy_text(path, error);
    artifact->kind = QA_QVM_UI; artifact->abi = QA_QVM_Q3_MODERN;
    artifact->view = cgame->descriptor->content;
    bool ok = artifact->path && qa_launch_instance_retain_metadata(cgame->descriptor,
        &artifact->descriptor, error) && qa_vfs_acquire_receipt(artifact->view, path,
        &artifact->resource, &artifact->acquisition, error);
    if (!ok) { artifact_free(artifact); return false; }
    qa_error qualification = {0};
    if (!qa_native_module_load(qa_resource_bytes(artifact->resource), path,
        QA_NATIVE_Q3_VMMAIN, NULL, &artifact->module, &qualification)) {
        artifact_free(artifact);
        if (qualification.code == QA_ERROR_MEMORY) { if (error) *error = qualification; return false; }
        return true;
    }
    qa_native_target expected = native_ui_target(cgame);
    qa_native_target actual = qa_native_module_describe(artifact->module).image.target;
    if (actual.os != expected.os || actual.arch != expected.arch || actual.abi != expected.abi ||
        actual.pointer_bytes != expected.pointer_bytes) {
        artifact_free(artifact); return true;
    }
    artifact->next = engine->artifacts; engine->artifacts = artifact;
    *loaded = true; return true;
}

bool application_guest_q3_source_ui_create(struct application_q3_guest *engine,
    uint32_t seat, q3g_role **out, qa_error *error)
{
    q3g_role *cgame = NULL;
    for (q3g_role *role = engine ? engine->roles : NULL; role; role = role->next)
        if (role->kind == QA_QVM_CGAME && role->seat == seat && role->ready && !role->retired) {
            if (cgame) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous actual source UI client");
            cgame = role;
        }
    qa_cvars *cvars = NULL;
    if (!out || !cgame || !qa_q3_host_console(cgame->host, &cvars, NULL) || !cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source UI requires its actual prepared client registry");
    const qa_cvar_view *choice = qa_cvars_find(cvars, "vm_ui");
    const qa_cvar_view *restricted = qa_cvars_find(cvars, "fs_restrict");
    const char *path = "vm/ui.qvm";
    if (choice && isfinite(choice->number) && truncf(choice->number) == 0 &&
        (!restricted || restricted->number == 0)) {
        const char *native_path = native_ui_path(cgame);
        bool loaded = false;
        if (native_path && !native_artifact(engine, cgame, native_path, &loaded, error)) return false;
        if (loaded) path = native_path;
    }
    return q3g_role_create(engine, QA_QVM_UI, seat, path, false, out, error);
}

#include "guest_q3_factory.h"
#include "guest_q3_private.h"
#include "qa/network_q3.h"

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
    return cgame->client_source ? q3g_role_create_client(engine, QA_QVM_UI, seat, path, false,
        cgame->client_source, out, error) : q3g_role_create(engine, QA_QVM_UI, seat, path, false, out, error);
}

static bool received_current(q3g_role *cgame, const qa_q3_gamestate *state, qa_error *error)
{
    struct application_q3_guest *engine = cgame->engine;
    ++engine->calls;
    const qa_q3_gamestate *actual = cgame->client_services.gamestate(cgame->client_services.context);
    --engine->calls;
    return (actual == state && state->client_number == (int32_t)cgame->client) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Source UI policy lost its actual received local gamestate");
}

static void discard_candidate(struct application_q3_guest *engine, q3g_role *role)
{
    role->retired = true;
    if (!q3g_role_destroy(role, NULL)) { role->next = engine->roles; engine->roles = role; }
}

bool application_guest_q3_source_ui_received(q3g_role *cgame, const qa_q3_gamestate *state,
    q3g_role **out, qa_error *error)
{
    if (!cgame || !state || !out || cgame->kind != QA_QVM_CGAME || !cgame->local_client ||
        !cgame->ready || cgame->retired || cgame->initialized || !cgame->host ||
        cgame->engine->calls || !cgame->client_services.gamestate ||
        !received_current(cgame, state, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source UI policy requires its post-Begin local client");
    struct application_q3_guest *engine = cgame->engine;
    q3g_role **position = NULL;
    for (q3g_role **link = &engine->roles; *link; link = &(*link)->next)
        if ((*link)->kind == QA_QVM_UI && (*link)->seat == cgame->seat &&
            (*link)->ready && !(*link)->retired) {
            if (position) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous received source UI");
            position = link;
        }
    q3g_role *ui = position ? *position : NULL;
    if (!ui || ui->descriptor->storage != cgame->descriptor->storage ||
        ui->descriptor->content != cgame->descriptor->content)
        return application_fail(error, QA_ERROR_ARGUMENT, "Received source UI has another physical descriptor");
    char pure[QA_Q3_BIG_INFO_CHARS];
    if (!qa_q3_info_value(qa_q3_configstring(state, 1), "sv_pure", pure, sizeof(pure), error)) return false;
    if (!strtol(pure, NULL, 10) || ui->image) { *out = ui; return true; }
    q3g_role *replacement = NULL;
    bool created = cgame->client_source ? q3g_role_create_client(engine, QA_QVM_UI, cgame->seat,
        "vm/ui.qvm", false, cgame->client_source, &replacement, error) :
        q3g_role_create(engine, QA_QVM_UI, cgame->seat, "vm/ui.qvm", false, &replacement, error);
    if (!created) return false;
    bool current = received_current(cgame, state, error) &&
        qa_q3_info_value(qa_q3_configstring(state, 1), "sv_pure", pure, sizeof(pure), error) &&
        strtol(pure, NULL, 10) != 0;
    if (!current) {
        if (error && error->code == QA_OK)
            application_fail(error, QA_ERROR_ARGUMENT, "Received source UI pure policy changed during preparation");
        discard_candidate(engine, replacement); return false;
    }
    ui->source_cleared = true;
    q3g_role *next = ui->next;
    if (!q3g_role_shutdown_source(ui, false, error) || !q3g_role_destroy(ui, error)) {
        discard_candidate(engine, replacement); return false;
    }
    replacement->next = next;
    *position = replacement;
    *out = replacement;
    return true;
}

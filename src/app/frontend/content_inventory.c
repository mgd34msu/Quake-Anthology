#include "internal.h"
#include "content_inventory.h"
#include "qa/scene_resource_save.h"
#include "qa/vfs_view_save.h"
#include "qa/ui_menu_save.h"
#include "qa/catalog_save.h"

static bool visit_view(const qa_application_content_visitor *visitor, const qa_vfs *view, qa_error *error)
{
    if (!view) return true;
    qa_resource_pool *pool = qa_vfs_resources(view);
    return pool ? visitor->pool(visitor->context, pool, error) &&
        visitor->view(visitor->context, view, error) :
        frontend_fail(error, QA_ERROR_ARGUMENT, "frontend content view has no actual pool");
}
static bool visit_catalog(const qa_application_content_visitor *visitor, const qa_catalog *catalog, qa_error *error)
{
    if (!catalog) return true;
    qa_resource_pool *pool = qa_catalog_resources(catalog);
    return pool ? visitor->pool(visitor->context, pool, error) &&
        visitor->catalog(visitor->context, catalog, error) &&
        visit_view(visitor, qa_catalog_files(catalog), error) :
        frontend_fail(error, QA_ERROR_ARGUMENT, "frontend catalog has no actual pool");
}
bool frontend_content_visit(void *context, const qa_application *application,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    const qa_frontend *frontend = context;
    if (!frontend || !application || frontend->application != application || frontend->stepping || frontend->source_restoring ||
        !visitor || !visitor->pool || !visitor->view || !visitor->catalog ||
        !frontend_native_q2_callbacks_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend content inventory requires idle actual owners");
    if (!visit_view(visitor, frontend->mounts, error) || !visit_view(visitor, frontend->ui_mounts, error)) return false;
    for (size_t i = 0; i < frontend_source_group_count(frontend); ++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(frontend, i, &group))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend source group is not a qualified content owner");
        if (!visit_view(visitor, group.source_files, error) || !visit_view(visitor, group.mounts, error)) return false;
    }
    for (unsigned family = 0; family < 2; ++family) {
        for (size_t i = 0;; ++i) {
            const qa_scene_resources *images = family == 0 ?
                frontend_event_images_at((qa_frontend *)frontend, i) :
                frontend_visual_images_at((qa_frontend *)frontend, i);
            if (!images) break;
            if (!visit_view(visitor, qa_scene_resources_files(images), error)) return false;
        }
    }
    /* Native GAME leases can own a VFS before any lazy image/sound owner. */
    for (size_t i = 0;; ++i) {
        const qa_vfs *view = frontend_native_q2_files_at(frontend, i);
        if (!view) break;
        if (!visit_view(visitor, view, error)) return false;
    }
    if (!frontend_tools_content_visit(frontend, visitor, error)) return false;
    if (frontend->seats) for (unsigned i = 0; i < frontend->options.seats; ++i) {
        const frontend_seat *seat = &frontend->seats[i];
        if (seat->frontend != frontend || seat->id != i)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend catalog seat has foreign ownership");
        if (!visit_catalog(visitor, qa_ui_library_catalog(seat->library), error) ||
            !visit_catalog(visitor, qa_ui_mods_catalog(seat->mods), error)) return false;
    }
    return true;
}

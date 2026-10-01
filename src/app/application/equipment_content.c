#include "equipment_runtime.h"
#include "qa/application_equipment_content.h"

static bool source_read(const qa_application *app, qa_actor_owner owner,
    application_equipment_runtime_source *out)
{
    if (!app || !app->session || app->destroy_requested || !owner ||
        !app->equipment_runtime) return false;
    for (size_t i = 0; i < application_equipment_runtime_source_count(app->equipment_runtime); ++i) {
        application_equipment_runtime_source source;
        if (!application_equipment_runtime_source_at(app->equipment_runtime, i, &source, NULL)) return false;
        if (source.gear_owner != owner) continue;
        qa_application_equipment_event witness = {.provider = owner,
            .selected_provider = source.selected_owner, .service_owner = source.service_owner};
        if (!source.gear || !source.definition || !source.descriptor || !source.content ||
            !source.artifact || !source.acquisition || !source.selected_owner || !source.service_owner ||
            source.acquisition->resource_id != qa_resource_id(source.artifact) ||
            !application_equipment_runtime_event_current(app->equipment_runtime, &witness) ||
            !qa_vfs_acquisition_retained(source.content, source.acquisition, NULL)) return false;
        *out = source; return true;
    }
    return false;
}

bool qa_application_equipment_content_read(const qa_application *app, qa_actor_owner owner,
    qa_application_equipment_content *out, qa_error *error)
{
    application_equipment_runtime_source source;
    if (!out || !source_read(app, owner, &source))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear content has no current retained private namespace");
    *out = (qa_application_equipment_content){.owner = source.gear_owner,
        .selected_owner = source.selected_owner, .service_owner = source.service_owner,
        .descriptor = source.descriptor, .artifact = source.artifact,
        .acquisition = source.acquisition, .files = source.content};
    return true;
}

bool qa_application_equipment_content_current(const qa_application *app,
    const qa_application_equipment_content *view)
{
    application_equipment_runtime_source source;
    return view && source_read(app, view->owner, &source) &&
        source.selected_owner == view->selected_owner && source.service_owner == view->service_owner &&
        source.descriptor == view->descriptor && source.artifact == view->artifact &&
        source.acquisition == view->acquisition && source.content == view->files;
}

#include "internal.h"
#include "component_scene.h"
#include "content_inventory.h"
#include "qa/scene_resource_save.h"
#include "qa/vfs_view_save.h"
#include "qa/ui_menu_save.h"
#include "qa/catalog_save.h"
#include "qa/material_library_save.h"
#include "capture.h"
#include "campaign.h"
#include "ui_features.h"
#include "native_q3_client.h"
#include "config_store.h"
#include "keys.h"
#include "client_registry.h"
#include "equipment_q3.h"
#include "equipment_gear.h"
#include "selected_character.h"
#include "selected_effects.h"
#include "global_settings_storage.h"
#include "music_sources.h"
#include "remote_q3_client.h"
#include "remote_q1_client.h"
#include "remote_q1_skins.h"
#include "remote_q2_client.h"
#include "client_source.h"
#include "remote_unified.h"

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
static bool visit_material_catalog(const qa_application_content_visitor *visitor,
    const qa_material_library *library,qa_error *error)
{
    const qa_scene_resources *images=qa_material_library_resource_owner(library);
    const qa_vfs *files=images?qa_scene_resources_files(images):NULL;
    qa_resource_pool *pool=files?qa_vfs_resources(files):NULL;
    if (!library || !images || !files || !pool)
        return frontend_fail(error,QA_ERROR_FORMAT,"Material catalog lacks its actual image and content owners");
    if (!visit_view(visitor,files,error)) return false;
    size_t count=qa_material_library_catalog_resource_count(library);
    for (size_t i=0;i<count;++i) {
        const qa_resource *resource=qa_material_library_catalog_resource_at(library,i);
        if (!resource || qa_resource_pool_find(pool,qa_resource_id(resource))!=resource)
            return frontend_fail(error,QA_ERROR_FORMAT,"Retained shader catalog source leaves its genuine content pool");
    }
    return true;
}
bool frontend_content_visit(void *context, const qa_application *application,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    const qa_frontend *frontend = context;
    if (!frontend || !application || frontend->application != application || frontend->stepping || frontend->preparing ||
        frontend->source_restoring || !frontend->capture ||
        !visitor || !visitor->pool || !visitor->view || !visitor->catalog ||
        !frontend_native_q2_callbacks_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend content inventory requires idle actual owners");
    if (!visit_catalog(visitor,frontend->input_catalog,error) ||
        !visit_view(visitor, frontend->mounts, error) || !visit_view(visitor, frontend->ui_mounts, error) ||
        !visit_view(visitor, frontend->input_config, error)) return false;
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
    for (size_t i=0;;++i) {
        const qa_scene_resources *images=frontend_capture_images_at(frontend->capture,i);
        if(!images) break;
        if(!visit_view(visitor,qa_scene_resources_files(images),error)) return false;
    }
    for (size_t i=0;;++i) {
        const qa_material_library *library=frontend_capture_library_at(frontend->capture,i);
        if (!library) break;
        if (!visit_material_catalog(visitor,library,error)) return false;
    }
    for(size_t i=0;i<frontend_remote_q1_count(frontend);++i) {
        frontend_remote_q1_view owner;
        if(!frontend_remote_q1_metadata_read(frontend_remote_q1_at(frontend,i),&owner,error) ||
            !visit_catalog(visitor,owner.domain.catalog,error) || !visit_catalog(visitor,owner.content.catalog,error) ||
            !visit_view(visitor,owner.content.mounts,error)) return false;
        frontend_remote_q1_skins *skins=frontend_remote_q1_skins_owner(frontend_remote_q1_at(frontend,i));
        if(skins && !visit_view(visitor,frontend_remote_q1_skins_files(skins),error)) return false;
    }
    for(size_t i=0;i<frontend_component_scene_count(frontend);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(frontend,i,&row,error) ||
            !visit_catalog(visitor,row.catalog,error) || !visit_view(visitor,row.files,error) ||
            (row.descriptor && !visit_view(visitor,row.descriptor->content,error))) return false;
    }
    if (!frontend_native_q3_content_visit(frontend,visitor,error) ||
        !frontend_client_sources_visit(frontend,visitor,error) ||
        !frontend_remote_q2_content_visit(frontend,visitor,error) ||
        !frontend_remote_unified_content_visit(frontend,visitor,error) ||
        !frontend_music_sources_content_visit(frontend->music_sources,visitor,error) ||
        (frontend->global_settings_storage &&
            !frontend_global_settings_storage_visit(frontend->global_settings_storage,visitor,error)) ||
        !frontend_remote_q3_content_visit(frontend,visitor,error) ||
        !frontend_equipment_q3_content_visit(frontend,visitor,error) ||
        !frontend_selected_character_content_visit(frontend,visitor,error) ||
        !frontend_selected_effects_content_visit(frontend,visitor,error) ||
        !frontend_equipment_gear_content_visit(frontend,visitor,error) ||
        !frontend_client_registries_visit(frontend,visitor,error) ||
        !frontend_network_content_visit(frontend,application,visitor,error) ||
        !frontend_keys_visit(frontend->keys,visitor,error) ||
        !frontend_config_store_visit(frontend->config_store,visitor,error) ||
        !frontend_ui_features_content_visit(frontend,visitor,error) ||
        !frontend_campaign_content_visit(frontend,visitor,error) ||
        !frontend_tools_content_visit(frontend, visitor, error)) return false;
    if (frontend->seats) for (unsigned i = 0; i < frontend->options.seats; ++i) {
        const frontend_seat *seat = &frontend->seats[i];
        if (seat->frontend != frontend || seat->id != i)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend catalog seat has foreign ownership");
        if (!visit_catalog(visitor, qa_ui_library_catalog(seat->library), error) ||
            !visit_catalog(visitor, qa_ui_mods_catalog(seat->mods), error)) return false;
    }
    return true;
}

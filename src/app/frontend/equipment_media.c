#include "equipment_media_private.h"
#include "equipment_held_stock.h"
#include "../application/equipment_runtime.h"
#include "qa/application_equipment_content.h"
#include <stdio.h>

bool frontend_equipment_media_namespace_current(const qa_frontend *frontend,
    const frontend_equipment_media *row)
{
    if (!row->gear_namespace) {
        if (row->gear_service_owner) return false;
        if (row->family != QA_GAME_Q3) return true;
        qa_q3_product product;
        if (!frontend || !frontend->application || !qa_application_equipment_q3_product_read(
                frontend->application, row->provider, &product, NULL)) return false;
        const char *item = qa_strings_cstr(qa_session_strings(
            qa_application_session(frontend->application)), row->item);
        size_t count = 0; const qa_q3_item *items = qa_q3_items(product, &count);
        for (size_t i = 0; item && i < count; ++i) {
            if (items[i].kind != QA_Q3_ITEM_WEAPON || !items[i].model ||
                strcmp(items[i].model, row->view_path)) continue;
            const char *name = qa_q3_weapon_identity_name((qa_q3_weapon)items[i].tag);
            char key[64];
            if (name) {
                snprintf(key, sizeof(key), "q3:weapon/%s", name);
                if (!strcmp(item, key)) return true;
            }
        }
        return false;
    }
    qa_application_equipment_content content;
    if (row->family != QA_GAME_Q3 || !frontend || !frontend->application ||
        !qa_application_equipment_content_read(frontend->application, row->gear_namespace, &content, NULL) ||
        content.selected_owner != row->provider || content.service_owner != row->gear_service_owner ||
        !qa_vfs_lookup_equal(content.files, row->owner.mounts)) return false;
    application_equipment_runtime *runtime = frontend->application->equipment_runtime;
    for (size_t i = 0; i < application_equipment_runtime_source_count(runtime); ++i) {
        application_equipment_runtime_source source;
        if (!application_equipment_runtime_source_at(runtime, i, &source, NULL)) return false;
        if (source.gear_owner == row->gear_namespace)
            return source.weapon_item == row->item && source.definition &&
                source.definition->presentation.view_model &&
                !strcmp(source.definition->presentation.view_model, row->view_path);
    }
    return false;
}

void frontend_equipment_media_dispose(frontend_equipment_media *media)
{
    qa_scene_model_destroy(media->held_scene);
    frontend_held_model_free(&media->held);
    frontend_model_release(media->held_lease);
    frontend_held_declaration_free(&media->declaration);
    qa_resource_release((qa_resource *)media->view.resource);
    qa_resource_release((qa_resource *)media->held_parent.resource);
    free(media->saved_parent_path); free(media->view_path); free(media);
}

bool frontend_equipment_idle(const qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment) return true;
    if (frontend->equipment->admitting) return false;
    for (const frontend_equipment_media *row = frontend->equipment->media; row; row = row->next)
        if (row->users || (row->held_scene && !qa_scene_model_idle(row->held_scene))) return false;
    return true;
}

bool frontend_equipment_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_equipment_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment media has an active real scene or admission");
    if (!frontend || !frontend->equipment) return true;
    frontend_equipment_media *row = frontend->equipment->media;
    while (row) { frontend_equipment_media *next = row->next; frontend_equipment_media_dispose(row); row = next; }
    frontend->equipment->media = frontend->equipment->tail = NULL;
    return true;
}

void frontend_equipment_destroy(qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment) return;
    qa_error error = {0};
    if (!frontend_equipment_retire(frontend, &error)) return;
    free(frontend->equipment); frontend->equipment = NULL;
}

static bool held_declaration(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_equipment_media *row, qa_error *error)
{
    size_t length = strlen(view->view_model);
    if (length > SIZE_MAX - sizeof(".held.json"))
        return frontend_fail(error, QA_ERROR_MEMORY, "Equipment declaration path exceeds address space");
    char *path = malloc(length + sizeof(".held.json"));
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual equipment declaration path");
    memcpy(path, view->view_model, length);
    memcpy(path + length, ".held.json", sizeof(".held.json"));
    bool found = false;
    uint64_t size;
    bool ok = qa_vfs_probe(row->owner.mounts, path, &found, &size, error);
    if (ok && found) {
        qa_resource *source = NULL;
        ok = qa_vfs_acquire(row->owner.mounts, path, &source, NULL, error);
        if (ok) ok = frontend_held_declaration_read(source, &row->declaration, error);
        qa_resource_release(source);
    } else if (ok) {
        const char *item = view->item ?
            qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), view->item) : NULL;
        ok = frontend_held_stock(view->family, view->view_model, item, &row->declaration, &found, error);
        if (ok && !found)
            ok = frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected equipment has no authored held declaration");
    }
    free(path);
    return ok;
}

static bool held_model(qa_frontend *frontend, frontend_equipment_media *row, qa_error *error)
{
    if (row->declaration.none) return true;
    const char *path = row->declaration.path;
    bool found = false;
    uint64_t size;
    if (!qa_vfs_probe(row->owner.mounts, path, &found, &size, error)) return false;
    if (!found && row->declaration.fallback) {
        path = row->declaration.fallback;
        if (!qa_vfs_probe(row->owner.mounts, path, &found, &size, error)) return false;
    }
    if (!found) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Actual source held model is absent");
    if (!frontend_visual_model_acquire(frontend, row->provider, row->family, path, NULL,
            &row->held_parent, error)) return false;
    qa_resource_retain((qa_resource *)row->held_parent.resource);
    if (row->family == QA_GAME_Q2 && !row->declaration.source) {
        bool known;
        if (!frontend_held_stock_q2_grip(row->held_parent.resource,
                &row->declaration.grip, &known, error)) return false;
    }
    if (!frontend_held_model_prepare(&row->declaration, row->held_parent.resource,
            row->held_parent.model, &row->held, error)) return false;
    const qa_scene_image_options *options = qa_scene_model_image_options(row->held_parent.scene);
    if (!options) return frontend_fail(error, QA_ERROR_FORMAT, "Held parent has no actual scene image policy");
    return qa_scene_model_create(row->held.model, row->owner.images, row->owner.materials,
        options, &row->held_scene, error);
}

static bool prepare_media(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_held_declaration *authored, frontend_equipment_media **out, qa_error *error)
{
    if (!frontend || !view || !out || !view->selected || !view->view_model || !view->view_model[0] ||
        (view->family == QA_GAME_Q3 && (!authored || !authored->source)) ||
        !qa_application_equipment_current(frontend->application, view) ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Foreign equipment media requires its actual selected source observation");
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (row->held_scene && !qa_scene_model_idle(row->held_scene))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment admission requires idle physical scenes");
    if (!frontend->equipment) {
        frontend->equipment = calloc(1, sizeof(*frontend->equipment));
        if (!frontend->equipment) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating retained equipment media owner");
    }
    for (frontend_equipment_media *row = frontend->equipment->media; row; row = row->next)
        if (row->provider == view->provider && row->family == view->family && row->item == view->item &&
            row->gear_namespace == view->gear_namespace && row->gear_service_owner == view->gear_service_owner &&
            !strcmp(row->view_path, view->view_model) && (view->family == QA_GAME_Q3 ||
                !view->view_source || row->view.resource == view->view_source)) {
            *out = row; return true;
        }
    frontend_equipment_media *row = calloc(1, sizeof(*row));
    if (!row) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating physical selected equipment media row");
    row->provider = view->provider; row->family = view->family; row->item = view->item;
    row->gear_namespace = view->gear_namespace; row->gear_service_owner = view->gear_service_owner;
    size_t length = strlen(view->view_model) + 1;
    row->view_path = malloc(length);
    if (!row->view_path) { frontend_equipment_media_dispose(row); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected model identity"); }
    memcpy(row->view_path, view->view_model, length);
    frontend->equipment->admitting = true;
    bool ok = frontend_visual_media_acquire(frontend, view->provider, view->family, &row->owner, error);
    if (ok && view->family != QA_GAME_Q3)
        ok = frontend_visual_model_acquire(frontend, view->provider, view->family, view->view_model,
            view->view_source, &row->view, error);
    if (ok) {
        qa_resource_retain((qa_resource *)row->view.resource);
        if (authored) { row->declaration = *authored; *authored = (frontend_held_declaration){0}; }
        else ok = held_declaration(frontend, view, row, error);
        if (ok) ok = held_model(frontend, row, error);
    }
    if (ok && (!qa_application_equipment_current(frontend->application, view) ||
        !frontend_equipment_media_namespace_current(frontend, row)))
        ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment admission lost its actual selected source owner");
    frontend->equipment->admitting = false;
    if (!ok) { frontend_equipment_media_dispose(row); return false; }
    row->bound = true;
    if (frontend->equipment->tail) frontend->equipment->tail->next = row;
    else frontend->equipment->media = row;
    frontend->equipment->tail = row;
    *out = row;
    return true;
}

bool frontend_equipment_media_prepare(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_equipment_media **out, qa_error *error)
{ return prepare_media(frontend, view, NULL, out, error); }

bool frontend_equipment_media_prepare_q3_held(qa_frontend *frontend,
    const qa_application_equipment_view *view, frontend_equipment_media **out,
    bool *authored, qa_error *error)
{
    if (!frontend || !view || !out || *out || !authored || view->family != QA_GAME_Q3 ||
        !view->selected || !view->view_model || !view->view_model[0] ||
        !qa_application_equipment_current(frontend->application, view) ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held admission requires its actual selected source and empty output");
    *authored = false;
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (row->held_scene && !qa_scene_model_idle(row->held_scene))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held admission requires idle physical scenes");
    for (frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next) {
        if (row->provider == view->provider && row->family == view->family && row->item == view->item &&
            row->gear_namespace == view->gear_namespace && row->gear_service_owner == view->gear_service_owner &&
            !strcmp(row->view_path, view->view_model)) {
            if (!row->bound || !row->declaration.source)
                return frontend_fail(error, QA_ERROR_FORMAT, "Q3 held media lost its retained authored declaration");
            *out = row; *authored = true; return true;
        }
    }
    frontend_visual_owner_view owner;
    if (!frontend_visual_media_acquire(frontend, view->provider, view->family, &owner, error)) return false;
    size_t length = strlen(view->view_model);
    if (length > SIZE_MAX - sizeof(".held.json"))
        return frontend_fail(error, QA_ERROR_MEMORY, "Q3 held declaration path exceeds address space");
    char *path = malloc(length + sizeof(".held.json"));
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Q3 held declaration path");
    memcpy(path, view->view_model, length);
    memcpy(path + length, ".held.json", sizeof(".held.json"));
    bool found = false;
    uint64_t size;
    bool okay = qa_vfs_probe(owner.mounts, path, &found, &size, error);
    qa_resource *resource = NULL; frontend_held_declaration declaration = {0};
    if (okay && found) okay = qa_vfs_acquire(owner.mounts, path, &resource, NULL, error) &&
        frontend_held_declaration_read(resource, &declaration, error);
    free(path); qa_resource_release(resource);
    if (okay && found) okay = prepare_media(frontend, view, &declaration, out, error);
    frontend_held_declaration_free(&declaration);
    if (okay && !qa_application_equipment_current(frontend->application, view))
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held probe retired its actual selected source");
    if (okay) *authored = found;
    return okay;
}

bool frontend_equipment_media_read(const frontend_equipment_media *row,
    frontend_equipment_media_view *out)
{
    if (!row || !out) return false;
    *out = (frontend_equipment_media_view){row->provider, row->family, row->item,
        row->view_path, row->owner, row->view, row->held_parent, &row->declaration,
        &row->held, row->held_scene, row->gear_namespace, row->gear_service_owner};
    return true;
}

bool frontend_equipment_media_retain(frontend_equipment_media *row, qa_error *error)
{
    if (!row || row->users == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment media retain exceeds its actual holder lifetime");
    ++row->users;
    return true;
}

void frontend_equipment_media_release(frontend_equipment_media *row)
{
    if (row) --row->users;
}

size_t frontend_equipment_media_count(const qa_frontend *frontend)
{
    size_t count = 0;
    if (frontend && frontend->equipment)
        for (const frontend_equipment_media *row = frontend->equipment->media; row; row = row->next) ++count;
    return count;
}

bool frontend_equipment_media_at(const qa_frontend *frontend, size_t ordinal,
    frontend_equipment_media_view *out)
{
    const frontend_equipment_media *row = frontend && frontend->equipment ? frontend->equipment->media : NULL;
    while (row && ordinal) { row = row->next; --ordinal; }
    return frontend_equipment_media_read(row, out);
}

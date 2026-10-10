#include "remote_unified_private.h"
#include "remote_unified_media_private.h"
#include "capture.h"
#include "shared_resource_policy.h"
#include "q3_render_policy.h"
#include "visual_access.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_assets_custody.h"
#include "qa/scene_world_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"
#include "remote_unified_material_movies_bridge.h"
#include "qa/media_library_save.h"
#include "qa/media_resource.h"

#include <stdlib.h>
#include <string.h>

static qa_scene_image_options image_options(qa_game_family family, qa_scene_image_usage usage)
{
    return (qa_scene_image_options){.family = family, .usage = usage,
        .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR,
        .mipmap = true, .transparent_index = 255};
}
static qa_scene_world_options world_options(qa_game_family family)
{
    /* These are the actual generic renderer's authored construction defaults,
     * shared with frontend_scene_sync. Q3's reached ENGINE policy replaces its
     * subdivision and lightmap shift before loading the immutable BSP. */
    return (qa_scene_world_options){.images = image_options(family, QA_IMAGE_USAGE_WALL),
        .subdivisions = 64, .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 1};
}
static bool bank(frontend_unified_media *owner, const char *content, unified_media_bank **out, qa_error *error)
{
    if (!owner || owner->importing || owner->busy || !content || !out || !frontend_unified_media_current(owner))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media lost its retained recipe content");
    for (unified_media_bank *row = owner->banks; row; row = row->next)
        if (row->content && !strcmp(row->content, content)) {
            if (row->constructing || row->construction_failed)
                return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified content bank retains an incomplete construction");
            *out = row; return true;
        }
    if (owner->frontend->resource_inventory)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified bank creation overlaps retained resource inventory");
    unified_media_bank *row = calloc(1, sizeof(*row));
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining unified content media bank");
    row->content = malloc(strlen(content) + 1);
    if (row->content) strcpy(row->content, content);
    row->constructing = true;
    row->next = owner->banks; owner->banks = row;
    bool okay = row->content && qa_executable_recipe_content(owner->recipe, content, &row->files, &row->product, error);
    if (okay) row->images = qa_scene_resources_create(row->files, error);
    if (okay) okay = row->images && frontend_image_policy_initialize(owner->frontend, row->images, error);
    if (okay) {
        row->materials = qa_material_library_create(row->images, owner->frontend->order, error);
        row->fonts = qa_font_library_create(row->files, row->images, error);
        okay = row->materials && row->fonts && qa_audio_bank_create(row->files, &row->sounds, error);
    }
    if (okay && row->product->family == QA_GAME_Q3)
        okay = frontend_source_identity_allocate(owner->frontend, &row->cinematic_audio_owner, error) &&
            frontend_q3_material_profile_initialize(owner->frontend, row->materials, error);
    if (okay) okay = frontend_unified_material_movies_create(owner, 0, error);
    if (okay) {
        qa_game_family family = row->product->family == QA_GAME_Q1 ? QA_GAME_Q1 :
            row->product->family == QA_GAME_Q2 ? QA_GAME_Q2 : QA_GAME_Q3;
        qa_scene_image_options images = image_options(family, QA_IMAGE_USAGE_WALL);
        okay = qa_material_library_load_scripts(row->materials, row->files, &images, error);
    }
    if (!okay) {
        row->constructing = false; row->construction_failed = true;
        if (!frontend_unified_material_movies_clear(owner, 0, NULL)) return false;
        owner->banks = row->next;
        qa_audio_bank_destroy(row->sounds); qa_font_library_destroy(row->fonts);
        qa_material_library_destroy(row->materials); qa_scene_resources_destroy(row->images);
        free(row->content); free(row); return false;
    }
    row->constructing = false; *out = row; return true;
}
bool frontend_unified_media_bank(frontend_unified_media *owner, const char *content,
    qa_scene_resources **images, qa_material_library **materials, qa_font_library **fonts,
    qa_audio_bank **sounds, qa_error *error)
{
    unified_media_bank *row;
    if (!images || !materials || !fonts || !sounds || !bank(owner, content, &row, error)) return false;
    *images = row->images; *materials = row->materials; *fonts = row->fonts; *sounds = row->sounds; return true;
}
bool frontend_unified_media_files(frontend_unified_media *owner, const char *content,
    qa_vfs **files, const qa_product **product, qa_error *error)
{
    unified_media_bank *row;
    if (!files || !product || !bank(owner, content, &row, error)) return false;
    *files = row->files; *product = row->product; return true;
}
bool frontend_unified_media_q3_assets(frontend_unified_media *owner, const char *content,
    qa_q3_presentation_assets **out, qa_error *error)
{
    unified_media_bank *row;
    if (!out || !bank(owner, content, &row, error)) return false;
    if (!row->q3_assets) {
        if (owner->frontend->resource_inventory)
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified Q3 registry creation overlaps resource inventory");
        qa_q3_presentation_asset_options options = {.provider = {
            .mounts = row->files, .images = row->images, .materials = row->materials,
            .family = QA_GAME_Q3}, .sounds = row->sounds, .movies = row->media};
        if (!qa_q3_presentation_assets_create(&options, &row->q3_assets, error)) return false;
    }
    *out = row->q3_assets; return true;
}
bool frontend_unified_media_q3_assets_read(const frontend_unified_media *owner, const qa_product *product,
    qa_q3_presentation_assets **out)
{
    if (!owner || !product || !out || owner->busy) return false;
    for (unified_media_bank *row=owner->banks;row;row=row->next)
        if (row->product==product) { *out=row->q3_assets; return row->q3_assets!=NULL; }
    return false;
}
bool frontend_unified_media_create(qa_frontend *frontend, qa_executable_recipe *recipe, uint32_t physical_seat,
    frontend_unified_media **out, qa_error *error)
{
    if (!frontend || !recipe || physical_seat >= frontend->options.seats || !out || *out || frontend->resource_inventory || !frontend->order ||
        !qa_executable_recipe_current(recipe, qa_executable_recipe_catalog(recipe)))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified world preparation needs its actual admitted recipe");
    frontend_unified_media *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating unified immutable scene owners");
    owner->frontend = frontend; owner->recipe = recipe; owner->physical_seat = physical_seat;
    const qa_recipe_choices *choices = qa_executable_recipe_choices(recipe);
    const qa_product *product = qa_catalog_product(qa_executable_recipe_catalog(recipe), choices->world.geometry);
    qa_resource *map = qa_executable_recipe_map(recipe); qa_bsp_view bsp;
    bool okay = product && map && bank(owner, product->identity, &owner->world_bank, error) &&
        qa_bsp_open(qa_resource_bytes(map), &bsp, error) && qa_bsp_validate(&bsp, error);
    if (okay) {
        qa_game_family family = bsp.family == QA_BSP_Q1 ? QA_GAME_Q1 : bsp.family == QA_BSP_Q2 ? QA_GAME_Q2 : QA_GAME_Q3;
        qa_scene_world_options options = world_options(family);
        for (size_t i = 0; i < qa_executable_recipe_sidecar_count(recipe); ++i) {
            const qa_recipe_sidecar *sidecar = qa_executable_recipe_sidecar(recipe, i);
            size_t length = sidecar && sidecar->path ? strlen(sidecar->path) : 0;
            if (sidecar && sidecar->product == product->id && length >= 4 &&
                !strcmp(sidecar->path + length - 4, ".lit") && sidecar->resource)
                options.external_lit = qa_resource_bytes(sidecar->resource);
            if (sidecar && sidecar->product == product->id && length >= 4 &&
                !strcmp(sidecar->path + length - 4, ".ent") && sidecar->resource) {
                options.external_entities = qa_resource_bytes(sidecar->resource);
                options.has_external_entities = true;
            }
        }
        okay = (family != QA_GAME_Q3 || frontend_q3_world_policy_initialize(frontend, &options, error)) &&
            qa_scene_world_create(&bsp, owner->world_bank->images, owner->world_bank->materials, &options, &owner->world, error) &&
            qa_scene_world_source_resource_bind(owner->world, map, error) &&
            frontend_world_scratch_create(owner->world,&owner->world_scratch,error);
    }
    if (!okay) {
        if (!frontend_unified_media_destroy(owner, NULL)) *out = owner;
        return false;
    }
    *out = owner; return true;
}

static bool same_bytes(qa_bytes a, qa_bytes b)
{ return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size)); }
static bool same_options(const qa_scene_image_options *a, const qa_scene_image_options *b)
{
    return a->family == b->family && a->usage == b->usage && a->wrap == b->wrap && a->filter == b->filter &&
        a->mipmap == b->mipmap && a->transparent == b->transparent && a->fullbright_only == b->fullbright_only &&
        a->transparent_index == b->transparent_index && same_bytes(a->translation, b->translation) &&
        same_bytes(a->palette_rgb, b->palette_rgb) && !a->source_q3 && !b->source_q3;
}
qa_material_library *frontend_unified_model_materials(const qa_scene_model *model)
{
    const qa_scene_image_options *options = qa_scene_model_image_options(model);
    return options && options->family == QA_GAME_Q3 ? qa_scene_model_material_owner(model) : NULL;
}
bool frontend_unified_media_model(frontend_unified_media *owner, const char *content,
    const char *path, qa_game_family family, const qa_scene_image_options *options,
    frontend_unified_model *out, qa_error *error)
{
    if (!owner || owner->importing || owner->busy || !path || !*path || !options || !out || options->family != family ||
        (options->translation.size && !options->translation.data) ||
        (options->palette_rgb.size && !options->palette_rgb.data) || options->source_q3)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified model needs its genuine content and retained image request");
    if (path[0] == '*') {
        uint32_t index = 0;
        if (!path[1]) return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified inline model has no received index");
        for (const char *p = path + 1; *p; ++p) {
            if (*p < '0' || *p > '9' || index > (UINT32_MAX - (uint32_t)(*p - '0')) / 10)
                return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified inline model exceeds its received model domain");
            index = index * 10 + (uint32_t)(*p - '0');
        }
        const qa_recipe_choices *choices = qa_executable_recipe_choices(owner->recipe);
        const qa_product *world = qa_catalog_product(qa_executable_recipe_catalog(owner->recipe), choices->world.geometry);
        if (!world || strcmp(content, world->identity))
            return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified inline model differs from the offered world content");
        *out = (frontend_unified_model){.brush_world = owner->world, .inline_model = index, .is_inline = true};
        return true;
    }
    unified_media_bank *files;
    if (!bank(owner, content, &files, error)) return false;
    for (unified_media_model *row = owner->models; row; row = row->next)
        if (row->bank == files && row->family == family && !strcmp(row->path, path) && same_options(&row->options, options)) {
            *out = (frontend_unified_model){.resource=row->resource,.opening=&row->opening,
                .model=row->world?NULL:(row->source?row->source:&row->decoded),.scene=row->scene,.brush_world=row->world}; return true;
        }
    if (owner->frontend->resource_inventory)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified model creation overlaps retained resource inventory");
    unified_media_model *row = calloc(1, sizeof(*row));
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining unified model geometry");
    row->path = malloc(strlen(path) + 1);
    if (row->path) strcpy(row->path, path);
    row->bank = files; row->family = family; row->options = *options; owner->busy = true;
    if (options->palette_rgb.size) { row->palette = malloc(options->palette_rgb.size);
        if (row->palette) { memcpy(row->palette, options->palette_rgb.data, options->palette_rgb.size);
            row->options.palette_rgb.data = row->palette; } }
    if (options->translation.size) { row->translation = malloc(options->translation.size);
        if (row->translation) { memcpy(row->translation, options->translation.data, options->translation.size);
            row->options.translation.data = row->translation; } }
    bool okay = row->path && (!options->palette_rgb.size || row->palette) &&
        (!options->translation.size || row->translation) &&
        qa_vfs_acquire_receipt(files->files, path, &row->resource, &row->opening, error);
    size_t length = strlen(path);
    bool brush = length >= 4 && !strcmp(path + length - 4, ".bsp");
    if (okay && brush) {
        qa_bsp_view bsp; qa_scene_world_options world = world_options(family);
        okay = qa_bsp_open(qa_resource_bytes(row->resource), &bsp, error) && qa_bsp_validate(&bsp, error) &&
            (family != QA_GAME_Q3 || frontend_q3_world_policy_initialize(owner->frontend, &world, error)) &&
            qa_scene_world_create(&bsp, files->images, files->materials, &world, &row->world, error) &&
            qa_scene_world_source_resource_bind(row->world, row->resource, error);
    } else if (okay) okay = qa_model_load(qa_resource_bytes(row->resource), &row->decoded, error) &&
        qa_scene_model_create(&row->decoded, files->images, files->materials, &row->options, &row->scene, error) &&
        qa_scene_model_source_resource_bind(row->scene, row->resource, error) &&
        frontend_visual_model_opening_initialize(owner->frontend, family, files->files, row->resource,
            &row->opening, &row->decoded, row->scene, error);
    owner->busy = false;
    if (!okay) {
        qa_scene_world_destroy(row->world); qa_scene_model_destroy(row->scene); qa_model_free(&row->decoded);
        qa_resource_release(row->resource); qa_vfs_acquisition_dispose(&row->opening);
        free(row->palette); free(row->translation); free(row->path); free(row); return false;
    }
    row->next = owner->models; owner->models = row;
    *out = (frontend_unified_model){.resource=row->resource,.opening=&row->opening,
        .model=row->world?NULL:&row->decoded,.scene=row->scene,.brush_world=row->world}; return true;
}
size_t frontend_unified_media_bank_count(const frontend_unified_media *owner)
{
    size_t count=0; if (owner) for (const unified_media_bank *row=owner->banks;row;row=row->next) ++count;
    return count;
}
bool frontend_unified_media_bank_read(const frontend_unified_media *owner,size_t index,frontend_unified_bank_view *out)
{
    if (!owner || !out) return false;
    const unified_media_bank *row=owner->banks;
    while (row && index--) row=row->next;
    if (!row) return false;
    *out=(frontend_unified_bank_view){row->content,row->files,row->product,row->images,row->materials,row->fonts,row->sounds,row->q3_assets,row->media,
        owner->physical_seat,row->cinematic_audio_owner};
    return true;
}
size_t frontend_unified_media_model_count(const frontend_unified_media *owner)
{
    size_t count=0; if (owner) for (const unified_media_model *row=owner->models;row;row=row->next) ++count;
    return count;
}
bool frontend_unified_media_model_read(const frontend_unified_media *owner,size_t index,frontend_unified_model_view *out)
{
    if (!owner || !out) return false;
    const unified_media_model *row=owner->models;
    while (row && index--) row=row->next;
    if (!row) return false;
    size_t ordinal=0;
    for (const unified_media_bank *bank_row=owner->banks;bank_row;bank_row=bank_row->next,++ordinal)
        if (bank_row==row->bank) {
            *out=(frontend_unified_model_view){ordinal,row->path,row->family,&row->options,row->resource,&row->opening,
                row->world||row->saved_world?NULL:(row->source?row->source:row->scene?&row->decoded:NULL),row->scene,row->world};
            return true;
        }
    return false;
}
qa_executable_recipe *frontend_unified_media_recipe(const frontend_unified_media *owner)
{ return owner?owner->recipe:NULL; }
bool frontend_unified_media_importing(const frontend_unified_media *owner)
{ return owner && owner->importing; }
qa_scene_world *frontend_unified_media_world(const frontend_unified_media *owner)
{ return owner ? owner->world : NULL; }
void frontend_unified_media_world_scratch(const frontend_unified_media *owner, qa_scene_world_input *input)
{
    input->scratch=owner->world_scratch.view;
    input->child_scratch=owner->world_scratch.child;
}
bool frontend_unified_media_q3_row(size_t bank, size_t model, uint64_t *out)
{
    if (!out || bank >= UINT32_MAX || model >= UINT32_MAX) return false;
    *out = ((uint64_t)(bank + 1) << 32) | (uint64_t)(model + 1); return true;
}
bool frontend_unified_media_current(const frontend_unified_media *owner)
{ return owner && qa_executable_recipe_current(owner->recipe, qa_executable_recipe_catalog(owner->recipe)); }
bool frontend_unified_media_ready(const frontend_unified_media *owner)
{
    if (!frontend_unified_media_current(owner) || owner->importing || !owner->world || !owner->world_bank)
        return false;
    bool world = false;
    for (const unified_media_bank *row=owner->banks;row;row=row->next) {
        if (row==owner->world_bank) world=true;
        if (row->constructing || row->construction_failed || !row->content || !row->files || !row->product ||
            !row->images || !row->materials || !row->fonts || !row->sounds || !row->media || !row->shader_movies)
            return false;
    }
    return world;
}
static bool world_returned(const qa_scene_world *world,const frontend_capture *capture)
{
    return qa_scene_world_idle(world) ||
        (frontend_capture_holds(capture,world) && qa_scene_world_observation_ready(world));
}
static bool model_returned(const qa_scene_model *model,const frontend_capture *capture)
{
    return qa_scene_model_idle(model) ||
        (frontend_capture_holds(capture,model) && qa_scene_model_observation_ready(model));
}
static bool media_returned(const frontend_unified_media *owner,const frontend_capture *capture)
{
    if (!owner) return true;
    if (owner->busy || !frontend_unified_material_movies_idle(owner) ||
        (owner->world && !world_returned(owner->world,capture))) return false;
    for (const unified_media_model *row=owner->models;row;row=row->next)
        if ((row->world && !world_returned(row->world,capture)) ||
            (row->scene && !model_returned(row->scene,capture))) return false;
    for (const unified_media_bank *row=owner->banks;row;row=row->next)
        if (row->constructing || (row->q3_assets && !qa_q3_assets_idle(row->q3_assets) &&
                !frontend_capture_holds(capture,row->q3_assets)) ||
            (row->images && !qa_scene_resources_idle(row->images) && !frontend_capture_holds(capture,row->images)) ||
            (row->materials && !qa_material_library_idle(row->materials) && !frontend_capture_holds(capture,row->materials)) ||
            (row->fonts && !qa_font_library_idle(row->fonts) && !frontend_capture_holds(capture,row->fonts))) return false;
    return true;
}
bool frontend_unified_media_idle(const frontend_unified_media *owner)
{ return media_returned(owner,NULL); }
bool frontend_unified_media_checkpoint_ready(const frontend_unified_media *owner)
{ return media_returned(owner,owner?owner->frontend->capture:NULL); }
bool frontend_unified_media_visit(const frontend_unified_media *owner,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!owner || owner->busy || !visitor ||
        !qa_executable_recipe_content_visit(owner->recipe,visitor,error)) return false;
    for (const unified_media_bank *row=owner->banks;row;row=row->next) {
        if (!row->media) continue;
        qa_resource_pool *pool=qa_vfs_resources(row->files);
        if (!pool || qa_media_library_resource_owner(row->media)!=row->images ||
            !visitor->pool(visitor->context,pool,error)) return false;
        for (size_t i=0;i<qa_media_library_record_count(row->media);++i) {
            const qa_cinematic_asset *asset=qa_media_library_record_at(row->media,i);
            const qa_resource *resource=asset?qa_cinematic_asset_resource(asset):NULL;
            if (!resource || qa_resource_pool_find(pool,qa_resource_id(resource))!=resource)
                return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified movie cache left its retained content pool");
        }
    }
    return true;
}
bool frontend_unified_media_destroy(frontend_unified_media *owner, qa_error *error)
{
    if (!owner) return true;
    if ((owner->frontend->resource_inventory && !owner->frontend->source_restoring) ||
        !frontend_unified_media_idle(owner))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media cleanup requires all captured children to return");
    for (unified_media_bank *row = owner->banks; row; row = row->next)
        if (!qa_q3_assets_services_retire(row->q3_assets,error)) return false;
    size_t ordinal = 0;
    for (unified_media_bank *row = owner->banks; row; row = row->next, ++ordinal)
        if (!frontend_unified_material_movies_clear(owner, ordinal, error)) return false;
    /* Registry retirement observes its borrowed map; retire it while the real
     * map and its bank are still alive. Family users retire before this owner. */
    for (unified_media_bank *row = owner->banks; row; row = row->next) {
        qa_q3_presentation_assets_destroy(row->q3_assets); row->q3_assets = NULL;
    }
    frontend_world_scratch_destroy(&owner->world_scratch);
    qa_scene_world_destroy(owner->world);
    while (owner->models) {
        unified_media_model *row = owner->models; owner->models = row->next;
        qa_scene_world_destroy(row->world); qa_scene_model_destroy(row->scene); qa_model_free(&row->decoded);
        frontend_model_release(row->source_lease);
        qa_resource_release(row->resource); qa_vfs_acquisition_dispose(&row->opening);
        free(row->palette); free(row->translation); free(row->path); free(row);
    }
    while (owner->banks) {
        unified_media_bank *row = owner->banks; owner->banks = row->next;
        qa_buffer_free(&row->saved_assets);
        qa_audio_bank_destroy(row->sounds); qa_font_library_destroy(row->fonts);
        qa_material_library_destroy(row->materials); qa_scene_resources_destroy(row->images); free(row->content); free(row);
    }
    free(owner); return true;
}

#include "remote_unified_private.h"
#include "remote_unified_media.h"
#include "shared_resource_policy.h"
#include "q3_render_policy.h"
#include "visual_access.h"

#include <stdlib.h>
#include <string.h>

typedef struct unified_media_bank {
    struct unified_media_bank *next;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
} unified_media_bank;
typedef struct unified_media_model {
    struct unified_media_model *next;
    unified_media_bank *bank;
    char *path;
    qa_scene_family family;
    qa_scene_image_options options;
    uint8_t *palette, *translation;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    qa_model decoded;
    qa_scene_model *scene;
    qa_scene_world *world;
} unified_media_model;
struct frontend_unified_media {
    qa_frontend *frontend;
    qa_executable_recipe *recipe;
    unified_media_bank *banks;
    unified_media_model *models;
    qa_scene_world *world;
    unified_media_bank *world_bank;
    bool busy;
};

static qa_scene_image_options image_options(qa_scene_family family, qa_scene_image_usage usage)
{
    return (qa_scene_image_options){.family = family, .usage = usage,
        .wrap = QA_SCENE_REPEAT, .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR,
        .mipmap = true, .transparent_index = 255};
}
static qa_scene_world_options world_options(qa_scene_family family)
{
    /* These are the actual generic renderer's authored construction defaults,
     * shared with frontend_scene_sync. Q3's reached ENGINE policy replaces its
     * subdivision and lightmap shift before loading the immutable BSP. */
    return (qa_scene_world_options){.images = image_options(family, QA_IMAGE_USAGE_WALL),
        .subdivisions = 64, .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 1};
}
static bool bank(frontend_unified_media *owner, const char *content, unified_media_bank **out, qa_error *error)
{
    if (!owner || !content || !out || !frontend_unified_media_current(owner))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media lost its retained recipe content");
    for (unified_media_bank *row = owner->banks; row; row = row->next)
        if (!strcmp(row->content, content)) { *out = row; return true; }
    unified_media_bank *row = calloc(1, sizeof(*row));
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining unified content media bank");
    row->content = malloc(strlen(content) + 1);
    if (row->content) strcpy(row->content, content);
    bool okay = row->content && qa_executable_recipe_content(owner->recipe, content, &row->files, &row->product, error);
    if (okay) row->images = qa_scene_resources_create(row->files, error);
    if (okay) okay = row->images && frontend_image_policy_initialize(owner->frontend, row->images, error);
    if (okay) {
        row->materials = qa_material_library_create(row->images, owner->frontend->order, error);
        row->fonts = qa_font_library_create(row->files, row->images, error);
        okay = row->materials && row->fonts && qa_audio_bank_create(row->files, &row->sounds, error);
    }
    if (okay) {
        qa_scene_family family = row->product->family == QA_GAME_Q1 ? QA_SCENE_Q1 :
            row->product->family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3;
        qa_scene_image_options images = image_options(family, QA_IMAGE_USAGE_WALL);
        okay = (family != QA_SCENE_Q3 || frontend_q3_material_profile_initialize(owner->frontend, row->materials, error)) &&
            qa_material_library_load_scripts(row->materials, row->files, &images, error);
    }
    if (!okay) {
        qa_audio_bank_destroy(row->sounds); qa_font_library_destroy(row->fonts);
        qa_material_library_destroy(row->materials); qa_scene_resources_destroy(row->images);
        free(row->content); free(row); return false;
    }
    row->next = owner->banks; owner->banks = row; *out = row; return true;
}
bool frontend_unified_media_bank(frontend_unified_media *owner, const char *content,
    qa_scene_resources **images, qa_material_library **materials, qa_font_library **fonts,
    qa_audio_bank **sounds, qa_error *error)
{
    unified_media_bank *row;
    if (!images || !materials || !fonts || !sounds || !bank(owner, content, &row, error)) return false;
    *images = row->images; *materials = row->materials; *fonts = row->fonts; *sounds = row->sounds; return true;
}
bool frontend_unified_media_create(qa_frontend *frontend, qa_executable_recipe *recipe,
    frontend_unified_media **out, qa_error *error)
{
    if (!frontend || !recipe || !out || *out || frontend->resource_inventory || !frontend->order ||
        !qa_executable_recipe_current(recipe, qa_executable_recipe_catalog(recipe)))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified world preparation needs its actual admitted recipe");
    frontend_unified_media *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating unified immutable scene owners");
    owner->frontend = frontend; owner->recipe = recipe;
    const qa_recipe_choices *choices = qa_executable_recipe_choices(recipe);
    const qa_product *product = qa_catalog_product(qa_executable_recipe_catalog(recipe), choices->world.geometry);
    qa_resource *map = qa_executable_recipe_map(recipe); qa_bsp_view bsp;
    bool okay = product && map && bank(owner, product->identity, &owner->world_bank, error) &&
        qa_bsp_open(qa_resource_bytes(map), &bsp, error) && qa_bsp_validate(&bsp, error);
    if (okay) {
        qa_scene_family family = bsp.family == QA_BSP_Q1 ? QA_SCENE_Q1 : bsp.family == QA_BSP_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3;
        qa_scene_world_options options = world_options(family);
        for (size_t i = 0; i < qa_executable_recipe_sidecar_count(recipe); ++i) {
            const qa_recipe_sidecar *sidecar = qa_executable_recipe_sidecar(recipe, i);
            size_t length = sidecar && sidecar->path ? strlen(sidecar->path) : 0;
            if (sidecar && sidecar->product == product->id && length >= 4 &&
                !strcmp(sidecar->path + length - 4, ".lit") && sidecar->resource)
                options.external_lit = qa_resource_bytes(sidecar->resource);
        }
        okay = (family != QA_SCENE_Q3 || frontend_q3_world_policy_initialize(frontend, &options, error)) &&
            qa_scene_world_create(&bsp, owner->world_bank->images, owner->world_bank->materials, &options, &owner->world, error);
    }
    if (!okay) { (void)frontend_unified_media_destroy(owner, NULL); return false; }
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
bool frontend_unified_media_model(frontend_unified_media *owner, const char *content,
    const char *path, qa_scene_family family, const qa_scene_image_options *options,
    frontend_unified_model *out, qa_error *error)
{
    if (!owner || owner->busy || !path || !*path || !options || !out || options->family != family ||
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
            *out = (frontend_unified_model){row->resource, &row->opening, row->world ? NULL : &row->decoded, row->scene, row->world}; return true;
        }
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
            (family != QA_SCENE_Q3 || frontend_q3_world_policy_initialize(owner->frontend, &world, error)) &&
            qa_scene_world_create(&bsp, files->images, files->materials, &world, &row->world, error);
    } else if (okay) okay = qa_model_load(qa_resource_bytes(row->resource), &row->decoded, error) &&
        qa_scene_model_create(&row->decoded, files->images, files->materials, &row->options, &row->scene, error) &&
        frontend_visual_model_opening_initialize(owner->frontend, family, files->files, row->resource,
            &row->opening, &row->decoded, row->scene, error);
    owner->busy = false;
    if (!okay) {
        qa_scene_world_destroy(row->world); qa_scene_model_destroy(row->scene); qa_model_free(&row->decoded);
        qa_resource_release(row->resource); qa_vfs_acquisition_dispose(&row->opening);
        free(row->palette); free(row->translation); free(row->path); free(row); return false;
    }
    row->next = owner->models; owner->models = row;
    *out = (frontend_unified_model){row->resource, &row->opening, row->world ? NULL : &row->decoded, row->scene, row->world}; return true;
}
qa_scene_world *frontend_unified_media_world(const frontend_unified_media *owner)
{ return owner ? owner->world : NULL; }
bool frontend_unified_media_current(const frontend_unified_media *owner)
{ return owner && qa_executable_recipe_current(owner->recipe, qa_executable_recipe_catalog(owner->recipe)); }
bool frontend_unified_media_visit(const frontend_unified_media *owner,
    const qa_application_content_visitor *visitor, qa_error *error)
{ return owner && !owner->busy && qa_executable_recipe_content_visit(owner->recipe, visitor, error); }
bool frontend_unified_media_destroy(frontend_unified_media *owner, qa_error *error)
{
    if (!owner) return true;
    if (owner->busy) return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media cleanup overlaps an actual registration");
    qa_scene_world_destroy(owner->world);
    while (owner->models) {
        unified_media_model *row = owner->models; owner->models = row->next;
        qa_scene_world_destroy(row->world); qa_scene_model_destroy(row->scene); qa_model_free(&row->decoded);
        qa_resource_release(row->resource); qa_vfs_acquisition_dispose(&row->opening);
        free(row->palette); free(row->translation); free(row->path); free(row);
    }
    while (owner->banks) {
        unified_media_bank *row = owner->banks; owner->banks = row->next;
        qa_audio_bank_destroy(row->sounds); qa_font_library_destroy(row->fonts);
        qa_material_library_destroy(row->materials); qa_scene_resources_destroy(row->images); free(row->content); free(row);
    }
    free(owner); return true;
}

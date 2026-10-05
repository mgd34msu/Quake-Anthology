#include "remote_q1_private.h"
#include "internal.h"
#include "shared_resource_policy.h"
#include "visual_access.h"
#include "equipment_media.h"
#include "qa/material.h"
#include "qa/scene_world_save.h"
#include "qa/scene_model_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qa_scene_image_options image_options(qa_scene_image_usage usage)
{ return (qa_scene_image_options){.family = QA_SCENE_Q1, .usage = usage, .wrap = QA_SCENE_REPEAT,
    .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = 255}; }
void remote_q1_media_clear(frontend_remote_q1 *row)
{
    qa_scene_world_destroy(row->world); row->world = NULL;
    while (row->model_cache) {
        remote_q1_model *model = row->model_cache; row->model_cache = model->next;
        qa_scene_world_destroy(model->world); qa_scene_model_destroy(model->scene);
        qa_model_free(&model->decoded); frontend_model_release(model->source_lease);
        qa_resource_release(model->resource); qa_vfs_acquisition_dispose(&model->opening); free(model->path); free(model);
    }
    for (unsigned i = 0; i < 6; ++i) { qa_scene_image_release(row->sky_images[i]); row->sky_images[i] = NULL; }
    row->sky_found = 0;
    qa_audio_bank_destroy(row->sound_bank); row->sound_bank = NULL;
    qa_material_library_destroy(row->materials); row->materials = NULL;
    qa_scene_resources_destroy(row->images); row->images = NULL;
}
bool remote_q1_media_prepare(frontend_remote_q1 *row, qa_error *error)
{
    qa_bsp_view bsp;
    const qa_product *product=qa_catalog_product(row->content.catalog,row->content.product);
    if (!row->map || !qa_bsp_open(qa_resource_bytes(row->map), &bsp, error) || bsp.family != QA_BSP_Q1 || !qa_bsp_validate(&bsp, error))
        return remote_q1_fail(error, QA_ERROR_FORMAT, "Remote Q1 requires its actually received Quake BSP world");
    row->images = qa_scene_resources_create(row->content.mounts, error);
    if (!row->images || !frontend_image_policy_initialize(row->frontend, row->images, error)) return false;
    row->materials = qa_material_library_create(row->images, row->frontend->order, error);
    qa_scene_world_options options = {.images = image_options(QA_IMAGE_USAGE_WALL), .subdivisions = 64,
        .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 1};
    if (!row->materials || !qa_material_library_load_scripts(row->materials, row->content.mounts, &options.images, error) ||
        !qa_scene_world_create(&bsp, row->images, row->materials, &options, &row->world, error) ||
        !qa_scene_world_source_resource_bind(row->world, row->map, error) ||
        !qa_audio_bank_create(row->content.mounts, &row->sound_bank, error) ||
        !frontend_q1_faces_prepare(row->content.mounts,row->images,row->materials,
            product && !strcmp(product->campaign,"rogue"),error)) return false;
    row->sound_available = row->sound_count ? calloc(row->sound_count, sizeof(*row->sound_available)) : NULL;
    if (row->sound_count && !row->sound_available) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining received sound availability");
    for (size_t i = 0; i < row->sound_count; ++i) {
        qa_audio_asset *asset = NULL;
        if (!qa_audio_bank_register(row->sound_bank, row->sounds[i], QA_AUDIO_Q1, &asset, error)) return false;
        row->sound_available[i] = asset != NULL;
        qa_audio_asset_release(asset);
    }
    return true;
}
bool remote_q1_sky_load(frontend_remote_q1 *row, qa_error *error)
{
    static const char *const suffix[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
    for (unsigned i = 0; i < 6; ++i) { qa_scene_image_release(row->sky_images[i]); row->sky_images[i] = NULL; }
    row->sky_found = 0;
    if (!row->skybox || !*row->skybox) return true;
    if (!row->images) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Received skybox has no actual CLIENT content bank");
    size_t size = strlen(row->skybox);
    if (size > SIZE_MAX - 16) return false;
    char *path = malloc(size + 16);
    if (!path) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining remote Q1 sky request");
    qa_scene_image_options options = {.family = QA_SCENE_Q1, .usage = QA_IMAGE_USAGE_SKY,
        .wrap = QA_SCENE_CLAMP, .filter = QA_SCENE_LINEAR, .transparent_index = -1};
    bool ok = true;
    for (unsigned i = 0; ok && i < 6; ++i) {
        qa_scene_image *image = NULL; qa_error issue = {0};
        snprintf(path, size + 16, "gfx/env/%s%s.tga", row->skybox, suffix[i]);
        bool found = qa_scene_image_load_exact(row->images, path, &options, &image, &issue);
        if (!found && (issue.code == QA_ERROR_NOT_FOUND || issue.code == QA_ERROR_FORMAT)) {
            issue = (qa_error){0}; snprintf(path, size + 16, "gfx/env/%s%s.png", row->skybox, suffix[i]);
            found = qa_scene_image_load_exact(row->images, path, &options, &image, &issue);
        }
        if (found) row->sky_images[i] = image, row->sky_found |= (uint8_t)(1u << i);
        else if (issue.code == QA_ERROR_NOT_FOUND || issue.code == QA_ERROR_FORMAT) {
            row->sky_images[i] = qa_scene_missing(row->images); qa_scene_image_retain(row->sky_images[i]);
        } else { if (error) *error = issue; ok = false; }
    }
    free(path); return ok;
}
bool remote_q1_model_read(frontend_remote_q1 *row, const frontend_remote_q1_entity_view *view, remote_q1_model **out, qa_error *error)
{
    if (!remote_q1_mutable(row) || !view || !view->model || !out)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 model admission overlaps its actual parent lease");
    *out = NULL;
    for (remote_q1_model *m = row->model_cache; m; m = m->next)
        if (!strcmp(m->path, view->model) && m->colors == view->has_colors &&
            (!m->colors || (m->top == view->top_color && m->bottom == view->bottom_color))) { *out = m; return true; }
    remote_q1_model *m = calloc(1, sizeof(*m));
    if (!m) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining remote Q1 model");
    m->colors = view->has_colors; m->top = view->top_color; m->bottom = view->bottom_color;
    bool ok = remote_q1_string(&m->path, view->model, error) &&
        qa_vfs_acquire_receipt(row->content.mounts, m->path, &m->resource, &m->opening, error);
    size_t length = strlen(view->model);
    bool brush = length >= 4 && view->model[length - 4] == '.' &&
        (view->model[length - 3] == 'b' || view->model[length - 3] == 'B') &&
        (view->model[length - 2] == 's' || view->model[length - 2] == 'S') &&
        (view->model[length - 1] == 'p' || view->model[length - 1] == 'P');
    qa_scene_image_options options = image_options(QA_IMAGE_USAGE_SKIN); uint8_t translation[256];
    if (m->colors) {
        for (unsigned i = 0; i < 256; ++i) translation[i] = (uint8_t)i;
        unsigned top = m->top * 16u, bottom = m->bottom * 16u;
        for (unsigned i = 0; i < 16; ++i) {
            translation[16 + i] = (uint8_t)(top < 128 ? top + i : top + 15 - i);
            translation[96 + i] = (uint8_t)(bottom < 128 ? bottom + i : bottom + 15 - i);
        }
        options.translation = (qa_bytes){translation, sizeof(translation)};
    }
    if (ok && brush) {
        qa_bsp_view bsp;
        qa_scene_world_options world_options = {.images = image_options(QA_IMAGE_USAGE_WALL), .subdivisions = 64,
            .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 1};
        ok = qa_bsp_open(qa_resource_bytes(m->resource), &bsp, error) && qa_bsp_validate(&bsp, error) &&
            qa_scene_world_create(&bsp, row->images, row->materials, &world_options, &m->world, error) &&
            qa_scene_world_source_resource_bind(m->world, m->resource, error);
    } else if (ok) ok = qa_model_load(qa_resource_bytes(m->resource), &m->decoded, error) &&
        qa_scene_model_create(&m->decoded, row->images, row->materials, &options, &m->scene, error) &&
        qa_scene_model_source_resource_bind(m->scene, m->resource, error) &&
        frontend_visual_model_opening_initialize(row->frontend, QA_SCENE_Q1, row->content.mounts,
            m->resource, &m->opening, &m->decoded, m->scene, error);
    if (!ok) {
        qa_scene_world_destroy(m->world); qa_scene_model_destroy(m->scene); qa_model_free(&m->decoded); qa_resource_release(m->resource);
        qa_vfs_acquisition_dispose(&m->opening); free(m->path); free(m); return false;
    }
    m->source = brush ? NULL : &m->decoded; m->next = row->model_cache; row->model_cache = m; *out = m; return true;
}
bool frontend_remote_q1_media_read(const frontend_remote_q1 *row, frontend_remote_q1_media *out, qa_error *error)
{
    if (!row || !out || row->busy) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 media observation overlaps its producer");
    *out = (frontend_remote_q1_media){row->content.mounts, row->map, &row->map_opening,
        row->images, row->materials, row->sound_bank, row->world}; return true;
}
size_t frontend_remote_q1_model_count(const frontend_remote_q1 *row)
{ size_t count = 0; if (row) for (remote_q1_model *m = row->model_cache; m; m = m->next) ++count; return count; }
bool frontend_remote_q1_model_at(const frontend_remote_q1 *row, size_t index,
    frontend_remote_q1_model_view *out, qa_error *error)
{
    if (!row || !out || row->busy) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 model observation overlaps its producer");
    remote_q1_model *m = row->model_cache; while (m && index) { m = m->next; --index; }
    if (!m) return remote_q1_fail(error, QA_ERROR_NOT_FOUND, "Q1 retained model ordinal is absent");
    *out = (frontend_remote_q1_model_view){m->path, m->resource, &m->opening, m->source,
        m->scene, m->world, m->colors, m->top, m->bottom}; return true;
}

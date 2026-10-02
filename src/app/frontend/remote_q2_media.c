#include "remote_q2_private.h"
#include "shared_resource_policy.h"
#include "visual_access.h"
#include "remote_q2_effects_bridge.h"
#include "remote_q2_footsteps.h"
#include "remote_q2_clientinfo.h"
#include "qa/material.h"
#include "qa/hash.h"
#include "qa/scene_world_save.h"
#include "qa/network_q2_materials.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static qa_scene_image_options image_options(qa_scene_image_usage usage)
{
    return (qa_scene_image_options){.family = QA_SCENE_Q2, .usage = usage,
        .wrap = usage == QA_IMAGE_USAGE_PICTURE ? QA_SCENE_CLAMP : QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = usage != QA_IMAGE_USAGE_PICTURE,
        .transparent = true, .transparent_index = 255};
}
static bool model_materials(frontend_remote_q2 *row, const qa_model *model, qa_error *error)
{
    if (model->format != QA_MODEL_MD3) return true;
    for (size_t i = 0; i < model->mesh_count; ++i)
        for (size_t j = 0; j < model->meshes[i].shader_count; ++j) {
            const char *name = model->meshes[i].shaders[j].name;
            size_t length = strlen(name);
            if (length < 7 || strcmp(name + length - 7, ".shader")) continue;
            if (!row->options.material_scripts)
                return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 model material was not explicitly negotiated");
            qa_resource *script = NULL; qa_vfs_acquisition opening = {0};
            if (!qa_vfs_acquire_receipt(row->content.mounts, name, &script, &opening, error)) return false;
            qa_scene_image_options options = image_options(QA_IMAGE_USAGE_SKIN);
            bool ok = qa_vfs_acquisition_retained(row->content.mounts, &opening, error) &&
                qa_q2_material_script_import(row->materials, name, qa_resource_bytes(script), &options, error);
            qa_vfs_acquisition_dispose(&opening); qa_resource_release(script);
            if (!ok) return false;
        }
    return true;
}
bool remote_q2_model_read(frontend_remote_q2 *row, const char *path, remote_q2_model **out, qa_error *error)
{
    if (!row || row->image_policy || row->frontend->resource_inventory || row->frontend->capture || row->frontend->source_restoring)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 model registration requires its live source resource owner");
    *out = NULL;
    for (remote_q2_model *m = row->models; m; m = m->next)
        if (!strcmp(m->path, path)) { *out = m; return true; }
    for (remote_q2_missing_model *m = row->missing_models; m; m = m->next)
        if (!strcmp(m->path, path)) return remote_q2_fail(error, QA_ERROR_NOT_FOUND, "Q2 model has a retained missing-resource admission");
    remote_q2_model *m = calloc(1, sizeof(*m));
    if (!m) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining remote Q2 model");
    m->path = malloc(strlen(path) + 1);
    if (m->path) strcpy(m->path, path);
    qa_scene_image_options options = image_options(QA_IMAGE_USAGE_SKIN);
    bool ok = m->path && qa_vfs_acquire_receipt(row->content.mounts, path, &m->resource, &m->opening, error) &&
        qa_model_load(qa_resource_bytes(m->resource), &m->decoded, error) && model_materials(row, &m->decoded, error);
    if (ok && m->decoded.format == QA_MODEL_MD3) options.family = QA_SCENE_Q3;
    if (ok) ok = qa_scene_model_create(&m->decoded, row->images, row->materials, &options, &m->scene, error) &&
        frontend_visual_model_opening_initialize(row->frontend, options.family, row->content.mounts,
            m->resource, &m->opening, &m->decoded, m->scene, error);
    if (!ok) {
        if (!m->resource && m->path && error && error->code == QA_ERROR_NOT_FOUND) {
            remote_q2_missing_model *missing = calloc(1, sizeof(*missing));
            if (missing) {
                missing->path = m->path; m->path = NULL;
                missing->next = row->missing_models; row->missing_models = missing;
            } else remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining absent Q2 model admission");
        }
        qa_scene_model_destroy(m->scene); qa_model_free(&m->decoded); frontend_model_release(m->source_lease); qa_resource_release(m->resource);
        qa_vfs_acquisition_dispose(&m->opening); free(m->path); free(m); return false;
    }
    m->source = &m->decoded; m->next = row->models; row->models = m; *out = m; return true;
}
const qa_scene_image *remote_q2_picture_read(void *context, const char *name, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || row->image_policy || row->frontend->resource_inventory || row->frontend->capture || row->frontend->source_restoring) {
        remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 picture registration requires its live resource owner"); return NULL;
    }
    for (remote_q2_picture *p = row->pictures; p; p = p->next) if (!strcmp(p->name, name)) return p->image;
    size_t length = strlen(name);
    if (length > SIZE_MAX - 11) return NULL;
    char *path = malloc(length + 11);
    remote_q2_picture *p = calloc(1, sizeof(*p));
    if (!path || !p) { free(path); free(p); remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 picture"); return NULL; }
    if (name[0] == '/') strcpy(path, name + 1); else snprintf(path, length + 11, "pics/%s.pcx", name);
    qa_scene_image *image = NULL; qa_scene_image_options options = image_options(QA_IMAGE_USAGE_PICTURE);
    bool ok = qa_scene_image_load(row->images, path, &options, &image, error); free(path);
    if (!ok) { free(p); return NULL; }
    p->name = malloc(length + 1);
    if (!p->name) { qa_scene_image_release(image); free(p); remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 picture name"); return NULL; }
    strcpy(p->name, name); p->image = image; p->next = row->pictures; row->pictures = p; return image;
}
bool remote_q2_media_clear(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || row->image_policy)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 media remains held by its image policy transaction");
    if (!frontend_remote_q2_effects_destroy(&row->effects, error)) return false;
    qa_buffer_free(&row->saved_effects);
    row->effects_imported = false;
    if (row->frontend->audio && row->identity && !qa_audio_engine_stop_owner(row->frontend->audio,
        row->identity, row->options.domain.physical_seat, error)) return false;
    remote_q2_footsteps_clear(row);
    qa_scene_world_destroy(row->world); row->world = NULL;
    qa_collision_destroy(row->geometry); row->geometry = NULL;
    while (row->models) {
        remote_q2_model *m = row->models; row->models = m->next;
        qa_scene_model_destroy(m->scene); qa_model_free(&m->decoded); frontend_model_release(m->source_lease); qa_resource_release(m->resource);
        qa_vfs_acquisition_dispose(&m->opening); free(m->path); free(m);
    }
    while (row->missing_models) {
        remote_q2_missing_model *m = row->missing_models; row->missing_models = m->next;
        free(m->path); free(m);
    }
    while (row->pictures) {
        remote_q2_picture *p = row->pictures; row->pictures = p->next;
        qa_scene_image_release(p->image); free(p->name); free(p);
    }
    qa_font_library_destroy(row->fonts); row->fonts = NULL; row->classic = NULL;
    qa_scene_image_release(row->white); row->white = NULL;
    qa_audio_bank_destroy(row->sounds); row->sounds = NULL;
    qa_material_library_destroy(row->materials); row->materials = NULL;
    qa_scene_resources_destroy(row->images); row->images = NULL;
    qa_resource_release(row->map); row->map = NULL;
    qa_vfs_acquisition_dispose(&row->map_opening); row->media_ready = false;
    row->hit_marker_count = 0; row->hit_marker_frame = 0; row->hit_marker_ns = 0; row->hit_marker_set = false;
    return true;
}
bool remote_q2_map_validate(const frontend_remote_q2 *row, const qa_resource *resource, qa_bsp_view *bsp, qa_error *error)
{
    const char *checksum = frontend_remote_q2_config(row, row->layout.checksum);
    char *end = NULL; double value = strtod(checksum, &end);
    while (end && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) ++end;
    if (!*checksum || !end || *end || !isfinite(value))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Remote Q2 map checksum is not a complete number");
    double reduced = fmod(trunc(value), 4294967296.0); if (reduced < 0) reduced += 4294967296.0;
    if ((uint32_t)reduced != qa_block_checksum(qa_resource_bytes(resource)))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Remote Q2 map checksum differs from retained mounted content");
    if (!qa_bsp_open(qa_resource_bytes(resource), bsp, error) || bsp->family != QA_BSP_Q2 || !qa_bsp_validate(bsp, error))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Remote Q2 requires its actual Q2 BSP map");
    return true;
}
bool remote_q2_media_prepare(frontend_remote_q2 *row, qa_error *error)
{
    if (!remote_q2_media_clear(row, error)) return false;
    const char *map = frontend_remote_q2_config(row, row->layout.models + 1);
    if (!*map) return remote_q2_fail(error, QA_ERROR_FORMAT, "Remote Q2 server supplied no world model");
    if (!qa_vfs_acquire_receipt(row->content.mounts, map, &row->map, &row->map_opening, error)) return false;
    qa_bsp_view bsp;
    if (!remote_q2_map_validate(row, row->map, &bsp, error)) return false;
    if (!qa_collision_create(&bsp, &row->geometry, error) || !qa_collision_bind_resource(row->geometry, row->map, error)) return false;
    row->images = qa_scene_resources_create(row->content.mounts, error);
    if (!row->images || !frontend_image_policy_initialize(row->frontend, row->images, error)) return false;
    row->materials = qa_material_library_create(row->images, row->frontend->order, error);
    row->fonts = qa_font_library_create(row->content.mounts, row->images, error);
    if (!row->materials || !row->fonts || !qa_audio_bank_create(row->content.mounts, &row->sounds, error)) return false;
    const uint8_t pixel[4] = {255, 255, 255, 255};
    qa_scene_image_level level = {1, 1, pixel, sizeof(pixel)};
    qa_scene_image *white = NULL;
    if (!qa_scene_image_create(row->images, "remote-q2:white", QA_SCENE_RGBA8, &level, 1,
        QA_SCENE_CLAMP, QA_SCENE_NEAREST, (qa_scene_vec4){1, 1, 1, 1}, &white, error)) return false;
    row->white = white;
    qa_scene_world_options options = {.images = image_options(QA_IMAGE_USAGE_WALL),
        .subdivisions = 64, .q1_water_alpha = 1, .q2_light_modulate = 1,
        .q2_sky = frontend_remote_q2_config(row, 2)};
    if (!qa_scene_world_create(&bsp, row->images, row->materials, &options, &row->world, error)) return false;
    if (!qa_scene_world_source_resource_bind(row->world, row->map, error)) return false;
    if (!remote_q2_footsteps_prepare(row, error)) return false;
    for (size_t i = 1; i < row->layout.max_models; ++i) {
        const char *path = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + i));
        if (!*path || path[0] == '*' || path[0] == '#' || !strcmp(path, map)) continue;
        remote_q2_model *model = NULL; qa_error issue = {0};
        if (!remote_q2_model_read(row, path, &model, &issue) && issue.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = issue;
            return false;
        }
    }
    for (size_t i = 1; i < row->layout.max_images; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.images + i));
        if (!*name) continue;
        qa_error issue = {0};
        if (!remote_q2_picture_read(row, name, &issue) && issue.code != QA_OK && issue.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = issue;
            return false;
        }
    }
    for (size_t i = 1; i < row->layout.max_sounds; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.sounds + i));
        if (!*name || name[0] == '*') continue;
        qa_audio_asset *asset = NULL;
        if (!qa_audio_bank_register(row->sounds, name, QA_AUDIO_Q2, &asset, error)) return false;
        qa_audio_asset_release(asset);
    }
    qa_error issue = {0};
    if (!remote_q2_clientinfo_prepare(row, error)) return false;
    qa_error marker_issue = {0};
    if (!remote_q2_picture_read(row, "marker", &marker_issue) && marker_issue.code != QA_OK && marker_issue.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = marker_issue;
        return false;
    }
    const qa_scene_image *conchars = remote_q2_picture_read(row, "conchars", &issue);
    if (conchars && !qa_font_classic_create(row->fonts, "remote-q2:conchars", conchars,
        QA_FONT_BAKED_COLOR, &row->classic, error)) return false;
    if (!remote_q2_effects_create(row, error) || !remote_q2_live(row, error)) return false;
    row->media_ready = true; return true;
}

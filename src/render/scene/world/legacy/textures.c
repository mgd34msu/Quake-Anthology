/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool load_optional(qa_scene_world *world, const char *name, const qa_scene_image_options *options,
                          qa_scene_image **image, qa_error *error)
{
    qa_error local = {0};
    if (qa_scene_image_load(world->resources, name, options, image, &local)) return true;
    if (local.code == QA_ERROR_NOT_FOUND) { *image = NULL; return true; }
    if (error) *error = local;
    return false;
}

static bool create_image(qa_scene_world *world, const char *name, uint32_t width, uint32_t height,
                         const uint8_t *pixels, qa_scene_wrap wrap, qa_scene_image **image, qa_error *error)
{
    qa_scene_image_level level = {width, height, pixels, (size_t)width * height * 4};
    return qa_scene_image_create(world->resources, name, QA_SCENE_RGBA8, &level, 1, wrap,
                                 QA_SCENE_LINEAR, (qa_scene_vec4){0, 0, 0, 0}, image, error);
}

static bool embedded_texture(qa_scene_world *world, qawl_texture *texture,
                             const qa_bsp_texture *source, qa_error *error)
{
    if (!world->options.images.palette_rgb.size &&
        !qa_scene_resources_palette(world->resources, QA_SCENE_Q1, &world->options.images.palette_rgb, error)) return false;
    qa_bytes palette = world->options.images.palette_rgb;
    if (!palette.data || palette.size < 768) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Embedded brush textures require a 256-color palette");
        return false;
    }
    unsigned first_fullbright = qa_scene_resources_fullbright_first(world->resources);
    bool fullbright = first_fullbright < 256 && strncmp(texture->name, "sky", 3) != 0 && texture->name[0] != '*';
    qa_image images[4] = {0}, bright[4] = {0};
    qa_scene_image_level levels[4], bright_levels[4];
    size_t count = 0;
    bool result = false, fence = texture->name[0] == '{';
    qa_palette_options options = {.transparent_index = fence ? 255 : -1,
        .fullbright_first = first_fullbright < 256 ? (int)first_fullbright : -1,
        .fullbright_last = 255, .layer = QA_PALETTE_COMBINED,
        .translation = world->options.images.translation.size == 256 ? world->options.images.translation.data : NULL};
    for (size_t mip = 0; mip < 4; ++mip) {
        uint32_t width = source->width >> mip, height = source->height >> mip;
        if (!source->levels[mip].size || !width || !height) break;
        qa_indexed_level indexed = {.width = width, .height = height,
            .indices = {(uint8_t *)source->levels[mip].data, source->levels[mip].size}};
        options.layer = QA_PALETTE_COMBINED;
        if (!qa_image_expand_indexed(&indexed, palette, &options, &images[mip], error)) goto done;
        levels[mip] = (qa_scene_image_level){width, height, images[mip].rgba.data, images[mip].rgba.size};
        if (fullbright) {
            options.layer = QA_PALETTE_FULLBRIGHT;
            if (!qa_image_expand_indexed(&indexed, palette, &options, &bright[mip], error)) goto done;
            bright_levels[mip] = (qa_scene_image_level){width, height, bright[mip].rgba.data, bright[mip].rgba.size};
        }
        ++count;
    }
    if (!count) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Embedded brush texture has no pixels"); goto done; }
    if (!qa_scene_image_create(world->resources, texture->name, QA_SCENE_RGBA8, levels, count, QA_SCENE_REPEAT,
                               world->options.images.filter, (qa_scene_vec4){0,0,0,1}, &texture->image, error)) goto done;
    if (fullbright && !qa_scene_image_create(world->resources, texture->name, QA_SCENE_RGBA8, bright_levels, count,
                                            QA_SCENE_REPEAT, world->options.images.filter,
                                            (qa_scene_vec4){0,0,0,0}, &texture->fullbright, error)) goto done;
    result = true;
done:
    for (size_t i = 0; i < 4; ++i) { qa_image_free(&images[i]); qa_image_free(&bright[i]); }
    return result;
}

static bool split_sky(qa_scene_world *world, qawl_texture *texture, const qa_bsp_texture *source,
                      bool embedded, qa_error *error)
{
    const qa_scene_image_level *image = &texture->image->levels[0];
    bool q64 = world->bsp.format == QA_BSP_QUAKE64;
    if ((!q64 && (image->width != 256 || image->height != 128)) ||
        (q64 && (image->height < 2 || image->height % 2))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid classic sky layer dimensions");
        return false;
    }
    if (texture->image->kind != QA_SCENE_RGBA8 && texture->image->kind != QA_SCENE_RGB8) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Classic sky requires color image pixels");
        return false;
    }
    uint32_t width = q64 ? image->width : 128, height = q64 ? image->height / 2 : 128;
    if ((uint64_t)width * height > SIZE_MAX / 4) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Sky image is too large"); return false;
    }
    size_t count = (size_t)width * height;
    uint8_t *solid = malloc(count * 4), *overlay = malloc(count * 4);
    if (!solid || !overlay) {
        free(solid); free(overlay);
        qa_error_set(error, QA_ERROR_MEMORY, count, "Cannot allocate classic sky layers"); return false;
    }
    const uint8_t *pixels = image->pixels;
    uint64_t sum[3] = {0};
    for (size_t i = 0; i < count; ++i) {
        size_t front = q64 ? i : (i / width) * image->width + i % width;
        size_t back = q64 ? count + i : front + 128;
        for (size_t c = 0; c < 3; ++c) {
            solid[i * 4 + c] = pixels[back * 4 + c];
            overlay[i * 4 + c] = pixels[front * 4 + c];
            sum[c] += solid[i * 4 + c];
        }
        solid[i * 4 + 3] = 255;
        overlay[i * 4 + 3] = q64 ? 128 : embedded && source->levels[0].data[front] == 0 ? 0 :
            texture->image->kind == QA_SCENE_RGBA8 ? pixels[front * 4 + 3] : 255;
    }
    if (!q64) for (size_t i = 0; i < count; ++i) if (!overlay[i * 4 + 3])
        for (size_t c = 0; c < 3; ++c) overlay[i * 4 + c] = (uint8_t)(sum[c] / count);
    bool result = create_image(world, texture->name, width, height, solid, QA_SCENE_REPEAT, &texture->sky[0], error) &&
                  create_image(world, texture->name, width, height, overlay, QA_SCENE_REPEAT, &texture->sky[1], error);
    free(solid); free(overlay);
    return result;
}

static bool animations(qawl_world *data, qa_error *error)
{
    for (size_t i = 0; i < data->texture_count; ++i) {
        qawl_texture *texture = &data->textures[i];
        if (texture->name[0] != '+') continue;
        if (!texture->name[1]) { qa_error_set(error, QA_ERROR_FORMAT, i, "Q1 animated texture has no frame identifier"); return false; }
        for (size_t cycle = 0; cycle < 2; ++cycle)
            for (size_t frame = 0; frame < 10; ++frame) texture->animation[cycle][frame] = SIZE_MAX;
        for (size_t j = 0; j < data->texture_count; ++j) {
            const char *name = data->textures[j].name;
            if (name[0] != '+' || !name[1] || strcmp(name + 2, texture->name + 2)) continue;
            int code = toupper((unsigned char)name[1]);
            size_t cycle, frame;
            if (code >= '0' && code <= '9') { cycle = 0; frame = (size_t)(code - '0'); }
            else if (code >= 'A' && code <= 'J') { cycle = 1; frame = (size_t)(code - 'A'); }
            else { qa_error_set(error, QA_ERROR_FORMAT, j, "Invalid Q1 animated texture frame"); return false; }
            texture->animation[cycle][frame] = j;
            if (texture->animation_count[cycle] < frame + 1) texture->animation_count[cycle] = frame + 1;
        }
        for (size_t cycle = 0; cycle < 2; ++cycle)
            for (size_t frame = 0; frame < texture->animation_count[cycle]; ++frame)
                if (texture->animation[cycle][frame] == SIZE_MAX) {
                    qa_error_set(error, QA_ERROR_FORMAT, i, "Q1 animated texture sequence has a missing frame"); return false;
                }
    }
    return true;
}

bool qawl_textures_build(qa_scene_world *world, qa_error *error)
{
    qawl_world *data = world->legacy_data;
    bool q1 = world->bsp.family == QA_BSP_Q1;
    if (q1) { if (!qa_bsp_texture_count(&world->bsp, &data->texture_count, error)) return false; }
    else data->texture_count = qa_bsp_record_count(&world->bsp, QA_BSP_TEXINFO);
    if (data->texture_count > SIZE_MAX / sizeof(*data->textures)) {
        qa_error_set(error, QA_ERROR_FORMAT, data->texture_count, "Brush texture table is too large"); return false;
    }
    data->textures = calloc(data->texture_count ? data->texture_count : 1, sizeof(*data->textures));
    if (!data->textures) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate brush textures"); return false; }
    for (size_t i = 0; i < data->texture_count; ++i) {
        qawl_texture *texture = &data->textures[i];
        qa_bsp_texture source = {0};
        qa_bsp_texinfo info = {0};
        if (q1) { if (!qa_bsp_read_texture(&world->bsp, i, &source, error)) return false; }
        else if (!qa_bsp_read_texinfo(&world->bsp, i, &info, error)) return false;
        qa_bytes name = q1 ? source.name : info.name;
        if (!name.size) name = (qa_bytes){(const uint8_t *)"*missing", 8};
        texture->name = qaw_string(name, error);
        texture->next = q1 ? -1 : info.next;
        if (!texture->name) return false;
        size_t name_size = strlen(texture->name);
        char *path = malloc(name_size + 10);
        if (!path) { qa_error_set(error, QA_ERROR_MEMORY, i, "Cannot allocate brush texture name"); return false; }
        snprintf(path, name_size + 10, "textures/%s", texture->name);
        qa_scene_image_options options = world->options.images;
        options.family = q1 ? QA_SCENE_Q1 : QA_SCENE_Q2;
        options.usage = QA_IMAGE_USAGE_WALL;
        options.wrap = QA_SCENE_REPEAT;
        options.mipmap = true;
        options.fullbright_only = false;
        options.transparent = q1 && texture->name[0] == '{';
        options.transparent_index = options.transparent ? 255 : -1;
        bool loaded = load_optional(world, path, &options, &texture->image, error);
        bool embedded = false;
        if (loaded && !texture->image && q1 && source.storage == QA_BSP_TEXTURE_EMBEDDED) {
            loaded = embedded_texture(world, texture, &source, error);
            embedded = loaded;
        }
        if (loaded && texture->image && q1 && !embedded && strncmp(texture->name, "sky", 3) && texture->name[0] != '*') {
            options.fullbright_only = true;
            loaded = load_optional(world, path, &options, &texture->fullbright, error);
        }
        free(path);
        if (!loaded) return false;
        if (!texture->image) {
            texture->image = (qa_scene_image *)qa_scene_missing(world->resources);
            qa_scene_image_retain(texture->image);
        }
        texture->width = q1 && source.width ? source.width : texture->image->logical_width;
        texture->height = q1 && source.height ? source.height : texture->image->logical_height;
        texture->quake64_shift = source.quake64_shift;
        if (q1 && !strncmp(texture->name, "sky", 3) && texture->image != qa_scene_missing(world->resources) &&
            !split_sky(world, texture, &source, embedded, error)) return false;
    }
    if (q1 && !animations(data, error)) return false;
    if (!q1 || world->options.q2_sky) {
        static const char *const suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
        const char *name = world->options.q2_sky ? world->options.q2_sky : "unit1_";
        size_t length = strlen(name);
        if (length > SIZE_MAX - 7) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Sky texture name is too long"); return false; }
        char *path = malloc(length + 7);
        if (!path) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate sky texture path"); return false; }
        qa_scene_image_options options = world->options.images;
        options.family = q1 ? QA_SCENE_Q1 : QA_SCENE_Q2;
        options.usage = QA_IMAGE_USAGE_SKY;
        options.wrap = QA_SCENE_CLAMP; options.mipmap = false; options.transparent = false;
        options.fullbright_only = false; options.transparent_index = -1;
        for (size_t i = 0; i < 6; ++i) {
            snprintf(path, length + 7, "env/%s%s", name, suffixes[i]);
            if (!load_optional(world, path, &options, &data->sky[i], error)) { free(path); return false; }
            if (!data->sky[i]) { data->sky[i] = (qa_scene_image *)qa_scene_missing(world->resources); qa_scene_image_retain(data->sky[i]); }
        }
        free(path);
    }
    return true;
}

void qawl_textures_destroy(qawl_world *data)
{
    if (!data) return;
    for (size_t i = 0; data->textures && i < data->texture_count; ++i) {
        qawl_texture *texture = &data->textures[i];
        free(texture->name);
        qa_scene_image_release(texture->image);
        qa_scene_image_release(texture->fullbright);
        qa_scene_image_release(texture->sky[0]);
        qa_scene_image_release(texture->sky[1]);
    }
    for (size_t i = 0; i < 6; ++i) qa_scene_image_release(data->sky[i]);
    free(data->textures);
}

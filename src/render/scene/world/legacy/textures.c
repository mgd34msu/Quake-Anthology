#include "internal.h"
#include "../../resources_internal.h"

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
    qa_indexed_level levels[4] = {0}; size_t count = 0;
    image_asset_recipe recipe = {.kind = 1, .fullbright_first = first_fullbright,
        .fullbright_last = 255, .layer = QA_PALETTE_COMBINED};
    scene_image_asset_palette(world->resources, &recipe, &world->options.images);
    recipe.options.wrap = QA_SCENE_REPEAT;
    recipe.options.transparent = texture->name[0] == '{'; recipe.options.transparent_index = 255;
    for (size_t mip = 0; mip < 4; ++mip) {
        uint32_t width = source->width >> mip, height = source->height >> mip;
        if (!source->levels[mip].size || !width || !height) break;
        levels[mip] = (qa_indexed_level){width, height,
            {(uint8_t *)source->levels[mip].data, source->levels[mip].size}};
        recipe.offsets[mip] = (uint64_t)(source->levels[mip].data - world->bytes.data);
        recipe.widths[mip] = width; recipe.heights[mip] = height; ++count;
    }
    if (!count) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Embedded brush texture has no pixels"); return false; }
    if (fullbright) {
        fullbright = false;
        for (size_t mip = 0; mip < count && !fullbright; ++mip)
            for (size_t i = 0; i < levels[mip].indices.size; ++i) {
                unsigned index = levels[mip].indices.data[i];
                if (index >= first_fullbright &&
                    !(recipe.options.transparent && index == 255)) {
                    fullbright = true;
                    break;
                }
            }
    }
    recipe.level_count = (uint8_t)count;
    qa_palette_options options = {.transparent_index = recipe.options.transparent ? 255 : -1,
        .fullbright_first = first_fullbright < 256 ? (int)first_fullbright : -1,
        .fullbright_last = 255, .layer = QA_PALETTE_COMBINED,
        .translation = world->options.images.translation.size == 256 ? world->options.images.translation.data : NULL};
    qa_scene_image_options upload = world->options.images; upload.wrap = QA_SCENE_REPEAT;
    if (!scene_resource_indexed_image(world->resources, texture->name, levels, count, &upload,
        &options, false, (qa_scene_vec4){0,0,0,1}, &texture->image, error) ||
        !scene_image_asset_copy(texture->image, &recipe, error)) return false;
    if (fullbright) {
        options.layer = QA_PALETTE_FULLBRIGHT; recipe.layer = QA_PALETTE_FULLBRIGHT;
        if (!scene_resource_indexed_image(world->resources, texture->name, levels, count, &upload,
            &options, false, (qa_scene_vec4){0}, &texture->fullbright, error) ||
            !scene_image_asset_copy(texture->fullbright, &recipe, error)) return false;
    }
    texture->image->recipient_upload_pixels = true;
    texture->image->recipient_mipmap = world->options.images.mipmap;
    if (texture->fullbright) {
        texture->fullbright->recipient_upload_pixels = true;
        texture->fullbright->recipient_mipmap = world->options.images.mipmap;
    }
    return true;
}

static bool split_sky(qa_scene_world *world, qawl_texture *texture, const qa_bsp_texture *source,
                      bool embedded, qa_error *error)
{
    bool q64 = world->bsp.format == QA_BSP_QUAKE64;
    qa_bytes indices = embedded ? source->levels[0] : (qa_bytes){0};
    if (!scene_resource_sky_layer(world->resources, texture->name, texture->image, indices, q64, false,
        &texture->sky[0], error) || !scene_resource_sky_layer(world->resources, texture->name, texture->image,
        indices, q64, true, &texture->sky[1], error)) return false;
    const image_asset_recipe *original = ((const owned_image *)texture->image)->asset;
    if (original) {
        image_asset_recipe recipe = *original; recipe.quake64 = q64;
        for (size_t i = 0; i < 2; ++i) {
            recipe.sky_layer = (uint8_t)(i + 1);
            if (!scene_image_asset_copy(texture->sky[i], &recipe, error)) return false;
        }
    }
    return true;
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

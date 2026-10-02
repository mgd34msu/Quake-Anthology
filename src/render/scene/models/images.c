#include "internal.h"
#include <ctype.h>
#include <stdio.h>

static char *copy_name(const char *name, qa_error *error) {
    size_t length = strlen(name);
    char *out = malloc(length + 1);
    if (!out) qa_error_set(error, QA_ERROR_MEMORY, 0, "model image name allocation failed");
    else memcpy(out, name, length + 1);
    return out;
}

/* Embedded ../ records are resolved within the content root before VFS lookup. */
static char *image_path(const char *name, qa_error *error) {
    size_t length = strlen(name);
    char *out = malloc(length + 1);
    if (!out) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model image path allocation failed");
        return NULL;
    }
    size_t used = 0, begin = 0;
    if (!length || (isalpha((unsigned char)name[0]) && name[1] == ':')) goto invalid;
    for (size_t i = 0; i <= length; ++i) {
        if (i != length && name[i] != '/' && name[i] != '\\') continue;
        size_t part = i - begin;
        if (!part) goto invalid;
        if (part == 2 && name[begin] == '.' && name[begin + 1] == '.') {
            if (!used) goto invalid;
            while (used && out[used - 1] != '/') --used;
            if (used) --used;
        } else if (!(part == 1 && name[begin] == '.')) {
            if (used) out[used++] = '/';
            for (size_t j = begin; j < i; ++j) out[used++] = (char)tolower((unsigned char)name[j]);
        }
        begin = i + 1;
    }
    if (!used) goto invalid;
    out[used] = 0;
    return out;
invalid:
    free(out);
    qa_error_set(error, QA_ERROR_FORMAT, 0, "model image path escapes content root or has an empty component");
    return NULL;
}

static scene_model_image *image_entry(qa_scene_model *model, const char *name, qa_error *error) {
    scene_model_image *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model image allocation failed");
        return NULL;
    }
    entry->name = copy_name(name, error);
    if (!entry->name) { free(entry); return NULL; }
    entry->next = model->images;
    model->images = entry;
    return entry;
}

bool scene_model_external_material(qa_scene_model *model, qa_material_library *materials,
    const char *name, const qa_material **out, qa_error *error) {
    char *path = image_path(name, error);
    if (!path) return false;
    bool ok = qa_material_register(materials, path, &model->options, false, out, error);
    free(path);
    return ok;
}

bool scene_model_external(qa_scene_model *model, const char *name, scene_model_image **out,
                           qa_error *error) {
    char *path = image_path(name, error);
    if (!path) return false;
    for (scene_model_image *image = model->images; image; image = image->next)
        if (!strcmp(image->name, path)) { free(path); *out = image; return true; }
    scene_model_image *image = image_entry(model, path, error);
    free(path);
    if (!image) return false;
    if (model->options.family == QA_SCENE_Q3) {
        if (!qa_material_register(model->materials, image->name, &model->options, false,
                                  &image->material, error)) goto fail;
    } else {
        qa_error load_error = {0};
        qa_scene_image *base = NULL;
        if (!qa_scene_image_load(model->resources, image->name, &model->options, &base, &load_error)) {
            if (load_error.code != QA_ERROR_NOT_FOUND) { if (error) *error = load_error; goto fail; }
            image->base = qa_scene_missing(model->resources);
            qa_scene_image_retain(image->base);
        } else image->base = base;
        if (model->options.family == QA_SCENE_Q1 && base) {
            qa_scene_image_options fullbright = model->options;
            fullbright.fullbright_only = true;
            qa_scene_image *bright = NULL;
            if (!qa_scene_image_load(model->resources, image->name, &fullbright, &bright, &load_error)) {
                if (load_error.code != QA_ERROR_NOT_FOUND) { if (error) *error = load_error; goto fail; }
            } else image->fullbright = bright;
        }
    }
    *out = image;
    return true;
fail:
    model->images = image->next;
    qa_scene_image_release(image->base);
    qa_scene_image_release(image->fullbright);
    free(image->name);
    free(image);
    return false;
}

static bool flood_skin(uint8_t *pixels, uint32_t width, uint32_t height,
                        const uint8_t palette[768], qa_error *error) {
    uint8_t black = 0, fill = pixels[0];
    for (unsigned i = 0; i < 256; ++i)
        if (!(palette[i * 3] | palette[i * 3 + 1] | palette[i * 3 + 2])) { black = (uint8_t)i; break; }
    if (fill == black || fill == 255) return true;
    size_t count = (size_t)width * height;
    if (count > SIZE_MAX / sizeof(size_t)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model skin flood queue is too large");
        return false;
    }
    size_t *queue = malloc(count * sizeof(*queue));
    if (!queue) { qa_error_set(error, QA_ERROR_MEMORY, 0, "model skin flood queue allocation failed"); return false; }
    size_t tail = 1;
    queue[0] = 0;
    pixels[0] = 255;
    for (size_t head = 0; head < tail; ++head) {
        size_t pixel = queue[head], x = pixel % width, y = pixel / width;
        size_t neighbors[4] = {x ? pixel - 1 : SIZE_MAX, x + 1 < width ? pixel + 1 : SIZE_MAX,
                              y ? pixel - width : SIZE_MAX, y + 1 < height ? pixel + width : SIZE_MAX};
        uint8_t color = black;
        for (unsigned i = 0; i < 4; ++i) {
            size_t next = neighbors[i];
            if (next == SIZE_MAX) continue;
            if (pixels[next] == fill) { pixels[next] = 255; queue[tail++] = next; }
            else if (pixels[next] != 255) color = pixels[next];
        }
        pixels[pixel] = color;
    }
    free(queue);
    return true;
}

static bool upload_indexed(qa_scene_model *model, const char *name, const qa_indexed_level *indices,
                            bool sprite, qa_palette_layer layer, qa_scene_image **out, qa_error *error) {
    unsigned first_fullbright = qa_scene_resources_fullbright_first(model->resources);
    qa_palette_options options = {.transparent_index = sprite ? 255 : -1,
        .fullbright_first = (int)first_fullbright, .fullbright_last = sprite ? 254 : 255,
        .translation = model->options.translation.size ? model->translation : NULL, .layer = layer};
    qa_image image = {0};
    if (!qa_image_expand_indexed(indices, model->options.palette_rgb, &options, &image, error)) return false;
    qa_mip_chain chain = {0};
    if (model->options.mipmap && !sprite && !qa_image_mip_chain(&image, QA_MIP_BOX, &chain, error)) {
        qa_image_free(&image); return false;
    }
    qa_scene_image_level *levels = calloc(chain.count + 1, sizeof(*levels));
    if (!levels) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model skin mip descriptors allocation failed");
        qa_mip_chain_free(&chain); qa_image_free(&image); return false;
    }
    levels[0] = (qa_scene_image_level){image.width, image.height, image.rgba.data, image.rgba.size};
    for (size_t i = 0; i < chain.count; ++i)
        levels[i + 1] = (qa_scene_image_level){chain.levels[i].width, chain.levels[i].height,
                                              chain.levels[i].rgba.data, chain.levels[i].rgba.size};
    bool ok = qa_scene_image_create(model->resources, name, QA_SCENE_RGBA8, levels, chain.count + 1,
        model->options.wrap, model->options.filter, (qa_scene_vec4){0}, out, error);
    if (ok) (*out)->recipient_upload_pixels = true;
    free(levels); qa_mip_chain_free(&chain); qa_image_free(&image);
    return ok;
}

bool scene_model_indexed(qa_scene_model *model, const char *name, qa_bytes pixels,
                          uint32_t width, uint32_t height, bool sprite,
                          scene_model_image **out, qa_error *error) {
    if (!width || !height || width > SIZE_MAX / height || pixels.size != (size_t)width * height ||
        !pixels.data || model->options.palette_rgb.size != 768) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "indexed model requires pixels and a 256-color RGB palette");
        return false;
    }
    qa_indexed_level indices = {width, height, {malloc(pixels.size), pixels.size}};
    if (!indices.indices.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "model skin allocation failed"); return false; }
    memcpy(indices.indices.data, pixels.data, pixels.size);
    if (!sprite && !flood_skin(indices.indices.data, width, height, model->palette, error)) {
        qa_buffer_free(&indices.indices); return false;
    }
    scene_model_image *entry = image_entry(model, name, error);
    if (!entry) { qa_buffer_free(&indices.indices); return false; }
    qa_scene_image *base = NULL, *bright = NULL;
    bool ok = upload_indexed(model, name, &indices, sprite, QA_PALETTE_COMBINED, &base, error);
    bool has_bright = false;
    unsigned first_fullbright = qa_scene_resources_fullbright_first(model->resources);
    for (size_t i = 0; i < pixels.size; ++i)
        if (indices.indices.data[i] >= first_fullbright && (!sprite || indices.indices.data[i] != 255)) { has_bright = true; break; }
    if (ok && has_bright && !sprite) {
        size_t length = strlen(name);
        char *bright_name = malloc(length + 12);
        if (!bright_name) { qa_error_set(error, QA_ERROR_MEMORY, 0, "fullbright name allocation failed"); ok = false; }
        else {
            memcpy(bright_name, name, length);
            memcpy(bright_name + length, ":fullbright", 12);
            ok = upload_indexed(model, bright_name, &indices, sprite, QA_PALETTE_FULLBRIGHT, &bright, error);
            free(bright_name);
        }
    }
    qa_buffer_free(&indices.indices);
    if (!ok) {
        qa_scene_image_release(base); qa_scene_image_release(bright);
        model->images = entry->next; free(entry->name); free(entry); return false;
    }
    entry->base = base; entry->fullbright = bright; *out = entry;
    return true;
}

bool scene_model_indexed_override(qa_scene_model *model, const qa_scene_model_indexed_skin *skin,
    scene_model_image **out, qa_error *error) {
    if (!skin || !skin->name || !*skin->name || !skin->width || !skin->height ||
        skin->width > SIZE_MAX / skin->height || !skin->indices.data ||
        skin->indices.size != (size_t)skin->width * skin->height ||
        model->source->format != QA_MODEL_MDL || model->options.family != QA_SCENE_Q1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Indexed override requires an actual Q1 MDL skin");
        return false;
    }
    for (scene_model_image *entry = model->images; entry; entry = entry->next) {
        if (strcmp(entry->name, skin->name)) continue;
        if (!entry->indexed_override || entry->indexed_width != skin->width ||
            entry->indexed_height != skin->height || entry->indexed_pixels.size != skin->indices.size ||
            memcmp(entry->indexed_pixels.data, skin->indices.data, skin->indices.size)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Indexed skin name identifies different retained pixels");
            return false;
        }
        *out = entry; return true;
    }
    qa_buffer retained = {malloc(skin->indices.size), skin->indices.size};
    if (!retained.data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining indexed skin pixels"); return false;
    }
    memcpy(retained.data, skin->indices.data, skin->indices.size);
    scene_model_image *entry = NULL;
    if (!scene_model_indexed(model, skin->name, skin->indices, skin->width, skin->height,
        false, &entry, error)) { qa_buffer_free(&retained); return false; }
    entry->indexed_override = true; entry->indexed_pixels = retained;
    entry->indexed_width = skin->width; entry->indexed_height = skin->height;
    *out = entry; return true;
}

void scene_model_images_destroy(qa_scene_model *model) {
    scene_model_image *entry = model->images;
    while (entry) {
        scene_model_image *next = entry->next;
        qa_scene_image_release(entry->base); qa_scene_image_release(entry->fullbright);
        qa_buffer_free(&entry->indexed_pixels);
        free(entry->name); free(entry); entry = next;
    }
    free(model->skins); free(model->sprites);
}

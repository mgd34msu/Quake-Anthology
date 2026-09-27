#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool qa_font_fail(qa_error *error, qa_status status, size_t offset, const char *message) {
    qa_error_set(error, status, offset, "%s", message);
    return false;
}

char *qa_font_copy_string(const char *source, qa_error *error) {
    if (!source) {
        qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing font string");
        return NULL;
    }
    size_t length = strlen(source);
    if (length == SIZE_MAX) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Font string size overflow");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font string");
        return NULL;
    }
    memcpy(copy, source, length + 1);
    return copy;
}

bool qa_font_valid_scalar(uint32_t codepoint) {
    return codepoint <= 0x10ffffu && (codepoint < 0xd800u || codepoint > 0xdfffu);
}

static bool reserve(void **items, size_t *capacity, size_t count, size_t item_size,
                    qa_error *error) {
    if (count <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 8;
    while (next < count) {
        if (next > SIZE_MAX / 2)
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Font collection size overflow");
        next *= 2;
    }
    if (next > SIZE_MAX / item_size)
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Font allocation size overflow");
    void *grown = realloc(*items, next * item_size);
    if (!grown)
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font collection");
    *items = grown;
    *capacity = next;
    return true;
}

qa_font_library *qa_font_library_create(qa_vfs *vfs, qa_scene_resources *resources,
                                        qa_error *error) {
    if (!vfs || !resources) {
        qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing font VFS or scene resources");
        return NULL;
    }
    qa_font_library *library = calloc(1, sizeof(*library));
    if (!library) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font library");
        return NULL;
    }
    library->vfs = vfs;
    library->resources = resources;
    return library;
}

void qa_font_internal_destroy(qa_font *font) {
    if (!font)
        return;
    for (size_t i = 0; i < font->image_count; ++i)
        qa_scene_image_release(font->images[i]);
    for (size_t i = 0; i < font->source_count; ++i)
        qa_resource_release(font->sources[i]);
    free(font->images);
    free(font->sources);
    free(font->glyphs);
    free(font->truetype_coverage);
    free(font->name);
    free(font);
}

void qa_font_library_destroy(qa_font_library *library) {
    if (!library)
        return;
    for (size_t i = 0; i < library->font_count; ++i)
        qa_font_internal_destroy(library->fonts[i]);
    if (library->freetype)
        FT_Done_FreeType(library->freetype);
    free(library->fonts);
    free(library);
}

qa_font *qa_font_internal_create(qa_font_library *library, qa_font_kind kind, const char *name,
                                 qa_error *error) {
    if (!library || !name) {
        qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font construction");
        return NULL;
    }
    qa_font *font = calloc(1, sizeof(*font));
    if (!font) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font");
        return NULL;
    }
    font->name = qa_font_copy_string(name, error);
    if (!font->name) {
        free(font);
        return NULL;
    }
    font->library = library;
    font->kind = kind;
    return font;
}

bool qa_font_internal_add_glyph(qa_font *font, qa_font_glyph glyph, qa_error *error) {
    if (!font || !qa_font_valid_scalar(glyph.codepoint) || !isfinite(glyph.width) ||
        !isfinite(glyph.height) || !isfinite(glyph.advance) || !isfinite(glyph.bearing_x) ||
        !isfinite(glyph.bearing_y) || glyph.width < 0 || glyph.height < 0) {
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font glyph");
    }
    glyph.font = font;
    if (!reserve((void **)&font->glyphs, &font->glyph_capacity, font->glyph_count + 1,
                 sizeof(*font->glyphs), error))
        return false;
    font->glyphs[font->glyph_count++] = glyph;
    return true;
}

bool qa_font_internal_take_image(qa_font *font, const qa_scene_image *image, qa_error *error) {
    if (!font || !image)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing font atlas image");
    for (size_t i = 0; i < font->image_count; ++i) {
        if (font->images[i] == image) {
            qa_scene_image_release(image);
            return true;
        }
    }
    if (!reserve((void **)&font->images, &font->image_capacity, font->image_count + 1,
                 sizeof(*font->images), error)) {
        qa_scene_image_release(image);
        return false;
    }
    font->images[font->image_count++] = image;
    return true;
}

bool qa_font_internal_take_source(qa_font *font, qa_resource *resource, qa_error *error) {
    if (!font || !resource)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing retained font source");
    for (size_t i = 0; i < font->source_count; ++i) {
        if (font->sources[i] == resource) {
            qa_resource_release(resource);
            return true;
        }
    }
    if (!reserve((void **)&font->sources, &font->source_capacity, font->source_count + 1,
                 sizeof(*font->sources), error)) {
        qa_resource_release(resource);
        return false;
    }
    font->sources[font->source_count++] = resource;
    return true;
}

static int compare_glyph(const void *left, const void *right) {
    const qa_font_glyph *a = left, *b = right;
    return a->codepoint < b->codepoint ? -1 : a->codepoint > b->codepoint;
}

bool qa_font_internal_publish(qa_font *font, const qa_font **out, qa_error *error) {
    if (!font || !out || !(font->line_height > 0) || !isfinite(font->line_height) ||
        !isfinite(font->ascent) || !isfinite(font->descent))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Incomplete font publication");
    qsort(font->glyphs, font->glyph_count, sizeof(*font->glyphs), compare_glyph);
    qa_font_library *library = font->library;
    if (!reserve((void **)&library->fonts, &library->font_capacity, library->font_count + 1,
                 sizeof(*library->fonts), error))
        return false;
    library->fonts[library->font_count++] = font;
    *out = font;
    return true;
}

void qa_font_internal_measure_cap_ink(qa_font *font) {
    static const uint32_t samples[] = {'H', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (!font || !(font->line_height > 0))
        return;
    uint32_t top = UINT32_MAX, bottom = 0;
    bool found = false;
    for (size_t sample = 0; sample < sizeof(samples) / sizeof(samples[0]); ++sample) {
        const qa_font_glyph *glyph = NULL;
        for (size_t i = 0; i < font->glyph_count; ++i) {
            if (font->glyphs[i].codepoint == samples[sample]) {
                glyph = &font->glyphs[i];
                break;
            }
        }
        if (!glyph || !glyph->image || !glyph->image->level_count ||
            !glyph->image->levels[0].pixels ||
            (glyph->image->kind != QA_SCENE_RGBA8 && glyph->image->kind != QA_SCENE_RGB8))
            continue;
        const qa_scene_image_level *level = &glyph->image->levels[0];
        double source_x = (double)glyph->uv.x * level->width;
        double source_y = (double)glyph->uv.y * level->height;
        double source_width = glyph->width;
        double source_height = glyph->height;
        if (!isfinite(source_x) || !isfinite(source_y) || !isfinite(source_width) ||
            !isfinite(source_height) || source_x < 0 || source_y < 0 || source_width < 0 ||
            source_height < 0 || source_x > UINT32_MAX || source_y > UINT32_MAX ||
            source_width > UINT32_MAX || source_height > UINT32_MAX)
            continue;
        uint32_t x0 = (uint32_t)llround(source_x);
        uint32_t y0 = (uint32_t)llround(source_y);
        uint32_t width = (uint32_t)llround(source_width);
        uint32_t height = (uint32_t)llround(source_height);
        if (x0 > level->width || y0 > level->height || width > level->width - x0 ||
            height > level->height - y0 ||
            (level->height && level->width > SIZE_MAX / level->height / 4u) ||
            level->bytes < (size_t)level->width * level->height * 4u)
            continue;
        const uint8_t *pixels = level->pixels;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t offset = ((size_t)(y0 + y) * level->width + x0 + x) * 4u;
                if (pixels[offset + 3] <= 127)
                    continue;
                if (y < top)
                    top = y;
                if (y > bottom)
                    bottom = y;
                found = true;
            }
        }
    }
    if (found) {
        font->has_cap_ink = true;
        font->cap_top = (float)top * 8.0f / font->line_height;
        font->cap_height = (float)(bottom - top + 1u) * 8.0f / font->line_height;
    }
}

bool qa_font_internal_freetype(qa_font_library *library, FT_Library *out, qa_error *error) {
    if (!library || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid FreeType request");
    if (!library->freetype && FT_Init_FreeType(&library->freetype))
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0, "FreeType initialization failed");
    *out = library->freetype;
    return true;
}

bool qa_font_internal_picture(qa_font *font, const char *path, qa_scene_family family,
                              qa_scene_filter filter, const qa_scene_image **out, qa_error *error) {
    if (!font || !path || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font picture request");
    qa_scene_image_options options = {0};
    options.family = family;
    options.wrap = QA_SCENE_CLAMP;
    options.filter = filter;
    options.usage = QA_IMAGE_USAGE_PICTURE;
    options.transparent = true;
    options.transparent_index = 255;
    qa_scene_image *image = NULL;
    if (!qa_scene_image_load(font->library->resources, path, &options, &image, error))
        return false;
    if (!qa_font_internal_take_image(font, image, error))
        return false;
    *out = image;
    return true;
}

bool qa_font_classic_create(qa_font_library *library, const char *name, const qa_scene_image *image,
                            qa_font_color_policy policy, const qa_font **out, qa_error *error) {
    if (!library || !name || !image || !out || image->logical_width == 0 ||
        image->logical_height == 0 || image->logical_width % 16 || image->logical_height % 16 ||
        (policy != QA_FONT_TINTED && policy != QA_FONT_BAKED_COLOR))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "Classic charset must contain 16 by 16 cells");
    qa_font *font = qa_font_internal_create(library, QA_FONT_CLASSIC, name, error);
    if (!font)
        return false;
    qa_scene_image_retain(image);
    if (!qa_font_internal_take_image(font, image, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    float width = (float)image->logical_width / 16.0f;
    float height = (float)image->logical_height / 16.0f;
    font->line_height = font->ascent = height;
    font->descent = 0;
    for (uint32_t code = 0; code < 256; ++code) {
        float x = (float)(code & 15u) * width;
        float y = (float)(code >> 4) * height;
        qa_font_glyph glyph = {
            .codepoint = code,
            .image = image,
            .uv = {x / image->logical_width, y / image->logical_height,
                   (x + width) / image->logical_width, (y + height) / image->logical_height},
            .width = width,
            .height = height,
            .advance = width,
            .bearing_y = height,
            .visible = (code & 127u) != 32u,
            .baked_color = policy == QA_FONT_BAKED_COLOR,
        };
        if (!qa_font_internal_add_glyph(font, glyph, error)) {
            qa_font_internal_destroy(font);
            return false;
        }
    }
    if (!qa_font_internal_publish(font, out, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    return true;
}

bool qa_font_describe(const qa_font *font, qa_font_info *out) {
    if (!font || !out)
        return false;
    *out = (qa_font_info){
        .kind = font->kind,
        .name = font->name,
        .line_height = font->line_height,
        .ascent = font->ascent,
        .descent = font->descent,
        .cap_top = font->cap_top,
        .cap_height = font->cap_height,
        .has_cap_ink = font->has_cap_ink,
        .glyph_count = font->glyph_count,
    };
    return true;
}

bool qa_font_find_glyph(const qa_font *font, uint32_t codepoint, qa_font_glyph *out) {
    if (!font || !out)
        return false;
    size_t low = 0, high = font->glyph_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        uint32_t found = font->glyphs[middle].codepoint;
        if (found < codepoint)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == font->glyph_count || font->glyphs[low].codepoint != codepoint)
        return false;
    *out = font->glyphs[low];
    return true;
}

bool qa_font_selection_init(qa_font_selection *selection, uint32_t seat, const qa_font *classic,
                            const qa_font *primary, const qa_font *const *fallbacks,
                            size_t fallback_count, qa_error *error) {
    if (!selection || !classic || classic->kind != QA_FONT_CLASSIC ||
        (fallback_count && !fallbacks))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font selection");
    qa_font_library *library = classic->library;
    if ((primary && primary->library != library))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "Font selection crosses library ownership");
    qa_font_selection result = {0};
    result.seat = seat;
    result.classic = classic;
    result.primary = primary;
    for (size_t i = 0; i < fallback_count; ++i) {
        if (!fallbacks[i] || fallbacks[i]->library != library)
            return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Font fallback crosses library ownership");
        ++result.fallback_count;
    }
    result.fallbacks = fallbacks;
    *selection = result;
    return true;
}

static bool usable_glyph(const qa_font_glyph *glyph) {
    if (!glyph || !glyph->font)
        return false;
    if (glyph->font->kind == QA_FONT_TRUETYPE || glyph->font->kind == QA_FONT_CLASSIC)
        return true;
    return glyph->width > 0 || glyph->advance != 0;
}

bool qa_font_resolve(const qa_font_selection *selection, uint32_t codepoint, bool alternate,
                     qa_font_glyph *out) {
    if (!selection || !selection->classic || !out || !qa_font_valid_scalar(codepoint) ||
        (selection->fallback_count && !selection->fallbacks))
        return false;
    if (!selection->primary && codepoint <= 255u) {
        uint32_t classic = codepoint | (alternate ? 128u : 0u);
        if (qa_font_find_glyph(selection->classic, classic & 255u, out)) {
            out->visible = (codepoint & 127u) != 32u;
            return true;
        }
    }
    if (selection->primary && qa_font_find_glyph(selection->primary, codepoint, out) &&
        usable_glyph(out))
        return true;
    for (size_t i = 0; i < selection->fallback_count; ++i) {
        if (qa_font_find_glyph(selection->fallbacks[i], codepoint, out) && usable_glyph(out))
            return true;
    }
    if (codepoint <= 255u) {
        uint32_t classic = codepoint | (alternate ? 128u : 0u);
        if (qa_font_find_glyph(selection->classic, classic & 255u, out)) {
            out->visible = (codepoint & 127u) != 32u;
            return true;
        }
    }
    if (selection->primary && qa_font_find_glyph(selection->primary, '?', out) && usable_glyph(out))
        return true;
    for (size_t i = 0; i < selection->fallback_count; ++i) {
        if (qa_font_find_glyph(selection->fallbacks[i], '?', out) && usable_glyph(out))
            return true;
    }
    return qa_font_find_glyph(selection->classic, '?', out);
}

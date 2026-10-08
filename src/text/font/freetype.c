#include "internal.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct raster_glyph {
    qa_font_glyph glyph;
    uint32_t x, y;
} raster_glyph;

typedef struct atlas_page {
    uint8_t *pixels;
    uint32_t width, height, x, y, shelf_height, page;
    raster_glyph *glyphs;
    size_t glyph_count, glyph_capacity;
} atlas_page;

static int compare_codepoint(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return a < b ? -1 : a > b;
}

static bool append_codepoint(uint32_t **values, size_t *count, size_t *capacity, uint32_t value,
                             qa_error *error) {
    if (!qa_font_valid_scalar(value))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "TrueType coverage contains a non-scalar value");
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 256;
        if (next < *capacity || next > SIZE_MAX / sizeof(**values))
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "TrueType coverage size overflow");
        uint32_t *grown = realloc(*values, next * sizeof(**values));
        if (!grown)
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating TrueType coverage");
        *values = grown;
        *capacity = next;
    }
    (*values)[(*count)++] = value;
    return true;
}

static bool collect_codepoints(FT_Face face, const qa_font_truetype_options *options,
                               uint32_t **out, size_t *out_count, qa_error *error) {
    uint32_t *values = NULL;
    size_t count = 0, capacity = 0;
    bool success = true;
    if (options->codepoint_count) {
        if (!options->codepoints)
            success = qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing TrueType coverage values");
        for (size_t i = 0; success && i < options->codepoint_count; ++i)
            success = append_codepoint(&values, &count, &capacity, options->codepoints[i], error);
        if (success)
            success = append_codepoint(&values, &count, &capacity, ' ', error) &&
                      append_codepoint(&values, &count, &capacity, '?', error);
    } else {
        FT_UInt glyph_index = 0;
        FT_ULong character = FT_Get_First_Char(face, &glyph_index);
        while (success && glyph_index) {
            if (character <= UINT32_MAX && qa_font_valid_scalar((uint32_t)character))
                success = append_codepoint(&values, &count, &capacity, (uint32_t)character, error);
            character = FT_Get_Next_Char(face, character, &glyph_index);
        }
    }
    if (!success) {
        free(values);
        return false;
    }
    qsort(values, count, sizeof(*values), compare_codepoint);
    size_t unique = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!unique || values[i] != values[unique - 1])
            values[unique++] = values[i];
    }
    *out = values;
    *out_count = unique;
    return true;
}

static uint8_t bitmap_sample(const FT_Bitmap *bitmap, uint32_t x, uint32_t y,
                             const uint8_t **bgra) {
    ptrdiff_t pitch = bitmap->pitch;
    const uint8_t *row = bitmap->buffer + (ptrdiff_t)y * pitch;
    *bgra = NULL;
    switch (bitmap->pixel_mode) {
    case FT_PIXEL_MODE_GRAY:
        if (bitmap->num_grays > 1)
            return (uint8_t)((uint32_t)row[x] * 255u / ((uint32_t)bitmap->num_grays - 1u));
        return row[x] ? 255 : 0;
    case FT_PIXEL_MODE_MONO:
        return row[x >> 3] & (0x80u >> (x & 7u)) ? 255 : 0;
    case FT_PIXEL_MODE_GRAY2: {
        unsigned shift = 6u - (x & 3u) * 2u;
        return (uint8_t)((((uint32_t)row[x >> 2] >> shift) & 3u) * 85u);
    }
    case FT_PIXEL_MODE_GRAY4: {
        unsigned shift = (x & 1u) ? 0u : 4u;
        return (uint8_t)((((uint32_t)row[x >> 1] >> shift) & 15u) * 17u);
    }
    case FT_PIXEL_MODE_BGRA:
        *bgra = row + (size_t)x * 4u;
        return (*bgra)[3];
    default:
        return 0;
    }
}

static uint8_t straight(uint8_t component, uint8_t alpha) {
    if (!alpha)
        return 0;
    unsigned value = (unsigned)component * 255u / alpha;
    return (uint8_t)(value > 255u ? 255u : value);
}

static bool copy_bitmap(atlas_page *page, uint32_t x, uint32_t y, const FT_Bitmap *bitmap,
                        qa_error *error) {
    if ((uint64_t)x + bitmap->width > page->width || (uint64_t)y + bitmap->rows > page->height)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "TrueType glyph exceeds atlas page");
    for (uint32_t row = 0; row < bitmap->rows; ++row) {
        for (uint32_t column = 0; column < bitmap->width; ++column) {
            const uint8_t *bgra;
            uint8_t alpha = bitmap_sample(bitmap, column, row, &bgra);
            size_t offset = ((size_t)(y + row) * page->width + x + column) * 4u;
            if (bgra) {
                page->pixels[offset + 0] = straight(bgra[2], alpha);
                page->pixels[offset + 1] = straight(bgra[1], alpha);
                page->pixels[offset + 2] = straight(bgra[0], alpha);
            } else {
                page->pixels[offset + 0] = 255;
                page->pixels[offset + 1] = 255;
                page->pixels[offset + 2] = 255;
            }
            page->pixels[offset + 3] = alpha;
        }
    }
    return true;
}

static bool append_page_glyph(atlas_page *page, raster_glyph value, qa_error *error) {
    if (page->glyph_count == page->glyph_capacity) {
        size_t next = page->glyph_capacity ? page->glyph_capacity * 2 : 128;
        if (next < page->glyph_capacity || next > SIZE_MAX / sizeof(*page->glyphs))
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "TrueType page glyph count overflow");
        raster_glyph *grown = realloc(page->glyphs, next * sizeof(*page->glyphs));
        if (!grown)
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating TrueType page glyphs");
        page->glyphs = grown;
        page->glyph_capacity = next;
    }
    page->glyphs[page->glyph_count++] = value;
    return true;
}

static bool flush_page(qa_font *font, atlas_page *page, qa_error *error) {
    if (!page->glyph_count)
        return true;
    uint32_t used_height = page->y + page->shelf_height;
    if (!used_height)
        used_height = 1;
    char name[128];
    int count = snprintf(name, sizeof(name), "@font/%llu/%u",
                         (unsigned long long)qa_scene_identity(), page->page++);
    if (count < 0 || (size_t)count >= sizeof(name))
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Formatting TrueType atlas name");
    qa_scene_image_level level = {page->width, used_height, page->pixels,
                                  (size_t)page->width * used_height * 4u};
    qa_scene_image *image = NULL;
    if (!qa_scene_image_create(font->library->resources, name, QA_SCENE_RGBA8, &level, 1,
                               QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){0, 0, 0, 0}, &image,
                               error))
        return false;
    if (!qa_font_internal_take_image(font, image, error))
        return false;
    for (size_t i = 0; i < page->glyph_count; ++i) {
        raster_glyph item = page->glyphs[i];
        item.glyph.image = image;
        item.glyph.uv =
            (qa_scene_vec4){(float)item.x / (float)page->width, (float)item.y / (float)used_height,
                            (float)(item.x + (uint32_t)item.glyph.width) / (float)page->width,
                            (float)(item.y + (uint32_t)item.glyph.height) / (float)used_height};
        if (!qa_font_internal_add_glyph(font, item.glyph, error))
            return false;
    }
    memset(page->pixels, 0, (size_t)page->width * page->height * 4u);
    page->x = page->y = page->shelf_height = 0;
    page->glyph_count = 0;
    return true;
}

static bool place_glyph(qa_font *font, atlas_page *page, qa_font_glyph glyph,
                        const FT_Bitmap *bitmap, qa_error *error) {
    uint32_t width = bitmap->width, height = bitmap->rows;
    if (!width || !height) {
        glyph.image = NULL;
        glyph.uv = (qa_scene_vec4){0};
        return qa_font_internal_add_glyph(font, glyph, error);
    }
    if (width > page->width || height > page->height)
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, glyph.codepoint,
                            "TrueType glyph is larger than its atlas page");
    if (page->x && (uint64_t)page->x + width > page->width) {
        page->x = 0;
        page->y += page->shelf_height + 1u;
        page->shelf_height = 0;
    }
    if ((uint64_t)page->y + height > page->height) {
        if (!flush_page(font, page, error))
            return false;
    }
    uint32_t x = page->x, y = page->y;
    if (!copy_bitmap(page, x, y, bitmap, error) ||
        !append_page_glyph(page, (raster_glyph){glyph, x, y}, error))
        return false;
    uint64_t next_x = (uint64_t)page->x + width + 1u;
    page->x = next_x > UINT32_MAX ? UINT32_MAX : (uint32_t)next_x;
    if (height > page->shelf_height)
        page->shelf_height = height;
    return true;
}

bool qa_font_truetype_load(qa_font_library *library, const qa_font_truetype_options *options,
                           const qa_font **out, qa_error *error) {
    if (!library || !options || !options->path || !out || options->pixel_size == 0)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid TrueType font request");
    if (!qa_font_internal_admission_ready(library,error)) return false;
    uint32_t atlas_width = options->atlas_width ? options->atlas_width : 1024u;
    uint32_t atlas_height = options->atlas_height ? options->atlas_height : 2048u;
    if (atlas_width > SIZE_MAX / atlas_height / 4u)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid TrueType atlas dimensions");

    qa_resource *source = NULL;
    if (!qa_vfs_acquire(library->vfs, options->path, &source, NULL, error))
        return false;
    qa_font *font = qa_font_internal_create(library, QA_FONT_TRUETYPE,
                                            options->name ? options->name : options->path, error);
    if (!font) {
        qa_resource_release(source);
        return false;
    }
    if (!qa_font_internal_take_source(font, source, error)) {
        qa_font_internal_destroy(font);
        return false;
    }

    FT_Library freetype;
    FT_Face face = NULL;
    qa_bytes bytes = qa_resource_bytes(source);
    bool success = qa_font_internal_freetype(library, &freetype, error);
    if (success && (bytes.size > LONG_MAX ||
                    FT_New_Memory_Face(freetype, bytes.data, (FT_Long)bytes.size, 0, &face)))
        success = qa_font_fail(error, QA_ERROR_FORMAT, 0, "FreeType cannot open font face");
    if (success && FT_Select_Charmap(face, FT_ENCODING_UNICODE))
        success =
            qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0, "TrueType face has no Unicode charmap");
    if (success && FT_Set_Pixel_Sizes(face, 0, options->pixel_size))
        success = qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0,
                               "FreeType cannot select the requested pixel size");

    uint32_t *codepoints = NULL;
    size_t codepoint_count = 0;
    if (success)
        success = collect_codepoints(face, options, &codepoints, &codepoint_count, error);
    if (success) {
        const char *name = options->name ? options->name : options->path;
        uint64_t source_id = qa_resource_id(source);
        for (size_t i = 0; i < library->font_count; ++i) {
            const qa_font *cached = library->fonts[i];
            if (cached->kind != QA_FONT_TRUETYPE || cached->truetype_source != source_id ||
                cached->truetype_pixel_size != options->pixel_size ||
                cached->truetype_atlas_width != atlas_width ||
                cached->truetype_atlas_height != atlas_height ||
                cached->truetype_coverage_count != codepoint_count || strcmp(cached->name, name) ||
                (codepoint_count && memcmp(cached->truetype_coverage, codepoints,
                                           codepoint_count * sizeof(*codepoints))))
                continue;
            free(codepoints);
            FT_Done_Face(face);
            qa_font_internal_destroy(font);
            *out = cached;
            return true;
        }
        font->truetype_source = source_id;
        font->truetype_pixel_size = options->pixel_size;
        font->truetype_atlas_width = atlas_width;
        font->truetype_atlas_height = atlas_height;
        font->truetype_coverage = codepoints;
        font->truetype_coverage_count = codepoint_count;
        codepoints = NULL;
    }
    if (success) {
        font->ascent = face->units_per_EM
            ? (float)face->ascender * (float)options->pixel_size / face->units_per_EM
            : (float)face->size->metrics.ascender / 64.0f;
        if (!(font->ascent > 0))
            font->ascent = (float)options->pixel_size;
        font->line_height = font->ascent;
        font->descent = 0;
    }

    atlas_page page = {0};
    if (success) {
        page.width = atlas_width;
        page.height = atlas_height;
        page.pixels = calloc((size_t)atlas_width * atlas_height, 4u);
        if (!page.pixels)
            success = qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating TrueType atlas page");
    }
    for (size_t i = 0; success && i < codepoint_count; ++i) {
        uint32_t codepoint = font->truetype_coverage[i];
        FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
        if (!glyph_index)
            continue;
        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_HINTING | FT_LOAD_COLOR)) {
            success = qa_font_fail(error, QA_ERROR_FORMAT, codepoint, "FreeType cannot load glyph");
            break;
        }
        float descent = (float)(face->glyph->metrics.height -
                                 face->glyph->metrics.horiBearingY) / 64.0f;
        if (descent > font->descent)
            font->descent = descent;
        if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP &&
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
            success =
                qa_font_fail(error, QA_ERROR_FORMAT, codepoint, "FreeType cannot rasterize glyph");
            break;
        }
        FT_GlyphSlot slot = face->glyph;
        if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY &&
            slot->bitmap.pixel_mode != FT_PIXEL_MODE_MONO &&
            slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY2 &&
            slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY4 &&
            slot->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA && slot->bitmap.width &&
            slot->bitmap.rows) {
            success = qa_font_fail(error, QA_ERROR_UNSUPPORTED, codepoint,
                                   "FreeType produced an unsupported glyph bitmap");
            break;
        }
        qa_font_glyph glyph = {
            .codepoint = codepoint,
            .width = (float)slot->bitmap.width,
            .height = (float)slot->bitmap.rows,
            .advance = roundf((float)slot->advance.x / 64.0f),
            .bearing_x = (float)slot->bitmap_left,
            .bearing_y = (float)slot->bitmap_top,
            .visible = codepoint != 32u && slot->bitmap.width && slot->bitmap.rows,
            .baked_color = slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA,
        };
        success = place_glyph(font, &page, glyph, &slot->bitmap, error);
    }
    if (success)
        font->line_height = fmaxf(1, roundf(font->ascent + font->descent));
    if (success)
        success = flush_page(font, &page, error);
    free(page.glyphs);
    free(page.pixels);
    free(codepoints);
    if (face)
        FT_Done_Face(face);
    if (success)
        qa_font_internal_measure_cap_ink(font);
    if (!success || !font->glyph_count || !qa_font_internal_publish(font, out, error)) {
        if (success && !font->glyph_count)
            qa_font_fail(error, QA_ERROR_FORMAT, 0,
                         "TrueType coverage contains no available glyphs");
        qa_font_internal_destroy(font);
        return false;
    }
    return true;
}

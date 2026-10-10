#include "internal.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool terminated(const char *text, size_t capacity) {
    return memchr(text, 0, capacity) != NULL;
}

static bool validate_record(const qa_q3_font_record *record, qa_error *error) {
    if (!record || !isfinite(record->glyph_scale) || record->glyph_scale <= 0 ||
        !terminated(record->name, sizeof(record->name)))
        return qa_font_fail(error, QA_ERROR_FORMAT, 20480, "Invalid Q3 font header");
    for (size_t i = 0; i < QA_Q3_FONT_GLYPHS; ++i) {
        const qa_q3_glyph_record *glyph = &record->glyphs[i];
        if (glyph->height < 0 || glyph->pitch < 0 || glyph->image_width < 0 ||
            glyph->image_height < 0 || !isfinite(glyph->s) || !isfinite(glyph->t) ||
            !isfinite(glyph->s2) || !isfinite(glyph->t2) ||
            !terminated(glyph->shader_name, sizeof(glyph->shader_name)))
            return qa_font_fail(error, QA_ERROR_FORMAT, i * 80, "Invalid Q3 font glyph record");
    }
    return true;
}

bool qa_q3_font_record_decode(qa_bytes input, qa_q3_font_record *out, qa_error *error) {
    if (!out || !input.data || input.size != QA_Q3_FONT_RECORD_BYTES)
        return qa_font_fail(error, QA_ERROR_FORMAT, 0, "Q3 font record must contain 20548 bytes");
    qa_q3_font_record record = {0};
    for (size_t i = 0; i < QA_Q3_FONT_GLYPHS; ++i) {
        size_t at = i * 80;
        qa_q3_glyph_record *glyph = &record.glyphs[i];
        glyph->height = (int32_t)qa_load_u32le(input.data + at + 0);
        glyph->top = (int32_t)qa_load_u32le(input.data + at + 4);
        glyph->bottom = (int32_t)qa_load_u32le(input.data + at + 8);
        glyph->pitch = (int32_t)qa_load_u32le(input.data + at + 12);
        glyph->x_skip = (int32_t)qa_load_u32le(input.data + at + 16);
        glyph->image_width = (int32_t)qa_load_u32le(input.data + at + 20);
        glyph->image_height = (int32_t)qa_load_u32le(input.data + at + 24);
        glyph->s = qa_load_f32le(input.data + at + 28);
        glyph->t = qa_load_f32le(input.data + at + 32);
        glyph->s2 = qa_load_f32le(input.data + at + 36);
        glyph->t2 = qa_load_f32le(input.data + at + 40);
        glyph->handle = (int32_t)qa_load_u32le(input.data + at + 44);
        memcpy(glyph->shader_name, input.data + at + 48, 32);
    }
    record.glyph_scale = qa_load_f32le(input.data + 20480);
    memcpy(record.name, input.data + 20484, 64);
    if (!validate_record(&record, error))
        return false;
    *out = record;
    return true;
}

bool qa_q3_font_record_encode(const qa_q3_font_record *record, uint8_t out[QA_Q3_FONT_RECORD_BYTES],
                              qa_error *error) {
    if (!out || !validate_record(record, error))
        return false;
    memset(out, 0, QA_Q3_FONT_RECORD_BYTES);
    for (size_t i = 0; i < QA_Q3_FONT_GLYPHS; ++i) {
        size_t at = i * 80;
        const qa_q3_glyph_record *glyph = &record->glyphs[i];
        qa_store_u32le(out + at + 0, (uint32_t)glyph->height);
        qa_store_u32le(out + at + 4, (uint32_t)glyph->top);
        qa_store_u32le(out + at + 8, (uint32_t)glyph->bottom);
        qa_store_u32le(out + at + 12, (uint32_t)glyph->pitch);
        qa_store_u32le(out + at + 16, (uint32_t)glyph->x_skip);
        qa_store_u32le(out + at + 20, (uint32_t)glyph->image_width);
        qa_store_u32le(out + at + 24, (uint32_t)glyph->image_height);
        qa_store_f32le(out + at + 28, glyph->s);
        qa_store_f32le(out + at + 32, glyph->t);
        qa_store_f32le(out + at + 36, glyph->s2);
        qa_store_f32le(out + at + 40, glyph->t2);
        qa_store_u32le(out + at + 44, (uint32_t)glyph->handle);
        memcpy(out + at + 48, glyph->shader_name, 32);
    }
    qa_store_f32le(out + 20480, record->glyph_scale);
    memcpy(out + 20484, record->name, 64);
    return true;
}

static void set_name(char *destination, size_t capacity, const char *source) {
    memset(destination, 0, capacity);
    if (!source || !capacity)
        return;
    size_t length = strlen(source);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(destination, source, length);
}

static bool add_record_glyph(qa_font *font, uint32_t codepoint, const qa_q3_glyph_record *record,
                             const qa_scene_image *image, qa_error *error) {
    float scale = font->q3_record.glyph_scale;
    qa_font_glyph glyph = {
        .codepoint = codepoint,
        .image = image,
        .uv = {record->s, record->t, record->s2, record->t2},
        .width = (float)record->image_width * scale,
        .height = (float)record->image_height * scale,
        .advance = (float)record->x_skip * scale,
        .bearing_y = (float)record->top * scale,
        .visible = codepoint != 32u && image && record->image_width > 0 && record->image_height > 0,
    };
    return qa_font_internal_add_glyph(font, glyph, error);
}

static bool publish_record(qa_font *font, const char *cache_name, bool load_images,
                           const qa_font **out, qa_error *error) {
    set_name(font->q3_record.name, sizeof(font->q3_record.name), cache_name);
    float ascent = 0, descent = 0, tallest = 0;
    for (uint32_t code = 0; code < QA_Q3_FONT_GLYPHS; ++code) {
        qa_q3_glyph_record *record = &font->q3_record.glyphs[code];
        const qa_scene_image *image = NULL;
        if (load_images && code < 255u && record->shader_name[0]) {
            if (!qa_font_internal_picture(font, record->shader_name, QA_GAME_Q3, QA_SCENE_LINEAR,
                                          &image, error))
                return false;
        }
        if (!add_record_glyph(font, code, record, image, error))
            return false;
        float scale = font->q3_record.glyph_scale;
        if ((float)record->top * scale > ascent)
            ascent = (float)record->top * scale;
        float below = ((float)record->height - (float)record->top) * scale;
        if (below > descent)
            descent = below;
        if ((float)record->height * scale > tallest)
            tallest = (float)record->height * scale;
    }
    font->ascent = ascent > 0 ? ascent : tallest;
    font->descent = descent > 0 ? descent : 0;
    font->line_height = font->ascent + font->descent;
    if (!(font->line_height > 0))
        font->line_height = tallest > 0 ? tallest : 1;
    font->has_q3_record = true;
    return qa_font_internal_publish(font, out, error);
}

static bool load_dat(qa_font_library *library, const char *cache_name, qa_resource *resource,
                     const qa_font **out, qa_error *error) {
    qa_q3_font_record record;
    if (!qa_q3_font_record_decode(qa_resource_bytes(resource), &record, error)) {
        qa_resource_release(resource);
        return false;
    }
    qa_font *font = qa_font_internal_create(library, QA_FONT_Q3, cache_name, error);
    if (!font) {
        qa_resource_release(resource);
        return false;
    }
    if (!qa_font_internal_take_source(font, resource, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    font->q3_record = record;
    if (!publish_record(font, cache_name, true, out, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    return true;
}

typedef struct q3_bitmap {
    uint8_t *pixels;
    int32_t width, height, pitch, top, bottom, x_skip;
} q3_bitmap;

static int64_t floor_26_6(int64_t value) {
    int64_t quotient = value / 64, remainder = value % 64;
    if (remainder < 0)
        --quotient;
    return quotient * 64;
}

static int64_t ceil_26_6(int64_t value) {
    int64_t quotient = value / 64, remainder = value % 64;
    if (remainder > 0)
        ++quotient;
    return quotient * 64;
}

static void q3_bitmap_free(q3_bitmap *bitmap) {
    free(bitmap->pixels);
    *bitmap = (q3_bitmap){0};
}

static bool render_q3_glyph(FT_Library library, FT_Face face, uint32_t code, q3_bitmap *out,
                            qa_error *error) {
    FT_UInt index = FT_Get_Char_Index(face, code);
    if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT))
        return qa_font_fail(error, QA_ERROR_FORMAT, code, "FreeType cannot load Q3 glyph");
    FT_GlyphSlot slot = face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE)
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, code,
                            "Q3 font generation requires outline glyphs");
    int64_t bearing_x = slot->metrics.horiBearingX;
    int64_t bearing_y = slot->metrics.horiBearingY;
    int64_t metric_width = slot->metrics.width;
    int64_t metric_height = slot->metrics.height;
    if ((metric_width > 0 && bearing_x > INT64_MAX - metric_width) ||
        (metric_width < 0 && bearing_x < INT64_MIN - metric_width) ||
        (metric_height < 0 && bearing_y > INT64_MAX + metric_height) ||
        (metric_height > 0 && bearing_y < INT64_MIN + metric_height))
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, code,
                            "Q3 glyph metric arithmetic overflows");
    int64_t left = floor_26_6(bearing_x);
    int64_t right = ceil_26_6(bearing_x + metric_width);
    int64_t top = ceil_26_6(bearing_y);
    int64_t bottom = floor_26_6(bearing_y - metric_height);
    if (left < INT32_MIN || left > INT32_MAX || right < INT32_MIN || right > INT32_MAX ||
        top < INT32_MIN || top > INT32_MAX || bottom < INT32_MIN || bottom > INT32_MAX)
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, code,
                            "Q3 outline coordinates exceed fontInfo storage");
    int64_t width = (right - left) / 64;
    int64_t height = (top - bottom) / 64;
    int64_t pitch = (width + 3) & ~INT64_C(3);
    int64_t glyph_top = floor_26_6(bearing_y) / 64 + 1;
    int64_t x_skip = floor_26_6(slot->metrics.horiAdvance) / 64 + 1;
    if (width < 0 || height < 0 || pitch < 0 || width > INT32_MAX || height > INT32_MAX ||
        pitch > INT32_MAX || glyph_top < INT32_MIN || glyph_top > INT32_MAX || x_skip < 0 ||
        x_skip > INT32_MAX || (height && (uint64_t)pitch > SIZE_MAX / (uint64_t)height))
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, code,
                            "Q3 glyph metrics exceed native storage");
    size_t bytes = (size_t)pitch * (size_t)height;
    uint8_t *pixels = bytes ? calloc(bytes, 1) : NULL;
    if (bytes && !pixels)
        return qa_font_fail(error, QA_ERROR_MEMORY, code, "Allocating Q3 glyph bitmap");
    if (bytes) {
        FT_Bitmap target = {0};
        target.rows = (unsigned)height;
        target.width = (unsigned)width;
        target.pitch = (int)pitch;
        target.buffer = pixels;
        target.num_grays = 256;
        target.pixel_mode = FT_PIXEL_MODE_GRAY;
        FT_Outline_Translate(&slot->outline, (FT_Pos)-left, (FT_Pos)-bottom);
        if (FT_Outline_Get_Bitmap(library, &slot->outline, &target)) {
            free(pixels);
            return qa_font_fail(error, QA_ERROR_FORMAT, code,
                                "FreeType cannot rasterize Q3 outline");
        }
    }
    *out = (q3_bitmap){pixels,         (int32_t)width,     (int32_t)height,
                       (int32_t)pitch, (int32_t)glyph_top, (int32_t)bottom,
                       (int32_t)x_skip};
    return true;
}

static bool q3_page(qa_font *font, int32_t point_size, uint32_t page, uint32_t first, uint32_t end,
                    const uint8_t gray[65536], const qa_scene_image **glyph_images,
                    qa_error *error) {
    uint8_t maximum = 0;
    for (size_t i = 0; i < 65536; ++i) {
        if (gray[i] > maximum)
            maximum = gray[i];
    }
    uint8_t *rgba = malloc(65536u * 4u);
    if (!rgba)
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating generated Q3 atlas");
    for (size_t i = 0; i < 65536; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = (uint8_t)(maximum ? (uint32_t)gray[i] * 255u / maximum : 0);
    }
    char name[64];
    int written = snprintf(name, sizeof(name), "fonts/fontImage_%u_%d.tga", page, point_size);
    if (written < 0 || (size_t)written >= sizeof(name)) {
        free(rgba);
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "Generated Q3 atlas name exceeds MAX_QPATH");
    }
    qa_scene_image_level level = {256, 256, rgba, 65536u * 4u};
    qa_scene_image *image = NULL;
    bool success = qa_scene_image_create(font->library->resources, name, QA_SCENE_RGBA8, &level, 1,
                                         QA_SCENE_CLAMP, QA_SCENE_LINEAR,
                                         (qa_scene_vec4){0, 0, 0, 0}, &image, error);
    free(rgba);
    if (!success)
        return false;
    if (!qa_font_internal_take_image(font, image, error))
        return false;
    for (uint32_t code = first; code < end; ++code) {
        glyph_images[code] = image;
        set_name(font->q3_record.glyphs[code].shader_name,
                 sizeof(font->q3_record.glyphs[code].shader_name), name);
    }
    return true;
}

static bool generate_q3(qa_font_library *library, const char *cache_name, const char *path,
                        int32_t point_size, const qa_font **out, qa_error *error) {
    if (!path || !path[0])
        return qa_font_fail(error, QA_ERROR_NOT_FOUND, 0,
                            "Q3 font DAT is absent and no TrueType source was supplied");
#if LONG_MAX / 64 < INT32_MAX
    if (point_size > LONG_MAX / 64)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "Q3 point size exceeds FreeType 26.6 range");
#endif
    qa_resource *source = NULL;
    if (!qa_vfs_acquire(library->vfs, path, &source, NULL, error))
        return false;
    qa_font *font = qa_font_internal_create(library, QA_FONT_Q3, cache_name, error);
    if (!font) {
        qa_resource_release(source);
        return false;
    }
    if (!qa_font_internal_take_source(font, source, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    font->q3_record.glyph_scale = 48.0f / (float)point_size;
    set_name(font->q3_record.name, sizeof(font->q3_record.name), cache_name);

    FT_Library freetype;
    FT_Face face = NULL;
    qa_bytes bytes = qa_resource_bytes(source);
    bool success = qa_font_internal_freetype(library, &freetype, error);
    if (success && (bytes.size > LONG_MAX ||
                    FT_New_Memory_Face(freetype, bytes.data, (FT_Long)bytes.size, 0, &face)))
        success = qa_font_fail(error, QA_ERROR_FORMAT, 0, "FreeType cannot open Q3 font face");
    if (success && FT_Select_Charmap(face, FT_ENCODING_UNICODE))
        success =
            qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 TrueType face has no Unicode charmap");
    if (success &&
        FT_Set_Char_Size(face, (FT_F26Dot6)point_size * 64, (FT_F26Dot6)point_size * 64, 72, 72))
        success =
            qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0, "FreeType cannot select Q3 point size");

    int32_t max_height = 0;
    for (uint32_t code = 0; success && code < 255u; ++code) {
        q3_bitmap bitmap = {0};
        success = render_q3_glyph(freetype, face, code, &bitmap, error);
        if (success && bitmap.height > max_height)
            max_height = bitmap.height;
        q3_bitmap_free(&bitmap);
    }
    if (success && (max_height <= 0 || max_height >= 254))
        success = qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0,
                               "Q3 glyph height cannot fit a 256 pixel atlas");

    uint8_t *gray = success ? calloc(65536, 1) : NULL;
    if (success && !gray)
        success = qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating Q3 grayscale atlas");
    const qa_scene_image *glyph_images[QA_Q3_FONT_GLYPHS] = {0};
    uint32_t x = 0, y = 0, page = 0, page_start = 0;
    for (uint32_t code = 0; success && code < 255u; ++code) {
        q3_bitmap bitmap = {0};
        success = render_q3_glyph(freetype, face, code, &bitmap, error);
        if (!success)
            break;
        if ((uint32_t)bitmap.pitch + 1u >= 255u) {
            success = qa_font_fail(error, QA_ERROR_UNSUPPORTED, code,
                                   "Q3 glyph width cannot fit a 256 pixel atlas");
            q3_bitmap_free(&bitmap);
            break;
        }
        if ((uint64_t)x + (uint32_t)bitmap.pitch + 1u >= 255u) {
            x = 0;
            y += (uint32_t)max_height + 1u;
        }
        if ((uint64_t)y + (uint32_t)max_height + 1u >= 255u) {
            success =
                q3_page(font, point_size, page++, page_start, code, gray, glyph_images, error);
            memset(gray, 0, 65536);
            x = y = 0;
            page_start = code;
        }
        if (!success) {
            q3_bitmap_free(&bitmap);
            break;
        }
        for (int32_t row = 0; row < bitmap.height; ++row)
            memcpy(gray + ((size_t)y + (size_t)row) * 256u + x,
                   bitmap.pixels + (size_t)row * (size_t)bitmap.pitch, (size_t)bitmap.pitch);
        qa_q3_glyph_record *record = &font->q3_record.glyphs[code];
        record->height = bitmap.height;
        record->top = bitmap.top;
        record->bottom = bitmap.bottom;
        record->pitch = bitmap.pitch;
        record->x_skip = bitmap.x_skip;
        record->image_width = bitmap.pitch;
        record->image_height = bitmap.height;
        record->s = (float)x / 256.0f;
        record->t = (float)y / 256.0f;
        record->s2 = (float)(x + (uint32_t)bitmap.pitch) / 256.0f;
        record->t2 = (float)(y + (uint32_t)bitmap.height) / 256.0f;
        x += (uint32_t)bitmap.pitch + 1u;
        q3_bitmap_free(&bitmap);
    }
    if (success)
        success = q3_page(font, point_size, page, page_start, 255u, gray, glyph_images, error);
    free(gray);
    if (face)
        FT_Done_Face(face);
    if (success)
        success = publish_record(font, cache_name, false, out, error);
    if (success) {
        for (size_t i = 0; i < font->glyph_count; ++i) {
            uint32_t code = font->glyphs[i].codepoint;
            font->glyphs[i].image = glyph_images[code];
            font->glyphs[i].visible = code != 32u && code < 255u && glyph_images[code] &&
                                      font->glyphs[i].width > 0 && font->glyphs[i].height > 0;
        }
    }
    if (!success) {
        qa_font_internal_destroy(font);
        return false;
    }
    return true;
}

bool qa_font_q3_register(qa_font_library *library, const qa_font_q3_options *options,
                         const qa_font **out, qa_error *error) {
    if (!library || !options || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 font registration");
    if (!qa_font_internal_admission_ready(library,error)) return false;
    int32_t point_size = options->point_size <= 0 ? 12 : options->point_size;
    char cache_name[64];
    int count = snprintf(cache_name, sizeof(cache_name), "fonts/fontImage_%d.dat", point_size);
    if (count < 0 || (size_t)count >= sizeof(cache_name))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 font point size is too large");
    for (size_t i = 0; i < library->font_count; ++i) {
        qa_font *font = library->fonts[i];
        if (font->kind == QA_FONT_Q3 && font->has_q3_record &&
            !strcasecmp(font->q3_record.name, cache_name)) {
            *out = font;
            return true;
        }
    }
    qa_resource *resource = NULL;
    qa_error acquired = {0};
    if (qa_vfs_acquire(library->vfs, cache_name, &resource, NULL, &acquired)) {
        if (qa_resource_bytes(resource).size == QA_Q3_FONT_RECORD_BYTES)
            return load_dat(library, cache_name, resource, out, error);
        qa_resource_release(resource);
    }
    if (!options->generate_if_missing) {
        if (acquired.code != QA_OK)
            qa_error_set(error, acquired.code, acquired.offset, "%s", acquired.message);
        else
            qa_font_fail(error, QA_ERROR_FORMAT, 0, "Q3 font DAT has the wrong size");
        return false;
    }
    return generate_q3(library, cache_name, options->truetype_path, point_size, out, error);
}

bool qa_font_q3_export(const qa_font *font, qa_font_image_handle image_handle, void *context,
                       uint8_t out[QA_Q3_FONT_RECORD_BYTES], qa_error *error) {
    if (!font || font->kind != QA_FONT_Q3 || !font->has_q3_record || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Font is not a registered Q3 font");
    if (!qa_font_internal_admission_ready(font->library,error)) return false;
    ++font->library->callbacks;
    qa_q3_font_record record = font->q3_record;
    for (uint32_t code = 0; code < QA_Q3_FONT_GLYPHS; ++code) {
        qa_font_glyph glyph;
        const qa_scene_image *image = qa_font_find_glyph(font, code, &glyph) ? glyph.image : NULL;
        record.glyphs[code].handle = image_handle && image ? image_handle(context, image) : 0;
    }
    bool ok=qa_q3_font_record_encode(&record, out, error);
    --font->library->callbacks; return ok;
}

#include "internal.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct token {
    const uint8_t *data;
    size_t size, offset;
} token;

typedef struct lexer {
    qa_bytes source;
    size_t at;
} lexer;

static bool whitespace(uint8_t c) { return c <= 32; }

static bool next_token(lexer *input, token *out, qa_error *error) {
    while (input->at < input->source.size) {
        uint8_t c = input->source.data[input->at];
        if (c == 0) {
            input->at = input->source.size;
            break;
        }
        if (whitespace(c)) {
            ++input->at;
            continue;
        }
        if (c == '/' && input->at + 1 < input->source.size &&
            input->source.data[input->at + 1] == '/') {
            input->at += 2;
            while (input->at < input->source.size && input->source.data[input->at] != '\n')
                ++input->at;
            continue;
        }
        break;
    }
    if (input->at == input->source.size) {
        *out = (token){0};
        return true;
    }
    size_t begin = input->at;
    uint8_t first = input->source.data[input->at++];
    if (first == '"') {
        begin = input->at;
        while (input->at < input->source.size && input->source.data[input->at] != '"' &&
               input->source.data[input->at] != 0)
            ++input->at;
        if (input->at == input->source.size || input->source.data[input->at] != '"')
            return qa_font_fail(error, QA_ERROR_FORMAT, begin - 1,
                                "Unterminated KFONT quoted token");
        *out = (token){input->source.data + begin, input->at - begin, begin};
        ++input->at;
        return true;
    }
    if (first == '{' || first == '}') {
        *out = (token){input->source.data + begin, 1, begin};
        return true;
    }
    while (input->at < input->source.size && !whitespace(input->source.data[input->at]) &&
           input->source.data[input->at] != '{' && input->source.data[input->at] != '}' &&
           input->source.data[input->at] != 0)
        ++input->at;
    *out = (token){input->source.data + begin, input->at - begin, begin};
    return true;
}

static bool token_is(token value, const char *literal) {
    size_t length = strlen(literal);
    return value.size == length && !memcmp(value.data, literal, length);
}

static bool token_string(token value, char **out, qa_error *error) {
    if (!value.size || value.size == SIZE_MAX)
        return qa_font_fail(error, QA_ERROR_FORMAT, value.offset, "Missing KFONT string token");
    char *copy = malloc(value.size + 1);
    if (!copy)
        return qa_font_fail(error, QA_ERROR_MEMORY, value.offset, "Allocating KFONT token");
    memcpy(copy, value.data, value.size);
    copy[value.size] = 0;
    *out = copy;
    return true;
}

static bool token_int(token value, int32_t *out, qa_error *error) {
    if (!value.size || value.size >= 64)
        return qa_font_fail(error, QA_ERROR_FORMAT, value.offset, "Invalid KFONT integer");
    char text[64];
    memcpy(text, value.data, value.size);
    text[value.size] = 0;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno || end != text + value.size || parsed < INT32_MIN || parsed > INT32_MAX)
        return qa_font_fail(error, QA_ERROR_FORMAT, value.offset, "Invalid KFONT integer");
    *out = (int32_t)parsed;
    return true;
}

static bool read_required(lexer *input, token *out, qa_error *error) {
    return next_token(input, out, error) && out->size != 0
               ? true
               : qa_font_fail(error, QA_ERROR_FORMAT, input->at, "Truncated KFONT directive");
}

static bool put_glyph(qa_font *font, qa_font_glyph glyph, qa_error *error) {
    for (size_t i = 0; i < font->glyph_count; ++i) {
        if (font->glyphs[i].codepoint == glyph.codepoint) {
            glyph.font = font;
            font->glyphs[i] = glyph;
            return true;
        }
    }
    return qa_font_internal_add_glyph(font, glyph, error);
}

bool qa_font_kfont_load(qa_font_library *library, const char *path, const qa_font **out,
                        qa_error *error) {
    if (!library || !path || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid KFONT load request");
    if (!qa_font_internal_admission_ready(library,error)) return false;
    for (size_t i = 0; i < library->font_count; ++i) {
        if (library->fonts[i]->kind == QA_FONT_KFONT && !strcmp(library->fonts[i]->name, path)) {
            *out = library->fonts[i];
            return true;
        }
    }
    qa_resource *descriptor = NULL;
    if (!qa_vfs_acquire(library->vfs, path, &descriptor, NULL, error))
        return false;
    qa_font *font = qa_font_internal_create(library, QA_FONT_KFONT, path, error);
    if (!font) {
        qa_resource_release(descriptor);
        return false;
    }
    if (!qa_font_internal_take_source(font, descriptor, error)) {
        qa_font_internal_destroy(font);
        return false;
    }

    lexer input = {qa_resource_bytes(descriptor), 0};
    char *texture_path = NULL;
    typedef struct mapped {
        uint32_t codepoint;
        int32_t x, y, width, height;
    } mapped;
    mapped *mapped_glyphs = NULL;
    size_t mapped_count = 0, mapped_capacity = 0;
    bool success = true;

    for (;;) {
        token directive;
        if (!next_token(&input, &directive, error)) {
            success = false;
            break;
        }
        if (!directive.size)
            break;
        if (token_is(directive, "texture")) {
            token value;
            char *replacement = NULL;
            if (!read_required(&input, &value, error) ||
                !token_string(value, &replacement, error)) {
                success = false;
                break;
            }
            free(texture_path);
            texture_path = replacement;
        } else if (token_is(directive, "mapchar")) {
            token brace;
            if (!read_required(&input, &brace, error) || !token_is(brace, "{")) {
                success = qa_font_fail(error, QA_ERROR_FORMAT, directive.offset,
                                       "KFONT mapchar requires an opening brace");
                break;
            }
            for (;;) {
                token fields[6];
                if (!read_required(&input, &fields[0], error)) {
                    success = false;
                    break;
                }
                if (token_is(fields[0], "}"))
                    break;
                for (size_t i = 1; i < 6; ++i) {
                    if (!read_required(&input, &fields[i], error)) {
                        success = false;
                        break;
                    }
                }
                if (!success)
                    break;
                int32_t values[5];
                for (size_t i = 0; i < 5; ++i) {
                    if (!token_int(fields[i], &values[i], error)) {
                        success = false;
                        break;
                    }
                }
                if (!success)
                    break;
                if (values[0] < 0 || !qa_font_valid_scalar((uint32_t)values[0]) || values[1] < 0 ||
                    values[2] < 0 || values[3] < 0 || values[4] < 0) {
                    success = qa_font_fail(error, QA_ERROR_FORMAT, fields[0].offset,
                                           "Invalid KFONT glyph rectangle");
                    break;
                }
                if (mapped_count == mapped_capacity) {
                    size_t next = mapped_capacity ? mapped_capacity * 2 : 128;
                    if (next < mapped_capacity || next > SIZE_MAX / sizeof(*mapped_glyphs)) {
                        success = qa_font_fail(error, QA_ERROR_MEMORY, fields[0].offset,
                                               "KFONT glyph count overflow");
                        break;
                    }
                    mapped *grown = realloc(mapped_glyphs, next * sizeof(*mapped_glyphs));
                    if (!grown) {
                        success = qa_font_fail(error, QA_ERROR_MEMORY, fields[0].offset,
                                               "Allocating KFONT glyphs");
                        break;
                    }
                    mapped_glyphs = grown;
                    mapped_capacity = next;
                }
                mapped_glyphs[mapped_count++] =
                    (mapped){(uint32_t)values[0], values[1], values[2], values[3], values[4]};
            }
            if (!success)
                break;
        }
    }

    if (success && !texture_path)
        success = qa_font_fail(error, QA_ERROR_FORMAT, 0, "KFONT has no texture directive");
    qa_resource *texture_source = NULL;
    const qa_scene_image *image = NULL;
    if (success && !qa_vfs_acquire(library->vfs, texture_path, &texture_source, NULL, error))
        success = false;
    if (success && !qa_font_internal_take_source(font, texture_source, error))
        success = false;
    texture_source = NULL;
    if (success &&
        !qa_font_internal_picture(font, texture_path, QA_SCENE_Q2, QA_SCENE_LINEAR, &image, error))
        success = false;
    if (success) {
        uint32_t image_width = image->logical_width;
        uint32_t image_height = image->logical_height;
        if (!image_width || !image_height)
            success =
                qa_font_fail(error, QA_ERROR_FORMAT, 0, "KFONT texture has no drawable extent");
        for (size_t i = 0; i < mapped_count; ++i) {
            if (!success)
                break;
            mapped value = mapped_glyphs[i];
            if ((uint64_t)(uint32_t)value.x + (uint32_t)value.width > image_width ||
                (uint64_t)(uint32_t)value.y + (uint32_t)value.height > image_height) {
                success =
                    qa_font_fail(error, QA_ERROR_FORMAT, 0, "KFONT glyph exceeds its texture");
                break;
            }
            qa_font_glyph glyph = {
                .codepoint = value.codepoint,
                .image = image,
                .uv = {(float)value.x / image_width, (float)value.y / image_height,
                       (float)(value.x + value.width) / image_width,
                       (float)(value.y + value.height) / image_height},
                .width = (float)value.width,
                .height = (float)value.height,
                .advance = (float)value.width,
                .bearing_y = (float)value.height,
                .visible = value.codepoint != 32 && value.width > 0 && value.height > 0,
            };
            if (!put_glyph(font, glyph, error)) {
                success = false;
                break;
            }
            if ((float)value.height > font->line_height)
                font->line_height = (float)value.height;
        }
    }
    if (success && !(font->line_height > 0))
        success = qa_font_fail(error, QA_ERROR_FORMAT, 0, "KFONT has no drawable glyphs");
    font->ascent = font->line_height;
    font->descent = 0;
    if (success)
        qa_font_internal_measure_cap_ink(font);

    free(texture_path);
    free(mapped_glyphs);
    if (texture_source)
        qa_resource_release(texture_source);
    if (!success || !qa_font_internal_publish(font, out, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    return true;
}

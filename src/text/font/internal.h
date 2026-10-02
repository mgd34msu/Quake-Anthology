#ifndef QA_FONT_INTERNAL_H
#define QA_FONT_INTERNAL_H

#include "qa/font.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

struct qa_font {
    qa_font_library *library;
    qa_font_kind kind;
    char *name;
    float line_height, ascent, descent;
    float cap_top, cap_height;
    bool has_cap_ink;
    qa_font_glyph *glyphs;
    size_t glyph_count, glyph_capacity;
    const qa_scene_image **images;
    size_t image_count, image_capacity;
    qa_resource **sources;
    size_t source_count, source_capacity;
    uint64_t truetype_source;
    uint32_t truetype_pixel_size, truetype_atlas_width, truetype_atlas_height;
    uint32_t *truetype_coverage;
    size_t truetype_coverage_count;
    bool has_q3_record;
    qa_q3_font_record q3_record;
};

struct qa_font_library {
    qa_vfs *vfs;
    qa_scene_resources *resources;
    qa_font **fonts;
    size_t font_count, font_capacity;
    FT_Library freetype;
    struct qa_font_library_capture *capture;
    qa_font_resource_policy *policy;
    bool codec_active;
    unsigned callbacks;
};

bool qa_font_fail(qa_error *, qa_status, size_t, const char *);
char *qa_font_copy_string(const char *, qa_error *);
bool qa_font_valid_scalar(uint32_t);
qa_font *qa_font_internal_create(qa_font_library *, qa_font_kind, const char *, qa_error *);
void qa_font_internal_destroy(qa_font *);
bool qa_font_internal_add_glyph(qa_font *, qa_font_glyph, qa_error *);
bool qa_font_internal_take_image(qa_font *, const qa_scene_image *, qa_error *);
bool qa_font_internal_take_source(qa_font *, qa_resource *, qa_error *);
bool qa_font_internal_publish(qa_font *, const qa_font **, qa_error *);
void qa_font_internal_measure_cap_ink(qa_font *);
bool qa_font_internal_freetype(qa_font_library *, FT_Library *, qa_error *);
bool qa_font_internal_admission_ready(const qa_font_library *, qa_error *);
bool qa_font_internal_picture(qa_font *, const char *, qa_scene_family, qa_scene_filter,
                              const qa_scene_image **, qa_error *);

static inline uint32_t qa_font_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void qa_font_put_u32le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

#endif

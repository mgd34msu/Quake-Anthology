#ifndef QA_FONT_H
#define QA_FONT_H

#include "qa/scene.h"

#define QA_Q3_FONT_GLYPHS 256u
#define QA_Q3_FONT_RECORD_BYTES 20548u

typedef struct qa_font_library qa_font_library;
typedef struct qa_font qa_font;
typedef struct qa_font_world_store qa_font_world_store;
typedef struct qa_font_resource_policy qa_font_resource_policy;

typedef enum qa_font_kind {
    QA_FONT_CLASSIC,
    QA_FONT_KFONT,
    QA_FONT_TRUETYPE,
    QA_FONT_Q3,
    QA_FONT_ATLAS
} qa_font_kind;

typedef enum qa_font_color_policy { QA_FONT_TINTED, QA_FONT_BAKED_COLOR } qa_font_color_policy;

typedef struct qa_font_info {
    qa_font_kind kind;
    const char *name;
    float line_height, ascent, descent;
    /* Ink bounds for H/0..9 in the classic eight-unit coordinate system. */
    float cap_top, cap_height;
    bool has_cap_ink;
    size_t glyph_count;
} qa_font_info;

/* Images and strings are borrowed from the font library. Glyph dimensions and
 * bearings are source pixels before layout scaling. */
typedef struct qa_font_glyph {
    uint32_t codepoint;
    const qa_font *font;
    const qa_scene_image *image;
    qa_scene_vec4 uv;
    float width, height, advance, bearing_x, bearing_y;
    bool visible, baked_color;
} qa_font_glyph;

typedef struct qa_font_truetype_options {
    const char *path;
    const char *name;
    uint32_t pixel_size;
    /* Empty coverage enumerates the face's complete Unicode charmap. */
    const uint32_t *codepoints;
    size_t codepoint_count;
    uint32_t atlas_width, atlas_height;
} qa_font_truetype_options;

typedef struct qa_q3_glyph_record {
    int32_t height, top, bottom, pitch, x_skip, image_width, image_height;
    float s, t, s2, t2;
    int32_t handle;
    char shader_name[32];
} qa_q3_glyph_record;

typedef struct qa_q3_font_record {
    qa_q3_glyph_record glyphs[QA_Q3_FONT_GLYPHS];
    float glyph_scale;
    char name[64];
} qa_q3_font_record;

typedef struct qa_font_q3_options {
    /* point_size <= 0 selects the source default of 12. */
    int32_t point_size;
    /* Used only when fonts/fontImage_<size>.dat is absent. */
    const char *truetype_path;
    bool generate_if_missing;
} qa_font_q3_options;

/* The library borrows the VFS and scene resources. It owns all returned fonts,
 * retained source bytes and atlas image references until destroy. Calls use one
 * owner thread unless the caller supplies external synchronization. */
qa_font_library *qa_font_library_create(qa_vfs *, qa_scene_resources *, qa_error *);
void qa_font_library_destroy(qa_font_library *);
/* The exact borrowed view supplied to this library's constructor. */
const qa_vfs *qa_font_library_content(const qa_font_library *);
/* Prepare real glyph/image/metric bindings in the resource bank's private
 * destination. Existing published font objects retain their identity. New
 * fonts may be loaded into destination before ready seals its roster. Keep
 * the resource bank held through font finish/abort; publish bank then font
 * state before publishing selections mapped to stable font pointers. */
bool qa_font_resource_policy_prepare(qa_font_library *,qa_scene_resource_policy *,
    qa_font_resource_policy **,qa_error *);
qa_font_library *qa_font_resource_policy_source(const qa_font_resource_policy *);
qa_font_library *qa_font_resource_policy_destination(const qa_font_resource_policy *);
bool qa_font_resource_policy_font(const qa_font_resource_policy *,const qa_font *,const qa_font **);
bool qa_font_resource_policy_ready(qa_font_resource_policy *,qa_error *);
bool qa_font_resource_policy_ready_is(const qa_font_resource_policy *);
void qa_font_resource_policy_publish(qa_font_resource_policy *);
bool qa_font_resource_policy_finish(qa_font_resource_policy **,qa_error *);
bool qa_font_resource_policy_abort(qa_font_resource_policy **,qa_error *);
bool qa_font_classic_create(qa_font_library *, const char *name, const qa_scene_image *,
                            qa_font_color_policy, const qa_font **out, qa_error *);
/* Explicit authored atlas metrics; the library retains the actual image and
 * copies glyph values. Each glyph belongs to that single immutable image. */
bool qa_font_atlas_create(qa_font_library *, const char *name, const qa_scene_image *,
    const qa_font_glyph *, size_t, float line_height, const qa_font **, qa_error *);
bool qa_font_kfont_load(qa_font_library *, const char *path, const qa_font **out, qa_error *);
bool qa_font_truetype_load(qa_font_library *, const qa_font_truetype_options *, const qa_font **out,
                           qa_error *);
bool qa_font_q3_register(qa_font_library *, const qa_font_q3_options *, const qa_font **out,
                         qa_error *);
bool qa_font_describe(const qa_font *, qa_font_info *out);
bool qa_font_find_glyph(const qa_font *, uint32_t codepoint, qa_font_glyph *out);

/* Q3 records are little-endian on the wire. Export rewrites only the renderer
 * handle field; image_handle may be NULL to write zero handles. */
typedef int32_t (*qa_font_image_handle)(void *, const qa_scene_image *);
bool qa_q3_font_record_decode(qa_bytes, qa_q3_font_record *, qa_error *);
bool qa_q3_font_record_encode(const qa_q3_font_record *, uint8_t out[QA_Q3_FONT_RECORD_BYTES],
                              qa_error *);
bool qa_font_q3_export(const qa_font *, qa_font_image_handle, void *,
                       uint8_t out[QA_Q3_FONT_RECORD_BYTES], qa_error *);

/* This POD is private to one presentation seat. Fonts remain shared. */
typedef struct qa_font_selection {
    uint32_t seat;
    const qa_font *classic;
    const qa_font *primary;
    /* The caller keeps this borrowed array immutable and alive with the seat. */
    const qa_font *const *fallbacks;
    size_t fallback_count;
} qa_font_selection;

bool qa_font_selection_init(qa_font_selection *, uint32_t seat, const qa_font *classic,
                            const qa_font *primary, const qa_font *const *fallbacks,
                            size_t fallback_count, qa_error *);
bool qa_font_resolve(const qa_font_selection *, uint32_t codepoint, bool alternate,
                     qa_font_glyph *out);

typedef enum qa_font_color_codes { QA_FONT_COLOR_LITERAL, QA_FONT_COLOR_Q3 } qa_font_color_codes;

typedef enum qa_font_alignment {
    QA_FONT_ALIGN_LEFT,
    QA_FONT_ALIGN_CENTER,
    QA_FONT_ALIGN_RIGHT
} qa_font_alignment;

typedef struct qa_font_layout_options {
    qa_bytes text;
    float scale;
    qa_scene_vec4 color;
    qa_font_color_codes color_codes;
    qa_font_alignment alignment;
    bool force_color, alternate;
    /* Zero max_width is unbounded; zero line_height derives from scale. */
    float max_width, line_height;
    /* Zero max_glyphs is unbounded; zero tab_columns selects four. */
    size_t max_glyphs;
    uint32_t tab_columns;
} qa_font_layout_options;

typedef struct qa_font_positioned_glyph {
    qa_font_glyph glyph;
    qa_scene_rect_f rect;
    qa_scene_vec4 color;
    size_t source_offset;
} qa_font_positioned_glyph;

typedef struct qa_font_line {
    float width, y;
    size_t first_glyph, glyph_count;
} qa_font_line;

typedef struct qa_font_layout {
    uint32_t seat;
    float width, height, line_height;
    const qa_font_positioned_glyph *glyphs;
    size_t glyph_count;
    const qa_font_line *lines;
    size_t line_count;
} qa_font_layout;

/* Results and all pointed-to arrays live until the supplied arena is reset. */
bool qa_font_layout_build(const qa_font_selection *, const qa_font_layout_options *, qa_arena *,
                          qa_font_layout *out, qa_error *);

typedef enum qa_font_coordinate_space {
    QA_FONT_PIXELS,
    QA_FONT_STRETCH_640,
    QA_FONT_BASE_UI_640,
    QA_FONT_TEAM_UI_640
} qa_font_coordinate_space;

typedef struct qa_font_draw_options {
    uint32_t seat;
    qa_scene_rect target;
    qa_vec2 origin;
    qa_font_coordinate_space space;
    float shadow_offset;
} qa_font_draw_options;

typedef struct qa_font_seat_scale {
    float console, status_bar, crosshair;
    uint32_t console_width, console_height;
} qa_font_seat_scale;

bool qa_font_draw_layout(qa_scene_frame *, const qa_font_layout *, const qa_font_draw_options *,
                         qa_error *);
bool qa_font_seat_scale_for(qa_scene_rect viewport, float console_scale, float status_bar_scale,
                            float crosshair_scale, qa_font_seat_scale *out, qa_error *);

typedef enum qa_font_world_orientation {
    QA_FONT_WORLD_BILLBOARD,
    QA_FONT_WORLD_FIXED
} qa_font_world_orientation;

typedef enum qa_font_world_source {
    QA_FONT_WORLD_CLASSIC,
    QA_FONT_WORLD_SELECTED
} qa_font_world_source;

typedef struct qa_font_world_text {
    qa_bytes text;
    qa_vec3 origin, angles;
    qa_scene_vec4 color;
    float cell_size, distance_cull_factor;
    qa_font_world_orientation orientation;
    qa_font_world_source font;
    bool has_distance_cull, depth_test;
    uint64_t content;
} qa_font_world_text;

typedef struct qa_font_world_snapshot {
    const qa_font_world_text *texts;
    size_t count;
} qa_font_world_snapshot;

qa_font_world_store *qa_font_world_store_create(qa_error *);
void qa_font_world_store_destroy(qa_font_world_store *);
void qa_font_world_store_clear(qa_font_world_store *);
/* lifetime_seconds == 0 keeps the entry through exactly one observed frame. */
bool qa_font_world_store_submit(qa_font_world_store *, const qa_font_world_text *,
                                double now_seconds, double lifetime_seconds, qa_error *);
/* Descriptors use arena storage; text bytes remain store-owned until mutation.
 */
bool qa_font_world_store_snapshot(qa_font_world_store *, double now_seconds, uint64_t frame,
                                  qa_arena *, qa_font_world_snapshot *out, qa_error *);
/* Append world text after qa_scene_frame_finish so a Q2 depth-fog pass is
 * already present while depth-tested text can still reuse the world depth. */
bool qa_font_world_draw(qa_scene_frame *, const qa_scene_view *, const qa_font_world_snapshot *,
                        const qa_font_selection *, float distance_cull_override,
                        bool has_distance_cull_override, qa_error *);

#endif

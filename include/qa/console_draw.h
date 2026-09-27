#ifndef QA_CONSOLE_DRAW_H
#define QA_CONSOLE_DRAW_H

#include "qa/console_buffer.h"
#include "qa/console_discovery.h"
#include "qa/field.h"
#include "qa/font.h"

typedef struct qa_console_metrics_options {
    uint32_t width, height;
    float pixel_ratio, requested_scale;
} qa_console_metrics_options;

typedef struct qa_console_metrics {
    float scale, line_height, cell_width;
    size_t columns;
} qa_console_metrics;

typedef struct qa_console_glyph_metrics {
    float line_height, top;
} qa_console_glyph_metrics;

bool qa_console_cell_width(const qa_font_selection *, float line_height, float *out, qa_error *);
bool qa_console_metrics_calculate(const qa_console_metrics_options *, const qa_font_selection *,
                                  qa_console_metrics *out, qa_error *);
bool qa_console_glyph_measure(const qa_font_selection *, uint32_t scalar, float line_height,
                              float cell_width, qa_console_glyph_metrics *out, qa_error *);

typedef struct qa_console_draw_options {
    qa_scene_rect target;
    const qa_font_selection *font;
    const qa_console_buffer *buffer;
    const qa_field_view *field;
    const qa_console_discovery_entry *selected;
    const qa_scene_image *background;
    double now_milliseconds;
    /* Height is the animated visible extent in target pixels. */
    float height, scale;
    /* Zero derives the fixed grid width from the selected font. */
    float cell_width;
} qa_console_draw_options;

/* The buffer, field, discovery strings, font selection and background remain
 * caller-owned. Only visible row descriptors are queried; cell spans are drawn
 * in place. Temporary help text uses the frame arena. */
bool qa_console_draw(qa_scene_frame *, const qa_console_draw_options *, qa_error *);

#endif

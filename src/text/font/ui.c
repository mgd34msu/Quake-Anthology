#include "internal.h"

#include <math.h>

typedef struct transform_2d {
    float scale_x, scale_y, bias_x;
} transform_2d;

static bool transform_for(const qa_font_draw_options *options, transform_2d *out, qa_error *error) {
    if (!options || !out || options->target.width == 0 || options->target.height == 0 ||
        options->space < QA_FONT_PIXELS || options->space > QA_FONT_TEAM_UI_640 ||
        !isfinite(options->origin.x) || !isfinite(options->origin.y) ||
        !isfinite(options->shadow_offset) || options->shadow_offset < 0)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid text draw target");
    float width = (float)options->target.width;
    float height = (float)options->target.height;
    transform_2d transform = {1, 1, 0};
    switch (options->space) {
    case QA_FONT_PIXELS:
        break;
    case QA_FONT_STRETCH_640:
        transform.scale_x = width / 640.0f;
        transform.scale_y = height / 480.0f;
        break;
    case QA_FONT_BASE_UI_640:
        transform.scale_x = transform.scale_y = height / 480.0f;
        if ((double)options->target.width * 480.0 > (double)options->target.height * 640.0)
            transform.bias_x = 0.5f * (width - height * (640.0f / 480.0f));
        break;
    case QA_FONT_TEAM_UI_640:
        transform.scale_x = width / 640.0f;
        transform.scale_y = height / 480.0f;
        break;
    }
    *out = transform;
    return true;
}

static bool draw_pass(qa_scene_frame *frame, const qa_font_layout *layout,
                      const qa_font_draw_options *options, transform_2d transform, bool shadow,
                      qa_error *error) {
    float offset = shadow ? options->shadow_offset : 0;
    for (size_t i = 0; i < layout->glyph_count; ++i) {
        const qa_font_positioned_glyph *positioned = &layout->glyphs[i];
        if (!positioned->glyph.visible || !positioned->glyph.image || positioned->rect.width <= 0 ||
            positioned->rect.height <= 0)
            continue;
        qa_scene_rect_f rect = {
            options->target.x + transform.bias_x +
                (options->origin.x + positioned->rect.x + offset) * transform.scale_x,
            options->target.y +
                (options->origin.y + positioned->rect.y + offset) * transform.scale_y,
            positioned->rect.width * transform.scale_x,
            positioned->rect.height * transform.scale_y,
        };
        qa_scene_vec4 color =
            shadow ? (qa_scene_vec4){0, 0, 0, positioned->color.w} : positioned->color;
        if (!qa_scene_frame_picture_f(frame, positioned->glyph.image, options->target, rect,
                                      positioned->glyph.uv, color, error))
            return false;
    }
    return true;
}

bool qa_font_draw_layout(qa_scene_frame *frame, const qa_font_layout *layout,
                         const qa_font_draw_options *options, qa_error *error) {
    if (!frame || !layout || !options || layout->seat != options->seat ||
        (layout->glyph_count && !layout->glyphs) || (layout->line_count && !layout->lines))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Text layout belongs to a different seat");
    transform_2d transform;
    if (!transform_for(options, &transform, error))
        return false;
    if (options->shadow_offset > 0 && !draw_pass(frame, layout, options, transform, true, error))
        return false;
    return draw_pass(frame, layout, options, transform, false, error);
}

bool qa_font_seat_scale_for(qa_scene_rect viewport, float console_scale, float status_bar_scale,
                            float crosshair_scale, qa_font_seat_scale *out, qa_error *error) {
    if (!out || viewport.width == 0 || viewport.height == 0 || !isfinite(console_scale) ||
        !isfinite(status_bar_scale) || !isfinite(crosshair_scale))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid seat text scale request");
    double automatic = fmax(1.0, floor((double)viewport.height / 300.0));
    double requested = console_scale > 0 ? console_scale : automatic;
    double widest = fmax(320.0, (double)viewport.width);
    double logical = fmax(320.0, fmin((double)viewport.width / requested, widest));
    uint32_t console_width = (uint32_t)fmin(floor(logical), (double)UINT32_MAX) & ~7u;
    if (console_width < 8u)
        console_width = 8u;
    double logical_height = round((double)console_width * viewport.height / viewport.width);
    uint32_t console_height = (uint32_t)fmin(logical_height, (double)UINT32_MAX);
    float fit = fmaxf(1.0f, fminf(floorf((float)viewport.width / 320.0f),
                                  floorf((float)viewport.height / 144.0f)));
    float status = status_bar_scale > 0 ? fmaxf(1.0f, fminf(status_bar_scale, fit)) : fit;
    *out = (qa_font_seat_scale){
        .console = (float)viewport.width / console_width,
        .status_bar = status,
        .crosshair = fmaxf(1.0f, fminf(crosshair_scale, 10.0f)),
        .console_width = console_width,
        .console_height = console_height,
    };
    return true;
}

#include "internal.h"
#include <stdio.h>

static qa_scene_rect clip_rect(qa_ui *ui, qa_scene_rect target, qa_scene_rect_f logical) {
    double x = floor(ui->bias_x + logical.x * ui->scale);
    double y = floor(ui->bias_y + logical.y * ui->scale);
    double right = ceil(ui->bias_x + (logical.x + logical.width) * ui->scale);
    double bottom = ceil(ui->bias_y + (logical.y + logical.height) * ui->scale);
    x = fmax(target.x, fmin(x, (double)target.x + target.width));
    y = fmax(target.y, fmin(y, (double)target.y + target.height));
    right = fmax(target.x, fmin(right, (double)target.x + target.width));
    bottom = fmax(target.y, fmin(bottom, (double)target.y + target.height));
    return (qa_scene_rect){(int32_t)x, (int32_t)y,
        (uint32_t)fmax(0, right - x), (uint32_t)fmax(0, bottom - y)};
}
bool ui_fill(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target, qa_scene_rect_f rect,
              qa_scene_vec4 color, qa_error *error) {
    if (!target.width || !target.height)
        return true;
    qa_scene_rect_f pixels = {ui->bias_x + rect.x * ui->scale, ui->bias_y + rect.y * ui->scale,
                             rect.width * ui->scale, rect.height * ui->scale};
    return qa_scene_frame_picture_f(frame, ui->options.white, target, pixels,
                                     (qa_scene_vec4){0, 0, 1, 1}, color, error);
}
static bool text_layout(qa_ui *ui, qa_scene_frame *frame, const char *text,
                         qa_scene_vec4 color, float scale, qa_font_alignment alignment,
                         bool literal, bool localize, qa_font_layout *layout, qa_error *error) {
    *layout = (qa_font_layout){.seat = ui->options.seat};
    if (!text || !*text)
        return true;
    if (localize && ui->options.localize)
        text = ui->options.localize(ui->options.context, text);
    if (!text)
        return true;
    qa_font_layout_options options = {.text = {(const uint8_t *)text, strlen(text)},
        .scale = scale * ui->scale * ui->text_scale, .color = color,
        .color_codes = literal ? QA_FONT_COLOR_LITERAL : QA_FONT_COLOR_Q3,
        .force_color = ui->color_mode != QA_UI_COLOR_STANDARD, .alignment = alignment};
    if (!qa_font_layout_build(&ui->options.fonts, &options, &frame->storage, layout, error))
        return false;
    if (alignment!=QA_FONT_ALIGN_LEFT) {
        qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)layout->glyphs;
        for (size_t row=0;row<layout->line_count;++row) {
            const qa_font_line *line=layout->lines+row;
            float offset=line->width*(alignment==QA_FONT_ALIGN_CENTER?.5f:1);
            for (size_t i=0;i<line->glyph_count;++i) glyphs[line->first_glyph+i].rect.x-=offset;
        }
    }
    return true;
}
static bool draw_layout(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                         float x, float y, const qa_font_layout *layout, qa_error *error) {
    if (!layout->glyph_count || !target.width || !target.height)
        return true;
    qa_font_draw_options draw = {.seat = ui->options.seat, .target = target,
        .origin = {ui->bias_x + x * ui->scale - (float)target.x,
                   ui->bias_y + y * ui->scale - (float)target.y},
        .space = QA_FONT_PIXELS, .shadow_offset = 1};
    return qa_font_draw_layout(frame, layout, &draw, error);
}
static bool draw_text(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target, float x, float y,
                       const char *text, qa_scene_vec4 color, float scale,
                       qa_font_alignment alignment, bool literal, bool localize, qa_error *error) {
    if (!text || !*text || !target.width || !target.height)
        return true;
    qa_font_layout layout;
    return text_layout(ui, frame, text, color, scale, alignment, literal, localize, &layout, error) &&
        draw_layout(ui, frame, target, x, y, &layout, error);
}
bool ui_draw_text(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target, float x, float y,
                   const char *text, qa_scene_vec4 color, float scale,
                   qa_font_alignment alignment, qa_error *error) {
    return draw_text(ui, frame, target, x, y, text, color, scale, alignment, true, true, error);
}
bool ui_draw_source_text(qa_ui *ui,qa_scene_frame *frame,qa_scene_rect target,float x,float y,
    const char *text,qa_scene_vec4 color,float scale,qa_font_alignment alignment,qa_error *error)
{ return draw_text(ui,frame,target,x,y,text,color,scale,alignment,true,false,error); }
static bool field_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                        const qa_ui_control *control, bool focused,
                        qa_scene_vec4 color, qa_error *error) {
    const char *text = control->value.field.text ? control->value.field.text : "";
    ui_field *state = ui_field_get(ui, control->id, error);
    if (!state) return false;
    qa_bytes input = {(const uint8_t *)text, strlen(text)};
    typedef struct field_cell { uint32_t scalar; float width; } field_cell;
    if (input.size > SIZE_MAX / sizeof(field_cell) || input.size > (SIZE_MAX - 1) / 3)
        return ui_fail(error, "UI field drawing size overflow");
    field_cell *cells = input.size ? qa_arena_alloc(&frame->storage,
        input.size * sizeof(*cells), _Alignof(field_cell), error) : NULL;
    if (input.size && !cells) return false;
    size_t offset = 0, count = 0;
    uint32_t scalar;
    while (qa_utf8_next(input, &offset, &scalar)) {
        if (control->value.field.masked) scalar = '*';
        cells[count++] = (field_cell){scalar, ui_glyph_width(ui, scalar)};
    }
    if (state->cursor > count) state->cursor = count;
    if (state->top > state->cursor) state->top = state->cursor;
    float available = fmaxf(0, control->rect.width * .5f - 12), width = 0;
    for (size_t i = state->top; i < state->cursor; ++i) width += cells[i].width;
    while (state->top < state->cursor && width > available) width -= cells[state->top++].width;
    size_t end = state->cursor;
    float total = width;
    while (end < count && total + cells[end].width <= available) total += cells[end++].width;
    char *display = qa_arena_alloc(&frame->storage, input.size * 3 + 1, 1, error);
    if (!display) return false;
    size_t size = 0;
    for (size_t i = state->top; i < end; ++i) {
        char encoded[4]; size_t bytes = qa_utf8_encode(cells[i].scalar, encoded);
        memcpy(display + size, encoded, bytes); size += bytes;
    }
    display[size] = 0;
    float x = control->rect.x + control->rect.width * .5f;
    if (!ui_draw_text(ui, frame, target, control->rect.x + 10, control->rect.y + 6,
        control->label, color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_LEFT, error) ||
        !draw_text(ui, frame, target, x, control->rect.y + 6, display, color, UI_MENU_FONT_SCALE,
            QA_FONT_ALIGN_LEFT, true, false, error)) return false;
    return !focused || fmod(floor(ui->time_ms / 256), 2) != 0 || draw_text(ui, frame, target, x + width, control->rect.y + 6,
        state->overstrike ? "_" : "|", color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_LEFT, true, false, error);
}
static void layout_scale(qa_font_layout *layout, float factor) {
    qa_font_positioned_glyph *glyphs = (qa_font_positioned_glyph *)layout->glyphs;
    for (size_t i = 0; i < layout->glyph_count; ++i) {
        glyphs[i].rect.x *= factor; glyphs[i].rect.y *= factor;
        glyphs[i].rect.width *= factor; glyphs[i].rect.height *= factor;
    }
    qa_font_line *lines = (qa_font_line *)layout->lines;
    for (size_t i = 0; i < layout->line_count; ++i) {
        lines[i].width *= factor; lines[i].y *= factor;
    }
    layout->width *= factor; layout->height *= factor; layout->line_height *= factor;
}
static bool column_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                        const char *value, float x, float y, float width, float height,
                        qa_scene_vec4 color, qa_error *error) {
    value = value ? value : "";
    float available = fmaxf(0, width - 16);
    qa_font_layout layout;
    if (!text_layout(ui, frame, value, color, UI_MENU_FONT_SCALE,
        QA_FONT_ALIGN_LEFT, true, false, &layout, error)) return false;
    float factor = fmaxf(.75f, fminf(1, available / fmaxf(1, layout.width / ui->scale)));
    float scale = UI_MENU_FONT_SCALE * factor;
    if (factor < 1) layout_scale(&layout, factor);
    if (layout.width > available * ui->scale) {
        size_t length = strlen(value);
        if (length > (SIZE_MAX / sizeof(size_t)) - 1 || length > SIZE_MAX - 4)
            return ui_fail(error, "UI column text size overflow");
        size_t *ends = qa_arena_alloc(&frame->storage, (length + 1) * sizeof(*ends), _Alignof(size_t), error);
        char *shortened = qa_arena_alloc(&frame->storage, length + 4, 1, error);
        if (!ends || !shortened) return false;
        qa_bytes text = {(const uint8_t *)value, length};
        size_t offset = 0, count = 0; uint32_t scalar;
        ends[0] = 0;
        while (qa_utf8_next(text, &offset, &scalar)) ends[++count] = offset;
        size_t low = 0, high = count;
        while (low < high) {
            size_t middle = low + (high - low + 1) / 2;
            memcpy(shortened, value, ends[middle]);
            memcpy(shortened + ends[middle], "\xe2\x80\xa6", 4);
            if (!text_layout(ui, frame, shortened, color, scale,
                QA_FONT_ALIGN_LEFT, true, false, &layout, error)) return false;
            if (layout.width <= available * ui->scale) low = middle;
            else high = middle - 1;
        }
        memcpy(shortened, value, ends[low]);
        memcpy(shortened + ends[low], "\xe2\x80\xa6", 4);
        if (!text_layout(ui, frame, shortened, color, scale,
            QA_FONT_ALIGN_LEFT, true, false, &layout, error)) return false;
        if (layout.width > available * ui->scale) return true;
    }
    return draw_layout(ui, frame, clip_rect(ui, target, (qa_scene_rect_f){x, y, width, height}),
        x + 8, y + (height - 8 * scale * ui->text_scale) * .5f, &layout, error);
}
static bool list_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                       const qa_ui_control *control, qa_scene_vec4 color,
                       qa_scene_vec4 focused, qa_scene_vec4 accent, qa_scene_vec4 disabled,
                       qa_scene_vec4 background, bool active, qa_error *error) {
    ui_field *state = ui_list_state(ui, control, error);
    if (!state) return false;
    float height = fmaxf(1, control->value.list.row_height);
    size_t page = ui_list_page(control);
    size_t count = control->value.list.count, selected = control->value.list.selected;
    size_t maximum = count > page ? count - page : 0;
    float content_width = control->rect.width - (maximum ? 16 : 0);
    qa_scene_rect clipped = clip_rect(ui, target,
        (qa_scene_rect_f){control->rect.x, control->rect.y, content_width, control->rect.height});
    for (size_t i = state->top; i < count && i - state->top < page; ++i) {
        const qa_ui_row *row = &control->value.list.rows[i];
        float y = control->rect.y + (float)(i - state->top) * height;
        if (i == selected && !ui_fill(ui, frame, clipped,
            (qa_scene_rect_f){control->rect.x, y, content_width, height}, focused, error)) return false;
        qa_scene_vec4 text_color = !control->enabled || !row->enabled ? disabled : i == selected ? accent : color;
        if (row->cells) {
            if (control->value.list.column_count) {
                float x = control->rect.x;
                for (size_t column = 0; column < row->cell_count; ++column) {
                    float authored = column < control->value.list.column_count
                        ? control->value.list.column_widths[column] : content_width;
                    float width = fmaxf(0, fminf(authored, control->rect.x + content_width -
                        (row->action_label ? 28 : 0) - x));
                    if (!column_draw(ui, frame, clipped, row->cells[column], x, y, width, height, text_color, error)) return false;
                    x += width;
                }
            } else {
                size_t length = 0;
                for (size_t cell = 0; cell < row->cell_count; ++cell) {
                    size_t bytes = row->cells[cell] ? strlen(row->cells[cell]) : 0;
                    if (bytes > SIZE_MAX - length - 3) return ui_fail(error, "UI row text size overflow");
                    length += bytes + (cell ? 2 : 0);
                }
                char *joined = qa_arena_alloc(&frame->storage, length + 1, 1, error);
                if (!joined) return false;
                size_t offset = 0;
                for (size_t cell = 0; cell < row->cell_count; ++cell) {
                    const char *value = row->cells[cell] ? row->cells[cell] : "";
                    size_t bytes = strlen(value);
                    if (cell) { memcpy(joined + offset, "  ", 2); offset += 2; }
                    memcpy(joined + offset, value, bytes); offset += bytes;
                }
                joined[offset] = 0;
                if (!draw_text(ui, frame, clipped, control->rect.x + 8, y, joined,
                    text_color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_LEFT, true, false, error)) return false;
            }
            if (row->action_label && !ui_draw_text(ui, frame, clipped,
                control->rect.x + content_width - 14, y + (height - 8 * UI_MENU_FONT_SCALE * ui->text_scale) * .5f,
                row->action_label, text_color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_CENTER, error)) return false;
            continue;
        }
        float label_x = control->rect.x + 8;
        if (row->image) {
            qa_scene_rect_f pixels = {ui->bias_x + label_x * ui->scale,
                ui->bias_y + (y + 2) * ui->scale, (height - 4) * ui->scale, (height - 4) * ui->scale};
            if (!qa_scene_frame_picture_f(frame, row->image, clipped, pixels,
                (qa_scene_vec4){0, 0, 1, 1}, text_color, error)) return false;
            label_x += height;
        }
        if (!clipped.width || !clipped.height) continue;
        qa_font_layout label, detail;
        if (!text_layout(ui, frame, row->label, text_color, UI_MENU_FONT_SCALE,
                QA_FONT_ALIGN_LEFT, true, true, &label, error) ||
            !text_layout(ui, frame, row->detail, text_color, .75f * UI_MENU_FONT_SCALE,
                QA_FONT_ALIGN_RIGHT, true, true, &detail, error)) return false;
        float right = control->rect.x + content_width - 8;
        float available = fmaxf(0, right - label_x);
        float detail_width = fminf(detail.width / ui->scale, available * .5f);
        float label_width = fmaxf(0, available - detail_width - (detail_width > 0 ? 12 : 0));
        qa_scene_rect label_clip = clip_rect(ui, clipped,
            (qa_scene_rect_f){label_x, y, label_width, height});
        qa_scene_rect detail_clip = clip_rect(ui, clipped,
            (qa_scene_rect_f){right - detail_width, y, detail_width, height});
        if (!draw_layout(ui, frame, label_clip, label_x, y + 6, &label, error) ||
            !draw_layout(ui, frame, detail_clip, right, y + 6, &detail, error)) return false;
    }
    if (!maximum) return true;
    float thumb = fmaxf(24, control->rect.height * (float)page / (float)count);
    if (thumb > control->rect.height) thumb = control->rect.height;
    float x = control->rect.x + control->rect.width - 14;
    qa_scene_rect list_target = clip_rect(ui, target, control->rect);
    return ui_fill(ui, frame, list_target,
        (qa_scene_rect_f){x, control->rect.y, 12, control->rect.height}, background, error) &&
        ui_fill(ui, frame, list_target,
        (qa_scene_rect_f){x,
            control->rect.y + (control->rect.height - thumb) * (float)state->top / (float)maximum,
            12, thumb}, active ? accent : disabled, error);
}
typedef struct menu_colors {
    qa_scene_vec4 text, disabled, accent, control, focused, panel;
} menu_colors;
static menu_colors menu_palette(const qa_ui *ui, bool contrast, bool narrow) {
    menu_colors colors = {
        .text = {.9f, .92f, .94f, 1}, .disabled = {.48f, .5f, .53f, 1},
        .accent = {1, .73f, .35f, 1}, .control = {.06f, .08f, .1f, .83f},
        .focused = {.23f, .16f, .09f, .97f},
        .panel = {.025f, .035f, .045f, narrow ? .86f : .96f}
    };
    if (contrast) {
        colors.text = (qa_scene_vec4){1, 1, 1, 1};
        colors.disabled = (qa_scene_vec4){.65f, .65f, .65f, 1};
        colors.accent = (qa_scene_vec4){1, 1, 0, 1};
        colors.panel = colors.control = (qa_scene_vec4){0, 0, 0, 1};
        colors.focused = (qa_scene_vec4){.2f, .2f, .2f, 1};
    }
    if (ui->color_mode == QA_UI_COLOR_MONOCHROME) {
        colors.text = colors.accent = (qa_scene_vec4){1, 1, 1, 1};
        colors.focused = (qa_scene_vec4){.25f, .25f, .25f, 1};
    } else if (ui->color_mode == QA_UI_COLOR_BLUE_YELLOW) {
        colors.accent = (qa_scene_vec4){1, .9f, .2f, 1};
        colors.focused = (qa_scene_vec4){.06f, .2f, .42f, 1};
    }
    return colors;
}
static bool menu_backdrop(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect viewport,
                           qa_error *error) {
    const qa_scene_image *image = ui->options.art.main_background;
    if (!image) return true;
    float ratio = (float)viewport.width / (float)viewport.height;
    const float art_ratio = 1672.0f / 941.0f;
    float crop_x = ratio < art_ratio ? (1 - ratio / art_ratio) * .5f : 0;
    float crop_y = ratio > art_ratio ? (1 - art_ratio / ratio) * .5f : 0;
    return qa_scene_frame_picture_f(frame, image, viewport,
        (qa_scene_rect_f){(float)viewport.x, (float)viewport.y,
            (float)viewport.width, (float)viewport.height},
        (qa_scene_vec4){crop_x, crop_y, 1 - crop_x, 1 - crop_y},
        (qa_scene_vec4){1, 1, 1, 1}, error);
}
static bool menu_panel(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect viewport,
                       bool narrow, qa_scene_vec4 color, qa_error *error) {
    float fit = fminf((float)viewport.width / 640, (float)viewport.height / 480);
    float x = (float)viewport.x + ((float)viewport.width - 640 * fit) * .5f;
    float y = (float)viewport.y + ((float)viewport.height - 480 * fit) * .5f;
    return qa_scene_frame_picture_f(frame, ui->options.white, viewport,
        (qa_scene_rect_f){x + 40 * fit, y + 28 * fit, (narrow ? 264 : 560) * fit, 420 * fit},
        (qa_scene_vec4){0, 0, 1, 1}, color, error);
}
bool qa_ui_menu_text(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                       qa_scene_vec2 origin, const char *text,
                       const qa_ui_text_style *style, qa_error *error) {
    if (!ui || !ui->drawing || !frame || !style || !isfinite(origin.x) || !isfinite(origin.y) ||
        !isfinite(style->scale) || style->scale < 0 || !isfinite(style->fit_width) || style->fit_width < 0)
        return ui_fail(error, "Authored menu text requires its live draw owner and valid metrics");
    if (!text || !*text || !target.width || !target.height) return true;
    if (!style->source && ui->options.localize) text = ui->options.localize(ui->options.context, text);
    if (!text || !*text) return true;
    menu_colors colors = menu_palette(ui, ui->high_contrast, false);
    const qa_font_selection *fonts = style->heading && ui->options.title_fonts.classic
        ? &ui->options.title_fonts : &ui->options.fonts;
    qa_font_layout_options options = {.text = {(const uint8_t *)text, strlen(text)},
        .scale = (style->scale > 0 ? style->scale : 2.2f) * ui->scale,
        .color = style->accent ? colors.accent : colors.text,
        .color_codes = QA_FONT_COLOR_LITERAL, .alignment = QA_FONT_ALIGN_LEFT};
    qa_font_layout layout;
    if (!qa_font_layout_build(fonts, &options, &frame->storage, &layout, error)) return false;
    if (style->fit_width > 0 && layout.width > style->fit_width * ui->scale) {
        size_t length = strlen(text);
        if (length > (SIZE_MAX / sizeof(size_t)) - 1 || length > SIZE_MAX - 4)
            return ui_fail(error, "Authored menu text size overflow");
        size_t *ends = qa_arena_alloc(&frame->storage, (length + 1) * sizeof(*ends), _Alignof(size_t), error);
        char *fitted = qa_arena_alloc(&frame->storage, length + 4, 1, error);
        if (!ends || !fitted) return false;
        size_t offset = 0, count = 0; uint32_t scalar;
        ends[0] = 0;
        while (qa_utf8_next(options.text, &offset, &scalar)) ends[++count] = offset;
        size_t low = 0, high = count;
        while (low < high) {
            size_t middle = low + (high - low + 1) / 2;
            memcpy(fitted, text, ends[middle]); memcpy(fitted + ends[middle], "...", 4);
            options.text = (qa_bytes){(const uint8_t *)fitted, ends[middle] + 3};
            if (!qa_font_layout_build(fonts, &options, &frame->storage, &layout, error)) return false;
            if (layout.width <= style->fit_width * ui->scale) low = middle;
            else high = middle - 1;
        }
        memcpy(fitted, text, ends[low]); memcpy(fitted + ends[low], "...", 4);
        options.text = (qa_bytes){(const uint8_t *)fitted, ends[low] + 3};
        if (!qa_font_layout_build(fonts, &options, &frame->storage, &layout, error)) return false;
    }
    return draw_layout(ui, frame, target, origin.x, origin.y, &layout, error);
}
static bool menu_title(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                       const qa_ui_menu *menu, qa_error *error) {
    qa_ui_text_style style = {.scale = menu->narrow ? 6 : 4,
        .accent = true, .heading = true, .source = menu->source_title};
    return qa_ui_menu_text(ui, frame, target, (qa_scene_vec2){64, 44}, menu->title, &style, error);
}
static bool menu_text_controls(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                                const qa_ui_menu *menu, bool overlay, qa_error *error) {
    for (size_t i = 0; i < menu->count; ++i) {
        qa_ui_control control = ui_control(ui, menu, i);
        if (!control.visible || control.kind != QA_UI_TEXT || control.value.text.overlay != overlay) continue;
        qa_scene_rect clipped = control.scrolls && menu->scrollable
            ? clip_rect(ui, target, menu->scroll_rect) : target;
        if (!qa_ui_menu_text(ui, frame, clipped, (qa_scene_vec2){control.rect.x, control.rect.y},
            control.label, &control.value.text, error)) return false;
    }
    return true;
}
static bool slider_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                        const qa_ui_control *control, qa_scene_vec4 color,
                        qa_scene_vec4 disabled, qa_error *error) {
    char numeric[32];
    snprintf(numeric, sizeof(numeric), "%g", control->value.slider.value);
    const char *value = control->value.slider.label ? control->value.slider.label : numeric;
    float x = control->rect.x + control->rect.width * .6f;
    float width = control->rect.width * .32f, value_right = x - 12;
    qa_font_layout label, number;
    if (!text_layout(ui, frame, control->label, color, UI_MENU_FONT_SCALE,
            QA_FONT_ALIGN_LEFT, true, true, &label, error) ||
        !text_layout(ui, frame, value, color, UI_MENU_FONT_SCALE,
            QA_FONT_ALIGN_RIGHT, true, true, &number, error)) return false;
    float number_width = number.width / ui->scale;
    float value_factor = fminf(1, control->rect.width * .16f / fmaxf(1, number_width));
    float label_width = value_right - number_width * value_factor - 12 - (control->rect.x + 10);
    float label_factor = fminf(1, fmaxf(1, label_width) / fmaxf(1, label.width / ui->scale));
    layout_scale(&label, label_factor); layout_scale(&number, value_factor);
    if (!draw_layout(ui, frame, target, control->rect.x + 10, control->rect.y + 6, &label, error) ||
        !draw_layout(ui, frame, target, value_right, control->rect.y + 6, &number, error)) return false;
    double range = control->value.slider.maximum - control->value.slider.minimum;
    float position = range > 0 ? (float)fmax(0, fmin(1,
        (control->value.slider.value - control->value.slider.minimum) / range)) : 0;
    return ui_fill(ui, frame, target, (qa_scene_rect_f){x, control->rect.y + 13, width, 2}, disabled, error) &&
        ui_fill(ui, frame, target, (qa_scene_rect_f){x + width * position - 3,
            control->rect.y + 8, 6, 12}, color, error);
}
static bool draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect viewport, float scale,
                  bool contrast, bool startup, qa_error *error) {
    if (!ui || !frame || !viewport.width || !viewport.height || !isfinite(scale) || scale <= 0 ||
        (double)viewport.x + viewport.width > INT32_MAX ||
        (double)viewport.y + viewport.height > INT32_MAX)
        return ui_fail(error, "invalid UI draw target");
    ui->viewport = viewport;
    ui->high_contrast = contrast;
    ui->scale = fminf((float)viewport.width / 640, (float)viewport.height / 480) * scale;
    if (!isfinite(ui->scale) || ui->scale <= 0)
        return ui_fail(error, "UI canvas scale overflow");
    ui->bias_x = (float)viewport.x + ((float)viewport.width - 640 * ui->scale) * .5f;
    ui->bias_y = (float)viewport.y + ((float)viewport.height - 480 * ui->scale) * .5f;
    if (ui->has_pointer)
        ui->cursor = (qa_input_pair){(ui->pointer.x - ui->bias_x) / ui->scale,
                                     (ui->pointer.y - ui->bias_y) / ui->scale};
    qa_ui_menu menu;
    if (!ui_active(ui, &menu, error)) return false;
    if (!ui->depth) return true;
    menu_colors colors = menu_palette(ui, contrast, menu.narrow);
    if ((startup && !menu_backdrop(ui, frame, viewport, error)) ||
        !menu_panel(ui, frame, viewport, menu.narrow, colors.panel, error) ||
        !menu_title(ui, frame, viewport, &menu, error) ||
        !ui_fill(ui, frame, viewport, (qa_scene_rect_f){64, 104, menu.narrow ? 224 : 512, 1},
            (qa_scene_vec4){.6f, .39f, .18f, .65f}, error)) return false;
    if (!menu_text_controls(ui, frame, viewport, &menu, false, error)) return false;
    qa_ui_id focused = ui->stack[ui->depth - 1].control;
    for (size_t i = 0; i < menu.count; ++i) {
        qa_ui_control control = ui_control(ui, &menu, i);
        if (!control.visible || control.kind == QA_UI_TEXT) continue;
        qa_scene_rect target = control.scrolls && menu.scrollable
            ? clip_rect(ui, viewport, menu.scroll_rect) : viewport;
        bool selected = control.id == focused;
        qa_scene_vec4 color = !control.enabled ? colors.disabled : selected ? colors.accent : colors.text;
        if (control.kind == QA_UI_OWNER_DRAW) {
            if (control.value.owner.draw && !control.value.owner.draw(control.context,
                ui->options.seat, frame, target, control.rect, error)) return false;
            continue;
        }
        qa_scene_rect_f decoration = control.rect;
        decoration.height = fmaxf(0, decoration.height - 2);
        if (!ui_fill(ui, frame, target, decoration,
            selected && control.kind != QA_UI_LIST ? colors.focused : colors.control, error)) return false;
        if (control.kind == QA_UI_LIST) {
            if (!list_draw(ui, frame, target, &control, colors.text, colors.focused,
                colors.accent, colors.disabled, colors.control, selected, error)) return false;
            continue;
        }
        if (control.kind == QA_UI_FIELD) {
            if (!field_draw(ui, frame, clip_rect(ui, target, control.rect), &control, selected, color, error)) return false;
            continue;
        }
        if (control.kind == QA_UI_SLIDER) {
            if (!slider_draw(ui, frame, target, &control, color, colors.disabled, error)) return false;
            continue;
        }
        if (!ui_draw_text(ui, frame, target, control.rect.x + 10, control.rect.y + 6,
            control.label, color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_LEFT, error)) return false;
        const char *value = NULL;
        if (control.kind == QA_UI_TOGGLE) value = control.value.checked ? "On" : "Off";
        else if (control.kind == QA_UI_CHOICE && control.value.choice.selected < control.value.choice.count)
            value = control.value.choice.labels[control.value.choice.selected];
        if (!ui_draw_text(ui, frame, target, control.rect.x + control.rect.width - 10,
            control.rect.y + 6, value, color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_RIGHT, error)) return false;
    }
    if (menu.scrollable && menu.content_height > menu.scroll_rect.height) {
        float height = menu.scroll_rect.height;
        float thumb = fminf(height, fmaxf(24, height * height / menu.content_height));
        float x = menu.scroll_rect.x + menu.scroll_rect.width - 14;
        float top = menu.scroll_rect.y + (height - thumb) * ui->stack[ui->depth - 1].scroll /
            (menu.content_height - height);
        if (!ui_fill(ui, frame, viewport, (qa_scene_rect_f){x, menu.scroll_rect.y, 12, height}, colors.control, error) ||
            !ui_fill(ui, frame, viewport, (qa_scene_rect_f){x, top, 12, thumb}, colors.accent, error)) return false;
    }
    if (!menu_text_controls(ui, frame, viewport, &menu, true, error)) return false;
    return !ui->capture || ui_draw_text(ui, frame, viewport, 380, 432,
        "Press key/button. Esc cancels.", colors.accent, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_CENTER, error);
}
bool qa_ui_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect viewport, float scale,
                 bool contrast, bool startup, qa_error *error) {
    if (!ui || ui->handling) return ui_fail(error, "UI callback is active");
    ui->handling = true;
    ui->drawing = true;
    bool ok = draw(ui, frame, viewport, scale, contrast, startup, error);
    ui->handling = false;
    ui->drawing = false;
    return ok;
}

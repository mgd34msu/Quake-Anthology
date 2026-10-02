#include "internal.h"

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
static bool draw_text(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target, float x, float y,
                   const char *text, qa_scene_vec4 color, float scale,
                   qa_font_alignment alignment, bool literal, bool localize, qa_error *error) {
    if (!text || !*text || !target.width || !target.height)
        return true;
    if (localize && ui->options.localize)
        text = ui->options.localize(ui->options.context, text);
    if (!text)
        return true;
    qa_font_layout layout;
    qa_font_layout_options options = {.text = {(const uint8_t *)text, strlen(text)},
        .scale = scale * ui->scale * ui->text_scale, .color = color,
        .color_codes = literal ? QA_FONT_COLOR_LITERAL : QA_FONT_COLOR_Q3,
        .force_color = ui->color_mode != QA_UI_COLOR_STANDARD, .alignment = alignment};
    if (!qa_font_layout_build(&ui->options.fonts, &options, &frame->storage, &layout, error))
        return false;
    if (alignment!=QA_FONT_ALIGN_LEFT) {
        qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)layout.glyphs;
        for (size_t row=0;row<layout.line_count;++row) {
            const qa_font_line *line=layout.lines+row;
            float offset=line->width*(alignment==QA_FONT_ALIGN_CENTER?.5f:1);
            for (size_t i=0;i<line->glyph_count;++i) glyphs[line->first_glyph+i].rect.x-=offset;
        }
    }
    qa_font_draw_options draw = {.seat = ui->options.seat, .target = target,
        .origin = {ui->bias_x + x * ui->scale - (float)target.x,
                   ui->bias_y + y * ui->scale - (float)target.y},
        .space = QA_FONT_PIXELS, .shadow_offset = ui->scale};
    return qa_font_draw_layout(frame, &layout, &draw, error);
}
bool ui_draw_text(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target, float x, float y,
                   const char *text, qa_scene_vec4 color, float scale,
                   qa_font_alignment alignment, qa_error *error) {
    return draw_text(ui, frame, target, x, y, text, color, scale, alignment, false, true, error);
}
bool ui_draw_source_text(qa_ui *ui,qa_scene_frame *frame,qa_scene_rect target,float x,float y,
    const char *text,qa_scene_vec4 color,float scale,qa_font_alignment alignment,qa_error *error)
{ return draw_text(ui,frame,target,x,y,text,color,scale,alignment,false,false,error); }
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
    float cursor_width = state->cursor < count ? cells[state->cursor].width : ui_glyph_width(ui, ' ');
    return !focused || fmod(floor(ui->time_ms / 256), 2) != 0 || ui_fill(ui, frame, target,
        (qa_scene_rect_f){x + width, control->rect.y + 5,
            state->overstrike ? cursor_width : 1, 8 * UI_MENU_FONT_SCALE * ui->text_scale}, color, error);
}
static bool list_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect target,
                       const qa_ui_control *control, qa_scene_vec4 color,
                       qa_scene_vec4 accent, qa_error *error) {
    ui_field *state = ui_list_state(ui, control, error);
    if (!state) return false;
    float height = fmaxf(1, control->value.list.row_height);
    size_t page = ui_list_page(control);
    size_t count = control->value.list.count, selected = control->value.list.selected;
    size_t maximum = count > page ? count - page : 0;
    qa_scene_rect clipped = clip_rect(ui, target, control->rect);
    for (size_t i = state->top; i < count && i - state->top < page; ++i) {
        const qa_ui_row *row = &control->value.list.rows[i];
        float y = control->rect.y + (float)(i - state->top) * height;
        if (i == selected && !ui_fill(ui, frame, clipped,
            (qa_scene_rect_f){control->rect.x, y, control->rect.width, height}, accent, error)) return false;
        qa_scene_vec4 text_color = row->enabled ? color : (qa_scene_vec4){.5f, .5f, .5f, 1};
        float label_x = control->rect.x + 8;
        if (row->image) {
            qa_scene_rect_f pixels = {ui->bias_x + label_x * ui->scale,
                ui->bias_y + (y + 2) * ui->scale, (height - 4) * ui->scale, (height - 4) * ui->scale};
            if (!qa_scene_frame_picture_f(frame, row->image, clipped, pixels,
                (qa_scene_vec4){0, 0, 1, 1}, text_color, error)) return false;
            label_x += height;
        }
        if (!ui_draw_text(ui, frame, clipped, label_x, y + 6,
                           row->label, text_color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_LEFT, error) ||
            !ui_draw_text(ui, frame, clipped, control->rect.x + control->rect.width - 20,
                           y + 6, row->detail, text_color, .75f * UI_MENU_FONT_SCALE, QA_FONT_ALIGN_RIGHT, error)) return false;
    }
    if (!maximum) return true;
    float thumb = fmaxf(24, control->rect.height * (float)page / (float)count);
    if (thumb > control->rect.height) thumb = control->rect.height;
    return ui_fill(ui, frame, clipped,
        (qa_scene_rect_f){control->rect.x + control->rect.width - 10,
            control->rect.y + (control->rect.height - thumb) * (float)state->top / (float)maximum,
            8, thumb}, (qa_scene_vec4){.7f, .7f, .7f, 1}, error);
}
static bool draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect viewport, float scale,
                  bool contrast, qa_error *error) {
    if (!ui || !frame || !viewport.width || !viewport.height || !isfinite(scale) || scale <= 0 ||
        (double)viewport.x + viewport.width > INT32_MAX ||
        (double)viewport.y + viewport.height > INT32_MAX)
        return ui_fail(error, "invalid UI draw target");
    ui->viewport = viewport;
    ui->scale = fminf((float)viewport.width / 640, (float)viewport.height / 480) * scale;
    if (!isfinite(ui->scale) || ui->scale <= 0)
        return ui_fail(error, "UI canvas scale overflow");
    ui->bias_x = (float)viewport.x + ((float)viewport.width - 640 * ui->scale) * .5f;
    ui->bias_y = (float)viewport.y + ((float)viewport.height - 480 * ui->scale) * .5f;
    if (ui->has_pointer) ui->cursor = (qa_input_pair){(ui->pointer.x - ui->bias_x) / ui->scale,
                                                    (ui->pointer.y - ui->bias_y) / ui->scale};
    qa_ui_menu menu;
    if (!ui_active(ui, &menu, error)) return false;
    if (!ui->depth) return true;
    qa_scene_vec4 background = contrast ? (qa_scene_vec4){0, 0, 0, 1} : (qa_scene_vec4){.03f, .03f, .04f, .94f};
    qa_scene_vec4 accent = contrast ? (qa_scene_vec4){.15f, .15f, .15f, 1} : (qa_scene_vec4){.25f, .10f, .06f, 1};
    qa_scene_vec4 white = {1, 1, 1, 1};
    if (ui->color_mode == QA_UI_COLOR_MONOCHROME) accent = (qa_scene_vec4){.25f, .25f, .25f, 1};
    else if (ui->color_mode == QA_UI_COLOR_BLUE_YELLOW) accent = (qa_scene_vec4){.06f, .2f, .42f, 1};
    if (!ui_fill(ui, frame, viewport, (qa_scene_rect_f){0, 0, 640, 480}, background, error) ||
        !(menu.source_title ? ui_draw_source_text : ui_draw_text)(ui, frame, viewport, 320, 42, menu.title, white, 2,
                       QA_FONT_ALIGN_CENTER, error)) return false;
    qa_ui_id focused = ui->stack[ui->depth - 1].control;
    for (size_t i = 0; i < menu.count; ++i) {
        qa_ui_control control = ui_control(ui, &menu, i);
        if (!control.visible) continue;
        qa_scene_rect target = control.scrolls && menu.scrollable
            ? clip_rect(ui, viewport, menu.scroll_rect) : viewport;
        bool selected = control.id == focused;
        qa_scene_vec4 color = control.enabled ? white : contrast ? (qa_scene_vec4){.65f, .65f, .65f, 1} : (qa_scene_vec4){.5f, .5f, .5f, 1};
        if (!ui_fill(ui, frame, target, control.rect,
            selected ? accent : contrast ? (qa_scene_vec4){0, 0, 0, 1} : (qa_scene_vec4){.08f, .08f, .09f, 1}, error)) return false;
        if (control.kind == QA_UI_OWNER_DRAW) {
            if (control.value.owner.draw && !control.value.owner.draw(control.context,
                ui->options.seat, frame, target, control.rect, error)) return false;
            continue;
        }
        if (control.kind == QA_UI_LIST) {
            if (!list_draw(ui, frame, target, &control, color, accent, error)) return false;
            continue;
        }
        if (control.kind == QA_UI_FIELD) {
            if (!field_draw(ui, frame, clip_rect(ui, target, control.rect), &control, selected, color, error)) return false;
            continue;
        }
        if (!ui_draw_text(ui, frame, target, control.rect.x + 10, control.rect.y + 6,
                           control.label, color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_LEFT, error)) return false;
        const char *value = NULL;
        char numeric[32];
        if (control.kind == QA_UI_TOGGLE) value = control.value.checked ? "On" : "Off";
        else if (control.kind == QA_UI_CHOICE && control.value.choice.selected < control.value.choice.count)
            value = control.value.choice.labels[control.value.choice.selected];
        else if (control.kind == QA_UI_SLIDER) {
            if (!qa_format_number(control.value.slider.value, numeric, error)) return false;
            value = control.value.slider.label ? control.value.slider.label : numeric;
            float x = control.rect.x + control.rect.width * .55f;
            float width = control.rect.width * .32f;
            double range = control.value.slider.maximum - control.value.slider.minimum;
            float position = range > 0 ? (float)fmax(0, fmin(1,
                (control.value.slider.value - control.value.slider.minimum) / range)) : 0;
            if (!ui_fill(ui, frame, target, (qa_scene_rect_f){x, control.rect.y + 14, width, 2}, color, error) ||
                !ui_fill(ui, frame, target, (qa_scene_rect_f){x + width * position - 3,
                    control.rect.y + 8, 6, 14}, color, error)) return false;
        }
        if (!ui_draw_text(ui, frame, target, control.rect.x + control.rect.width - 10,
                           control.rect.y + 6, value, color, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_RIGHT, error)) return false;
    }
    if (menu.scrollable && menu.content_height > menu.scroll_rect.height) {
        float height = menu.scroll_rect.height;
        float thumb = fminf(height, fmaxf(24, height * height / menu.content_height));
        float top = menu.scroll_rect.y + (height - thumb) * ui->stack[ui->depth - 1].scroll /
            (menu.content_height - height);
        if (!ui_fill(ui, frame, viewport, (qa_scene_rect_f){menu.scroll_rect.x + menu.scroll_rect.width - 12,
            top, 10, thumb}, (qa_scene_vec4){.7f, .7f, .7f, 1}, error)) return false;
    }
    if (ui->capture && !ui_draw_text(ui, frame, viewport, 320, 432,
        "Press key/button. Esc cancels.", white, UI_MENU_FONT_SCALE, QA_FONT_ALIGN_CENTER, error)) return false;
    return ui_fill(ui, frame, viewport,
        (qa_scene_rect_f){ui->cursor.x, ui->cursor.y, 3, 14}, white, error);
}
bool qa_ui_draw(qa_ui *ui, qa_scene_frame *frame, qa_scene_rect viewport, float scale,
                 bool contrast, qa_error *error) {
    if (!ui || ui->handling) return ui_fail(error, "UI callback is active");
    ui->handling = true;
    ui->drawing = true;
    bool ok = draw(ui, frame, viewport, scale, contrast, error);
    ui->handling = false;
    ui->drawing = false;
    return ok;
}

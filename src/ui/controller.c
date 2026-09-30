#include "internal.h"

float ui_glyph_width(qa_ui *ui, uint32_t scalar) {
    qa_font_glyph glyph;
    qa_font_info info;
    if (!qa_font_resolve(&ui->options.fonts, scalar, false, &glyph) ||
        !qa_font_describe(glyph.font, &info)) return 8;
    return glyph.advance * 8 / fmaxf(1, info.line_height);
}
bool ui_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool ui_reserve(void **data, size_t *capacity, size_t count, size_t stride, qa_error *error) {
    if (count <= *capacity)
        return true;
    if (!stride || count > SIZE_MAX / stride) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "UI storage overflow");
        return false;
    }
    size_t next = *capacity ? *capacity : 8;
    while (next < count && next <= SIZE_MAX / 2)
        next *= 2;
    if (next < count || next > SIZE_MAX / stride)
        next = count;
    void *grown = realloc(*data, next * stride);
    if (!grown) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UI storage");
        return false;
    }
    *data = grown;
    *capacity = next;
    return true;
}
qa_ui_menu_registration *ui_registration(qa_ui *ui, qa_ui_id id) {
    for (size_t i = 0; i < ui->menu_count; ++i)
        if (ui->menus[i].id == id)
            return &ui->menus[i];
    return NULL;
}
static bool rectangle(qa_scene_rect_f rect) {
    return isfinite(rect.x) && isfinite(rect.y) && isfinite(rect.width) && isfinite(rect.height) &&
           rect.width >= 0 && rect.height >= 0 && isfinite(rect.x + rect.width) &&
           isfinite(rect.y + rect.height);
}
static bool menu_valid(const qa_ui_menu *menu, qa_ui_id id, qa_error *error) {
    if (menu->id != id || (menu->count && !menu->controls) ||
        (menu->scrollable && (!rectangle(menu->scroll_rect) || menu->scroll_rect.height <= 0 ||
          !isfinite(menu->content_height) || menu->content_height < 0)))
        return ui_fail(error, "menu factory returned invalid identity, controls or scrolling");
    for (size_t i = 0; i < menu->count; ++i) {
        const qa_ui_control *control = &menu->controls[i];
        if (!control->id || control->kind < QA_UI_BUTTON || control->kind > QA_UI_OWNER_DRAW ||
            !rectangle(control->rect)) return ui_fail(error, "invalid UI control");
        for (size_t j = 0; j < i; ++j)
            if (menu->controls[j].id == control->id) return ui_fail(error, "duplicate UI control identity");
        if (control->kind == QA_UI_LIST && ((control->value.list.count && !control->value.list.rows) ||
            !isfinite(control->value.list.row_height) || control->value.list.row_height <= 0))
            return ui_fail(error, "invalid UI list rows");
        if (control->kind == QA_UI_CHOICE && control->value.choice.count && !control->value.choice.labels)
            return ui_fail(error, "invalid UI choice labels");
        if (control->kind == QA_UI_SLIDER && (!isfinite(control->value.slider.value) ||
            !isfinite(control->value.slider.minimum) || !isfinite(control->value.slider.maximum) ||
            !isfinite(control->value.slider.step) || control->value.slider.step <= 0 ||
            control->value.slider.minimum > control->value.slider.maximum ||
            !isfinite(control->value.slider.maximum - control->value.slider.minimum)))
            return ui_fail(error, "invalid UI slider range");
    }
    return true;
}
bool ui_active(qa_ui *ui, qa_ui_menu *menu, qa_error *error) {
    *menu = (qa_ui_menu){0};
    if (!ui->depth)
        return true;
    ui_cursor *cursor = &ui->stack[ui->depth - 1];
    qa_ui_menu_registration *registration = ui_registration(ui, cursor->menu);
    if (!registration || !registration->factory(registration->context, ui->options.seat,
                                                menu, error))
        return false;
    if (!menu_valid(menu, cursor->menu, error)) return false;
    bool focused = false;
    qa_ui_id first = 0;
    for (size_t i = 0; i < menu->count; ++i) {
        const qa_ui_control *control = &menu->controls[i];
        if (!control->enabled || !control->visible)
            continue;
        if (!first)
            first = control->id;
        focused |= control->id == cursor->control;
    }
    if (!focused)
        cursor->control = first;
    cursor->scroll = menu->scrollable
                         ? fmaxf(0, fminf(cursor->scroll,
                             menu->content_height - menu->scroll_rect.height)) : 0;
    return true;
}
qa_ui_control ui_control(qa_ui *ui, const qa_ui_menu *menu, size_t index) {
    qa_ui_control control = menu->controls[index];
    if (control.scrolls && menu->scrollable && ui->depth)
        control.rect.y -= ui->stack[ui->depth - 1].scroll;
    return control;
}
ui_field *ui_field_get(qa_ui *ui, qa_ui_id id, qa_error *error) {
    qa_ui_id menu = ui->depth ? ui->stack[ui->depth - 1].menu : 0;
    for (size_t i = 0; i < ui->field_count; ++i)
        if (ui->fields[i].menu == menu && ui->fields[i].control == id)
            return &ui->fields[i];
    if (!ui_reserve((void **)&ui->fields, &ui->field_capacity, ui->field_count + 1,
                     sizeof(*ui->fields), error))
        return NULL;
    ui->fields[ui->field_count] = (ui_field){.menu = menu, .control = id, .cursor = SIZE_MAX};
    return &ui->fields[ui->field_count++];
}
size_t ui_list_page(const qa_ui_control *control) {
    double rows = floor((double)control->rect.height / fmaxf(1, control->value.list.row_height));
    return rows >= (double)SIZE_MAX ? SIZE_MAX : rows < 1 ? 1 : (size_t)rows;
}
ui_field *ui_list_state(qa_ui *ui, const qa_ui_control *control, qa_error *error) {
    ui_field *state = ui_field_get(ui, control->id, error);
    if (!state) return NULL;
    size_t page = ui_list_page(control);
    size_t count = control->value.list.count, selected = control->value.list.selected;
    size_t maximum = count > page ? count - page : 0;
    if (state->revision != control->value.list.revision || state->cursor != selected)
        state->scrolled = false;
    state->revision = control->value.list.revision;
    state->cursor = selected;
    if (state->top > maximum) state->top = maximum;
    if (selected < count && !state->scrolled) {
        if (selected < state->top) state->top = selected;
        else if (selected - state->top >= page) state->top = selected - page + 1;
    }
    return state;
}
void ui_capture_cancel(qa_ui *ui) {
    if (!ui->capture) return;
    ui->capture = false;
    if (ui->options.binding_cancel) ui->options.binding_cancel(ui->options.context, ui->options.seat);
}
static bool input_handler(void *context, qa_input_seat *seat, qa_input_focus focus,
                           const qa_input_event *event) {
    (void)seat;
    if (focus != QA_INPUT_UI)
        return false;
    qa_ui *ui = context;
    bool consumed = false;
    qa_error error = {0};
    if (!qa_ui_input(ui, event, &consumed, &error)) {
        ui->error = error;
        if (ui->options.sound)
            ui->options.sound(ui->options.context, ui->options.seat, QA_UI_REJECT);
        return true;
    }
    return consumed;
}
bool qa_ui_create(const qa_ui_options *options, qa_ui **out, qa_error *error) {
    if (!options || !out || !options->input || !options->white || !options->fonts.classic ||
        options->fonts.seat != options->seat)
        return ui_fail(error, "UI requires seat input, matching fonts and a white image");
    qa_ui *ui = calloc(1, sizeof(*ui));
    if (!ui) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating seat UI");
        return false;
    }
    ui->options = *options;
    ui->cursor = (qa_input_pair){320, 240};
    ui->scale = 1;
    *out = ui;
    return true;
}
bool qa_ui_register(qa_ui *ui, const qa_ui_menu_registration *registration, qa_error *error) {
    if (!ui || ui->drawing || !registration || !registration->id || !registration->factory ||
        ui_registration(ui, registration->id))
        return ui_fail(error, "invalid or duplicate UI menu registration");
    if (!ui_reserve((void **)&ui->menus, &ui->menu_capacity, ui->menu_count + 1,
                     sizeof(*ui->menus), error))
        return false;
    ui->menus[ui->menu_count++] = *registration;
    return true;
}
bool qa_ui_open(qa_ui *ui, qa_ui_id id, double time, qa_error *error) {
    if (!ui || ui->drawing || !isfinite(time) || time < 0)
        return ui_fail(error, "invalid UI open");
    ui->time_ms = time;
    qa_ui_menu_registration *found = ui_registration(ui, id);
    if (!found)
        return ui_fail(error, "UI menu is not registered");
    for (size_t i = 0; i < ui->depth; ++i)
        if (ui->stack[i].menu == id) {
            while (ui->depth > i + 1)
                if (!qa_ui_close(ui, time, error))
                    return false;
            return true;
        }
    qa_ui_menu_registration registration = *found;
    qa_ui_menu menu;
    if (!registration.factory(registration.context, ui->options.seat, &menu, error))
        return false;
    if (!menu_valid(&menu, id, error)) return false;
    if (!ui_reserve((void **)&ui->stack, &ui->stack_capacity, ui->depth + 1,
                     sizeof(*ui->stack), error))
        return false;
    if (registration.open && !registration.open(registration.context, ui->options.seat, error))
        return false;
    if (!ui->depth && !qa_input_seat_ui_push(ui->options.input, input_handler, ui, time,
                                            &ui->input_token, error)) {
        if (registration.close)
            registration.close(registration.context, ui->options.seat);
        return false;
    }
    if (!ui->depth && !qa_input_seat_set_focus(ui->options.input, QA_INPUT_UI, time, error)) {
        (void)qa_input_seat_ui_remove(ui->options.input, ui->input_token, time, NULL);
        ui->input_token = 0;
        if (registration.close) registration.close(registration.context, ui->options.seat);
        return false;
    }
    ui->stack[ui->depth++] = (ui_cursor){.menu = id};
    ui->dragging = 0;
    ui->menu_dragging = false;
    memset(ui->held_direction, 0, sizeof(ui->held_direction));
    if (ui->options.sound)
        ui->options.sound(ui->options.context, ui->options.seat, QA_UI_OPEN);
    return true;
}
bool qa_ui_close(qa_ui *ui, double time, qa_error *error) {
    if (!ui || ui->drawing || !isfinite(time) || time < 0)
        return ui_fail(error, "invalid UI close");
    ui->time_ms = time;
    if (!ui->depth)
        return true;
    bool ok = ui->depth != 1 || qa_input_seat_ui_remove(ui->options.input, ui->input_token, time, error);
    /* Token retirement commits even when releasing a held source binding fails. */
    qa_ui_id id = ui->stack[--ui->depth].menu;
    if (!ui->depth)
        ui->input_token = 0;
    ui_capture_cancel(ui);
    ui->dragging = 0;
    ui->menu_dragging = false;
    memset(ui->held_direction, 0, sizeof(ui->held_direction));
    qa_ui_menu_registration *registration = ui_registration(ui, id);
    if (registration && registration->close)
        registration->close(registration->context, ui->options.seat);
    if (ui->options.sound)
        ui->options.sound(ui->options.context, ui->options.seat, QA_UI_CLOSE);
    return ok;
}
bool qa_ui_close_all(qa_ui *ui, double time, qa_error *error) {
    if (!ui)
        return ui_fail(error, "missing UI");
    while (ui->depth)
        if (!qa_ui_close(ui, time, error))
            return false;
    return true;
}
bool qa_ui_unregister(qa_ui *ui, qa_ui_id id, double time, qa_error *error) {
    if (!ui || ui->drawing || !ui_registration(ui, id))
        return ui_fail(error, "unknown UI menu");
    for (size_t i = 0; i < ui->depth; ++i)
        if (ui->stack[i].menu == id) {
            while (ui->depth > i)
                if (!qa_ui_close(ui, time, error))
                    return false;
            break;
        }
    size_t index = (size_t)(ui_registration(ui, id) - ui->menus);
    memmove(ui->menus + index, ui->menus + index + 1,
            (ui->menu_count - index - 1) * sizeof(*ui->menus));
    --ui->menu_count;
    return true;
}
bool qa_ui_destroy(qa_ui *ui, double time, qa_error *error) {
    if (!ui)
        return true;
    if (ui->handling)
        return ui_fail(error, "UI callback is active");
    if (!qa_ui_close_all(ui, time, error))
        return false;
    free(ui->fields);
    free(ui->stack);
    free(ui->menus);
    free(ui);
    return true;
}
bool qa_ui_state_read(qa_ui *ui, qa_ui_state *out, qa_error *error) {
    if (!ui || !out)
        return ui_fail(error, "missing UI state output");
    qa_ui_menu menu;
    if (!ui_active(ui, &menu, error))
        return false;
    *out = (qa_ui_state){.seat = ui->options.seat, .menu = menu.id,
        .control = ui->depth ? ui->stack[ui->depth - 1].control : 0,
        .cursor = ui->cursor, .depth = ui->depth, .fullscreen = menu.fullscreen,
        .binding_capture = ui->capture};
    return true;
}
bool qa_ui_capture_binding(qa_ui *ui, bool capture, qa_error *error) {
    if (!ui || ui->drawing || (capture && (!ui->depth || !ui->options.binding)))
        return ui_fail(error, "binding capture requires an active menu and binding owner");
    ui_capture_cancel(ui);
    ui->capture = capture;
    memset(ui->held_direction, 0, sizeof(ui->held_direction));
    return true;
}
const qa_error *qa_ui_error(const qa_ui *ui) {
    return ui && ui->error.code != QA_OK ? &ui->error : NULL;
}

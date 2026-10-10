#include "internal.h"

bool ui_inside(qa_scene_rect_f rect, qa_vec2 point) {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}
bool ui_action(qa_ui *ui, const qa_ui_control *control, const qa_ui_action *action,
                qa_error *error) {
    if (!control->enabled || !control->visible || control->kind == QA_UI_TEXT || !control->action)
        return true;
    size_t depth = ui->depth;
    qa_ui_id menu = depth ? ui->stack[depth - 1].menu : 0;
    qa_ui_sound sound = QA_UI_CHANGE;
    if (action->kind == QA_UI_ACTIVATE || action->kind == QA_UI_SUBMIT ||
        action->kind == QA_UI_ROW_ACTIVATE)
        sound = QA_UI_OPEN;
    else if (control->kind == QA_UI_LIST && action->kind == QA_UI_SELECT)
        sound = QA_UI_MOVE;
    bool ok = control->action(control->context, ui->options.seat, control->id, action, error);
    if (ok && ui->options.sound && ui->depth == depth &&
        (!depth || ui->stack[depth - 1].menu == menu))
        ui->options.sound(ui->options.context, ui->options.seat, sound);
    return ok;
}
bool ui_move(qa_ui *ui, int direction, qa_error *error) {
    qa_ui_menu menu;
    if (!ui_active(ui, &menu, error))
        return false;
    if (!ui->depth || !menu.count)
        return true;
    ui_cursor *cursor = &ui->stack[ui->depth - 1];
    size_t selected = menu.count;
    for (size_t i = 0; i < menu.count; ++i)
        if (menu.controls[i].id == cursor->control)
            selected = i;
    for (size_t step = 0; step < menu.count; ++step) {
        selected = direction > 0 ? (selected == menu.count - 1 || selected == menu.count ? 0 : selected + 1)
                                 : (!selected || selected == menu.count ? menu.count - 1 : selected - 1);
        const qa_ui_control *control = &menu.controls[selected];
        if (!control->enabled || !control->visible || control->kind == QA_UI_TEXT)
            continue;
        cursor->control = control->id;
        if (menu.scrollable && control->scrolls) {
            float top = control->rect.y - cursor->scroll;
            if (top < menu.scroll_rect.y)
                cursor->scroll -= menu.scroll_rect.y - top;
            else if (top + control->rect.height > menu.scroll_rect.y + menu.scroll_rect.height)
                cursor->scroll += top + control->rect.height - menu.scroll_rect.y - menu.scroll_rect.height;
            cursor->scroll = fmaxf(0, fminf(cursor->scroll, menu.content_height - menu.scroll_rect.height));
        }
        if (ui->options.sound)
            ui->options.sound(ui->options.context, ui->options.seat, QA_UI_MOVE);
        break;
    }
    return true;
}
bool ui_change(qa_ui *ui, const qa_ui_control *control, int direction, qa_error *error) {
    qa_ui_action action = {.kind = QA_UI_CHANGE_NUMBER};
    switch (control->kind) {
    case QA_UI_TOGGLE:
        action.value.number = !control->value.checked;
        break;
    case QA_UI_SLIDER: {
        double minimum = control->value.slider.minimum, maximum = control->value.slider.maximum;
        double step = control->value.slider.step, value = control->value.slider.value;
        if (!isfinite(minimum) || !isfinite(maximum) || !isfinite(step) || !isfinite(value) ||
            minimum > maximum || step <= 0)
            return ui_fail(error, "invalid UI slider");
        action.value.number = fmax(minimum, fmin(maximum,
            minimum + floor((value + step * direction - minimum) / step + .5) * step));
        break;
    }
    case QA_UI_CHOICE:
        if (!control->value.choice.count)
            return true;
        action.kind = QA_UI_SELECT;
        action.value.row = control->value.choice.selected >= control->value.choice.count
            ? (direction > 0 ? 0 : control->value.choice.count - 1)
            : direction > 0 ? (control->value.choice.selected + 1) % control->value.choice.count
            : control->value.choice.selected ? control->value.choice.selected - 1 : control->value.choice.count - 1;
        break;
    default:
        return true;
    }
    return ui_action(ui, control, &action, error);
}
bool ui_activate(qa_ui *ui, const qa_ui_control *control, qa_error *error) {
    qa_ui_action action = {.kind = QA_UI_ACTIVATE};
    switch (control->kind) {
    case QA_UI_TOGGLE: case QA_UI_SLIDER: case QA_UI_CHOICE:
        return ui_change(ui, control, 1, error);
    case QA_UI_FIELD:
        action.kind = QA_UI_SUBMIT;
        action.value.text = control->value.field.text;
        break;
    case QA_UI_LIST:
        if (control->value.list.selected >= control->value.list.count ||
            !control->value.list.rows[control->value.list.selected].enabled)
            return true;
        action.kind = QA_UI_ROW_ACTIVATE;
        action.value.row = control->value.list.selected;
        break;
    case QA_UI_OWNER_DRAW: case QA_UI_TEXT:
        return true;
    default:
        break;
    }
    return ui_action(ui, control, &action, error);
}
static size_t characters(const char *text) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    size_t offset = 0, count = 0;
    uint32_t scalar;
    while (qa_utf8_next(bytes, &offset, &scalar))
        ++count;
    return count;
}
static size_t byte_at(const char *text, size_t position) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    size_t offset = 0;
    uint32_t scalar;
    while (position && qa_utf8_next(bytes, &offset, &scalar))
        --position;
    return offset;
}
static bool field_replace(qa_ui *ui, const qa_ui_control *control, size_t begin, size_t end,
                           const char *insert, size_t bytes, size_t advance, qa_error *error) {
    const char *text = control->value.field.text ? control->value.field.text : "";
    size_t length = strlen(text), first = byte_at(text, begin), last = byte_at(text, end);
    if (bytes > SIZE_MAX - (length - (last - first)) - 1)
        return ui_fail(error, "UI field size overflow");
    size_t size = first + bytes + length - last;
    char *value = malloc(size + 1);
    if (!value) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UI field edit");
        return false;
    }
    memcpy(value, text, first);
    if (bytes)
        memcpy(value + first, insert, bytes);
    memcpy(value + first + bytes, text + last, length - last + 1);
    qa_ui_action action = {.kind = QA_UI_CHANGE_TEXT, .value.text = value};
    qa_ui_id menu = ui->depth ? ui->stack[ui->depth - 1].menu : 0;
    bool ok = ui_action(ui, control, &action, error);
    free(value);
    if (ok && ui->depth && ui->stack[ui->depth - 1].menu == menu) {
        ui_field *field = ui_field_get(ui, control->id, error);
        if (!field)
            return false;
        field->cursor = begin + advance;
    }
    return ok;
}
bool ui_text(qa_ui *ui, const qa_ui_control *control, const char *incoming, qa_error *error) {
    if (control->kind != QA_UI_FIELD || !incoming)
        return true;
    ui_field *field = ui_field_get(ui, control->id, error);
    if (!field)
        return false;
    const char *text = control->value.field.text ? control->value.field.text : "";
    size_t length = characters(text), cursor = field->cursor < length ? field->cursor : length;
    size_t room = control->value.field.maximum > length ? control->value.field.maximum - length : 0;
    if (field->overstrike)
        room += length - cursor;
    size_t input_length = strlen(incoming);
    if (input_length > (SIZE_MAX - 1) / 3)
        return ui_fail(error, "UI input text too large");
    char *accepted = malloc(input_length * 3 + 1);
    if (!accepted) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UI text input");
        return false;
    }
    qa_bytes input = {(const uint8_t *)incoming, input_length};
    size_t offset = 0, bytes = 0, count = 0;
    uint32_t scalar;
    while (count < room && qa_utf8_next(input, &offset, &scalar)) {
        if (scalar < 32 || scalar == 127)
            continue;
        char encoded[4];
        size_t size = qa_utf8_encode(scalar, encoded);
        memcpy(accepted + bytes, encoded, size);
        bytes += size;
        ++count;
    }
    bool ok = !count || field_replace(ui, control, cursor,
        field->overstrike ? (cursor + count < length ? cursor + count : length) : cursor,
        accepted, bytes, count, error);
    free(accepted);
    return ok;
}
static bool field_key(qa_ui *ui, const qa_ui_control *control, uint32_t key,
                       bool *handled, qa_error *error) {
    *handled = true;
    ui_field *field = ui_field_get(ui, control->id, error);
    if (!field)
        return false;
    size_t length = characters(control->value.field.text ? control->value.field.text : "");
    if (field->cursor > length)
        field->cursor = length;
    size_t cursor = field->cursor;
    if (key == QA_KEY_LEFT) field->cursor = cursor ? cursor - 1 : 0;
    else if (key == QA_KEY_RIGHT) field->cursor = cursor < length ? cursor + 1 : length;
    else if (key == QA_KEY_HOME || (ui->control && key == 'a')) field->cursor = 0;
    else if (key == QA_KEY_END || (ui->control && key == 'e')) field->cursor = length;
    else if (key == QA_KEY_INSERT) field->overstrike = !field->overstrike;
    else if (key == QA_KEY_BACKSPACE || (ui->control && key == 'h')) {
        if (cursor)
            return field_replace(ui, control, cursor - 1, cursor, NULL, 0, 0, error);
    } else if (key == QA_KEY_DELETE) {
        if (cursor < length)
            return field_replace(ui, control, cursor, cursor + 1, NULL, 0, 0, error);
    } else if (ui->control && key == 'u')
        return field_replace(ui, control, 0, length, NULL, 0, 0, error);
    else if (ui->control && key == 'v')
        return ui_text(ui, control, ui->options.clipboard ? ui->options.clipboard(ui->options.context) : NULL, error);
    else *handled = false;
    return true;
}
static bool list_key(qa_ui *ui, const qa_ui_control *control, uint32_t key,
                      bool *handled, qa_error *error) {
    *handled = true;
    size_t count = control->value.list.count, selected = control->value.list.selected;
    if (!count)
        return true;
    int direction = 0;
    size_t steps = 1;
    if (key == QA_KEY_UP) direction = -1;
    else if (key == QA_KEY_DOWN) direction = 1;
    else if (key == QA_KEY_PAGEUP || key == QA_KEY_PAGEDOWN) {
        direction = key == QA_KEY_PAGEUP ? -1 : 1;
        steps = ui_list_page(control);
    } else if (key == QA_KEY_HOME) selected = 0;
    else if (key == QA_KEY_END) { selected = count - 1; direction = -1; steps = 0; }
    else if (key == QA_KEY_DELETE) {
        return selected < count && control->value.list.rows[selected].enabled
            ? ui_action(ui, control, &(qa_ui_action){.kind = QA_UI_ROW_DELETE, .value.row = selected}, error) : true;
    } else { *handled = false; return true; }
    if (selected >= count)
        selected = direction < 0 ? count - 1 : 0;
    if (direction)
        selected = direction > 0 ? (steps >= count - selected ? count - 1 : selected + steps)
                                  : (steps > selected ? 0 : selected - steps);
    size_t candidate = selected;
    while (!control->value.list.rows[candidate].enabled) {
        if (direction < 0 ? !candidate : candidate == count - 1)
            return true;
        candidate = direction < 0 ? candidate - 1 : candidate + 1;
    }
    return ui_action(ui, control, &(qa_ui_action){.kind = QA_UI_SELECT, .value.row = candidate}, error);
}
static bool key(qa_ui *ui, uint32_t code, bool down, double time, qa_error *error) {
    if (code == QA_KEY_SHIFT) { ui->shift = down; return true; }
    if (code == QA_KEY_CONTROL) { ui->control = down; return true; }
    qa_ui_menu menu;
    if (!ui_active(ui, &menu, error))
        return false;
    if (!ui->depth)
        return true;
    qa_ui_id focused = ui->stack[ui->depth - 1].control;
    for (size_t i = 0; i < menu.count; ++i) {
        qa_ui_control control = ui_control(ui, &menu, i);
        if (control.id != focused || !control.enabled || !control.visible)
            continue;
        bool handled = false;
        if (control.kind == QA_UI_OWNER_DRAW && control.value.owner.input) {
            qa_input_event event = {.kind = QA_INPUT_EVENT_KEY, .time_ms = time, .down = down,
                                   .input = {.kind = QA_PHYSICAL_KEY, .code = code}};
            if (!control.value.owner.input(control.context, ui->options.seat, &event, &handled, error)) return false;
            if (handled || !ui->depth || ui->stack[ui->depth - 1].menu != menu.id) return true;
        }
        if (!down) return true;
        if (code == QA_KEY_ESCAPE) return qa_ui_close(ui, ui->time_ms, error);
        if (code == QA_KEY_KP_UP) code = QA_KEY_UP;
        else if (code == QA_KEY_KP_DOWN) code = QA_KEY_DOWN;
        else if (code == QA_KEY_KP_LEFT) code = QA_KEY_LEFT;
        else if (code == QA_KEY_KP_RIGHT) code = QA_KEY_RIGHT;
        if (control.kind == QA_UI_FIELD) {
            if (!field_key(ui, &control, code, &handled, error)) return false;
        } else if (control.kind == QA_UI_LIST) {
            if (!list_key(ui, &control, code, &handled, error)) return false;
        }
        if (handled) return true;
        if (code == QA_KEY_LEFT || code == QA_KEY_RIGHT)
            return ui_change(ui, &control, code == QA_KEY_LEFT ? -1 : 1, error);
        if (code == QA_KEY_ENTER || code == QA_KEY_KP_ENTER || (code == QA_KEY_SPACE && control.kind != QA_UI_FIELD))
            return ui_activate(ui, &control, error);
        break;
    }
    if (!down) return true;
    if (code == QA_KEY_ESCAPE) return qa_ui_close(ui, ui->time_ms, error);
    return code == QA_KEY_TAB || code == QA_KEY_UP || code == QA_KEY_DOWN
        ? ui_move(ui, code == QA_KEY_UP || (code == QA_KEY_TAB && ui->shift) ? -1 : 1, error) : true;
}
static bool row_click(qa_ui *ui, const qa_ui_menu *menu, const qa_ui_control *control,
                       size_t row, bool row_action, qa_error *error) {
    const char *key = control->value.list.rows[row].key;
    if (!key) return ui_fail(error, "clicked UI row needs an identity");
    size_t length = strlen(key);
    char *identity = malloc(length + 1);
    if (!identity) { qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining clicked UI row"); return false; }
    memcpy(identity, key, length + 1);
    bool ok = ui_action(ui, control, &(qa_ui_action){.kind = QA_UI_SELECT, .value.row = row}, error);
    qa_ui_menu current;
    if (ok && ui->depth && ui->stack[ui->depth - 1].menu == menu->id) {
        ok = ui_active(ui, &current, error);
        if (ok) for (size_t i = 0; i < current.count; ++i) {
            qa_ui_control next = ui_control(ui, &current, i);
            if (next.id != control->id || next.kind != QA_UI_LIST || !next.enabled || !next.visible) continue;
            size_t picked = row;
            if (picked >= next.value.list.count || (!next.value.list.rows[picked].key || strcmp(next.value.list.rows[picked].key, identity))) {
                picked = next.value.list.count;
                for (size_t j = 0; j < next.value.list.count; ++j)
                    if (next.value.list.rows[j].key && !strcmp(next.value.list.rows[j].key, identity)) { picked = j; break; }
            }
            if (picked < next.value.list.count && next.value.list.rows[picked].enabled)
                ok = ui_action(ui, &next, &(qa_ui_action){.kind = row_action ? QA_UI_ROW_DELETE : QA_UI_ROW_ACTIVATE, .value.row = picked}, error);
            break;
        }
    }
    free(identity);
    return ok;
}
static bool input(qa_ui *ui, const qa_input_event *event, qa_error *error) {
    if (event->kind == QA_INPUT_EVENT_FOCUS) {
        if (!event->down) { ui->shift = ui->control = false; ui_capture_cancel(ui); ui->dragging = 0; ui->menu_dragging = false;
            memset(ui->held_direction, 0, sizeof(ui->held_direction)); }
        return true;
    }
    if (ui->capture) {
        if (event->kind == QA_INPUT_EVENT_KEY && event->down && event->input.code == QA_KEY_ESCAPE) {
            ui_capture_cancel(ui);
            return true;
        }
        if ((event->kind == QA_INPUT_EVENT_KEY && event->down && !event->repeat) ||
            (event->kind == QA_INPUT_EVENT_BUTTON && event->down) ||
            (event->kind == QA_INPUT_EVENT_AXIS && fabsf(event->value) >= .65f) ||
            (event->kind == QA_INPUT_EVENT_WHEEL && event->delta.y != 0)) {
            qa_physical_input physical = event->input;
            if (event->kind == QA_INPUT_EVENT_WHEEL)
                physical = (qa_physical_input){.kind = QA_PHYSICAL_KEY,
                    .code = event->delta.y > 0 ? QA_KEY_WHEEL_UP : QA_KEY_WHEEL_DOWN};
            if (physical.kind == QA_PHYSICAL_AXIS) physical.positive = event->value > 0;
            if (!ui->options.binding)
                return ui_fail(error, "UI binding capture has no binding owner");
            ui->capture = false;
            if (!ui->options.binding(ui->options.context, ui->options.seat, physical, error))
                return false;
            return true;
        }
        return true;
    }
    if (event->kind == QA_INPUT_EVENT_KEY)
        return key(ui, event->input.code, event->down, event->time_ms, error);
    if (event->kind == QA_INPUT_EVENT_AXIS) {
        uint32_t axis = event->input.code;
        if (axis != QA_AXIS_LEFT_X && axis != QA_AXIS_LEFT_Y)
            return true;
        if (fabsf(event->value) < .35f) { ui->held_direction[axis] = 0; return true; }
        if (fabsf(event->value) < .6f) return true;
        int code = axis == QA_AXIS_LEFT_X ? (event->value < 0 ? QA_KEY_LEFT : QA_KEY_RIGHT)
                                         : (event->value < 0 ? QA_KEY_UP : QA_KEY_DOWN);
        if (ui->held_direction[axis] != code) {
            ui->held_direction[axis] = code;
            ui->repeat_at[axis] = event->time_ms + 300;
            return key(ui, (uint32_t)code, true, event->time_ms, error);
        }
        return true;
    }
    if (event->kind == QA_INPUT_EVENT_BUTTON && event->input.kind == QA_PHYSICAL_BUTTON)
        return event->input.code <= 1 ? key(ui, event->input.code ? QA_KEY_ESCAPE : QA_KEY_ENTER,
                                             event->down, event->time_ms, error) : true;
    if (event->kind == QA_INPUT_EVENT_BUTTON && event->input.kind == QA_PHYSICAL_MOUSE &&
        event->input.code == 3 && event->down)
        return qa_ui_close(ui, ui->time_ms, error);
    qa_ui_menu menu;
    if (!ui_active(ui, &menu, error)) return false;
    if (!ui->depth) return true;
    if (event->kind == QA_INPUT_EVENT_MOUSE) {
        ui->pointer = event->position; ui->has_pointer = true;
        ui->cursor = (qa_vec2){(event->position.x - ui->bias_x) / ui->scale,
                                     (event->position.y - ui->bias_y) / ui->scale};
    }
    bool primary = event->kind == QA_INPUT_EVENT_BUTTON && event->input.kind == QA_PHYSICAL_MOUSE && event->input.code == 1;
    if (primary && !event->down) { ui->dragging = 0; ui->menu_dragging = false; return true; }
    if (menu.scrollable && menu.content_height > menu.scroll_rect.height &&
        (ui->menu_dragging || (primary && event->down && ui_inside(menu.scroll_rect, ui->cursor) &&
            ui->cursor.x >= menu.scroll_rect.x + menu.scroll_rect.width - 16))) {
        float height = menu.scroll_rect.height, maximum = menu.content_height - height;
        float thumb = fminf(height, fmaxf(24, height * height / menu.content_height));
        float travel = height - thumb;
        if (!ui->menu_dragging) {
            float top = menu.scroll_rect.y + travel * ui->stack[ui->depth - 1].scroll / maximum;
            ui->drag_offset = ui->cursor.y >= top && ui->cursor.y < top + thumb ? ui->cursor.y - top : thumb * .5f;
            ui->menu_dragging = true;
        }
        ui->stack[ui->depth - 1].scroll = travel > 0 ? fmaxf(0, fminf(1,
            (ui->cursor.y - menu.scroll_rect.y - ui->drag_offset) / travel)) * maximum : 0;
        return true;
    }
    qa_ui_id focused = ui->stack[ui->depth - 1].control;
    if (event->kind == QA_INPUT_EVENT_WHEEL) {
        for (size_t i = 0; i < menu.count; ++i)
            if (menu.controls[i].id == focused && menu.controls[i].kind == QA_UI_LIST &&
                menu.controls[i].enabled && menu.controls[i].visible) {
                const qa_ui_control *control = &menu.controls[i];
                ui_field *state = ui_list_state(ui, control, error);
                if (!state) return false;
                size_t page = ui_list_page(control);
                size_t maximum = control->value.list.count > page ? control->value.list.count - page : 0;
                state->top = (size_t)fmax(0, fmin((double)maximum, (double)state->top - event->delta.y * 3));
                state->scrolled = true;
                return true;
            }
        if (menu.scrollable) ui->stack[ui->depth - 1].scroll = fmaxf(0, fminf(
            ui->stack[ui->depth - 1].scroll - event->delta.y * 28,
            menu.content_height - menu.scroll_rect.height));
        return true;
    }
    for (size_t remaining = menu.count; remaining; --remaining) {
        qa_ui_control control = ui_control(ui, &menu, remaining - 1);
        if (!control.enabled || !control.visible || control.kind == QA_UI_TEXT)
            continue;
        if (event->kind == QA_INPUT_EVENT_TEXT && control.id == focused)
            return ui_text(ui, &control, event->text, error);
        if (event->kind != QA_INPUT_EVENT_MOUSE && event->kind != QA_INPUT_EVENT_BUTTON)
            continue;
        bool inside = ui_inside(control.rect, ui->cursor) &&
            (!control.scrolls || !menu.scrollable || ui_inside(menu.scroll_rect, ui->cursor));
        if (!inside && ui->dragging != control.id) continue;
        if (inside) ui->stack[ui->depth - 1].control = control.id;
        bool pressed = primary && event->down;
        if (control.kind == QA_UI_OWNER_DRAW && control.value.owner.input) {
            bool handled = false;
            if (!control.value.owner.input(control.context, ui->options.seat, event, &handled, error)) return false;
            if (handled || !ui->depth || ui->stack[ui->depth - 1].menu != menu.id) return true;
        }
        if (control.kind == QA_UI_LIST && control.value.list.count > ui_list_page(&control) &&
            (pressed || ui->dragging == control.id) &&
            (ui->dragging == control.id || ui->cursor.x >= control.rect.x + control.rect.width - 16)) {
            if (!event->down && event->kind == QA_INPUT_EVENT_BUTTON) { ui->dragging = 0; return true; }
            ui_field *state = ui_list_state(ui, &control, error);
            if (!state) return false;
            size_t page = ui_list_page(&control);
            size_t count = control.value.list.count, maximum = count > page ? count - page : 0;
            float thumb = count ? fminf(control.rect.height, fmaxf(24, control.rect.height * (float)page / (float)count)) : control.rect.height;
            float travel = control.rect.height - thumb;
            if (pressed) {
                ui->dragging = control.id;
                float top = maximum ? control.rect.y + travel * (float)state->top / (float)maximum : control.rect.y;
                ui->drag_offset = ui->cursor.y >= top && ui->cursor.y < top + thumb ? ui->cursor.y - top : thumb * .5f;
            }
            state->top = travel <= 0 ? 0 : (size_t)floorf(fmaxf(0, fminf(1,
                (ui->cursor.y - control.rect.y - ui->drag_offset) / travel)) * (float)maximum + .5f);
            state->scrolled = true;
            return true;
        }
        if (control.kind == QA_UI_SLIDER && (pressed || ui->dragging == control.id)) {
            if (!event->down && event->kind == QA_INPUT_EVENT_BUTTON) { ui->dragging = 0; return true; }
            if (pressed) ui->dragging = control.id;
            double minimum = control.value.slider.minimum, maximum = control.value.slider.maximum;
            double step = control.value.slider.step;
            if (!isfinite(step) || step <= 0 || !isfinite(minimum) || !isfinite(maximum) || minimum > maximum)
                return ui_fail(error, "invalid UI pointer slider");
            double fraction = fmax(0, fmin(1, (ui->cursor.x - control.rect.x - control.rect.width * .6f) /
                fmaxf(1, control.rect.width * .32f)));
            return ui_action(ui, &control, &(qa_ui_action){.kind = QA_UI_CHANGE_NUMBER,
                .value.number = fmax(minimum, fmin(maximum,
                    minimum + floor(fraction * (maximum - minimum) / step + .5) * step))}, error);
        }
        if (pressed && control.kind == QA_UI_LIST) {
            ui_field *state = ui_list_state(ui, &control, error);
            if (!state) return false;
            size_t row = state->top + (size_t)fmaxf(0, floorf((ui->cursor.y - control.rect.y) /
                                                                fmaxf(1, control.value.list.row_height)));
            if (row < control.value.list.count && control.value.list.rows[row].enabled) {
                float content_width = control.rect.width -
                    (control.value.list.count > ui_list_page(&control) ? 16 : 0);
                bool row_action = control.value.list.rows[row].action_label &&
                    ui->cursor.x >= control.rect.x + content_width - 28;
                return row_click(ui, &menu, &control, row, row_action, error);
            }
            return true;
        }
        if (pressed && control.kind == QA_UI_FIELD) {
            ui_field *state = ui_field_get(ui, control.id, error);
            if (!state) return false;
            const char *value = control.value.field.text ? control.value.field.text : "";
            qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
            size_t offset = 0, index = 0;
            uint32_t scalar;
            float width = 0, position = ui->cursor.x - control.rect.x - control.rect.width * .5f;
            state->cursor = state->top;
            while (qa_utf8_next(bytes, &offset, &scalar)) {
                if (index++ < state->top) continue;
                width += ui_glyph_width(ui, control.value.field.masked ? '*' : scalar);
                if (width >= position) break;
                ++state->cursor;
            }
            return true;
        }
        if (pressed) return ui_activate(ui, &control, error);
        if (event->kind == QA_INPUT_EVENT_MOUSE) return true;
    }
    if (event->kind == QA_INPUT_EVENT_BUTTON && !event->down) ui->dragging = 0;
    return true;
}
bool qa_ui_input(qa_ui *ui, const qa_input_event *event, bool *consumed, qa_error *error) {
    if (!ui || !event || !consumed || !isfinite(event->time_ms) || ui->handling)
        return ui_fail(error, "invalid or reentrant UI input");
    if (event->time_ms < 0 || event->kind < QA_INPUT_EVENT_KEY || event->kind > QA_INPUT_EVENT_TOUCH ||
        ((event->kind == QA_INPUT_EVENT_KEY || event->kind == QA_INPUT_EVENT_BUTTON || event->kind == QA_INPUT_EVENT_AXIS) &&
         !qa_input_physical_valid(event->input)) ||
        (event->kind == QA_INPUT_EVENT_KEY && event->input.kind != QA_PHYSICAL_KEY) ||
        (event->kind == QA_INPUT_EVENT_BUTTON && event->input.kind != QA_PHYSICAL_BUTTON && event->input.kind != QA_PHYSICAL_MOUSE) ||
        (event->kind == QA_INPUT_EVENT_AXIS && (event->input.kind != QA_PHYSICAL_AXIS || !isfinite(event->value))) ||
        ((event->kind == QA_INPUT_EVENT_MOUSE || event->kind == QA_INPUT_EVENT_WHEEL || event->kind == QA_INPUT_EVENT_TOUCH) &&
         (!isfinite(event->position.x) || !isfinite(event->position.y) || !isfinite(event->delta.x) || !isfinite(event->delta.y))))
        return ui_fail(error, "invalid UI physical event");
    if (!ui->options.input_now_ms) ui->time_ms = event->time_ms;
    *consumed = ui->depth != 0;
    if (!*consumed) return true;
    ui->handling = true;
    bool ok = input(ui, event, error);
    ui->handling = false;
    return ok;
}
bool qa_ui_tick(qa_ui *ui, double time, qa_error *error) {
    if (!ui || !isfinite(time) || time < 0 || ui->handling) return ui_fail(error, "invalid UI tick");
    double input_time;
    if (!ui_input_time(ui,time,&input_time,error)) return false;
    ui->time_ms = time;
    ui->handling = true;
    bool ok = true;
    for (size_t axis = 0; axis < QA_AXIS_COUNT && ok && ui->depth; ++axis)
        if (ui->held_direction[axis] && input_time >= ui->repeat_at[axis]) {
            ui->repeat_at[axis] = input_time + 80;
            ok = key(ui, (uint32_t)ui->held_direction[axis], true, input_time, error);
        }
    ui->handling = false;
    return ok;
}

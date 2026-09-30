#include "seat_internal.h"
#include "qa/console_seat_save.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void clear_staged(qa_seat_console *seat) {
    while (seat->staged_first) {
        staged_line *next = seat->staged_first->next;
        free(seat->staged_first);
        seat->staged_first = next;
    }
    seat->staged_last = NULL;
}
qa_seat_console *qa_seat_console_create(const qa_seat_console_options *options, qa_error *error) {
    if (!options || !options->commands || !options->now_ms || !options->connected ||
        !options->focus || !options->chat) {
        qac_fail(error, QA_ERROR_ARGUMENT, "missing seat console services");
        return NULL;
    }
    qa_seat_console *seat = calloc(1, sizeof(*seat));
    if (!seat) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating seat console");
        return NULL;
    }
    seat->options = *options;
    seat->staged = options->staged;
    if (options->command.script) {
        seat->script = qac_copy(options->command.script, error);
        if (!seat->script)
            goto fail;
        seat->options.command.script = seat->script;
    }
    seat->buffer = qa_console_buffer_create(options->command.dialect, 78, 32768, error);
    if (!seat->buffer)
        goto fail;
    seat->field = qa_text_field_create(1023, 78, error);
    if (!seat->field)
        goto fail;
    seat->chat = qa_text_field_create(1023, 30, error);
    if (!seat->chat)
        goto fail;
    seat->history = qa_console_history_create(32, error);
    if (!seat->history)
        goto fail;
    return seat;
fail:
    qa_seat_console_destroy(seat);
    return NULL;
}
void qa_seat_console_destroy(qa_seat_console *seat) {
    if (!seat)
        return;
    clear_staged(seat);
    qa_console_buffer_destroy(seat->buffer);
    qa_text_field_destroy(seat->field);
    qa_text_field_destroy(seat->chat);
    qa_console_history_destroy(seat->history);
    free(seat->script);
    free(seat);
}
qa_console_buffer *qa_seat_console_buffer(qa_seat_console *seat) { return seat->buffer; }
qa_text_field *qa_seat_console_field(qa_seat_console *seat, bool chat) {
    return chat ? seat->chat : seat->field;
}
qa_console_history *qa_seat_console_history(qa_seat_console *seat) { return seat->history; }
static bool print_inner(qa_seat_console *seat, const char *text, qa_error *error) {
    double time = seat->options.now_ms(seat->options.context);
    size_t length = strlen(text);
    staged_line *line = NULL;
    if (seat->staged) {
        if (length > SIZE_MAX - sizeof(*line) - 1)
            return qac_fail(error, QA_ERROR_MEMORY, "staged console output overflow");
        line = malloc(sizeof(*line) + length + 1);
        if (!line)
            return qac_fail(error, QA_ERROR_MEMORY, "allocating staged console output");
        *line = (staged_line){.dialect = seat->options.command.dialect, .time = time};
        memcpy(line->text, text, length + 1);
    }
    qa_console_buffer_dialect(seat->buffer, seat->options.command.dialect);
    if (!qa_console_buffer_print(seat->buffer, (qa_bytes){(const uint8_t *)text, length}, time,
                                 error)) {
        free(line);
        return false;
    }
    if (line) {
        if (seat->staged_last)
            seat->staged_last->next = line;
        else
            seat->staged_first = line;
        seat->staged_last = line;
    }
    return true;
}
void qa_seat_console_publish(qa_seat_console *seat, qa_input_focus focus) {
    clear_staged(seat);
    seat->staged = false;
    seat->opened = focus == QA_INPUT_CONSOLE;
    seat->control = seat->shift = seat->suppress_toggle_text = false;
}
static bool adopt_inner(qa_seat_console *seat, qa_seat_console *candidate, qa_input_focus focus,
                           qa_error *error) {
    if (seat == candidate || !candidate->staged ||
        seat->options.commands != candidate->options.commands ||
        seat->options.command.session != candidate->options.command.session ||
        seat->options.command.seat != candidate->options.command.seat)
        return qac_fail(error, QA_ERROR_ARGUMENT,
                        "console publication changed retained seat or command owner");
    for (staged_line *line = candidate->staged_first; line; line = line->next) {
        qa_console_buffer_dialect(seat->buffer, line->dialect);
        if (!qa_console_buffer_print(seat->buffer,
                                     (qa_bytes){(const uint8_t *)line->text, strlen(line->text)},
                                     line->time, error))
            return false;
    }
    free(seat->script);
    seat->script = candidate->script;
    candidate->script = NULL;
    seat->options = candidate->options;
    seat->options.command.script = seat->script;
    candidate->options.command.script = NULL;
    qa_console_buffer_dialect(seat->buffer, seat->options.command.dialect);
    qa_seat_console_publish(candidate, focus);
    qa_seat_console_publish(seat, focus);
    return true;
}
static bool open_inner(qa_seat_console *seat, bool open, qa_error *error) {
    seat->opened = open;
    qa_text_field_clear(seat->field);
    qa_console_buffer_clear_notify(seat->buffer);
    return seat->options.focus(seat->options.context, open ? QA_INPUT_CONSOLE : QA_INPUT_GAME,
                               false, error);
}
static bool toggle_inner(qa_seat_console *seat, bool from_key, bool repeat, qa_error *error) {
    if (from_key) {
        seat->suppress_toggle_text = true;
        if (repeat)
            return true;
    }
    return qa_seat_console_open(seat, !seat->opened, error);
}
static bool message_inner(qa_seat_console *seat, bool team, bool targeted, int32_t target,
                             qa_error *error) {
    qa_text_field_clear(seat->chat);
    seat->targeted = targeted;
    seat->chat_target = target;
    return seat->options.focus(seat->options.context, QA_INPUT_CHAT, team, error);
}
float qa_seat_console_animate(qa_seat_console *seat, bool open, float elapsed, float speed,
                              float target) {
    float destination = open ? target : 0, step = speed * elapsed / 1000;
    seat->fraction = seat->fraction < destination ? fminf(destination, seat->fraction + step)
                                                  : fmaxf(destination, seat->fraction - step);
    return seat->fraction;
}
static const char *trim_start(const char *text) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    size_t at = 0, start = 0;
    uint32_t code;
    while (qa_utf8_next(bytes, &at, &code)) {
        if (!qa_unicode_whitespace(code))
            break;
        start = at;
    }
    return text + start;
}
static bool submit_inner(qa_seat_console *seat, qa_error *error) {
    const char *text = qa_text_field_read(seat->field).text;
    if (!*text)
        return true;
    qac_text echo = {0}, command = {0};
    qa_command_tokens tokens = {0};
    if (!qac_text_string(&echo, "]", error) || !qac_text_string(&echo, text, error) ||
        !qac_text_string(&echo, "\n", error))
        goto fail;
    if (!qa_seat_console_print(seat, echo.data, error))
        goto fail;
    const char *trimmed = trim_start(text);
    bool explicit_command = *trimmed == '/' || *trimmed == '\\';
    if (explicit_command)
        ++trimmed;
    if (!qa_command_tokenize(trimmed, seat->options.command.dialect, true, &tokens, error))
        goto fail;
    qa_console_discovery_entry entry;
    bool known =
        tokens.count && qa_console_discovery_find(seat->options.commands, &seat->options.command,
                                                  tokens.values[0], &entry);
    bool chat = (seat->options.command.dialect == QA_CONSOLE_QW ||
                 seat->options.command.dialect == QA_CONSOLE_Q3) &&
                seat->options.connected(seat->options.context);
    if (!explicit_command && !known && chat) {
        if (!seat->options.chat(seat->options.context, text, false, false, 0, error))
            goto fail;
    } else {
        if (!qac_text_string(&command, trimmed, error) || !qac_text_string(&command, "\n", error) ||
            !qa_console_append(seat->options.commands, &seat->options.command, command.data, error))
            goto fail;
    }
    if (!qa_console_history_add(seat->history, text, error))
        goto fail;
    qa_text_field_clear(seat->field);
    qa_console_buffer_bottom(seat->buffer);
    free(echo.data);
    free(command.data);
    qa_command_tokens_free(&tokens);
    return true;
fail:
    free(echo.data);
    free(command.data);
    qa_command_tokens_free(&tokens);
    return false;
}
static bool complete(qa_seat_console *seat, qa_error *error) {
    qa_console_discovery entries = {0};
    if (!qa_console_discover(seat->options.commands, &seat->options.command, &entries, error))
        return false;
    const char **names = entries.count ? malloc(entries.count * sizeof(*names)) : NULL;
    if (entries.count && !names) {
        qa_console_discovery_free(&entries);
        return qac_fail(error, QA_ERROR_MEMORY, "allocating completion names");
    }
    for (size_t i = 0; i < entries.count; ++i)
        names[i] = entries.entries[i].name;
    bool ok = qa_text_field_complete(seat->field, names, entries.count, seat->shift, error);
    free(names);
    qa_console_discovery_free(&entries);
    return ok;
}
static bool selected_inner(qa_seat_console *seat, qa_console_discovery_entry *out) {
    const char *name = qa_text_field_read(seat->field).completion;
    return name &&
           qa_console_discovery_find(seat->options.commands, &seat->options.command, name, out);
}
static bool input_inner(qa_seat_console *seat, const qa_input_event *event, qa_input_focus focus,
                           bool team, bool *handled, qa_error *error) {
    *handled = true;
    if (event->kind == QA_INPUT_EVENT_FOCUS) {
        if (!event->down)
            seat->control = seat->shift = seat->suppress_toggle_text = false;
        return true;
    }
    if (event->kind == QA_INPUT_EVENT_TEXT && seat->suppress_toggle_text) {
        seat->suppress_toggle_text = false;
        if (!strcmp(event->text, "`") || !strcmp(event->text, "~"))
            return true;
    }
    if (event->kind == QA_INPUT_EVENT_KEY && event->input.code > INT32_MAX)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console key code");
    int key = event->kind == QA_INPUT_EVENT_KEY ? (int)event->input.code : 0;
    if (event->kind == QA_INPUT_EVENT_KEY && event->down && key != '`' && key != '~')
        seat->suppress_toggle_text = false;
    if (focus != QA_INPUT_CONSOLE && focus != QA_INPUT_CHAT) {
        *handled = false;
        return true;
    }
    qa_text_field *field = focus == QA_INPUT_CHAT ? seat->chat : seat->field;
    if (event->kind == QA_INPUT_EVENT_TEXT) {
        qa_text_field_insert(field, (qa_bytes){(const uint8_t *)event->text, strlen(event->text)});
        return true;
    }
    if (event->kind == QA_INPUT_EVENT_WHEEL) {
        double rows = trunc((double)event->delta.y * (seat->control ? 6 : 2));
        if (!isfinite(rows) || rows <= -0x1p63 || rows >= 0x1p63)
            return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console scroll delta");
        qa_console_buffer_scroll(seat->buffer, (int64_t)rows);
        return true;
    }
    if (event->kind != QA_INPUT_EVENT_KEY) {
        *handled = false;
        return true;
    }
    if (key == QA_KEY_CONTROL)
        seat->control = event->down;
    if (key == QA_KEY_SHIFT)
        seat->shift = event->down;
    if (!event->down)
        return true;
    if (key == '`' || key == '~')
        return qa_seat_console_toggle(seat, true, event->repeat, error);
    if (key == QA_KEY_ESCAPE)
        return qa_seat_console_open(seat, false, error);
    if (key == QA_KEY_ENTER || key == QA_KEY_KP_ENTER) {
        if (focus != QA_INPUT_CHAT)
            return qa_seat_console_submit(seat, error);
        const char *text = qa_text_field_read(field).text;
        if (*text && !seat->options.chat(seat->options.context, text, team, seat->targeted,
                                         seat->chat_target, error))
            return false;
        qa_text_field_clear(field);
        return seat->options.focus(seat->options.context, QA_INPUT_GAME, false, error);
    }
    if (focus == QA_INPUT_CONSOLE) {
        if (key == QA_KEY_TAB)
            return complete(seat, error);
        if (key == QA_KEY_UP || key == QA_KEY_KP_UP || (seat->control && key == 'p')) {
            const char *line;
            if (!qa_console_history_previous(seat->history, qa_text_field_read(field).text, &line,
                                             error))
                return false;
            qa_text_field_set(field, line);
            return true;
        }
        if (key == QA_KEY_DOWN || key == QA_KEY_KP_DOWN || (seat->control && key == 'n')) {
            qa_text_field_set(field, qa_console_history_next(seat->history));
            return true;
        }
        if (key == QA_KEY_PAGEUP || key == QA_KEY_PAGEDOWN) {
            qa_console_buffer_scroll(seat->buffer, key == QA_KEY_PAGEUP ? 2 : -2);
            return true;
        }
        if (seat->control && key == QA_KEY_HOME) {
            qa_console_buffer_top(seat->buffer);
            return true;
        }
        if (seat->control && key == QA_KEY_END) {
            qa_console_buffer_bottom(seat->buffer);
            return true;
        }
    }
    qa_field_controls controls = {seat->control, seat->shift, seat->options.context,
                                  seat->options.clipboard};
    return qa_text_field_key(field, key, &controls, handled, error);
}

bool qa_seat_console_idle(const qa_seat_console *seat) {
    return seat && !seat->active_depth && qa_console_idle(seat->options.commands);
}
static bool enter(qa_seat_console *seat, qa_error *error) {
    if (!seat || seat->active_depth == SIZE_MAX)
        return qac_fail(error, QA_ERROR_ARGUMENT, "seat console operation is unavailable");
    ++seat->active_depth;
    return true;
}
static bool leave(qa_seat_console *seat, bool result) {
    --seat->active_depth;
    return result;
}
bool qa_seat_console_print(qa_seat_console *seat, const char *text, qa_error *error) {
    return enter(seat, error) && leave(seat, print_inner(seat, text, error));
}
bool qa_seat_console_adopt(qa_seat_console *seat, qa_seat_console *candidate,
                           qa_input_focus focus, qa_error *error) {
    if (seat == candidate || !qa_seat_console_idle(seat) || !qa_seat_console_idle(candidate))
        return qac_fail(error, QA_ERROR_ARGUMENT, "seat console publication requires idle owners");
    ++seat->active_depth;
    ++candidate->active_depth;
    bool okay = adopt_inner(seat, candidate, focus, error);
    --candidate->active_depth;
    return leave(seat, okay);
}
bool qa_seat_console_open(qa_seat_console *seat, bool open, qa_error *error) {
    return enter(seat, error) && leave(seat, open_inner(seat, open, error));
}
bool qa_seat_console_toggle(qa_seat_console *seat, bool from_key, bool repeat, qa_error *error) {
    return enter(seat, error) && leave(seat, toggle_inner(seat, from_key, repeat, error));
}
bool qa_seat_console_message(qa_seat_console *seat, bool team, bool targeted, int32_t target,
                             qa_error *error) {
    return enter(seat, error) && leave(seat, message_inner(seat, team, targeted, target, error));
}
bool qa_seat_console_submit(qa_seat_console *seat, qa_error *error) {
    return enter(seat, error) && leave(seat, submit_inner(seat, error));
}
bool qa_seat_console_selected(qa_seat_console *seat, qa_console_discovery_entry *out) {
    return enter(seat, NULL) && leave(seat, selected_inner(seat, out));
}
bool qa_seat_console_input(qa_seat_console *seat, const qa_input_event *event, qa_input_focus focus,
                           bool team, bool *handled, qa_error *error) {
    return enter(seat, error) && leave(seat, input_inner(seat, event, focus, team, handled, error));
}

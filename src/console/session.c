/* Port of src/console/session.ts. GPL-2.0-or-later. */
#include "qa/console/session.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"

/* KeyCode values from src/input/key-codes.ts (input module pending). */
enum {
  KEY_TAB = 9,
  KEY_ENTER = 13,
  KEY_ESCAPE = 27,
  KEY_UP = 132,
  KEY_DOWN = 133,
  KEY_CONTROL = 137,
  KEY_SHIFT = 138,
  KEY_PAGE_DOWN = 141,
  KEY_PAGE_UP = 142,
  KEY_HOME = 143,
  KEY_END = 144,
  KEY_KEYPAD_UP = 161,
  KEY_KEYPAD_DOWN = 167,
  KEY_KEYPAD_ENTER = 169
};

/* TS plain `throw new Error(msg)`: same message, then abort. */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

static bool key_held(const qa_seat_console *console, double code) {
  for (size_t i = 0; i < console->key_count; i++)
    if (console->keys[i] == code) return true;
  return false;
}
static void key_add(qa_seat_console *console, double code) {
  if (key_held(console, code)) return;
  if (console->key_count == console->key_capacity) {
    size_t capacity = console->key_capacity == 0 ? 8 : console->key_capacity * 2;
    double *keys = (double *)realloc(console->keys, capacity * sizeof *keys);
    if (keys == NULL) return; /* OOM only */
    console->keys = keys;
    console->key_capacity = capacity;
  }
  console->keys[console->key_count++] = code;
}
static void key_remove(qa_seat_console *console, double code) {
  for (size_t i = 0; i < console->key_count; i++) {
    if (console->keys[i] == code) {
      memmove(console->keys + i, console->keys + i + 1,
              (console->key_count - i - 1) * sizeof(double));
      console->key_count--;
      return;
    }
  }
}

static void staged_clear(qa_seat_console *console) {
  for (size_t i = 0; i < console->staged_count; i++) free(console->staged_output[i].text);
  free(console->staged_output);
  console->staged_output = NULL;
  console->staged_count = 0;
  console->staged_capacity = 0;
}

bool qa_seat_console_init(qa_seat_console *console, const qa_seat_console_options *options) {
  memset(console, 0, sizeof *console);
  if (options->context.session.token != options->seat.session.token ||
      options->context.session.name != options->seat.session.name)
    qa_error("Console belongs to another session");
  console->options = *options;
  if (!qa_console_field_init(&console->field, QA_CONSOLE_FIELD_DEFAULT_MAXIMUM_LENGTH,
                             QA_CONSOLE_FIELD_DEFAULT_WIDTH_IN_CHARS))
    return false;
  if (!qa_console_field_init(&console->chat_field, QA_CONSOLE_FIELD_DEFAULT_MAXIMUM_LENGTH, 30))
    return false;
  if (!qa_console_history_init(&console->history, QA_CONSOLE_HISTORY_DEFAULT_CAPACITY))
    return false;
  if (!qa_console_buffer_init(&console->buffer, options->dialect, QA_CONSOLE_DEFAULT_WIDTH,
                              QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY))
    return false;
  if (options->staged) {
    console->staged_output = (qa_staged_console_output *)calloc(1, sizeof(qa_staged_console_output));
    if (console->staged_output == NULL) return false;
    console->staged_capacity = 1;
  }
  return true;
}

void qa_seat_console_destroy(qa_seat_console *console) {
  qa_console_field_destroy(&console->field);
  qa_console_field_destroy(&console->chat_field);
  qa_console_history_destroy(&console->history);
  qa_console_buffer_destroy(&console->buffer);
  free(console->keys);
  staged_clear(console);
  memset(console, 0, sizeof *console);
}

qa_console_discovery_entry *qa_seat_console_selected_completion_entry(qa_seat_console *console) {
  const char *selected = qa_console_field_selected_completion(&console->field);
  if (selected == NULL) return NULL;
  size_t count = 0;
  qa_console_discovery_entry *entries =
      qa_query_console_entries(console->options.commands, &console->options.context, &count);
  for (size_t i = 0; i < count; i++) {
    if (strcmp(entries[i].name, selected) == 0) {
      qa_console_discovery_entry *found =
          (qa_console_discovery_entry *)malloc(sizeof *found);
      if (found == NULL) {
        qa_console_discovery_entries_free(entries, count);
        return NULL; /* OOM only */
      }
      *found = entries[i];
      memset(&entries[i], 0, sizeof entries[i]);
      qa_console_discovery_entries_free(entries, count);
      return found;
    }
  }
  qa_console_discovery_entries_free(entries, count);
  return NULL;
}

void qa_seat_console_publish(qa_seat_console *console, qa_seat_input_focus focus) {
  staged_clear(console);
  console->opened = focus.kind != NULL && strcmp(focus.kind, "console") == 0;
  console->key_count = 0;
  console->suppress_toggle_text = false;
}

void qa_seat_console_adopt(qa_seat_console *console, qa_seat_console *candidate,
                           qa_seat_input_focus focus) {
  if (!qa_same_seat(candidate->options.seat, console->options.seat) ||
      candidate->options.commands != console->options.commands)
    qa_error("Console publication changed retained seat or command owner");
  if (candidate->staged_output == NULL) qa_error("Console candidate already published");
  console->options = candidate->options;
  for (size_t i = 0; i < candidate->staged_count; i++) {
    qa_console_buffer_set_dialect(&console->buffer, candidate->staged_output[i].dialect);
    qa_console_buffer_print(&console->buffer, candidate->staged_output[i].text,
                            candidate->staged_output[i].time);
  }
  qa_console_buffer_set_dialect(&console->buffer, console->options.dialect);
  qa_seat_console_publish(candidate, focus);
  qa_seat_console_publish(console, focus);
}

void qa_seat_console_print(qa_seat_console *console, const char *text) {
  double time = console->options.now(console->options.now_ctx);
  qa_command_dialect dialect = console->options.dialect;
  if (console->staged_output != NULL) {
    if (console->staged_count == console->staged_capacity) {
      size_t capacity = console->staged_capacity == 0 ? 8 : console->staged_capacity * 2;
      qa_staged_console_output *grown = (qa_staged_console_output *)realloc(
          console->staged_output, capacity * sizeof *grown);
      if (grown != NULL) {
        console->staged_output = grown;
        console->staged_capacity = capacity;
      }
    }
    if (console->staged_count < console->staged_capacity) {
      char *copy = dup_text(text);
      if (copy != NULL) {
        console->staged_output[console->staged_count].text = copy;
        console->staged_output[console->staged_count].time = time;
        console->staged_output[console->staged_count].dialect = dialect;
        console->staged_count++;
      }
    }
  }
  qa_console_buffer_set_dialect(&console->buffer, dialect);
  qa_console_buffer_print(&console->buffer, text, time);
}

static void send_focus(qa_seat_console *console, const char *kind, bool team) {
  qa_seat_input_focus focus;
  memset(&focus, 0, sizeof focus);
  focus.kind = kind;
  focus.team = team;
  focus.text = "";
  console->options.focus(console->options.focus_ctx, focus);
}

void qa_seat_console_open(qa_seat_console *console) {
  console->opened = true;
  qa_console_field_clear(&console->field);
  qa_console_buffer_clear_notify(&console->buffer);
  send_focus(console, "console", false);
}

void qa_seat_console_close(qa_seat_console *console) {
  console->opened = false;
  qa_console_field_clear(&console->field);
  qa_console_buffer_clear_notify(&console->buffer);
  send_focus(console, "game", false);
}

void qa_seat_console_toggle(qa_seat_console *console) {
  if (console->opened)
    qa_seat_console_close(console);
  else
    qa_seat_console_open(console);
}

void qa_seat_console_toggle_from_key(qa_seat_console *console, bool repeat) {
  console->suppress_toggle_text = true;
  if (!repeat) qa_seat_console_toggle(console);
}

void qa_seat_console_message(qa_seat_console *console, bool team, bool has_target,
                             double target) {
  qa_console_field_clear(&console->chat_field);
  console->has_chat_target = has_target;
  console->chat_target = target;
  send_focus(console, "chat", team);
}

double qa_seat_console_animate(qa_seat_console *console, bool open,
                               double elapsed_milliseconds, double speed,
                               double target_fraction) {
  double target = open ? target_fraction : 0;
  double step = speed * elapsed_milliseconds / 1000;
  if (console->fraction < target) {
    double next = console->fraction + step;
    console->fraction = next < target ? next : target;
  } else {
    double next = console->fraction - step;
    console->fraction = next > target ? next : target;
  }
  return console->fraction;
}

void qa_seat_console_submit(qa_seat_console *console) {
  char *text = qa_console_field_text(&console->field);
  if (text == NULL || text[0] == '\0') {
    free(text);
    return;
  }
  size_t length = strlen(text);
  char *echo = (char *)malloc(length + 3);
  if (echo != NULL) {
    sprintf(echo, "]%s\n", text);
    qa_seat_console_print(console, echo);
    free(echo);
  }
  const char *command_text = text;
  while (*command_text == ' ' || (*command_text >= '\t' && *command_text <= '\r')) command_text++;
  bool explicit = command_text[0] == '/' || command_text[0] == '\\';
  const char *body = explicit ? command_text + 1 : command_text;
  qa_command_tokens tokens;
  const char *first = "";
  bool have_tokens = qa_tokenize_command(body, console->options.dialect,
                                         QA_COMMAND_TEXT_SOURCE, &tokens);
  if (have_tokens && tokens.argc > 0) first = tokens.argv[0];
  size_t entry_count = 0;
  qa_console_discovery_entry *entries = qa_query_console_entries(
      console->options.commands, &console->options.context, &entry_count);
  char *folded_first = qa_ascii_fold(first);
  bool known = false;
  for (size_t i = 0; i < entry_count && !known; i++) {
    char *folded_name = qa_ascii_fold(entries[i].name);
    known = folded_first != NULL && folded_name != NULL &&
            strcmp(folded_name, folded_first) == 0;
    free(folded_name);
  }
  free(folded_first);
  qa_console_discovery_entries_free(entries, entry_count);
  if (have_tokens) qa_command_tokens_destroy(&tokens);
  bool chat = console->options.connected(console->options.connected_ctx) &&
              (console->options.dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ||
               console->options.dialect == QA_COMMAND_DIALECT_Q3);
  if (!explicit && !known && chat) {
    console->options.chat(console->options.chat_ctx, text, false, false, 0);
  } else {
    size_t body_length = strlen(body);
    char *line = (char *)malloc(body_length + 2);
    if (line != NULL) {
      sprintf(line, "%s\n", body);
      qa_command_buffer_append(console->options.commands, line, &console->options.context, -1);
      free(line);
    }
  }
  qa_console_history_add(&console->history, text);
  qa_console_field_clear(&console->field);
  qa_console_buffer_bottom(&console->buffer);
  free(text);
}

bool qa_seat_console_input(qa_seat_console *console, const qa_seat_input_event *event,
                           qa_seat_input_focus focus) {
  if (!qa_same_seat(event->seat, console->options.seat))
    qa_error("Console event belongs to another seat");
  const char *kind = event->kind == NULL ? "" : event->kind;
  if (strcmp(kind, "focus") == 0) {
    if (!event->focused) {
      console->key_count = 0;
      console->suppress_toggle_text = false;
    }
    return true;
  }
  if (strcmp(kind, "text") == 0 && console->suppress_toggle_text) {
    console->suppress_toggle_text = false;
    if (event->text != NULL && (strcmp(event->text, "`") == 0 || strcmp(event->text, "~") == 0))
      return true;
  }
  if (strcmp(kind, "key") == 0 && event->down && event->code != 96 && event->code != 126)
    console->suppress_toggle_text = false;
  const char *focus_kind = focus.kind == NULL ? "" : focus.kind;
  bool is_chat = strcmp(focus_kind, "chat") == 0;
  if (!is_chat && strcmp(focus_kind, "console") != 0) return false;
  qa_console_field *field = is_chat ? &console->chat_field : &console->field;
  if (strcmp(kind, "text") == 0) {
    qa_console_field_insert(field, event->text == NULL ? "" : event->text);
    return true;
  }
  if (strcmp(kind, "mouse-wheel") == 0) {
    qa_console_buffer_scroll(&console->buffer,
                             event->delta.y * (key_held(console, KEY_CONTROL) ? 6 : 2));
    return true;
  }
  if (strcmp(kind, "key") != 0) return false;
  if (event->down) {
    key_add(console, event->code);
  } else {
    key_remove(console, event->code);
    return true;
  }
  double code = event->code;
  bool control = key_held(console, KEY_CONTROL);
  bool shift = key_held(console, KEY_SHIFT);
  if (code == 96 || code == 126) {
    qa_seat_console_toggle_from_key(console, event->repeat);
    return true;
  }
  if (code == KEY_ESCAPE) {
    qa_seat_console_close(console);
    return true;
  }
  if (code == KEY_ENTER || code == KEY_KEYPAD_ENTER) {
    if (is_chat) {
      char *text = qa_console_field_text(field);
      if (text != NULL && text[0] != '\0')
        console->options.chat(console->options.chat_ctx, text, focus.team,
                              console->has_chat_target, console->chat_target);
      free(text);
      qa_console_field_clear(field);
      send_focus(console, "game", false);
    } else {
      qa_seat_console_submit(console);
    }
    return true;
  }
  if (!is_chat) {
    if (code == KEY_TAB) {
      size_t entry_count = 0;
      qa_console_discovery_entry *entries = qa_query_console_entries(
          console->options.commands, &console->options.context, &entry_count);
      const char **names = entry_count == 0
                               ? NULL
                               : (const char **)malloc(entry_count * sizeof *names);
      if (names != NULL) {
        for (size_t i = 0; i < entry_count; i++) names[i] = entries[i].name;
        qa_console_field_complete(field, names, entry_count, shift);
        free(names);
      }
      qa_console_discovery_entries_free(entries, entry_count);
      return true;
    }
    if (code == KEY_UP || code == KEY_KEYPAD_UP || (control && code == 112)) {
      char *current = qa_console_field_text(field);
      if (current != NULL) {
        const char *previous = qa_console_history_previous(&console->history, current);
        qa_console_field_set_text(field, previous);
        free(current);
      }
      return true;
    }
    if (code == KEY_DOWN || code == KEY_KEYPAD_DOWN || (control && code == 110)) {
      qa_console_field_set_text(field, qa_console_history_next(&console->history));
      return true;
    }
    if (code == KEY_PAGE_UP || code == KEY_PAGE_DOWN) {
      qa_console_buffer_scroll(&console->buffer, code == KEY_PAGE_UP ? 2 : -2);
      return true;
    }
    if (control && code == KEY_HOME) {
      qa_console_buffer_top(&console->buffer);
      return true;
    }
    if (control && code == KEY_END) {
      qa_console_buffer_bottom(&console->buffer);
      return true;
    }
  }
  return qa_console_field_key(field, (int64_t)code, control, shift,
                              console->options.clipboard, console->options.clipboard_ctx);
}

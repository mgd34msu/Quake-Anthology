/* Port of src/console/dedicated.ts. GPL-2.0-or-later. */
#include "qa/console/dedicated.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}

bool qa_dedicated_console_init(qa_dedicated_console *console) {
  memset(console, 0, sizeof *console);
  return qa_event_ring_init(&console->lines, 16384, sizeof(char *));
}

static void free_queued(qa_dedicated_console *console) {
  size_t count = 0;
  char **lines = (char **)qa_event_ring_drain_all(&console->lines, &count);
  for (size_t i = 0; i < count; i++) free(lines[i]);
  free(lines);
}

void qa_dedicated_console_destroy(qa_dedicated_console *console) {
  free_queued(console);
  qa_event_ring_destroy(&console->lines);
  free(console->pending);
  free(console->error_message);
  memset(console, 0, sizeof *console);
}

static void admit_line(qa_dedicated_console *console, const char *text,
                       size_t length) {
  if (length > 0 && text[length - 1] == '\r') length--;
  char *line = dup_range(text, length);
  if (line != NULL) (void)qa_event_ring_admit(&console->lines, &line);
}

static void split_pending(qa_dedicated_console *console) {
  for (;;) {
    if (console->pending == NULL || console->pending_length == 0) return;
    char *newline = (char *)memchr(console->pending, '\n',
                                   console->pending_length);
    if (newline == NULL) return;
    size_t index = (size_t)(newline - console->pending);
    admit_line(console, console->pending, index);
    size_t rest = console->pending_length - index - 1;
    memmove(console->pending, console->pending + index + 1, rest);
    console->pending_length = rest;
  }
}

void qa_dedicated_console_feed(qa_dedicated_console *console,
                               const char *data, size_t length) {
  if (console->closed) return;
  if (console->pending_length + length + 1 > console->pending_capacity) {
    size_t cap = console->pending_capacity == 0 ? 256
                                                : console->pending_capacity;
    while (cap < console->pending_length + length + 1) cap *= 2;
    char *grown = (char *)realloc(console->pending, cap);
    if (grown == NULL) return;
    console->pending = grown;
    console->pending_capacity = cap;
  }
  memcpy(console->pending + console->pending_length, data, length);
  console->pending_length += length;
  split_pending(console);
}

void qa_dedicated_console_feed_text(qa_dedicated_console *console,
                                    const char *text) {
  qa_dedicated_console_feed(console, text, strlen(text));
}

void qa_dedicated_console_end(qa_dedicated_console *console) {
  if (console->closed) return;
  split_pending(console);
  if (console->pending_length > 0) {
    /* decoder.end() text was already fed as bytes; see header note. */
    char *line = dup_range(console->pending, console->pending_length);
    if (line != NULL) (void)qa_event_ring_admit(&console->lines, &line);
  }
  console->pending_length = 0;
  console->ended = true;
}

void qa_dedicated_console_fail(qa_dedicated_console *console,
                               const char *message) {
  if (console->closed) return;
  free(console->error_message);
  console->error_message = dup_range(message, strlen(message));
}

bool qa_dedicated_console_eof(const qa_dedicated_console *console) {
  return console->ended && qa_event_ring_live(&console->lines) == 0;
}

size_t qa_dedicated_console_drain(qa_dedicated_console *console,
                                  qa_command_buffer *commands) {
  if (console->closed) qa_error("Dedicated console is closed");
  if (console->error_message != NULL) qa_error(console->error_message);
  size_t count = 0;
  char **lines = (char **)qa_event_ring_drain_all(&console->lines, &count);
  for (size_t i = 0; i < count; i++) {
    size_t length = strlen(lines[i]);
    char *text = (char *)malloc(length + 2);
    if (text != NULL) {
      memcpy(text, lines[i], length);
      text[length] = '\n';
      text[length + 1] = '\0';
      qa_command_buffer_append(commands, text, NULL, -1);
      free(text);
    }
    free(lines[i]);
  }
  free(lines);
  return count;
}

void qa_dedicated_console_close(qa_dedicated_console *console) {
  if (console->closed) return;
  console->closed = true;
}

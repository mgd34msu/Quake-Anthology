/*
 * Port of src/console/session.ts (SeatConsole): one seat's console
 * field/history/buffer plus staged-publication support.
 * C accommodations:
 *  - Option closures become function pointers with per-callback ctx.
 *    The chat target (TS number | null) is has_target + double.
 *  - The TS plain-Error throws (foreign session/seat, changed
 *    publication owner, double publish) abort with the same
 *    messages, as they reach the TS top-level crash handler.
 *  - Event/focus kinds are the contracts' exact kind strings; key
 *    codes arrive as the contracts' double and compare against the
 *    KeyCode values of src/input/key-codes.ts (named in session.c).
 *  - selectedCompletionEntry returns a malloc'd single entry (free
 *    with qa_console_discovery_entries_free(entry, 1)) or NULL for
 *    the TS undefined.
 * Ownership: the console owns its fields, history, buffer, key set
 * and staged output; options are copied by value (the command
 * buffer/cvar registry pointers stay borrowed). init returns false
 * only on OOM. GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_SESSION_H
#define QA_CONSOLE_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/console/buffer.h"
#include "qa/console/discovery.h"
#include "qa/console/field.h"
#include "qa/contracts/common.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"

typedef struct qa_seat_console_options {
  bool staged;
  qa_seat_id seat;
  qa_command_dialect dialect;
  qa_command_context context;
  qa_command_buffer *commands; /* borrowed */
  qa_cvar_registry *cvars;     /* borrowed */
  double (*now)(void *ctx);
  void *now_ctx;
  bool (*connected)(void *ctx);
  void *connected_ctx;
  qa_console_clipboard_fn clipboard;
  void *clipboard_ctx;
  void (*focus)(void *ctx, qa_seat_input_focus focus);
  void *focus_ctx;
  void (*chat)(void *ctx, const char *text, bool team, bool has_target, double target);
  void *chat_ctx;
} qa_seat_console_options;

typedef struct qa_staged_console_output {
  char *text; /* owned */
  double time;
  qa_command_dialect dialect;
} qa_staged_console_output;

typedef struct qa_seat_console {
  qa_seat_console_options options;
  qa_console_field field;
  qa_console_field chat_field;
  qa_console_history history;
  qa_console_buffer buffer;
  double *keys; /* held key codes (TS Set<number>) */
  size_t key_count;
  size_t key_capacity;
  bool has_chat_target;
  double chat_target;
  double fraction;
  bool opened;
  bool suppress_toggle_text;
  /* NULL mirrors the TS null (published); otherwise staged lines. */
  qa_staged_console_output *staged_output;
  size_t staged_count;
  size_t staged_capacity;
} qa_seat_console;

/* Aborts when the context belongs to another session. False on OOM. */
bool qa_seat_console_init(qa_seat_console *console, const qa_seat_console_options *options);
void qa_seat_console_destroy(qa_seat_console *console);
/* Malloc'd entry or NULL (TS undefined); see header note. */
qa_console_discovery_entry *qa_seat_console_selected_completion_entry(qa_seat_console *console);
void qa_seat_console_publish(qa_seat_console *console, qa_seat_input_focus focus);
void qa_seat_console_adopt(qa_seat_console *console, qa_seat_console *candidate,
                           qa_seat_input_focus focus);
void qa_seat_console_print(qa_seat_console *console, const char *text);
void qa_seat_console_open(qa_seat_console *console);
void qa_seat_console_close(qa_seat_console *console);
void qa_seat_console_toggle(qa_seat_console *console);
void qa_seat_console_toggle_from_key(qa_seat_console *console, bool repeat);
/* has_target false is the TS default target null. */
void qa_seat_console_message(qa_seat_console *console, bool team, bool has_target,
                             double target);
/* TS defaults: speed 3, target_fraction 0.5. Returns the fraction. */
double qa_seat_console_animate(qa_seat_console *console, bool open,
                               double elapsed_milliseconds, double speed,
                               double target_fraction);
void qa_seat_console_submit(qa_seat_console *console);
bool qa_seat_console_input(qa_seat_console *console, const qa_seat_input_event *event,
                           qa_seat_input_focus focus);

#endif

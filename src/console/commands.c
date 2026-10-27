/* Port of src/console/commands.ts. GPL-2.0-or-later. */
#include "qa/console/commands.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

static char *join2(const char *first, const char *second) {
  size_t a = strlen(first);
  size_t b = strlen(second);
  char *out = (char *)malloc(a + b + 1);
  if (out != NULL) {
    memcpy(out, first, a);
    memcpy(out + a, second, b + 1);
  }
  return out;
}

static bool ends_with(const char *text, const char *suffix) {
  size_t length = strlen(text);
  size_t tail = strlen(suffix);
  return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

static void print_wrote(qa_console_command_services *services,
                        const char *path) {
  size_t length = strlen(path) + 8;
  char *text = (char *)malloc(length);
  if (text == NULL) return;
  (void)snprintf(text, length, "Wrote %s\n", path);
  services->print(services->ctx, text);
  free(text);
}

static bool seat_of(qa_command_invocation *invocation, qa_seat_id *out) {
  const qa_command_origin *origin = &invocation->source.origin;
  while (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SCRIPT) {
    origin = origin->caller;
  }
  if (origin == NULL || origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    return false;
  }
  *out = origin->seat;
  return true;
}

typedef qa_console_command_registration registration;

static void toggleconsole_handler(void *ctx, qa_command_invocation *invocation) {
  registration *reg = (registration *)ctx;
  qa_seat_id seat;
  qa_seat_console *console = seat_of(invocation, &seat)
      ? reg->services.console(reg->services.ctx, seat)
      : NULL;
  if (console != NULL) qa_seat_console_toggle(console);
}

static void clear_handler(void *ctx, qa_command_invocation *invocation) {
  registration *reg = (registration *)ctx;
  qa_seat_id seat;
  qa_seat_console *console = seat_of(invocation, &seat)
      ? reg->services.console(reg->services.ctx, seat)
      : NULL;
  if (console != NULL) qa_console_buffer_clear(&console->buffer);
}

static void message_impl(void *ctx, qa_command_invocation *invocation,
                         bool team) {
  registration *reg = (registration *)ctx;
  qa_seat_id seat;
  bool has_seat = seat_of(invocation, &seat);
  if (!reg->services.can_chat(reg->services.ctx)) {
    reg->services.print(reg->services.ctx,
                        "Chat requires an active connection.\n");
    return;
  }
  if (has_seat) {
    qa_seat_console *console = reg->services.console(reg->services.ctx, seat);
    if (console != NULL) qa_seat_console_message(console, team, false, 0);
  }
}

static void messagemode_handler(void *ctx, qa_command_invocation *invocation) {
  message_impl(ctx, invocation, false);
}

static void messagemode2_handler(void *ctx, qa_command_invocation *invocation) {
  message_impl(ctx, invocation, true);
}

typedef struct dump_operation {
  qa_console_command_services services;
  qa_config_store *store; /* borrowed, captured as the TS closes over it */
  char *path;
  char *contents;
} dump_operation;

static void condump_operation_run(void *op_ctx) {
  dump_operation *op = (dump_operation *)op_ctx;
  if (!qa_config_store_dump(op->store, op->path, op->contents)) {
    free(op->path);
    free(op->contents);
    free(op);
    qa_error("condump write failed");
  }
  size_t length = strlen(op->path) + 26;
  char *text = (char *)malloc(length);
  if (text != NULL) {
    (void)snprintf(text, length, "Dumped console text to %s\n", op->path);
    op->services.print(op->services.ctx, text);
    free(text);
  }
  free(op->path);
  free(op->contents);
  free(op);
}

static void condump_handler(void *ctx, qa_command_invocation *invocation) {
  registration *reg = (registration *)ctx;
  qa_seat_id seat;
  bool has_seat = seat_of(invocation, &seat);
  const char *name = invocation->argc > 1 ? invocation->argv[1] : NULL;
  qa_seat_console *console = has_seat
      ? reg->services.console(reg->services.ctx, seat)
      : NULL;
  if (name == NULL || console == NULL || !has_seat || invocation->argc != 2) {
    reg->services.print(reg->services.ctx, "condump <filename>\n");
    return;
  }
  char *path = qa_command_dialect_is_q2(invocation->dialect) &&
                       !ends_with(name, ".txt")
                   ? join2(name, ".txt")
                   : dup_text(name);
  char *contents = qa_console_buffer_dump(&console->buffer);
  dump_operation *op = (dump_operation *)calloc(1, sizeof *op);
  if (op == NULL || path == NULL || contents == NULL) {
    free(op);
    free(path);
    free(contents);
    return;
  }
  op->services = reg->services;
  op->store = reg->services.config(reg->services.ctx, seat);
  op->path = path;
  op->contents = contents;
  reg->services.queue(reg->services.ctx, condump_operation_run, op, false);
}

typedef struct writeconfig_operation {
  qa_console_command_services services;
  qa_config_store *store; /* borrowed */
  char *path;
  char *contents;
  qa_configuration_write_notify notify;
  bool completed;
} writeconfig_operation;

static void writeconfig_complete(writeconfig_operation *op,
                                 qa_configuration_write_kind kind,
                                 const char *error_message) {
  if (op->completed) return;
  op->completed = true;
  if (op->notify.notify != NULL) {
    qa_configuration_write_result result = {kind, error_message};
    op->notify.notify(op->notify.ctx, result);
  }
}

static void writeconfig_operation_run(void *op_ctx) {
  writeconfig_operation *op = (writeconfig_operation *)op_ctx;
  if (!qa_config_store_dump(op->store, op->path, op->contents)) {
    writeconfig_complete(op, QA_CONFIGURATION_FAILED,
                         "writeconfig write failed");
    free(op->path);
    free(op->contents);
    free(op);
    qa_error("writeconfig write failed");
  }
  print_wrote(&op->services, op->path);
  writeconfig_complete(op, QA_CONFIGURATION_WRITTEN, NULL);
  free(op->path);
  free(op->contents);
  free(op);
}

static void writeconfig_handler(void *ctx, qa_command_invocation *invocation) {
  registration *reg = (registration *)ctx;
  qa_seat_id seat;
  bool has_seat = seat_of(invocation, &seat);
  if (!has_seat || reg->services.console(reg->services.ctx, seat) == NULL) {
    reg->services.print(reg->services.ctx,
                        "writeconfig requires an active local seat\n");
    return;
  }
  const char *name = invocation->argc > 1 ? invocation->argv[1] : "config.cfg";
  char *path = ends_with(name, ".cfg") ? dup_text(name) : join2(name, ".cfg");
  if (path == NULL) return;
  writeconfig_operation *op =
      (writeconfig_operation *)calloc(1, sizeof *op);
  if (op == NULL) {
    free(path);
    return;
  }
  op->services = reg->services;
  op->path = path;
  if (reg->services.configuration_write_started != NULL) {
    op->notify = reg->services.configuration_write_started(
        reg->services.ctx, invocation->source, path);
  }
  char *error = NULL;
  char *contents =
      reg->services.configuration(reg->services.ctx, invocation, &error);
  if (contents == NULL) {
    writeconfig_complete(op, QA_CONFIGURATION_FAILED, error);
    char *copy = dup_text(error != NULL ? error : "configuration failed");
    free(error);
    free(op->path);
    free(op);
    qa_error(copy != NULL ? copy : "configuration failed");
  }
  op->store = reg->services.config(reg->services.ctx, seat);
  op->contents = contents;
  reg->services.queue(reg->services.ctx, writeconfig_operation_run, op, false);
}

typedef struct capture_operation {
  qa_console_command_services services;
  qa_seat_id seat;
  const char *format; /* static, screenshot only */
  char *argument;     /* NULL = TS undefined argv[1] */
  char *map_name;
  bool levelshot_command;
} capture_operation;

static void capture_operation_free(capture_operation *op) {
  free(op->argument);
  free(op->map_name);
  free(op);
}

static void screenshot_operation_run(void *op_ctx) {
  capture_operation *op = (capture_operation *)op_ctx;
  char *path = NULL;
  if (op->levelshot_command ||
      (op->argument != NULL && strcmp(op->argument, "levelshot") == 0)) {
    path = op->services.capture_levelshot(op->services.ctx, op->seat,
                                          op->map_name);
  } else if (op->argument == NULL || strcmp(op->argument, "silent") == 0) {
    path = op->services.capture_screenshot(op->services.ctx, op->seat,
                                           op->format, NULL);
  } else {
    path = op->services.capture_screenshot(op->services.ctx, op->seat,
                                           op->format, op->argument);
  }
  bool silent = !op->levelshot_command && op->argument != NULL &&
                strcmp(op->argument, "silent") == 0;
  if (path == NULL) {
    capture_operation_free(op);
    qa_error("Screenshot capture failed");
  }
  if (!silent) print_wrote(&op->services, path);
  free(path);
  capture_operation_free(op);
}

static void screenshot_handler(void *ctx,
                               qa_command_invocation *invocation,
                               const char *format) {
  registration *reg = (registration *)ctx;
  qa_seat_id seat;
  bool has_seat = seat_of(invocation, &seat);
  bool has_capture =
      has_seat && reg->services.has_capture(reg->services.ctx, seat);
  const char *argument = invocation->argc > 1 ? invocation->argv[1] : NULL;
  if (!has_capture) {
    reg->services.print(reg->services.ctx,
                        "Screenshot requires an active seat renderer\n");
    return;
  }
  const char *map = reg->services.map_name(reg->services.ctx);
  capture_operation *op = (capture_operation *)calloc(1, sizeof *op);
  if (op == NULL) return;
  op->services = reg->services;
  op->seat = seat;
  op->format = format;
  op->argument = dup_text(argument);
  op->map_name = dup_text(map != NULL ? map : "");
  reg->services.queue(reg->services.ctx, screenshot_operation_run, op, true);
}

static void screenshot_tga_handler(void *ctx,
                                   qa_command_invocation *invocation) {
  screenshot_handler(ctx, invocation, "tga");
}

static void screenshot_jpg_handler(void *ctx,
                                   qa_command_invocation *invocation) {
  screenshot_handler(ctx, invocation, "jpg");
}

static void screenshot_png_handler(void *ctx,
                                   qa_command_invocation *invocation) {
  screenshot_handler(ctx, invocation, "png");
}

static void levelshot_handler(void *ctx, qa_command_invocation *invocation) {
  registration *reg = (registration *)ctx;
  if (invocation->dialect == QA_COMMAND_DIALECT_Q3) {
    qa_command_buffer_forward_to_server(reg->services.commands, invocation);
    return;
  }
  qa_seat_id seat;
  bool has_seat = seat_of(invocation, &seat);
  bool has_capture =
      has_seat && reg->services.has_capture(reg->services.ctx, seat);
  const char *map = reg->services.map_name(reg->services.ctx);
  if (has_capture) {
    capture_operation *op = (capture_operation *)calloc(1, sizeof *op);
    if (op == NULL) return;
    op->services = reg->services;
    op->seat = seat;
    op->map_name = dup_text(map != NULL ? map : "");
    op->levelshot_command = true;
    reg->services.queue(reg->services.ctx, screenshot_operation_run, op,
                        true);
  }
}

static void add_command(registration *reg, const char *name,
                        qa_command_handler_fn handler,
                        const qa_command_documentation *documentation) {
  if (!qa_command_buffer_exists(reg->services.commands, name) &&
      qa_command_buffer_register_engine(reg->services.commands, name, handler,
                                        reg, documentation)) {
    if (reg->name_count < 10) reg->names[reg->name_count++] = name;
  }
}

qa_console_command_registration *qa_register_console_commands(
    const qa_console_command_services *services) {
  registration *reg = (registration *)calloc(1, sizeof *reg);
  if (reg == NULL) return NULL;
  reg->services = *services;
  static const char *const clear_examples[] = {"clear"};
  static const char *const condump_examples[] = {"condump console.txt"};
  static const char *const writeconfig_examples[] = {"writeconfig config.cfg"};
  static const qa_command_documentation clear_doc = {
      "Clear the invoking seat's console output.", "clear", clear_examples,
      1, NULL, 0};
  static const qa_command_documentation condump_doc = {
      "Write the invoking seat's console output to a file.",
      "condump <filename>", condump_examples, 1, NULL, 0};
  static const qa_command_documentation writeconfig_doc = {
      "Save the invoking seat's configuration.", "writeconfig [filename]",
      writeconfig_examples, 1, NULL, 0};
  add_command(reg, "toggleconsole", toggleconsole_handler, NULL);
  add_command(reg, "clear", clear_handler, &clear_doc);
  add_command(reg, "messagemode", messagemode_handler, NULL);
  add_command(reg, "messagemode2", messagemode2_handler, NULL);
  add_command(reg, "condump", condump_handler, &condump_doc);
  add_command(reg, "writeconfig", writeconfig_handler, &writeconfig_doc);
  add_command(reg, "screenshot", screenshot_tga_handler, NULL);
  add_command(reg, "screenshotJPEG", screenshot_jpg_handler, NULL);
  add_command(reg, "screenshotPNG", screenshot_png_handler, NULL);
  add_command(reg, "levelshot", levelshot_handler, NULL);
  return reg;
}

void qa_console_command_registration_remove(
    qa_console_command_registration *registration_value) {
  if (registration_value == NULL) return;
  for (size_t i = 0; i < registration_value->name_count; i++) {
    (void)qa_command_buffer_unregister(registration_value->services.commands,
                                       registration_value->names[i], NULL,
                                       NULL, false);
  }
  free(registration_value);
}

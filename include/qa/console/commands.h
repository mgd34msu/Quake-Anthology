/*
 * Port of src/console/commands.ts: the toggleconsole/clear/
 * messagemode/condump/writeconfig/screenshot/levelshot engine
 * commands registered from ConsoleCommandServices.
 * C accommodations:
 *  - SeatConsole/ConfigStore are the ported qa_seat_console /
 *    qa_config_store, reached through the TS console(seat) /
 *    config(seat) lookups as function pointer + ctx. FrameCapture's
 *    file writing is not ported yet (see include/qa/capture/index.h),
 *    so capture stays decomposed into the operations this file
 *    calls; has_capture is the TS `capture(seat) === null` check,
 *    taken at the same instant. A failed qa_config_store_dump (the
 *    TS rejected dump) reports the fixed message named in commands.c
 *    to the writeconfig failure notification.
 *  - Async queue() work becomes a queued operation (fn + heap ctx)
 *    the host runs after renderer completion, as the TS documents;
 *    the operation frees its ctx when run. Throwing TS operations
 *    (failed dump/configuration/capture) abort with the message
 *    after any writeconfig failure notification, like a top-level
 *    crash on the rethrown error.
 *  - `unknown` write errors become message strings (malloc'd into
 *    *error_out by the failing service call; freed by this module).
 *  - The TS unregister closure becomes a registration object freed
 *    by qa_console_command_registration_remove().
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_COMMANDS_H
#define QA_CONSOLE_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/console/session.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/settings/config.h"

/* TS ConfigurationWriteResult. error_message is borrowed for the
 * call and set only for the failed kind (TS error: unknown). */
typedef enum qa_configuration_write_kind {
  QA_CONFIGURATION_WRITTEN, /* "written" */
  QA_CONFIGURATION_FAILED   /* "failed" */
} qa_configuration_write_kind;
typedef struct qa_configuration_write_result {
  qa_configuration_write_kind kind;
  const char *error_message;
} qa_configuration_write_result;
typedef void (*qa_configuration_write_notify_fn)(
    void *ctx, qa_configuration_write_result result);
typedef struct qa_configuration_write_notify {
  qa_configuration_write_notify_fn notify; /* NULL = TS undefined */
  void *ctx;
} qa_configuration_write_notify;
/* TS ConfigurationWriteStarted (the services field is optional). */
typedef qa_configuration_write_notify (*qa_configuration_write_started_fn)(
    void *ctx, qa_command_context source, const char *path);

typedef void (*qa_console_queued_operation_fn)(void *op_ctx);
/* TS queue(operation, frameReadback?). Takes ownership of op_ctx. */
typedef void (*qa_console_queue_fn)(void *ctx,
                                    qa_console_queued_operation_fn operation,
                                    void *op_ctx, bool frame_readback);

/** TS ConsoleCommandServices. One ctx serves every callback.
 * String results are malloc'd; object results are borrowed. */
typedef struct qa_console_command_services {
  void *ctx;
  qa_command_buffer *commands;
  /* config(seat): borrowed store, never NULL in TS. */
  qa_config_store *(*config)(void *ctx, qa_seat_id seat);
  /* configuration(invocation); NULL fills *error_out (TS throw). */
  char *(*configuration)(void *ctx, qa_command_invocation *invocation,
                         char **error_out);
  qa_configuration_write_started_fn configuration_write_started; /* or NULL */
  /* console(seat): borrowed console or NULL (TS null). */
  qa_seat_console *(*console)(void *ctx, qa_seat_id seat);
  bool (*can_chat)(void *ctx);
  /* capture(seat): screenshot(options) with format "tga"|"jpg"|"png"
   * and name NULL for the TS {format}-only form; levelshot(map).
   * Both return the malloc'd CaptureResult.path. */
  bool (*has_capture)(void *ctx, qa_seat_id seat);
  char *(*capture_screenshot)(void *ctx, qa_seat_id seat, const char *format,
                              const char *name);
  char *(*capture_levelshot)(void *ctx, qa_seat_id seat, const char *map_name);
  const char *(*map_name)(void *ctx); /* borrowed; copied by handlers */
  qa_command_print_fn print;
  qa_console_queue_fn queue;
} qa_console_command_services;

typedef struct qa_console_command_registration {
  qa_console_command_services services;
  const char *names[10];
  size_t name_count;
} qa_console_command_registration;

/* NULL only on OOM. Copies the services struct (callbacks + ctx). */
qa_console_command_registration *qa_register_console_commands(
    const qa_console_command_services *services);
/* The TS returned closure: unregisters and frees. Queued operations
 * already handed to queue() keep their own services copy. */
void qa_console_command_registration_remove(
    qa_console_command_registration *registration);

#endif

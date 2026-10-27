/*
 * Port of src/input/client-commands.ts: guest instances own
 * claims; one published input owns the shared dispatchers. The
 * TS symbol instance becomes an opaque identity pointer, and the
 * registration object becomes the owner handle used with the
 * owner functions. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_CLIENT_COMMANDS_H
#define QA_INPUT_CLIENT_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"

/** TS ClientCommandHandler. */
typedef struct qa_client_command_handler {
  void *ctx;
  const void *instance; /* TS symbol */
  const char *label;
  void (*execute)(void *ctx, qa_command_invocation *command);
} qa_client_command_handler;

/** TS (command, seat) => undefined fallback execution. */
typedef void (*qa_client_command_execute_fn)(void *ctx,
                                             qa_command_invocation *command,
                                             qa_seat_id seat);

typedef struct qa_client_command_bindings qa_client_command_bindings;
/** TS ClientCommandRegistration owner handle. */
typedef struct qa_client_command_owner qa_client_command_owner;

/* NULL only on allocation failure. The seat list is copied. */
qa_client_command_bindings *qa_client_command_bindings_create(
    qa_command_buffer *commands, const qa_seat_id *seats, size_t seat_count,
    qa_client_command_execute_fn execute, void *execute_ctx);
void qa_client_command_bindings_destroy(qa_client_command_bindings *bindings);
void qa_client_command_bindings_publish_seats(qa_client_command_bindings *bindings,
                                              const qa_seat_id *seats,
                                              size_t seat_count);
/* handler NULL mirrors the TS absent handler. Aborts with the TS
 * Error when the seat is not published. */
qa_client_command_owner *qa_client_command_bindings_create_owner(
    qa_client_command_bindings *bindings, qa_seat_id seat,
    const qa_client_command_handler *handler);
void qa_client_command_owner_register(qa_client_command_bindings *bindings,
                                      qa_client_command_owner *owner,
                                      const char *name);
void qa_client_command_owner_remove(qa_client_command_bindings *bindings,
                                    qa_client_command_owner *owner,
                                    const char *name);
void qa_client_command_owner_close(qa_client_command_bindings *bindings,
                                   qa_client_command_owner *owner);
void qa_client_command_bindings_activate(qa_client_command_bindings *bindings);
void qa_client_command_bindings_deactivate(qa_client_command_bindings *bindings);
bool qa_client_command_bindings_dispatch(qa_client_command_bindings *bindings,
                                         qa_command_invocation *command);

#endif

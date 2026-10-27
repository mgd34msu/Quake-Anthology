/*
 * Port of src/input/bindings.ts: named physical inputs, default
 * bindings, cfg archiving, bind/unbind console commands, wheel
 * command aliases, and VM-facing source key numbers. The TS
 * BindingCommandSeat Pick becomes a small vtable; registration
 * cleanup closures become registration objects. String results
 * are malloc'd for the caller. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_BINDINGS_H
#define QA_INPUT_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
#include "qa/input/binding_store.h"
#include "qa/input/seat.h"
#include "qa/input/weapon_bindings.h"

/* False is the TS null. On success out's string fields are static
 * literals (do not dispose out). device mirrors the TS default 0. */
bool qa_named_physical_input(const char *name, double device,
                             qa_physical_input *out);
/* Malloc'd display name; caller frees. */
char *qa_physical_input_name(const qa_physical_input *input);
/* Malloc'd bindings owning their strings; dispose each with
 * qa_input_binding_dispose and free the array. NULL only on OOM. */
qa_input_binding *qa_default_bindings(double device, qa_command_dialect dialect,
                                      const qa_weapon_binding_item *items,
                                      size_t item_count, size_t *out_count);
/* Malloc'd command strings; caller frees each and the array. */
char **qa_archived_bindings(const qa_input_binding *bindings, size_t count,
                            bool include_controller_bindings, size_t *out_count);

/** TS BindingCommandSeat (Pick of BindingStore). */
typedef struct qa_binding_command_seat {
  void *ctx;
  const qa_input_binding *(*bindings)(void *ctx, size_t *out_count);
  const qa_input_binding_target *(*binding)(void *ctx,
                                            const qa_physical_input *input);
  bool (*bind)(void *ctx, const qa_input_binding *binding);
  void (*unbind)(void *ctx, const qa_physical_input *input);
  void (*unbind_all)(void *ctx);
} qa_binding_command_seat;

qa_binding_command_seat qa_binding_store_command_seat(qa_binding_store *store);
qa_binding_command_seat qa_seat_input_command_seat(qa_seat_input *input);

/** TS (seat) => BindingCommandSeat | null lookup. */
typedef bool (*qa_binding_seat_lookup_fn)(void *ctx, qa_seat_id seat,
                                          qa_binding_command_seat *out);
typedef struct qa_binding_commands_registration qa_binding_commands_registration;
/* console_bindings NULL mirrors the TS null default. */
qa_binding_commands_registration *qa_register_binding_commands(
    qa_command_buffer *commands, qa_binding_seat_lookup_fn lookup,
    void *lookup_ctx, qa_command_print_fn print, void *print_ctx,
    const qa_binding_command_seat *console_bindings);
void qa_binding_commands_registration_destroy(
    qa_binding_commands_registration *registration);

/* Malloc'd canonical text (a copy of the input when not a wheel
 * command); caller frees. */
char *qa_canonical_wheel_command(const char *text);

/** TS wheel modes. */
typedef enum qa_wheel_mode {
  QA_WHEEL_WEAPONS, QA_WHEEL_POWERUPS
} qa_wheel_mode;
typedef void (*qa_wheel_fn)(void *ctx, qa_seat_id seat, qa_wheel_mode mode,
                            bool down);
typedef struct qa_wheel_commands_registration qa_wheel_commands_registration;
qa_wheel_commands_registration *qa_register_wheel_commands(
    qa_command_buffer *commands, qa_wheel_fn wheel, void *wheel_ctx);
void qa_wheel_commands_registration_destroy(
    qa_wheel_commands_registration *registration);

/* VM-facing source key values. False is the TS null. */
bool qa_source_key_number(double key, qa_game_family family, double *out);

#endif

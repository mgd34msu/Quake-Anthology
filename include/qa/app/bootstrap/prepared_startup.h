/* Port of src/app/bootstrap/prepared-startup.ts. TS restarts are
 * synchronous here (PORTING.md): the promise tail is serialized by
 * construction. allowSeatConfigurationCommand and the seat
 * configuration gating are ported; registry/config-script work is
 * in-file over landed core services; world-lifecycle hooks ride
 * the host below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PREPARED_STARTUP_H
#define QA_APP_BOOTSTRAP_PREPARED_STARTUP_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/console.h"
#include "qa/app/bootstrap/startup_config.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/core/q3_product_policy.h"
#include "qa/input/binding_store.h"
#include "qa/input/bindings.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/seat.h"
#include "qa/settings/config.h"
/* TS allowSeatConfigurationCommand. */
bool qa_app_allow_seat_configuration_command(qa_command_context source);
typedef struct qa_app_prepared_startup_host {
  void *ctx;
  /* World-lifecycle hooks mirrored on the host; borrowed errors. */
  const char *(*prepare_lifecycle)(void *ctx, const void *options, const void *content, bool refine, bool resumed);
  const char *(*set_local_seats)(void *ctx, const void *identities, size_t count);
  const char *(*reset_to_menu)(void *ctx, const char *status);
  double (*local_player_capacity)(void *ctx);   /* NULL -> 4 */
  void (*host_reset_settings)(void *ctx, const char *status); /* may be NULL */
} qa_app_prepared_startup_host;
typedef struct qa_app_prepared_startup qa_app_prepared_startup;
typedef struct qa_app_prepared_seat {
  qa_command_context context;
  qa_seat_id id;
  qa_cvar_registry *cvars;
  qa_mouse_settings mouse;
  qa_seat_input *input; /* borrowed */
  /* TS PreparedSeat.allBindingsChosen / collectingBindings. */
  bool all_bindings_chosen;
  bool collecting_bindings;
} qa_app_prepared_seat;
/* The constructor's completed-restart surface rides the host; the
 * seat roster is created by set_seat_count. */
qa_app_prepared_startup *qa_app_prepared_startup_create(qa_command_buffer *commands,
    qa_app_prepared_startup_host host);
qa_command_buffer *qa_app_prepared_startup_commands(const qa_app_prepared_startup *startup);
const qa_app_prepared_seat *qa_app_prepared_startup_seats(const qa_app_prepared_startup *startup, size_t *out_count);
void qa_app_prepared_startup_close(qa_app_prepared_startup *startup);
/* --- Concrete TS PreparedStartup (prepared-startup.ts). Registries,
 * scripts and seat cvar owners are borrowed; the command buffer, a
 * created fallback registry, routing, seat inputs and the console
 * binding store are owned and released by qa_app_prepared_startup_close.
 * Async flows are synchronous (PORTING.md): execute drives the same
 * frame loop; next_frame is invoked between frames. --- */
/* TS PreparedSeatConfiguration. profile NULL mirrors the TS null. */
typedef struct qa_app_prepared_seat_configuration {
  qa_command_context context;
  qa_seat_id id;
  qa_cvar_registry *cvars;
  qa_mouse_settings mouse;
  const qa_seat_settings *profile; /* NULL = TS null */
  const qa_cvar_archive_entry *archive; size_t archive_count;
  const qa_cvar_archive_entry *mouse_archive; size_t mouse_archive_count;
} qa_app_prepared_seat_configuration;
/* TS forward: (name, args, source) => undefined. */
typedef void (*qa_app_prepared_forward_fn)(void *ctx, const char *name,
    const char *const *args, size_t arg_count, qa_command_context source);
typedef struct qa_app_prepared_startup_options {
  const char *const *startup_commands; size_t startup_command_count;
  bool has_q3_policy; qa_q3_product_policy q3_policy;
  qa_command_dialect dialect;
  qa_command_dialect movement_dialect;
  const qa_app_prepared_seat_configuration *seats; size_t seat_count;
  qa_cvar_registry *shared; /* NULL = TS null */
  const char *const *shared_names; size_t shared_name_count;
  qa_command_print_source_fn print; void *print_ctx;
  qa_app_prepared_forward_fn forward; void *forward_ctx;
} qa_app_prepared_startup_options;
/* TS constructor(source, movement, scripts, options). scripts may be
 * NULL only when no script is ever read and no writeconfig runs. */
qa_app_prepared_startup *qa_app_prepared_startup_create_concrete(
    qa_cvar_registry *source, qa_cvar_registry *movement,
    qa_app_console_script_files *scripts,
    const qa_app_prepared_startup_options *options);
qa_cvar_registry *qa_app_prepared_startup_source(const qa_app_prepared_startup *startup);
qa_cvar_registry *qa_app_prepared_startup_movement(const qa_app_prepared_startup *startup);
qa_cvar_registry *qa_app_prepared_startup_fallback(const qa_app_prepared_startup *startup);
qa_app_console_script_files *qa_app_prepared_startup_scripts(const qa_app_prepared_startup *startup);
qa_binding_store *qa_app_prepared_startup_console_bindings(const qa_app_prepared_startup *startup);
/* TS sharedCvars (NULL mirrors the TS null). */
qa_cvar_registry *qa_app_prepared_startup_shared_cvars(const qa_app_prepared_startup *startup);
bool qa_app_prepared_startup_pending(const qa_app_prepared_startup *startup);
bool qa_app_prepared_startup_configuration_can_continue(const qa_app_prepared_startup *startup);
/* TS bindOutput: returns an unbind handle (>= 0). */
int64_t qa_app_prepared_startup_bind_output(qa_app_prepared_startup *startup,
    qa_command_print_source_fn print, void *ctx);
void qa_app_prepared_startup_unbind_output(qa_app_prepared_startup *startup, int64_t binding);
bool qa_app_prepared_startup_is_configuration_source(qa_app_prepared_startup *startup, qa_command_context source);
void qa_app_prepared_startup_note_world_action(qa_app_prepared_startup *startup);
void qa_app_prepared_startup_forward_commands(qa_app_prepared_startup *startup,
    qa_app_prepared_forward_fn forward, void *forward_ctx);
/* TS readScript/readConfiguration: malloc'd text or NULL (undefined).
 * readConfiguration aborts with the TS Error when no reader was set. */
char *qa_app_prepared_startup_read_script(qa_app_prepared_startup *startup, const char *name, qa_command_context source);
char *qa_app_prepared_startup_read_configuration(qa_app_prepared_startup *startup, const char *name,
    qa_command_context source, qa_app_startup_script_scope scope);
void qa_app_prepared_startup_on_script_complete(qa_app_prepared_startup *startup, const qa_script_completion *event);
/* TS StartupConfigOptions["read"] fn+ctx. */
typedef char *(*qa_app_prepared_scoped_read_fn)(void *ctx, const char *name,
    qa_command_context source, qa_app_startup_script_scope scope);
void qa_app_prepared_startup_adopt_readers(qa_app_prepared_startup *startup,
    qa_app_console_script_files *scripts, qa_app_prepared_scoped_read_fn read, void *read_ctx);
/* TS PreparedConfigurationContinuation. */
typedef struct qa_app_prepared_configuration_continuation {
  void *ctx;
  qa_app_startup_config *(*active)(void *ctx); /* NULL = TS null */
  bool (*advance)(void *ctx, qa_app_prepared_startup *owner);
} qa_app_prepared_configuration_continuation;
void qa_app_prepared_startup_adopt_configuration_continuation(qa_app_prepared_startup *startup,
    qa_app_prepared_configuration_continuation continuation);
/* TS validateOwners / adopt owners argument. */
typedef struct qa_app_prepared_owners {
  qa_cvar_registry *source, *movement, *fallback;
  qa_app_console_script_files *scripts;
  qa_app_prepared_scoped_read_fn read; void *read_ctx;
} qa_app_prepared_owners;
void qa_app_prepared_startup_validate_owners(qa_app_prepared_startup *startup, const qa_app_prepared_owners *owners);
/* owners NULL mirrors the TS undefined argument. */
void qa_app_prepared_startup_adopt(qa_app_prepared_startup *startup, qa_command_cvar_routing routing,
    qa_app_prepared_forward_fn forward, void *forward_ctx, const qa_app_prepared_owners *owners);
/* Routing borrowed from a landed ApplicationConsoleRouting owner: the
 * prepared startup drops its own routing owner on adoption. */
void qa_app_prepared_startup_adopt_console_routing(qa_app_prepared_startup *startup,
    qa_app_console_routing *routing, qa_app_prepared_forward_fn forward, void *forward_ctx,
    const qa_app_prepared_owners *owners);
/* TS publishSeats / setActiveSeats / adoptSeat. publish_seats seats
 * carry id/input/context/cvars/mouse; active NULL+0 uses every seat. */
void qa_app_prepared_startup_publish_seats(qa_app_prepared_startup *startup,
    const qa_app_prepared_seat *seats, size_t seat_count,
    const qa_seat_id *active, size_t active_count, bool has_active);
void qa_app_prepared_startup_set_active_seats(qa_app_prepared_startup *startup,
    const qa_seat_id *ids, size_t count);
/* cvars/mouse NULL mirror the TS undefined arguments. */
void qa_app_prepared_startup_adopt_seat(qa_app_prepared_startup *startup, qa_seat_id id,
    qa_cvar_registry *cvars, const qa_mouse_settings *mouse);
/* TS bindings/previewBindings: malloc'd shallow copies (target text
 * borrowed); caller frees the array only. */
qa_input_binding *qa_app_prepared_startup_preview_bindings(qa_app_prepared_startup *startup,
    qa_seat_id id, const qa_input_binding *defaults, size_t default_count, size_t *out_count);
qa_input_binding *qa_app_prepared_startup_bindings(qa_app_prepared_startup *startup,
    qa_seat_id id, const qa_input_binding *defaults, size_t default_count, size_t *out_count);
void qa_app_prepared_startup_adopt_binding_defaults(qa_app_prepared_startup *startup,
    qa_seat_id id, const qa_input_binding *defaults, size_t default_count);
void qa_app_prepared_startup_reset_bindings(qa_app_prepared_startup *startup, qa_seat_id id);
/* Borrowed seat binding-choice state (TS PreparedSeat fields). False
 * when the seat is absent; authored NULL count 0 mirrors TS null,
 * selected has_selected false mirrors TS undefined. */
bool qa_app_prepared_startup_seat_choices(const qa_app_prepared_startup *startup, qa_seat_id id,
    bool *all_bindings_chosen, char *const **overridden_keys, size_t *overridden_count,
    bool *has_selected, const qa_input_binding **selected, size_t *selected_count,
    const qa_input_binding **authored, size_t *authored_count);
/* TS execute()/advanceFrame() options (run() argument). */
typedef struct qa_app_prepared_execute_options {
  qa_app_prepared_scoped_read_fn read; void *read_ctx;
  bool has_mod;
  void (*apply_launch_options)(void *ctx); void *apply_ctx;
  void (*next_frame)(void *ctx); void *next_frame_ctx;
  const qa_cvar_archive_entry *source_archive; size_t source_archive_count;
  const qa_cvar_archive_entry *movement_archive; size_t movement_archive_count;
  const qa_cvar_archive_entry *fallback_archive; size_t fallback_archive_count;
  const qa_cvar_archive_entry *shared_archive; size_t shared_archive_count;
} qa_app_prepared_execute_options;
void qa_app_prepared_startup_execute(qa_app_prepared_startup *startup,
    const qa_app_prepared_execute_options *options);
bool qa_app_prepared_startup_advance_frame(qa_app_prepared_startup *startup);
/* TS PreparedClientCommands. Owned; destroy releases the program and
 * staged binding owners (publish first to keep staged edits). */
typedef struct qa_app_prepared_client_commands qa_app_prepared_client_commands;
qa_command_buffer *qa_app_prepared_client_commands_buffer(qa_app_prepared_client_commands *client);
bool qa_app_prepared_client_commands_prepare_prefix(qa_app_prepared_client_commands *client,
    qa_prepare_prefix_run_fn run, void *run_ctx);
/* TS releaseCommands.append (release dialect). */
void qa_app_prepared_client_commands_release_append(qa_app_prepared_client_commands *client,
    const char *text, const qa_command_context *source);
/* TS input(seat): staged BindingCommandSeat view; false = TS null. */
bool qa_app_prepared_client_commands_input(qa_app_prepared_client_commands *client, qa_seat_id seat,
    qa_binding_command_seat *out);
bool qa_app_prepared_client_commands_input_is_down(qa_app_prepared_client_commands *client,
    qa_seat_id seat, const qa_physical_input *input);
void qa_app_prepared_client_commands_input_clear_states(qa_app_prepared_client_commands *client, qa_seat_id seat);
void qa_app_prepared_client_commands_release_inputs(qa_app_prepared_client_commands *client, double time);
void qa_app_prepared_client_commands_validate_publication(qa_app_prepared_client_commands *client);
void qa_app_prepared_client_commands_publish(qa_app_prepared_client_commands *client);
void qa_app_prepared_client_commands_destroy(qa_app_prepared_client_commands *client);
/* TS prepareClientCommands free function. inputs carry id/input/context. */
qa_app_prepared_client_commands *qa_app_prepare_client_commands(qa_command_buffer *commands,
    const qa_app_prepared_seat *inputs, size_t input_count,
    qa_cvar_registry *const *live_registries, size_t live_registry_count,
    const qa_command_buffer_options *options, qa_binding_store *console_bindings);
/* TS PreparedStartup.prepareClientCommands (inputs NULL = this.seats). */
qa_app_prepared_client_commands *qa_app_prepared_startup_prepare_client_commands(
    qa_app_prepared_startup *startup, const qa_command_buffer_options *options,
    const qa_app_prepared_seat *inputs, size_t input_count, bool has_inputs);
/* Full TS allowSeatConfigurationCommand over a landed routing view.
 * seats carry id/cvars/mouse. */
bool qa_app_allow_seat_configuration_command_for(qa_command_invocation *command,
    qa_command_cvar_routing routing, const qa_app_prepared_seat *seats, size_t seat_count,
    qa_command_print_source_fn print, void *print_ctx);
#endif

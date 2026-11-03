/* Port of src/app/bootstrap/input.ts: ApplicationInput, the per-world
 * owner of local seats, their consoles, mouse settings, haptics, the
 * input router, controller and device owners, and the application
 * command bindings.
 *
 * C accommodations:
 *  - The TS async factories (open/prepare/prepareForClient), readSettings,
 *    controller settle and the haptic/sound loaders run synchronously
 *    (PORTING.md).
 *  - Lifetime: objects an ApplicationInput creates (seat inputs, consoles,
 *    mouse registries, command buffer, controllers, device owners) are
 *    reached by later inputs, the prepared startup and the client
 *    bootstrap, as GC references in TS. Each ApplicationInput is
 *    reference counted: a later input that retains any of them holds its
 *    creator, and publications into a prepared startup or client hold the
 *    creator for that owner until qa_app_input_release_holder(owner).
 *    qa_app_application_input_destroy drops the caller's reference; close
 *    keeps the TS close() semantics.
 *  - Closures become fn+ctx host structs; every TS optional member is a
 *    NULL function pointer.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_INPUT_H
#define QA_APP_BOOTSTRAP_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/console.h"
#include "qa/app/bootstrap/controller_settings.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/prepared_startup.h"
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/app/bootstrap/q3_client/qvm_scalars.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/audio/types.h"
#include "qa/console/llm.h"
#include "qa/console/session.h"
#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/input/bindings.h"
#include "qa/input/client_commands.h"
#include "qa/input/haptics.h"
#include "qa/input/mouse.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/router.h"
#include "qa/input/seat.h"
#include "qa/input/user_command.h"
#include "qa/input/weapon_bindings.h"
#include "qa/platform/controller.h"
#include "qa/platform/sdl.h"
#include "qa/settings/config.h"
#include "qa/ui/settings/action_catalog.h"
#include "qa/world/session/session.h"

typedef struct qa_app_application_input qa_app_application_input;

/* TS LocalPlayer. */
typedef struct qa_app_local_player {
  qa_session_seat *seat; /* borrowed SessionSeat */
  qa_actor_id actor;
} qa_app_local_player;

/* TS LocalInput. The record and its pointees belong to the ApplicationInput
 * (see the lifetime note); console changes when a client retains one. */
typedef struct qa_app_local_input {
  qa_app_local_player player;
  qa_seat_input *input;
  qa_seat_console *console;
  qa_input_command_builder *builder;
  qa_seat_haptics *haptics;
} qa_app_local_input;

/* TS Q3CommandSelection. */
typedef struct qa_app_q3_command_selection { double weapon, sensitivity; } qa_app_q3_command_selection;

/* TS Pick<ArsenalIntent, "provider" | "weapon">; weapon NULL is TS null. */
typedef struct qa_app_arsenal_selection { qa_provider_id provider; qa_item_id weapon; } qa_app_arsenal_selection;

/* TS movementDialect. options->network_kind/movement are read; recipe NULL
 * is TS undefined. Raises the TS Error when the recipe has no timing for its
 * movement provider. */
qa_command_dialect qa_app_movement_dialect(const qa_app_options *options,
    const qa_executable_recipe *recipe);

/* TS SdlWindow as ApplicationInput reads it: the router's window surface,
 * window ticks, and the native window for UI video settings (NULL for a
 * window double). Identity is input.ctx. */
typedef struct qa_app_input_window {
  qa_input_window input;
  uint32_t (*ticks)(void *ctx);
  qa_sdl_window *sdl;
} qa_app_input_window;
qa_app_input_window qa_app_input_window_from_sdl(qa_sdl_window *window);

/* TS ApplicationInputCommands["console"]. */
typedef struct qa_app_input_console_routes {
  void *ctx;
  qa_command_dialect (*dialect)(void *ctx);
  bool (*server)(void *ctx, qa_app_console_server *out); /* false = TS null */
  qa_cvar_registry *(*seat)(void *ctx, qa_seat_id id);   /* NULL = TS null */
} qa_app_input_console_routes;

/* TS ApplicationInputCommands. Every function shares ctx; NULL members are
 * TS undefined. quit/execute/print are required. */
typedef struct qa_app_input_commands {
  void *ctx;
  qa_app_prepared_profile *configuration;  /* NULL = undefined */
  qa_app_console_script_files *scripts;    /* NULL = undefined */
  bool has_read_script; qa_app_source_script_reader read_script;
  bool has_read_mounted_script; qa_app_source_script_reader read_mounted_script;
  /* TS startupReader?(scripts, options): false = undefined. */
  bool (*startup_reader)(void *ctx, qa_app_console_script_files *scripts, const qa_app_options *options,
                         qa_app_prepared_scoped_read_fn *out_read, void **out_read_ctx);
  const qa_llm_command_requester *llm;     /* NULL = undefined */
  double (*local_player_capacity)(void *ctx);
  bool (*can_remove_local_player)(void *ctx, qa_seat_id seat);
  qa_ui_binding_capabilities (*binding_capabilities)(void *ctx);
  /* Borrowed items valid until the next call. */
  const qa_weapon_binding_item *(*binding_items)(void *ctx, qa_seat_id seat, size_t *out_count);
  /* TS arsenalImpulseProvider?(seat): false = null. */
  bool (*arsenal_impulse_provider)(void *ctx, qa_seat_id seat, qa_provider_id *out);
  qa_cvar_registry *shared_cvars;          /* NULL = undefined */
  void (*quit)(void *ctx);
  /* seat NULL is TS null; source NULL is TS undefined. */
  void (*execute)(void *ctx, const char *name, const char *const *args, size_t arg_count,
                  const qa_seat_id *seat, const qa_command_context *source);
  void (*print)(void *ctx, const char *text);
  const qa_app_input_console_routes *console; /* NULL = undefined */
  bool (*client_input)(void *ctx, const qa_seat_input_event *event);
  bool (*client_captures_input)(void *ctx, qa_seat_id seat);
} qa_app_input_commands;

/* TS ApplicationInputUi. sample rewrites the sample in place (TS returns the
 * replacement sample). clear_prompt/cycle_weapon/switch_weapon may be NULL. */
typedef struct qa_app_input_ui {
  void *ctx;
  bool (*input)(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus);
  void (*close_menus)(void *ctx);
  void (*clear_prompt)(void *ctx);
  void (*sample)(void *ctx, qa_seat_input_sample *sample);
  void (*wheel)(void *ctx, qa_wheel_mode mode, bool down);
  bool (*cycle_weapon)(void *ctx, int direction);
  bool (*switch_weapon)(void *ctx, double first, double second);
} qa_app_input_ui;

/* TS ApplicationInputCommandOwner. NULL members are TS undefined. */
typedef struct qa_app_input_command_owner {
  qa_cvar_registry *movement;
  qa_cvar_registry *fallback;
  qa_app_console_script_files *scripts;
  qa_cvar_registry *cvars;
  qa_command_buffer *commands;
  qa_command_cvar_routing routing;
  qa_mouse_settings *input_settings;
} qa_app_input_command_owner;

/* TS now: () => number. */
typedef double (*qa_app_input_now_fn)(void *ctx);

/* The ApplicationInput.open/prepare/prepareForClient arguments. NULL pointers
 * are TS undefined. players are copied; the pointees are borrowed. */
typedef struct qa_app_input_open_args {
  qa_app_input_window window;
  const qa_app_local_player *players; size_t player_count;
  const qa_app_options *options;        /* borrowed for the input's life */
  qa_command_dialect dialect;
  const qa_app_sim_presentation_access *simulation; /* player_view; borrowed */
  qa_app_input_commands actions;
  qa_app_input_now_fn now; void *now_ctx;
  const qa_config_store *settings;      /* borrowed */
  const qa_app_input_command_owner *owner;
  qa_app_application_input *previous;
  qa_app_prepared_startup *prepared;
} qa_app_input_open_args;

/* TS ApplicationInput.open. Returns a new reference. */
qa_app_application_input *qa_app_application_input_open(const qa_app_input_open_args *args);
/* TS ApplicationInput.prepare(liveRegistries, ...). */
qa_app_application_input *qa_app_application_input_prepare(qa_cvar_registry *const *live_registries,
    size_t live_registry_count, const qa_app_input_open_args *args);
/* TS ApplicationInput.prepareForClient(client, liveRegistries, ...). */
qa_app_application_input *qa_app_application_input_prepare_for_client(qa_app_client_bootstrap *client,
    qa_cvar_registry *const *live_registries, size_t live_registry_count, const qa_app_input_open_args *args);
/* Drops the caller's reference (memory is released with the last holder). */
void qa_app_application_input_destroy(qa_app_application_input *input);
/* Releases every hold taken for owner (a prepared startup or client). */
void qa_app_input_release_holder(const void *owner);

const qa_app_options *qa_app_application_input_options(const qa_app_application_input *input);
qa_command_dialect qa_app_application_input_dialect(const qa_app_application_input *input);
double qa_app_application_input_now(qa_app_application_input *input);
qa_app_console_script_files *qa_app_application_input_scripts(const qa_app_application_input *input);
qa_ui_binding_capabilities qa_app_application_input_binding_capabilities(qa_app_application_input *input);
qa_cvar_registry *qa_app_application_input_shared_cvars(const qa_app_application_input *input);
/* TS sharedSettings(): the current shared owner or NULL. */
qa_cvar_registry *qa_app_application_input_shared_settings(const qa_app_application_input *input);
/* TS guestCommands. */
double qa_app_application_input_guest_execute_now(qa_app_application_input *input, const char *text,
    const qa_command_context *source);
void qa_app_application_input_guest_insert(qa_app_application_input *input, const char *text,
    const qa_command_context *source);
void qa_app_application_input_guest_append(qa_app_application_input *input, const char *text,
    const qa_command_context *source, int dialect);
/* TS guestCvars(seat): caller destroys the returned object. */
qa_app_q3_client_cvars *qa_app_application_input_guest_cvars(qa_app_application_input *input, qa_seat_id seat);
void qa_app_application_input_publish_shared_cvars(qa_app_application_input *input, qa_cvar_registry *registry);
void qa_app_application_input_adopt_cvar_owner(qa_app_application_input *input, qa_cvar_registry *candidate,
    qa_cvar_registry *retained);
/* owner->scripts is required. */
void qa_app_application_input_adopt_client_source(qa_app_application_input *input, const qa_app_input_command_owner *owner);
void qa_app_application_input_adopt_mouse_owner(qa_app_application_input *input, qa_seat_id seat, qa_mouse_settings retained);
/* TS guestInput(seat): the view lives as long as the input. */
qa_app_qvm_client_input qa_app_application_input_guest_input(qa_app_application_input *input, qa_seat_id seat);
void qa_app_application_input_validate_candidate_commands(qa_app_application_input *input);
void qa_app_application_input_release_into_candidate(qa_app_application_input *input,
    qa_app_application_input *previous, bool world_changed);
void qa_app_application_input_publish_candidate_commands(qa_app_application_input *input);
qa_command_buffer *qa_app_application_input_commands(const qa_app_application_input *input);
qa_cvar_registry *qa_app_application_input_cvars(const qa_app_application_input *input);
double qa_app_application_input_local_player_capacity(qa_app_application_input *input);
bool qa_app_application_input_can_remove_local_player(qa_app_application_input *input, qa_seat_id seat);
/* TS locals: the rows are input-owned records (NULL past the end). */
size_t qa_app_application_input_local_count(const qa_app_application_input *input);
qa_app_local_input *qa_app_application_input_local(const qa_app_application_input *input, size_t index);
qa_sdl_controllers *qa_app_application_input_controllers(const qa_app_application_input *input);
qa_input_router *qa_app_application_input_router(const qa_app_application_input *input);
qa_app_controller_settings *qa_app_application_input_controller_settings(const qa_app_application_input *input);
qa_app_input_devices *qa_app_application_input_input_devices(const qa_app_application_input *input);
void qa_app_application_input_enable_archive_persistence(qa_app_application_input *input);
/* source NULL is TS undefined. */
void qa_app_application_input_print(qa_app_application_input *input, const char *text, const qa_command_context *source);
/* TS inputCvars(id): has_id false is TS null. */
qa_cvar_registry *qa_app_application_input_input_cvars(const qa_app_application_input *input, bool has_id, qa_seat_id id);
/* TS retireLocalSeats. client NULL is TS undefined. Raises an
 * AggregateError ("Local input retirement failed") after running every
 * disposal when any of them raised. */
void qa_app_application_input_retire_local_seats(qa_app_application_input *input, qa_session_seat *const *seats,
    size_t seat_count, qa_app_client_bootstrap *client);
/* TS prepareLocalSeats: publish/discard handle. */
typedef struct qa_app_local_seat_preparation qa_app_local_seat_preparation;
qa_app_local_seat_preparation *qa_app_application_input_prepare_local_seats(qa_app_application_input *input,
    qa_session_seat *const *seats, size_t seat_count);
/* client/source NULL are TS undefined; retain false is "replace". */
void qa_app_local_seat_preparation_publish(qa_app_local_seat_preparation *preparation,
    const qa_app_local_player *players, size_t player_count, qa_app_client_bootstrap *client,
    const qa_app_sim_presentation_access *source, bool retain);
void qa_app_local_seat_preparation_discard(qa_app_local_seat_preparation *preparation);
void qa_app_local_seat_preparation_destroy(qa_app_local_seat_preparation *preparation);
void qa_app_application_input_poll_loading_events(qa_app_application_input *input);
void qa_app_application_input_validate_startup_adoption(qa_app_application_input *input);
void qa_app_application_input_adopt_startup(qa_app_application_input *input);
bool qa_app_application_input_can_reset_bindings(qa_app_application_input *input, qa_seat_id id);
void qa_app_application_input_reset_bindings(qa_app_application_input *input, qa_seat_id id);
bool qa_app_application_input_advance_startup(qa_app_application_input *input);
void qa_app_application_input_pump(qa_app_application_input *input, bool execute_commands);
/* TS bindHaptics(load): load returns false for the TS null, else true with
 * malloc'd bytes. */
typedef bool (*qa_app_haptic_load_fn)(void *ctx, qa_resource_request request, uint8_t **out_bytes, size_t *out_length);
void qa_app_application_input_bind_haptics(qa_app_application_input *input, qa_app_haptic_load_fn load, void *load_ctx);
void qa_app_application_input_stop_haptics(qa_app_application_input *input);
/* actor NULL is TS null. */
void qa_app_application_input_sound_haptics(qa_app_application_input *input, qa_content_id content,
    const char *sound, const qa_actor_id *actor, qa_audio_audience audience);
/* TS build/buildForSeat: wall_elapsed defaults to elapsed. build returns a
 * malloc'd array the caller frees; the arsenal pointers reference storage
 * owned by the input until the next build. */
qa_actor_command *qa_app_application_input_build(qa_app_application_input *input, double elapsed_milliseconds,
    double server_milliseconds, double server_frame, double wall_elapsed_milliseconds, size_t *out_count);
qa_actor_command qa_app_application_input_build_for_seat(qa_app_application_input *input, qa_seat_id seat,
    double elapsed_milliseconds, double server_milliseconds, double server_frame, double wall_elapsed_milliseconds);
double qa_app_application_input_next_command_sequence(const qa_app_application_input *input);
void qa_app_application_input_resume_commands(qa_app_application_input *input, double sequence);
bool qa_app_application_input_input(qa_app_application_input *input, const qa_seat_input_event *event);
void qa_app_application_input_set_q3_command_selection(qa_app_application_input *input, qa_seat_id seat,
    qa_app_q3_command_selection selection);
/* selection NULL is TS null. */
void qa_app_application_input_set_arsenal_selection(qa_app_application_input *input, qa_seat_id seat,
    const qa_app_arsenal_selection *selection);
void qa_app_application_input_bind_arsenal_provider(qa_app_application_input *input, qa_seat_id seat,
    qa_provider_id provider);
/* TS clientCommandRegistration(seat, handler?). */
qa_client_command_owner *qa_app_application_input_client_command_registration(qa_app_application_input *input,
    qa_seat_id seat, const qa_client_command_handler *handler);
qa_client_command_bindings *qa_app_application_input_client_commands(const qa_app_application_input *input);
/* TS attachUi: returns the detach token; pass it to _detach_ui. */
typedef struct qa_app_input_ui_attachment qa_app_input_ui_attachment;
qa_app_input_ui_attachment *qa_app_application_input_attach_ui(qa_app_application_input *input, qa_seat_id seat,
    qa_app_input_ui ui);
void qa_app_input_ui_attachment_detach(qa_app_input_ui_attachment *attachment);
void qa_app_application_input_enqueue_client_command(qa_app_application_input *input, const char *text,
    const qa_command_context *source);
/* TS dispatch(text, source). */
typedef void (*qa_app_reliable_dispatch_fn)(void *ctx, const char *text, const qa_command_context *source);
void qa_app_application_input_enqueue_client_reliable(qa_app_application_input *input, const char *text,
    const qa_command_context *source, qa_app_reliable_dispatch_fn dispatch, void *dispatch_ctx);
bool qa_app_application_input_profile_changed(qa_app_application_input *input);
/* commands NULL is TS undefined. */
void qa_app_application_input_release_for_profile_change(qa_app_application_input *input,
    const qa_command_append_target *commands);
qa_app_input_window qa_app_application_input_window(const qa_app_application_input *input);
void qa_app_application_input_publish_window(qa_app_application_input *input, qa_app_input_window next);
void qa_app_application_input_transfer_platform_to(qa_app_application_input *input, qa_app_application_input *next);
void qa_app_application_input_transfer_platform_to_frontend(qa_app_application_input *input,
    qa_input_router *next, const qa_command_append_target *commands);
void qa_app_application_input_activate_prepared_platform(qa_app_application_input *input);
void qa_app_application_input_publish_client_seats(qa_app_application_input *input, qa_app_client_bootstrap *client,
    bool retain);
void qa_app_application_input_publish_client_platform(qa_app_application_input *input,
    qa_app_client_bootstrap *client, bool retain);
/* world false is "source-round". */
void qa_app_application_input_rebind_players(qa_app_application_input *input, const qa_app_local_player *players,
    size_t player_count, const qa_app_sim_presentation_access *simulation, bool world);
void qa_app_application_input_save_settings(qa_app_application_input *input);
void qa_app_application_input_close(qa_app_application_input *input);
#endif

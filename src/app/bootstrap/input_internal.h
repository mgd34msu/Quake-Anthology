/* Private state of the src/app/bootstrap/input.ts port, shared by
 * input.c (construction, ownership, accessors), input_commands.c
 * (activateCommands/retireCommands, print, staged commands) and
 * input_platform.c (pump, publication, seats, build, save, close).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_INPUT_INTERNAL_H
#define QA_APP_BOOTSTRAP_INPUT_INTERNAL_H
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/q1_client_commands.h"
#include "qa/app/bootstrap/q2_client_commands.h"
#include "qa/console/discovery.h"
#include "qa/foundation/event_ring.h"
#include "qa/input/buttons.h"

/* One LocalInput plus what the C port keeps beside it. pub is first. */
typedef struct input_local {
  qa_app_local_input pub;
  qa_mouse_input *mouse_input;      /* owned; the builder borrows it */
  qa_seat_console *created_console; /* owned console this input built */
  bool retired;                     /* removed from the seat set */
  bool has_profile_rumble; bool profile_rumble; double profile_rumble_strength;
} input_local;

/* TS uiCallbacks values. joined is createJoinedLocal's closure, which
 * captured its console; the constructor's reads localConsole.console. */
typedef struct input_ui_callback {
  qa_app_application_input *owner;
  qa_seat_input *input;
  qa_seat_id seat;
  input_local *local;
  qa_seat_console *captured_console; /* joined only */
  bool joined;
} input_ui_callback;

typedef struct input_seat_ui { qa_seat_id seat; qa_app_input_ui ui; } input_seat_ui;
typedef struct input_mouse { qa_seat_id seat; qa_mouse_settings settings; } input_mouse;
typedef struct input_q3_selection { qa_seat_id seat; qa_app_q3_command_selection selection; } input_q3_selection;
typedef struct input_arsenal { qa_seat_id seat; char *provider; char *weapon; } input_arsenal;
typedef struct input_offhand { qa_seat_id seat; qa_input_button grapple, grenade; } input_offhand;
typedef struct input_registry_pair { qa_cvar_registry *from, *to; } input_registry_pair;
typedef struct input_candidate_defaults { qa_seat_id seat; qa_input_binding *bindings; size_t count; } input_candidate_defaults;
typedef struct input_candidate_gamepad { qa_seat_input *input; qa_gamepad_tuning tuning; } input_candidate_gamepad;

/* TS stagedCommands record; text and the origin chain are owned. */
typedef struct input_staged_command {
  bool reliable;
  char *text;
  qa_command_context *source; /* deep copy */
  qa_app_reliable_dispatch_fn dispatch; void *dispatch_ctx;
} input_staged_command;

/* TS unregister / releaseUi cleanup entries. */
typedef enum input_cleanup_kind {
  INPUT_CLEANUP_COMMAND, INPUT_CLEANUP_RUN_BINDING, INPUT_CLEANUP_WHEEL, INPUT_CLEANUP_INPUT_COMMANDS,
  INPUT_CLEANUP_BINDING_COMMANDS, INPUT_CLEANUP_DISCOVERY, INPUT_CLEANUP_LLM, INPUT_CLEANUP_Q2,
  INPUT_CLEANUP_Q1, INPUT_CLEANUP_STARTUP_OUTPUT
} input_cleanup_kind;
typedef struct input_cleanup {
  input_cleanup_kind kind;
  void *object;               /* registration / registry / command ctx */
  int64_t handle;
} input_cleanup;
typedef struct input_ui_release { qa_seat_input *input; qa_seat_ui_binding *binding; } input_ui_release;

/* A registered application command's handler context. */
typedef struct input_command {
  qa_app_application_input *owner;
  char *name;
  int kind; /* see input_commands.c */
} input_command;

/* Owned allocation freed at the final release. */
typedef struct input_owned { void *object; void (*destroy)(void *object); } input_owned;

struct qa_app_application_input {
  int refs;
  qa_app_application_input **held; size_t held_count, held_capacity; /* creators this input holds */
  input_owned *owned; size_t owned_count, owned_capacity;
  bool closed;

  qa_app_input_window window;
  const qa_app_options *options;
  qa_command_dialect dialect;
  const qa_app_sim_presentation_access *simulation;
  qa_app_input_commands actions;
  qa_app_input_now_fn now; void *now_ctx;
  const qa_config_store *settings;

  qa_app_prepared_startup *startup;
  bool configuration_published;
  qa_app_console_script_files *script_owner;
  qa_cvar_registry *shared_owner;
  input_registry_pair *published; size_t published_count, published_capacity;
  qa_cvar_registry *cvar_owner;
  qa_cvar_registry *console_cvars;
  qa_app_console_routing *console_routing; /* owned; NULL when an owner routes */
  bool has_external_routing; qa_command_cvar_routing external_routing;
  qa_command_buffer *commands;
  bool archive_persistence;

  input_local **locals; size_t local_count, local_capacity;
  input_local **all_locals; size_t all_local_count, all_local_capacity; /* every record built */

  qa_sdl_controllers *controllers;
  qa_input_router *router;
  qa_app_controller_settings *controller_settings;
  qa_app_input_devices *input_devices;
  qa_config_store device_store; bool has_device_store;
  bool owns_input_devices;
  bool owns_controllers;
  double sequence;

  qa_event_ring *window_events; qa_event_cursor *window_cursor;
  qa_event_ring *controller_events; qa_event_cursor *controller_cursor;
  qa_app_haptic_load_fn haptic_load; void *haptic_load_ctx;
  /* ApplicationAudio.bindHaptics through qa_app_application_input_audio_haptics: the audio-shaped loader. */
  uint8_t *(*audio_haptic_load)(void *ctx, qa_resource_request request, size_t *out_length); void *audio_haptic_load_ctx;

  input_seat_ui *seat_uis; size_t seat_ui_count, seat_ui_capacity;
  input_mouse *mice; size_t mouse_count, mouse_capacity;
  input_q3_selection *q3; size_t q3_count, q3_capacity;
  input_arsenal *arsenal; size_t arsenal_count, arsenal_capacity;
  input_offhand **offhand; size_t offhand_count, offhand_capacity;
  input_cleanup *unregister; size_t unregister_count, unregister_capacity;
  input_ui_callback **ui_callbacks; size_t ui_callback_count, ui_callback_capacity;
  input_ui_release *release_ui; size_t release_ui_count, release_ui_capacity;
  qa_client_command_bindings *client_commands;
  bool commands_active;
  qa_event_ring *staged; qa_event_cursor *staged_cursor;

  qa_app_prepared_client_commands *candidate_program;
  bool owns_candidate_program;
  input_candidate_defaults *candidate_defaults; size_t candidate_default_count, candidate_default_capacity;
  input_candidate_gamepad *candidate_gamepads; size_t candidate_gamepad_count, candidate_gamepad_capacity;

  /* Per-build storage for ActorCommand.arsenal pointers. */
  qa_arsenal_intent *arsenal_out; size_t arsenal_out_count, arsenal_out_capacity;
  char *clipboard_text; /* last readSdlClipboard decode handed to a console */
  qa_command_dialect open_source_dialect; /* the constructor's sourceDialect */
};

/* Allocation helpers (abort on OOM, which TS never reports). */
void *qa_app_input_internal_grow(void *items, size_t *capacity, size_t count, size_t size);
char *qa_app_input_internal_dup(const char *text);
#define INPUT_PUSH(array, count, capacity) \
  ((array) = qa_app_input_internal_grow((array), &(capacity), (count), sizeof *(array)), &(array)[(count)++])

/* Ownership. */
void qa_app_input_internal_own(qa_app_application_input *input, void *object, void (*destroy)(void *object));
void qa_app_input_internal_register_object(qa_app_application_input *creator, const void *object);
/* Holds the creator of object (if another input) for input. */
void qa_app_input_internal_retain_object(qa_app_application_input *input, const void *object);
/* Holds the creator of object for an external owner (startup/client). */
void qa_app_input_internal_hold_for(const void *owner, const void *object);

/* Shared lookups. */
double qa_app_input_internal_now(qa_app_application_input *input);
input_local *qa_app_input_internal_find_local(const qa_app_application_input *input, qa_seat_id seat);
input_seat_ui *qa_app_input_internal_seat_ui(const qa_app_application_input *input, qa_seat_id seat);
qa_mouse_settings *qa_app_input_internal_mouse(const qa_app_application_input *input, qa_seat_id seat);
void qa_app_input_internal_set_mouse(qa_app_application_input *input, qa_seat_id seat, qa_mouse_settings settings);
void qa_app_input_internal_forget_seat(qa_app_application_input *input, input_local *local);
void qa_app_input_internal_set_published(qa_app_application_input *input, qa_cvar_registry *from, qa_cvar_registry *to);
qa_cvar_registry *qa_app_input_internal_console_seat(qa_app_application_input *input, qa_seat_id seat);
bool qa_app_input_internal_routing(qa_app_application_input *input, qa_command_cvar_routing *out);
qa_command_dialect qa_app_input_internal_source_dialect(qa_app_application_input *input);
qa_command_context qa_app_input_internal_seat_context(qa_session_id session, const qa_app_local_player *player);
const qa_command_origin *qa_app_input_internal_unwind(const qa_command_origin *origin);
void qa_app_input_internal_execute(qa_app_application_input *input, const char *name, const char *const *args,
    size_t count, const qa_seat_id *seat, const qa_command_context *source);
input_ui_callback *qa_app_input_internal_add_ui_callback(qa_app_application_input *input, qa_seat_input *seat_input,
    qa_seat_id seat, input_local *local, qa_seat_console *captured, bool joined);
void qa_app_input_internal_remove_ui_callback(qa_app_application_input *input, qa_seat_input *seat_input);
/* SeatConsole options shared by both construction sites. */
qa_seat_console *qa_app_input_internal_create_console(qa_app_application_input *input, qa_seat_id seat,
    qa_command_dialect dialect, qa_command_context context, qa_seat_input *seat_input, input_local *local, bool joined);
qa_seat_haptics *qa_app_input_internal_create_haptics(qa_app_application_input *input, qa_seat_id seat);
qa_cvar_registry *qa_app_input_internal_create_registry(qa_app_application_input *input, qa_command_dialect dialect,
    qa_command_context context, bool print_in_context);

/* input_commands.c */
void qa_app_input_internal_activate_commands(qa_app_application_input *input, bool preserve_focus);
void qa_app_input_internal_retire_commands(qa_app_application_input *input);
void qa_app_input_internal_print_source(void *ctx, const char *text, const qa_command_context *source);
void qa_app_input_internal_staged_reset(qa_app_application_input *input);
void qa_app_input_internal_staged_filter_client(qa_app_application_input *input, qa_client_id client);
qa_command_context *qa_app_input_internal_copy_context(const qa_command_context *source);
void qa_app_input_internal_free_context(qa_command_context *context);
void qa_app_input_internal_forward_to_server(void *ctx, qa_command_invocation *invocation);

/* input_platform.c */
void qa_app_input_internal_release_offhand(qa_app_application_input *input, bool all);
void qa_app_input_internal_synchronize_client_focus(qa_app_application_input *input);
void qa_app_input_internal_publish_seats_to_startup(qa_app_application_input *input, bool retain);
#endif

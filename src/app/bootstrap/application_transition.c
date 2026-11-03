/* Port of src/app/bootstrap/application.ts, group P4 (world replacement):
 * WorldReplacementRequest, PreviousWorld, WorldPlan, StageSlot and
 * WorldTransition, replaceWorld, prepareAndReplaceWorld and its stages, and
 * changeLevel; plus world-replacement.ts (WorldReplacement), whose only
 * consumer is this transaction. TS awaits are synchronous calls; a TS
 * try/catch/finally is one qa_try over the body (support.h). Layout and
 * per-member status: docs/application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/component_client_save.h"
#include "qa/app/bootstrap/menu_art.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/app/bootstrap/simulation/network_unified.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/content/user_data.h"
#include "qa/foundation/js.h"
#include "qa/render/q3_hardware.h"
#include "qa/formats/q2_map.h"
#include "qa/app/bootstrap/loading.h"
#include "qa/app/bootstrap/match_preflight.h"
#include "qa/app/bootstrap/q1_source_cvars.h"
#include "qa/app/bootstrap/simulation/monster_navigation.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/startup_source.h"
#include "qa/content/catalog/launch.h"
#include "qa/network/common/js_number.h"
#include "qa/settings/server/index.h"
#include "qa/world/session/mod_commands.h"
#include "qa/world/session/mod_files.h"

/* TS registry.find(name) !== undefined (the C find fills a read the caller frees). */
static bool cvar_exists(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(cvars, name, &read)) return false;
  qa_cvar_read_free(&read);
  return true;
}
static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void grow(void **items, size_t *capacity, size_t needed, size_t size) {
  if (needed <= *capacity) return;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  while (next < needed) next *= 2;
  *items = checked(realloc(*items, next * size));
  *capacity = next;
}
static char *dup_text(const char *text) { return text == NULL ? NULL : checked(strdup(text)); }
/* C-only: a record the application owns until destroy (TS GC); seat_records
 * releases it with release, then frees it. */
static void keep_record(qa_application *app, void *record, void (*release)(void *record)) {
  grow((void **)&app->seat_records, &app->seat_record_capacity, app->seat_record_count + 1, sizeof *app->seat_records);
  app->seat_records[app->seat_record_count].record = record;
  app->seat_records[app->seat_record_count++].release = release;
}
static void keep_text(qa_application *app, char *text) { keep_record(app, text, NULL); }
/* TS `${error}` / String(error) of an Error. */
static void error_text(const qa_caught_error *error, char *out, size_t capacity) {
  (void)snprintf(out, capacity, "%s: %s", error->name, error->message);
}

/* A deep copy of ApplicationOptions: the loaded content takes over the options it is given (content.c). */
static char **dup_lines(char *const *lines, size_t count) {
  if (count == 0) return NULL;
  char **out = checked(calloc(count, sizeof *out));
  for (size_t i = 0; i < count; i++) out[i] = dup_text(lines[i]);
  return out;
}
static qa_app_options options_dup(const qa_app_options *options) {
  qa_app_options copy = *options;
  copy.network_transport_relay = dup_text(options->network_transport_relay);
  copy.startup_commands = dup_lines(options->startup_commands, options->startup_command_count);
  copy.remote_content.directory = dup_text(options->remote_content.directory);
  copy.server_profile_path = dup_text(options->server_profile_path);
  copy.corpus_root = dup_text(options->corpus_root);
  copy.user_content_root = dup_text(options->user_content_root);
  if (options->addon_content_root_count != 0) {
    copy.addon_content_roots = checked(calloc(options->addon_content_root_count, sizeof *copy.addon_content_roots));
    for (size_t i = 0; i < options->addon_content_root_count; i++) {
      copy.addon_content_roots[i].family = options->addon_content_roots[i].family;
      copy.addon_content_roots[i].root = dup_text(options->addon_content_roots[i].root);
    }
  } else copy.addon_content_roots = NULL;
  copy.product = dup_text(options->product);
  copy.map_product = dup_text(options->map_product);
  copy.map = dup_text(options->map);
  copy.quakec_program = dup_text(options->quakec_program);
  copy.q2_game_library = dup_text(options->q2_game_library);
  copy.weapon_behavior_product = dup_text(options->weapon_behavior_product);
  copy.weapon_behavior_id = dup_text(options->weapon_behavior_id);
  if (options->mod_count != 0) {
    copy.mods = checked(calloc(options->mod_count, sizeof *copy.mods));
    for (size_t i = 0; i < options->mod_count; i++) {
      copy.mods[i].product = dup_text(options->mods[i].product);
      copy.mods[i].id = dup_text(options->mods[i].id);
    }
  } else copy.mods = NULL;
  copy.movement_product = dup_text(options->movement_product);
  copy.character_model = dup_text(options->character_model);
  copy.character_skin = dup_text(options->character_skin);
  copy.network_host = dup_text(options->network_host);
  copy.network_remote = dup_text(options->network_remote);
  return copy;
}
/* C-only: the deep copy openSource hands the content loader (application_internal.h). */
qa_app_options qa_application_options_copy(const qa_app_options *options) { return options_dup(options); }
/* The application's options borrow their strings (application_class.h); a planned destination's options are a
 * deep copy the application keeps (TS GC). */
static void options_release(void *record) { qa_app_options_destroy(record); }
static qa_app_options *kept_options(qa_application *app, const qa_app_options *options) {
  qa_app_options *copy = checked(malloc(sizeof *copy));
  *copy = options_dup(options);
  keep_record(app, copy, options_release);
  return copy;
}

/* ================= world-replacement.ts: WorldReplacement ================= */
typedef struct qa_application_world_replacement {
  qa_app_shared_simulation *simulation;
  qa_app_application_assets *assets;
  qa_native_ui_art *art;
  qa_app_sim_bot_service *bots;
  qa_application_local_q3_guest_world *local_guest;
  qa_application_local_q3_guest_browser *guest_browser;
  qa_application_saved_clients *saved_clients;
  qa_app_application_audio *audio;
  qa_app_application_effects *effects;
  qa_app_application_input *input;
  qa_app_console_script_files *scripts;  /* staged for publication; NULL once the application took them */
  bool has_guest_transition; qa_app_q3_guest_map_transition guest_transition;
  qa_application_graphical *graphical;
  qa_app_capture *capture;
  qa_app_world_seat_presentation **presentations; size_t presentation_count, presentation_capacity;
  qa_seat_id *native_q2_seats; qa_application_native_q2_seat_client *native_q2; size_t native_q2_count, native_q2_capacity;
  qa_app_application_q3_client **clients; size_t client_count, client_capacity;
  bool committed;         /* the session published the new world; rollback is no longer possible */
  bool native_committed;  /* the previous native or guest source was already torn down */
  qa_app_loaded_content *content;
  qa_application_local_q3_guest_browser *previous_guest_browser;
} qa_application_world_replacement;

static qa_application_world_replacement *world_replacement_create(qa_app_loaded_content *content,
    qa_application_local_q3_guest_browser *previous_guest_browser) {
  qa_application_world_replacement *staged = checked(calloc(1, sizeof *staged));
  staged->content = content;
  staged->previous_guest_browser = previous_guest_browser;
  staged->guest_browser = previous_guest_browser;
  return staged;
}
/* TS takeScripts(). */
static qa_app_console_script_files *world_replacement_take_scripts(qa_application_world_replacement *staged) {
  qa_app_console_script_files *scripts = staged->scripts;
  staged->scripts = NULL;
  return scripts;
}
/* TS rollback(error, retainedRenderer): close every resource still owned, then throw. */
typedef struct rollback_errors { size_t count; } rollback_errors;
typedef struct rollback_step { qa_application_world_replacement *staged; size_t index; qa_app_native_renderer *renderer; double sequence; } rollback_step;
static void discard(rollback_errors *errors, void (*close)(void *ctx), rollback_step *step) {
  qa_caught_error caught;
  if (!qa_try(close, step, &caught)) errors->count++;
}
static void rb_capture(void *ctx) { rollback_step *s = ctx; if (s->staged->capture != NULL) qa_app_capture_destroy(s->staged->capture); s->staged->capture = NULL; }
static void rb_client(void *ctx) { rollback_step *s = ctx; qa_app_application_q3_client_close(s->staged->clients[s->index]); }
static void rb_browser_view(void *ctx) { rollback_step *s = ctx; qa_q3_browser_view_close(s->staged->guest_browser->view); }
static void rb_browser_host(void *ctx) { rollback_step *s = ctx; qa_app_startup_server_browser_close(s->staged->guest_browser->host); }
static void rb_mod_presentations(void *ctx) {
  rollback_step *s = ctx;
  if (s->staged->graphical != NULL) qa_app_application_mod_presentations_close(s->staged->graphical->mod_presentations);
}
static void rb_presentation(void *ctx) {
  rollback_step *s = ctx;
  qa_app_world_seat_presentation_close(s->staged->presentations[s->index]);
}
static void rb_native_effects(void *ctx) { rollback_step *s = ctx; qa_app_application_effects_close(s->staged->native_q2[s->index].effects); }
static void rb_audio(void *ctx) { rollback_step *s = ctx; if (s->staged->audio != NULL) qa_app_application_audio_close(s->staged->audio); }
static void rb_effects(void *ctx) { rollback_step *s = ctx; if (s->staged->effects != NULL) qa_app_application_effects_close(s->staged->effects); }
static void rb_input(void *ctx) { rollback_step *s = ctx; if (s->staged->input != NULL) qa_app_application_input_close(s->staged->input); }
static void rb_scripts(void *ctx) { rollback_step *s = ctx; if (s->staged->scripts != NULL) qa_app_console_script_files_close(s->staged->scripts); }
static void rb_bots(void *ctx) { rollback_step *s = ctx; if (s->staged->bots != NULL) s->staged->bots->close(s->staged->bots->ctx, false); }
static void rb_shutdown_guest(void *ctx) { rollback_step *s = ctx; if (s->staged->simulation != NULL) qa_app_shared_sim_shutdown_q3_guest(s->staged->simulation); }
static void rb_discard_guest(void *ctx) {
  rollback_step *s = ctx;
  qa_app_q3_qvm_server_game *guest = s->staged->simulation == NULL ? NULL : qa_app_shared_sim_q3_guest(s->staged->simulation);
  if (guest != NULL) qa_app_q3_qvm_server_game_discard(guest);
}
static void rb_simulation(void *ctx) { rollback_step *s = ctx; if (s->staged->simulation != NULL) qa_app_shared_sim_close(s->staged->simulation); }
static void rb_saved_client(void *ctx) { rollback_step *s = ctx; qa_session_client_close(s->staged->saved_clients->added[s->index]); }
static void rb_art(void *ctx) { rollback_step *s = ctx; if (s->staged->art != NULL) qa_native_ui_art_close(s->staged->art); }
static void rb_assets(void *ctx) { rollback_step *s = ctx; if (s->staged->assets != NULL) qa_app_application_assets_close(s->staged->assets); }
static void rb_renderer(void *ctx) {
  rollback_step *s = ctx;
  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.owner = qa_app_native_renderer_owner(s->renderer);
  frame.sequence = s->sequence;
  qa_app_native_renderer_execute(s->renderer, frame);
}
static void rb_content(void *ctx) { rollback_step *s = ctx; qa_app_loaded_content_close(s->staged->content); }
static noreturn void world_replacement_rollback(qa_application_world_replacement *staged, const qa_caught_error *error,
    qa_app_native_renderer *retained_renderer, double sequence) {
  rollback_errors errors = { 0 };
  rollback_step step = { staged, 0, retained_renderer, sequence };
  discard(&errors, rb_capture, &step);
  for (step.index = 0; step.index < staged->client_count; step.index++) discard(&errors, rb_client, &step);
  for (size_t i = 0; staged->local_guest != NULL && i < staged->local_guest->seat_count; i++)
    qa_app_local_q3_client_state_retire(staged->local_guest->seats[i].state);
  if (staged->local_guest != NULL) staged->local_guest->seat_count = 0;
  if (staged->guest_browser != NULL && staged->guest_browser != staged->previous_guest_browser) {
    discard(&errors, rb_browser_view, &step);
    discard(&errors, rb_browser_host, &step);
    qa_application_release_guest_browser(staged->guest_browser);
    staged->guest_browser = NULL;
  }
  discard(&errors, rb_mod_presentations, &step);
  for (step.index = 0; step.index < staged->presentation_count; step.index++) discard(&errors, rb_presentation, &step);
  for (step.index = 0; step.index < staged->native_q2_count; step.index++) discard(&errors, rb_native_effects, &step);
  discard(&errors, rb_audio, &step);
  discard(&errors, rb_effects, &step);
  discard(&errors, rb_input, &step);
  discard(&errors, rb_scripts, &step);
  discard(&errors, rb_bots, &step);
  if (staged->has_guest_transition) discard(&errors, rb_shutdown_guest, &step);
  discard(&errors, rb_discard_guest, &step);
  discard(&errors, rb_simulation, &step);
  for (step.index = 0; staged->saved_clients != NULL && step.index < staged->saved_clients->added_count; step.index++)
    discard(&errors, rb_saved_client, &step);
  discard(&errors, rb_art, &step);
  discard(&errors, rb_assets, &step);
  if (retained_renderer != NULL) discard(&errors, rb_renderer, &step);
  discard(&errors, rb_content, &step);
  if (errors.count != 0) qa_raise("AggregateError", 0, (long long)(errors.count + 1), "World preparation failed");
  qa_rethrow(error);
}

/* ================= WorldPlan, PreviousWorld, StageSlot, WorldTransition ================= */
/* TS PreviousWorld: what the application held before the replacement started. */
typedef struct qa_application_previous_world {
  qa_application_graphical *graphical;
  qa_app_loaded_content *content;
  qa_app_shared_simulation *simulation;
  qa_application_local_q3_guest_world *local_guest;
  qa_app_options options;
  qa_seat_id *client_cvar_seats; qa_cvar_registry **client_cvars; size_t client_cvar_count;
  qa_app_team_arena_overrides *overrides;
  qa_app_sim_classic_guest_client *native_clients; size_t native_client_count;
  qa_app_sim_bot_client *bot_clients; size_t bot_client_count;
  qa_app_sim_bot_service *bots;
} qa_application_previous_world;

/* TS StageSlot<T>: reading a slot before its stage ran is a sequencing fault. */
static void slot_read(bool set, const char *name) {
  if (!set) qa_raise("Error", 0, 0, "World transition stage output %s was read before its stage ran", name);
}
#define SLOT(tx, field) (slot_read((tx)->has_##field, #field), (tx)->field)

/* TS WorldTransition (WorldPlan fields first). */
typedef struct qa_application_world_transition {
  /* WorldPlan */
  const char *map;
  const qa_save_image *save;
  const qa_app_team_arena_skirmish *skirmish;
  const qa_app_q3_map_launch *q3_map;
  const qa_app_q2_travel_target *native_travel;
  void (*next_frame)(void *ctx); void *next_frame_ctx;
  bool guest_restart;
  const char *q2_next_server;
  const qa_app_simulation_travel *carry;
  qa_app_simulation_travel carry_value;
  double initial_source_milliseconds;
  qa_app_options *options;
  qa_app_loaded_content *content;
  bool has_settings; qa_app_sim_saved_settings settings;
  qa_app_sim_saved_bots *saved_bots; qa_app_sim_decoded_bots_checkpoint *decoded_bots;
  bool same_client_owner, same_source_owner;
  qa_app_frontend_preference_overrides frontend_overrides;
  qa_app_q3_source_runtime *q3;
  bool preserve_bots;
  qa_app_sim_bot_client *bot_clients; size_t bot_client_count;
  bool has_next_rules; qa_app_startup_rules next_rules;
  bool has_q3_session; qa_app_q3_captured_session q3_session;
  bool has_server_profile; qa_server_profile server_profile;
  qa_cvar_registry *q1_cvars_source;
  qa_cvar_archive_entry *source_archive; size_t source_archive_count;
  bool has_q2_cvars; qa_app_sim_cvar_name_value *q2_cvars; size_t q2_cvar_count;
  bool has_q3_cvars; qa_app_sim_cvar_name_value *q3_cvars; size_t q3_cvar_count;
  qa_cvar_snapshot *q2_snapshots; size_t q2_snapshot_count; /* storage of q2_cvars */
  qa_cvar_snapshot *q3_snapshots; size_t q3_snapshot_count; /* storage of q3_cvars */
  qa_app_application_preset_result *preset; qa_resolve_launch_result *launch; /* storage of a skirmish recipe */
  qa_executable_recipe recipe;
  qa_app_team_arena_overrides *next_overrides;
  /* #before */
  bool has_before; qa_application_previous_world before;
  qa_app_console_script_files *retired_scripts;
  bool has_post_publication_error; qa_caught_error post_publication_error;
  bool guest_network_published;
  double guest_network_command_bytes;
  bool candidate_published;
  qa_command_buffer *candidate_commands;
  bool has_candidate_images; qa_app_image_client_settings candidate_images;
  bool has_next_network_host; qa_application_native_server_host next_network_host;
  bool has_recording_transition_error; qa_caught_error recording_transition_error;
  struct { char label[64]; qa_caught_error error; } *retirement_errors; size_t retirement_error_count, retirement_error_capacity;
  /* set by initializeWorldTransition */
  bool has_staged; qa_application_world_replacement *staged;
  bool has_candidate_graph; qa_source_debug_graph *candidate_graph;
  bool has_guest_rejected_clients; qa_app_q3_rejection *guest_rejected_clients; size_t guest_rejected_client_count, guest_rejected_client_capacity;
  bool has_guest_network_commands; qa_event_ring guest_network_commands; /* { slot, text } records */
  bool has_guest_candidate_output; qa_app_q3_guest_output guest_candidate_output;
  bool has_carried_guest_commands; double *carried_slots; qa_q3_base_user_command *carried_commands; bool *carried_present; size_t carried_count;
  bool has_next_client_cvars; qa_seat_id *next_client_cvar_seats; qa_cvar_registry **next_client_cvars; size_t next_client_cvar_count, next_client_cvar_capacity;
  /* set by stageCandidateInputs and stageCandidateCommands */
  bool has_q1_source_registry; qa_cvar_registry *q1_source_registry;
  bool has_clients; qa_client_id *clients; size_t client_count;
  bool has_mod_commands; qa_mod_commands *mod_commands;
  bool has_candidate_actions; qa_application_command_request *candidate_actions; size_t candidate_action_count, candidate_action_capacity;
  bool has_candidate_action; /* candidateAction: candidate_action_admit with ctx tx */
  bool has_native_command;   /* nativeCommand: native_command_append with ctx tx */
  bool has_guest_commands;   /* guestCommands: guest_commands_buffer with ctx tx */
  bool has_monster_navigation; qa_app_sim_application_monster_navigation *monster_navigation;
  bool has_mod_travel; qa_mod_travel_entry *mod_travel; size_t mod_travel_count;
  bool has_continue_q3_clock; bool continue_q3_clock;
  bool has_destination_source_milliseconds; double destination_source_milliseconds;
  /* set by stageSimulation, stageClientState, stageNetworkHost and stageSourceCommands */
  bool has_next_simulation; qa_app_shared_simulation *next_simulation;
  bool has_admissions; double *admission_slots; qa_actor_id *admissions; size_t admission_count, admission_capacity;
  bool has_next_unified_host; bool unified_host_present; qa_app_unified_server_host next_unified_host; qa_app_unified_composition *next_unified_composition;
  bool has_prepared_commands; qa_application_prepared_source_commands *prepared_commands;
  /* set by stageCapture, stageReplacementSet, stageHeadlessAdoption and stageCommit */
  qa_app_capture *previous_capture;
  bool has_next_capture; qa_app_capture *next_capture;
  bool has_publish_audio; qa_unified_audio_transfer publish_audio;
  bool has_replacement_presentations; qa_session_presentation_entry *replacement_presentations; size_t replacement_presentation_count;
  bool has_replacement_clients; qa_session_client_change replacement_clients;
  void (*adopt_headless)(void *ctx); void *adopt_headless_ctx;
  bool has_replacement; qa_session_resource replacement;
  qa_application *app;
  qa_application_simulation_view *next_view; /* the qa_simulation the session publishes for next_simulation */
} qa_application_world_transition;

/* TS get before(). */
static qa_application_previous_world *tx_before(qa_application_world_transition *tx) {
  if (!tx->has_before) qa_raise("Error", 0, 0, "The world transition has ended");
  return &tx->before;
}
/* TS end(): drop the previous world's references; closures the new world kept still hold this transaction. */
static void world_transition_end(qa_application_world_transition *tx) {
  if (tx->has_before) {
    free(tx->before.client_cvar_seats); free(tx->before.client_cvars);
    free(tx->before.native_clients); free(tx->before.bot_clients);
  }
  tx->has_before = false;
  tx->q3 = NULL;
  if (tx->has_q3_session) qa_app_q3_captured_session_destroy(&tx->q3_session);
  tx->has_q3_session = false;
  tx->q1_cvars_source = NULL;
  tx->has_server_profile = false;
  free(tx->bot_clients); tx->bot_clients = NULL; tx->bot_client_count = 0;
  tx->has_next_rules = false;
  tx->carry = NULL;
  tx->next_overrides = NULL;
  tx->carried_count = 0;
  tx->has_replacement = false;
  tx->adopt_headless = NULL;
  tx->previous_capture = NULL;
  free(tx->mod_travel); tx->mod_travel = NULL; tx->has_mod_travel = false;
  tx->has_publish_audio = false;
}

/* ================= replaceWorld ================= */
typedef struct replace_run {
  qa_application *app;
  const char *map;
  const qa_app_simulation_travel *carry;
  const qa_application_replace_world_options *options;
} replace_run;
static void replace_operation(void *ctx, qa_app_loading_frame *frame);
static void loading_next_frame(void *ctx) { qa_app_loading_next_frame(ctx); }
static void poll_loading_events(void *ctx) {
  qa_application *app = ctx;
  if (!app->closed && app->graphical != NULL) qa_app_application_input_poll_loading_events(app->graphical->input);
}
static void replace_operation(void *ctx, qa_app_loading_frame *frame) {
  replace_run *run = ctx;
  qa_application_world_replacement_request request;
  memset(&request, 0, sizeof request);
  request.map = run->map;
  request.carry = run->carry;
  request.initial_source_milliseconds = run->options->initial_source_milliseconds;
  request.save = run->options->save;
  request.skirmish = run->options->skirmish;
  request.native_travel = run->options->native_travel;
  request.q3_map = run->options->q3_map;
  request.next_frame = loading_next_frame; request.next_frame_ctx = frame;
  request.guest_restart = run->options->guest_restart;
  request.q2_next_server = run->options->q2_next_server;
  qa_application_prepare_and_replace_world(run->app, &request);
}
static void replace_body(void *ctx) {
  replace_run *run = ctx;
  qa_app_service_loading(replace_operation, run, poll_loading_events, run->app);
}
void qa_application_replace_world_with(qa_application *app, const char *map, const qa_app_simulation_travel *carry,
    const qa_application_replace_world_options *options) {
  qa_application_replace_world_options defaults;
  memset(&defaults, 0, sizeof defaults);
  if (options == NULL) options = &defaults;
  if (qa_application_campaign_movie(app) != NULL) qa_raise("Error", 0, 0, "Finish or skip the campaign cinematic before changing worlds");
  if (app->world_operation != QA_APPLICATION_IDLE) qa_raise("Error", 0, 0, "Another world operation is in progress");
  app->world_operation = QA_APPLICATION_TRAVEL;
  app->world_operation_completion = true;
  replace_run run = { app, map, carry, options };
  qa_caught_error caught;
  bool ok = qa_try(replace_body, &run, &caught);
  app->world_operation = QA_APPLICATION_IDLE;
  app->world_operation_completion = false;
  app->world_operations++;
  if (!app->closed) qa_application_resume_input(app);
  if (!ok) {
    if (app->fatal_world_failure && !app->stepping) qa_application_close_failed_world(app, &caught);
    qa_rethrow(&caught);
  }
  if (options->published != NULL) options->published(options->published_ctx);
  else {
    const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(app->loaded_content);
    qa_app_resource_request destination = { recipe->map.geometry_content, recipe->map.geometry.requested_path };
    qa_app_campaign_unit_visit *visit = qa_app_campaign_unit_stage(app->campaign_unit, destination,
        options->native_travel != NULL && options->native_travel->new_unit, NULL);
    qa_app_campaign_unit_visit_commit(visit);
  }
  if (options->save == NULL) qa_application_autosave_level(app);
}
void qa_application_replace_world(qa_application *app, const char *map, const qa_app_simulation_travel *carry) {
  qa_application_replace_world_with(app, map, carry, NULL);
}

/* ================= prepareAndReplaceWorld ================= */
static void plan_world_replacement(qa_application *app, const qa_application_world_replacement_request *request,
    qa_application_world_transition *tx);
static void initialize_world_transition(qa_application *app, qa_application_world_transition *tx);
static void stage_candidate_inputs(qa_application *app, qa_application_world_transition *tx);
static void stage_candidate_commands(qa_application *app, qa_application_world_transition *tx);
static void stage_native_map_travel(qa_application *app, qa_application_world_transition *tx);
/* The variables of a CvarRegistry save state ({ variables: ({ name, value, latchedValue } | null)[] }); strings
 * borrow the state tree; a null entry has name NULL. The rows are kept with the application. */
typedef struct guest_variable { const char *name, *value, *latched_value; } guest_variable;
static guest_variable *guest_variables(qa_application *app, const qa_save_value *state, size_t *out_count) {
  qa_save_reader reader, variables;
  if (!qa_save_reader_init(&reader, state, "cvars") || !qa_save_reader_field(&reader, "variables", &variables)) qa_range_error("Out of memory");
  size_t count = qa_save_reader_list_count(&variables);
  guest_variable *rows = checked(calloc(count + 1, sizeof *rows));
  keep_record(app, rows, NULL);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, name, value, latched;
    if (!qa_save_reader_item(&variables, i, &item)) qa_range_error("Out of memory");
    if (!qa_save_reader_is_null(&item)) {
      if (!qa_save_reader_field(&item, "name", &name) || !qa_save_reader_field(&item, "value", &value)
          || !qa_save_reader_field(&item, "latchedValue", &latched)) qa_range_error("Out of memory");
      rows[i].name = qa_save_reader_string(&name);
      rows[i].value = qa_save_reader_string(&value);
      rows[i].latched_value = qa_save_reader_is_null(&latched) ? NULL : qa_save_reader_string(&latched);
      qa_save_reader_destroy(&name); qa_save_reader_destroy(&value); qa_save_reader_destroy(&latched);
    }
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&variables);
  qa_save_reader_destroy(&reader);
  *out_count = count;
  return rows;
}
static void stage_guest_map_shutdown(qa_application *app, qa_application_world_transition *tx);
static void stage_simulation(qa_application *app, qa_application_world_transition *tx);
static void stage_client_state(qa_application *app, qa_application_world_transition *tx);
static void stage_network_host(qa_application *app, qa_application_world_transition *tx);
static void stage_source_commands(qa_application *app, qa_application_world_transition *tx);
static void stage_guest_continuation(qa_application *app, qa_application_world_transition *tx);
static void stage_bots(qa_application *app, qa_application_world_transition *tx);
static void stage_graphical(qa_application *app, qa_application_world_transition *tx);
static void stage_capture(qa_application *app, qa_application_world_transition *tx);
static void stage_replacement_set(qa_application *app, qa_application_world_transition *tx);
static void stage_headless_adoption(qa_application *app, qa_application_world_transition *tx);
static void stage_commit(qa_application *app, qa_application_world_transition *tx);
static void publish_world(qa_application *app, qa_application_world_transition *tx);
static void finish_publication(qa_application *app, qa_application_world_transition *tx);

static void transition_stages(void *ctx) {
  qa_application_world_transition *tx = ctx;
  qa_application *app = tx->app;
  stage_candidate_inputs(app, tx);
  stage_candidate_commands(app, tx);
  stage_native_map_travel(app, tx);
  stage_guest_map_shutdown(app, tx);
  stage_simulation(app, tx);
  stage_client_state(app, tx);
  stage_network_host(app, tx);
  stage_source_commands(app, tx);
  stage_guest_continuation(app, tx);
  stage_bots(app, tx);
  stage_graphical(app, tx);
  stage_capture(app, tx);
  stage_replacement_set(app, tx);
  stage_headless_adoption(app, tx);
  stage_commit(app, tx);
  publish_world(app, tx);
  finish_publication(app, tx);
}
typedef struct prepare_run { qa_application *app; const qa_application_world_replacement_request *request; qa_application_world_transition *tx; } prepare_run;
static void prepare_body(void *ctx) {
  prepare_run *run = ctx;
  qa_application *app = run->app;
  qa_application_world_transition *tx = run->tx;
  initialize_world_transition(app, tx);
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_caught_error caught;
  if (!qa_try(transition_stages, tx, &caught)) {
    if (staged->committed) {
      app->fatal_world_failure = true;
      app->closed = true;
      app->stopping = true;
      qa_rethrow(&caught);
    }
    if (staged->native_committed) app->fatal_world_failure = true;
    qa_application_graphical *before = tx_before(tx)->graphical;
    world_replacement_rollback(staged, &caught, before == NULL ? NULL : before->renderer, app->presentation_frames);
  }
  if (tx->has_post_publication_error) qa_rethrow(&tx->post_publication_error);
}
/* The transaction outlives this call: closures the new world keeps hold it (TS GC). */
static void transition_release(void *record) {
  qa_application_world_transition *tx = record;
  world_transition_end(tx);
  if (tx->has_settings) { free(tx->settings.client_slots); qa_save_value_free(tx->settings.value); }
  if (tx->saved_bots != NULL) { qa_save_value_free(tx->saved_bots->value); free(tx->saved_bots->connections); free(tx->saved_bots); }
  if (tx->decoded_bots != NULL) { qa_app_sim_decoded_bots_checkpoint_free(tx->decoded_bots); free(tx->decoded_bots); }
  for (size_t i = 0; i < tx->source_archive_count; i++) { free(tx->source_archive[i].name); free(tx->source_archive[i].value); }
  free(tx->source_archive);
  free(tx->q2_cvars); free(tx->q3_cvars);
  for (size_t i = 0; i < tx->q2_snapshot_count; i++) qa_cvar_read_free(&tx->q2_snapshots[i]);
  free(tx->q2_snapshots);
  for (size_t i = 0; i < tx->q3_snapshot_count; i++) qa_cvar_read_free(&tx->q3_snapshots[i]);
  free(tx->q3_snapshots);
  if (tx->preset != NULL) { qa_app_application_preset_result_free(tx->preset); free(tx->preset); }
  if (tx->launch != NULL) { qa_resolve_launch_result_free(tx->launch); free(tx->launch); }
  free(tx->guest_rejected_clients);
  if (tx->has_guest_network_commands) qa_event_ring_destroy(&tx->guest_network_commands);
  free(tx->carried_slots); free(tx->carried_commands); free(tx->carried_present);
  free(tx->next_client_cvar_seats); free(tx->next_client_cvars);
  free(tx->clients);
  for (size_t i = 0; i < tx->candidate_action_count; i++) qa_application_command_request_free(&tx->candidate_actions[i]);
  free(tx->candidate_actions);
  free(tx->admission_slots); free(tx->admissions);
  free(tx->replacement_presentations);
  free(tx->retirement_errors);
  if (tx->has_staged) {
    free(tx->staged->presentations); free(tx->staged->native_q2_seats); free(tx->staged->native_q2); free(tx->staged->clients);
    free(tx->staged);
  }
}
void qa_application_prepare_and_replace_world(qa_application *app, const qa_application_world_replacement_request *request) {
  qa_application_world_transition *tx = checked(calloc(1, sizeof *tx));
  tx->app = app;
  keep_record(app, tx, transition_release);
  plan_world_replacement(app, request, tx);
  prepare_run run = { app, request, tx };
  qa_caught_error caught;
  bool ok = qa_try(prepare_body, &run, &caught);
  /* Closures kept by the new world share the transaction; it must not keep the old world alive (AUDIT-001). */
  world_transition_end(tx);
  if (!ok) qa_rethrow(&caught);
}

/* ================= planWorldReplacement ================= */
/* recipe.execution.some(module => module.role === "server-game" && (qvm || native q3-qagame)). */
static bool qvm_server_game(const qa_executable_recipe *recipe) {
  for (size_t i = 0; i < recipe->execution_count; i++) {
    const qa_resolved_execution_module *module = &recipe->execution[i];
    if (module->role == NULL || strcmp(module->role, "server-game") != 0) continue;
    if (module->kind == QA_EXECUTION_MODULE_QVM) return true;
    if (module->kind == QA_EXECUTION_MODULE_NATIVE && module->api.kind == QA_GAME_API_Q3 && module->api.as.q3.kind == QA_Q3_API_QAGAME)
      return true;
  }
  return false;
}
static bool same_reference(qa_provider_reference left, qa_provider_reference right) {
  return strcmp(left.content, right.content) == 0 && strcmp(left.provider, right.provider) == 0;
}
static qa_app_sim_cvar_name_value *latched_values(qa_cvar_snapshot *snapshots, size_t count, bool skip_map_names, size_t *out_count) {
  qa_app_sim_cvar_name_value *values = checked(calloc(count + 1, sizeof *values));
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    if (skip_map_names && (strcmp(snapshots[i].name, "sv_mapname") == 0 || strcmp(snapshots[i].name, "mapname") == 0)) continue;
    values[kept].name = snapshots[i].name;
    values[kept++].value = snapshots[i].latched_value != NULL ? snapshots[i].latched_value : snapshots[i].value;
  }
  *out_count = kept;
  return values;
}
typedef struct rules_try { qa_app_options options; qa_cvar_registry *source; double capacity; qa_app_startup_rules rules; } rules_try;
static void rules_body(void *ctx) {
  rules_try *t = ctx;
  t->rules = qa_app_resolve_startup_rules(t->options, t->source, t->capacity, NULL, 0, false);
}
static void content_close_body(void *ctx) { qa_app_loaded_content_close(ctx); }
/* TS nextFrame default: async () => { await Bun.sleep(0); } (one turn of an event loop the C port does not have). */
static void default_next_frame(void *ctx) {
  (void)ctx; /* the default closure captures nothing */
}
static void native_target_release(void *record) {
  qa_app_q2_travel_target *target = record;
  free(target->name);
  free(target->spawn_point);
}
static void plan_world_replacement(qa_application *app, const qa_application_world_replacement_request *request,
    qa_application_world_transition *tx) {
  const qa_save_image *save = request->save;
  /* The destination world keeps what the request points at (TS GC): the transaction keeps copies. */
  char *map = dup_text(request->map);
  keep_text(app, map);
  tx->map = map;
  tx->save = save;
  tx->skirmish = request->skirmish;
  if (request->q3_map != NULL) {
    qa_app_q3_map_launch *q3_map = checked(malloc(sizeof *q3_map));
    keep_record(app, q3_map, NULL);
    *q3_map = *request->q3_map;
    for (size_t i = 0; i < q3_map->cvar_count; i++)
      if (q3_map->cvars[i].value == request->q3_map->gametype_value) q3_map->cvars[i].value = q3_map->gametype_value;
    tx->q3_map = q3_map;
  }
  if (request->native_travel != NULL) {
    qa_app_q2_travel_target *target = checked(calloc(1, sizeof *target));
    keep_record(app, target, native_target_release);
    target->kind = request->native_travel->kind;
    target->name = dup_text(request->native_travel->name);
    target->spawn_point = dup_text(request->native_travel->spawn_point);
    target->new_unit = request->native_travel->new_unit;
    tx->native_travel = target;
  }
  tx->next_frame = request->next_frame != NULL ? request->next_frame : default_next_frame;
  tx->next_frame_ctx = request->next_frame_ctx;
  tx->guest_restart = request->guest_restart;
  char *next_server = dup_text(request->q2_next_server == NULL ? "" : request->q2_next_server);
  keep_text(app, next_server);
  tx->q2_next_server = next_server;
  tx->initial_source_milliseconds = request->initial_source_milliseconds;
  tx->carry = request->carry;
  if (save != NULL && app->network != NULL) qa_raise("Error", 0, 0, "Save/load unavailable while hosting a network game.");
  if (save != NULL && app->graphical == NULL) qa_app_require_component_client_presentation(save);
  qa_app_sim_quakec_source *quakec = qa_app_shared_sim_quakec_source(app->world_simulation);
  if (save == NULL && tx->carry == NULL && quakec != NULL && strcmp(qa_app_sim_quakec_source_kind(quakec), "quakeworld") == 0) {
    tx->carry_value = qa_app_shared_sim_capture_travel(app->world_simulation, "");
    tx->carry = &tx->carry_value;
  }
  if (save != NULL) {
    tx->settings = qa_app_sim_saved_simulation_settings(*save);
    tx->has_settings = true;
    tx->saved_bots = qa_app_sim_saved_bot_checkpoint(*save);
    if (tx->saved_bots != NULL) {
      tx->decoded_bots = checked(calloc(1, sizeof *tx->decoded_bots));
      qa_app_sim_decode_application_bots_checkpoint(tx->saved_bots->value, tx->decoded_bots);
    }
  }
  /* const { authoredCampaignStart, ...travelOptions } = restoredOptions ?? this.options; */
  qa_app_options *options = kept_options(app, request->restored_options != NULL ? request->restored_options : &app->launch_options);
  tx->options = options;
  options->authored_campaign_start = false;
  free(options->map);
  options->map = qa_app_map_resource_path(request->map);
  if (tx->skirmish != NULL) {
    options->team_arena_skirmish = tx->skirmish;
    free(options->character_model);
    options->character_model = dup_text(tx->skirmish->player_model);
  }
  if (tx->has_settings) {
    options->skill = tx->settings.skill;
    options->mode = tx->settings.mode;
    options->seed = tx->settings.seed;
  }
  if (save != NULL || tx->q3_map != NULL) options->team_arena_skirmish = NULL;
  if (tx->q3_map != NULL)
    options->mode = tx->q3_map->single_player ? "singleplayer" : tx->q3_map->game_type == 2 ? "singleplayer" : "deathmatch";
  qa_content_catalog *catalog = (qa_content_catalog *)qa_app_loaded_content_catalog(app->loaded_content);
  const qa_executable_recipe *recipe;
  if (save != NULL) recipe = &save->recipe;
  else if (tx->skirmish != NULL) {
    const qa_executable_recipe *current = qa_app_loaded_content_recipe(app->loaded_content);
    qa_app_native_provider_sources sources = { current->movement, current->character.definition };
    tx->preset = checked(calloc(1, sizeof *tx->preset));
    if (!qa_app_application_preset_for_catalog(catalog, options, &sources, NULL, tx->preset)) qa_range_error("Out of memory");
    tx->launch = checked(calloc(1, sizeof *tx->launch));
    qa_open_mount_options mount_options;
    memset(&mount_options, 0, sizeof mount_options);
    qa_resolve_launch_options launch;
    memset(&launch, 0, sizeof launch);
    launch.choice = qa_preset_choice(tx->preset->preset.id);
    launch.preset = &tx->preset->preset;
    launch.catalog = catalog;
    launch.mounts_options = &mount_options;
    launch.services = qa_app_native_launch_services();
    if (!qa_resolve_launch(&launch, tx->launch)) qa_range_error("Out of memory");
    recipe = &tx->launch->recipe;
  } else {
    tx->recipe = qa_app_resolve_application_travel(app->loaded_content, options->map);
    recipe = &tx->recipe;
  }
  if (save != NULL && qvm_server_game(recipe)) options->has_bot_skill = false;
  /* The travel recipe already came from this catalog, so discovery only repeats; Q3 restricts its catalog per
   * product, so it still discovers. */
  qa_q3_application_product current_product;
  bool same_catalog = !qa_app_loaded_content_q3_product(app->loaded_content, &current_product) && !options->has_remote_content;
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, options_dup(options), recipe, true, NULL,
      same_catalog ? catalog : NULL, NULL);
  if (content == NULL) qa_raise("Error", 0, 0, "Destination content preparation has no concrete loader");
  tx->content = content;
  qa_q3_application_product product;
  if (qa_app_loaded_content_q3_product(content, &product)) { options->has_q3_product = true; options->q3_product = product; }
  qa_application_previous_world *before = &tx->before;
  before->content = app->loaded_content;
  before->graphical = app->graphical;
  before->simulation = app->world_simulation;
  before->local_guest = app->local_guest;
  before->options = app->launch_options;
  before->client_cvar_count = app->client_cvar_count;
  before->client_cvar_seats = checked(calloc(app->client_cvar_count + 1, sizeof *before->client_cvar_seats));
  before->client_cvars = checked(calloc(app->client_cvar_count + 1, sizeof *before->client_cvars));
  if (app->client_cvar_count != 0) {
    memcpy(before->client_cvar_seats, app->client_cvar_seats, app->client_cvar_count * sizeof *before->client_cvar_seats);
    memcpy(before->client_cvars, app->client_cvars, app->client_cvar_count * sizeof *before->client_cvars);
  }
  before->overrides = app->team_arena_overrides;
  {
    qa_app_sim_classic_guest_world *classic = NULL;
    qa_app_sim_rerelease_guest_world *rerelease = NULL;
    size_t count = 0;
    const qa_app_sim_classic_guest_client *clients = NULL;
    if (qa_app_shared_sim_q2_native(app->world_simulation, &classic, &rerelease))
      clients = classic != NULL ? qa_app_sim_classic_guest_world_clients(classic, &count) : qa_app_sim_rerelease_guest_world_clients(rerelease, &count);
    before->native_clients = checked(calloc(count + 1, sizeof *before->native_clients));
    if (count != 0) memcpy(before->native_clients, clients, count * sizeof *before->native_clients);
    before->native_client_count = count;
  }
  {
    size_t count = 0;
    const qa_app_sim_bot_client *clients = app->bots == NULL ? NULL : app->bots->clients(app->bots->ctx, &count);
    before->bot_clients = checked(calloc(count + 1, sizeof *before->bot_clients));
    if (count != 0) memcpy(before->bot_clients, clients, count * sizeof *before->bot_clients);
    before->bot_client_count = count;
  }
  before->bots = app->bots;
  tx->has_before = true;
  tx->next_overrides = before->overrides;
  const qa_executable_recipe *next = qa_app_loaded_content_recipe(content), *previous = qa_app_loaded_content_recipe(before->content);
  tx->same_client_owner = same_reference(next->engine_behavior, previous->engine_behavior);
  tx->same_source_owner = same_reference(next->map.entities, previous->map.entities);
  tx->frontend_overrides = qa_application_frontend_settings(app);
  qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(app->world_simulation);
  tx->q3 = q3;
  tx->preserve_bots = tx->q3_map == NULL
      ? tx->skirmish == NULL && (tx->initial_source_milliseconds != 0 || q3 == NULL || qa_app_q3_source_runtime_game_type(q3) != 2)
      : !tx->q3_map->kill_bots;
  tx->bot_client_count = tx->preserve_bots ? before->bot_client_count : 0;
  tx->bot_clients = checked(calloc(tx->bot_client_count + 1, sizeof *tx->bot_clients));
  if (tx->bot_client_count != 0) memcpy(tx->bot_clients, before->bot_clients, tx->bot_client_count * sizeof *tx->bot_clients);
  qa_cvar_registry *current_source = qa_application_source_cvars(app, NULL);
  if (tx->q3_map == NULL && save == NULL && tx->skirmish == NULL && tx->same_source_owner && current_source != NULL) {
    rules_try rules = { *options, current_source, app->world_simulation->options.max_clients, { { 0 }, 0 } };
    qa_caught_error caught;
    if (!qa_try(rules_body, &rules, &caught)) {
      /* The candidate content is open but the cleanup block has not been entered yet. */
      qa_caught_error close_error;
      if (!qa_try(content_close_body, content, &close_error)) qa_raise("AggregateError", 0, 2, "World preparation failed");
      qa_rethrow(&caught);
    }
    tx->has_next_rules = true;
    tx->next_rules = rules.rules;
    *options = rules.rules.options;
  }
  if (q3 != NULL) { tx->q3_session = qa_app_q3_source_runtime_capture_session(q3); tx->has_q3_session = true; }
  qa_app_shared_sim_server_profile(app->world_simulation, &tx->server_profile);
  tx->has_server_profile = true;
  const qa_ss_source_runtime *q1 = qa_app_shared_sim_q1_source(app->world_simulation);
  qa_app_application_world world = qa_app_loaded_content_world(content);
  if (save == NULL && strcmp(world.kind, "q1-bsp") == 0 && qa_app_loaded_content_prepared_quakec(content) == NULL && q1 != NULL)
    tx->q1_cvars_source = q1->q1_cvars;
  qa_cvar_registry *source = qa_application_source_cvars(app, NULL);
  if (source != NULL) tx->source_archive = qa_cvar_registry_archive_entries(source, NULL, NULL, &tx->source_archive_count);
  qa_cvar_registry *q2 = qa_app_shared_sim_q2_server_cvars(app->world_simulation);
  if (q2 != NULL) {
    tx->q2_snapshots = qa_cvar_registry_snapshots(q2, 0, &tx->q2_snapshot_count);
    tx->q2_cvars = latched_values(tx->q2_snapshots, tx->q2_snapshot_count, false, &tx->q2_cvar_count);
    tx->has_q2_cvars = true;
  }
  if (q3 != NULL) {
    tx->q3_snapshots = qa_cvar_registry_snapshots(qa_app_q3_source_runtime_host(q3)->cvars, 0, &tx->q3_snapshot_count);
    tx->q3_cvars = latched_values(tx->q3_snapshots, tx->q3_snapshot_count, true, &tx->q3_cvar_count);
    tx->has_q3_cvars = true;
  }
}

/* ================= initializeWorldTransition and the candidate guest output ================= */
typedef struct guest_network_command { double slot; char *text; } guest_network_command;
static void guest_configstring(void *ctx, double index, const char *value) {
  qa_application_world_transition *tx = ctx;
  if (!tx->guest_network_published) return;
  qa_app_q3_guest_output output = qa_application_guest_output(tx->app, NULL, true);
  output.configstring(output.ctx, index, value);
}
static void guest_send_server_command(void *ctx, double slot, const char *text) {
  qa_application_world_transition *tx = ctx;
  if (tx->guest_network_published) {
    qa_app_q3_guest_output output = qa_application_guest_output(tx->app, NULL, true);
    output.send_server_command(output.ctx, slot, text);
    return;
  }
  tx->guest_network_command_bytes += (double)strlen(text);
  if (qa_event_ring_live(&tx->guest_network_commands) >= 1024 || tx->guest_network_command_bytes > 1024 * 1024)
    qa_raise("Error", 0, 0, "Q3 map initialization overflowed pending reliable commands");
  guest_network_command command = { slot, dup_text(text) };
  (void)qa_event_ring_admit(&tx->guest_network_commands, &command);
}
static void guest_drop_client(void *ctx, double slot, const char *reason) {
  qa_application_world_transition *tx = ctx;
  if (!tx->guest_network_published) {
    const qa_app_q3_guest_map_client *carried = NULL;
    qa_application_world_replacement *staged = tx->staged;
    for (size_t i = 0; staged->has_guest_transition && i < staged->guest_transition.client_count && carried == NULL; i++)
      if ((double)staged->guest_transition.clients[i].client.slot == slot) carried = &staged->guest_transition.clients[i];
    if (tx->app->network == NULL || tx->app->network->kind != QA_APPLICATION_NETWORK_Q3 || carried == NULL)
      qa_raise("Error", 0, 0, "Q3 map initialization dropped client %s: %s", QA_JS_NUMBER(slot), reason);
    grow((void **)&tx->guest_rejected_clients, &tx->guest_rejected_client_capacity, tx->guest_rejected_client_count + 1,
        sizeof *tx->guest_rejected_clients);
    size_t i = 0;
    while (i < tx->guest_rejected_client_count && !qa_same_client(tx->guest_rejected_clients[i].client, carried->client)) i++;
    char *kept = dup_text(reason);
    keep_text(tx->app, kept);
    tx->guest_rejected_clients[i].client = carried->client;
    tx->guest_rejected_clients[i].reason = kept;
    if (i == tx->guest_rejected_client_count) tx->guest_rejected_client_count++;
    return;
  }
  qa_app_q3_guest_output output = qa_application_guest_output(tx->app, NULL, true);
  output.drop_client(output.ctx, slot, reason);
}
static void initialize_world_transition(qa_application *app, qa_application_world_transition *tx) {
  qa_app_shared_simulation *previous_simulation = tx_before(tx)->simulation;
  qa_source_debug_graph *candidate_graph = checked(malloc(sizeof *candidate_graph));
  qa_source_debug_graph_init(candidate_graph);
  keep_record(app, candidate_graph, NULL);
  qa_application_world_replacement *staged = world_replacement_create(tx->content, app->guest_browser);
  tx->retired_scripts = NULL;
  tx->has_post_publication_error = false;
  tx->guest_network_published = false;
  if (!qa_event_ring_init(&tx->guest_network_commands, 1024, sizeof(guest_network_command))) qa_range_error("Out of memory");
  tx->has_guest_network_commands = true;
  tx->guest_network_command_bytes = 0;
  qa_app_q3_guest_output output = { tx, guest_drop_client, guest_send_server_command, guest_configstring };
  /* A loop, not a closure: closures in this scope are stored in the new world and must not capture the previous simulation. */
  qa_app_q3_qvm_server_game *old_guest = qa_app_shared_sim_q3_guest(previous_simulation);
  if (old_guest != NULL) {
    size_t count = qa_app_q3_qvm_server_game_players(old_guest, NULL, 0);
    qa_app_q3_player *players = checked(calloc(count + 1, sizeof *players));
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, players);
    (void)qa_app_q3_qvm_server_game_players(old_guest, players, count);
    tx->carried_slots = checked(calloc(count + 1, sizeof *tx->carried_slots));
    tx->carried_commands = checked(calloc(count + 1, sizeof *tx->carried_commands));
    tx->carried_present = checked(calloc(count + 1, sizeof *tx->carried_present));
    for (size_t i = 0; i < count; i++) {
      size_t at = 0;
      while (at < tx->carried_count && tx->carried_slots[at] != (double)players[i].client.slot) at++;
      tx->carried_slots[at] = (double)players[i].client.slot;
      tx->carried_present[at] = qa_app_q3_server_state_get_user_command(qa_app_q3_qvm_server_game_state(old_guest),
          players[i].source_entity, &tx->carried_commands[at]);
      if (at == tx->carried_count) tx->carried_count++;
    }
    qa_cleanup_pop(&cleanup);
    free(players);
  }
  tx->has_candidate_images = false;
  tx->staged = staged; tx->has_staged = true;
  tx->candidate_graph = candidate_graph; tx->has_candidate_graph = true;
  tx->has_guest_rejected_clients = true;
  tx->guest_candidate_output = output; tx->has_guest_candidate_output = true;
  tx->has_carried_guest_commands = true;
  /* Never NULL: overrideSeats reads a NULL row array as this.clientCvars. */
  tx->next_client_cvar_seats = checked(calloc(1, sizeof *tx->next_client_cvar_seats));
  tx->next_client_cvars = checked(calloc(1, sizeof *tx->next_client_cvars));
  tx->next_client_cvar_capacity = 1;
  tx->has_next_client_cvars = true;
}

/* nextClientCvars.set(seat, cvars). */
static void next_client_cvars_set(qa_application_world_transition *tx, qa_seat_id seat, qa_cvar_registry *cvars) {
  for (size_t i = 0; i < tx->next_client_cvar_count; i++)
    if (qa_same_seat(tx->next_client_cvar_seats[i], seat)) { tx->next_client_cvars[i] = cvars; return; }
  grow((void **)&tx->next_client_cvar_seats, &tx->next_client_cvar_capacity, tx->next_client_cvar_count + 1, sizeof *tx->next_client_cvar_seats);
  tx->next_client_cvars = checked(realloc(tx->next_client_cvars, tx->next_client_cvar_capacity * sizeof *tx->next_client_cvars));
  tx->next_client_cvar_seats[tx->next_client_cvar_count] = seat;
  tx->next_client_cvars[tx->next_client_cvar_count++] = cvars;
}
static qa_cvar_registry *next_client_cvars_get(qa_application_world_transition *tx, qa_seat_id seat) {
  for (size_t i = 0; i < tx->next_client_cvar_count; i++) if (qa_same_seat(tx->next_client_cvar_seats[i], seat)) return tx->next_client_cvars[i];
  return NULL;
}
static qa_cvar_registry *previous_client_cvars(qa_application_previous_world *before, qa_seat_id seat) {
  for (size_t i = 0; i < before->client_cvar_count; i++) if (qa_same_seat(before->client_cvar_seats[i], seat)) return before->client_cvars[i];
  return NULL;
}
/* admissions.set(slot, actor). */
static void admissions_set(qa_application_world_transition *tx, double slot, qa_actor_id actor) {
  for (size_t i = 0; i < tx->admission_count; i++) if (tx->admission_slots[i] == slot) { tx->admissions[i] = actor; return; }
  grow((void **)&tx->admission_slots, &tx->admission_capacity, tx->admission_count + 1, sizeof *tx->admission_slots);
  tx->admissions = checked(realloc(tx->admissions, tx->admission_capacity * sizeof *tx->admissions));
  tx->admission_slots[tx->admission_count] = slot;
  tx->admissions[tx->admission_count++] = actor;
}

/* ================= stageCandidateInputs ================= */
static void host_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
static qa_app_preflight_content preflight_content(qa_app_loaded_content *content) {
  qa_app_preflight_content out;
  memset(&out, 0, sizeof out);
  out.catalog = qa_app_loaded_content_catalog(content);
  out.recipe = qa_app_loaded_content_recipe(content);
  qa_app_application_world world = qa_app_loaded_content_world(content);
  if (strcmp(world.kind, "q1-bsp") == 0) {
    out.world_kind = QA_Q3_TA_WORLD_Q1_BSP;
    out.world_entities = ((const qa_q1_map *)world.value)->world.entities;
  } else if (strcmp(world.kind, "q2-bsp") == 0) {
    out.world_kind = QA_Q3_TA_WORLD_Q2_BSP;
    out.world_entities = ((const qa_q2_decoded_map *)world.value)->world.entities;
  } else {
    out.world_kind = QA_Q3_TA_WORLD_Q3_BSP;
    out.world_entities = ((const qa_q3_world_geometry *)world.value)->entities;
  }
  if (out.world_entities == NULL) out.world_entities = "";
  return out;
}
/* [...(tx.q3Cvars ?? []), ...(skirmish === undefined ? [] : teamArenaSourceCvars(skirmish, tx.q3Cvars ?? [])), ...(q3Map?.cvars ?? [])] */
static qa_app_sim_cvar_name_value *destination_q3_cvars(qa_application *app, qa_application_world_transition *tx, size_t *out_count) {
  size_t team_count = 0;
  qa_app_name_value *team = NULL;
  if (tx->skirmish != NULL) {
    qa_app_name_value *baseline = checked(calloc(tx->q3_cvar_count + 1, sizeof *baseline));
    for (size_t i = 0; i < tx->q3_cvar_count; i++) { baseline[i].name = (char *)tx->q3_cvars[i].name; baseline[i].value = (char *)tx->q3_cvars[i].value; }
    team = qa_app_team_arena_source_cvars(tx->skirmish, baseline, tx->q3_cvar_count, &team_count);
    free(baseline);
    /* The values are kept for the destination world (TS GC). */
    for (size_t i = 0; i < team_count; i++) { keep_text(app, team[i].name); keep_text(app, team[i].value); }
    keep_text(app, (char *)team);
  }
  size_t map_count = tx->q3_map == NULL ? 0 : tx->q3_map->cvar_count;
  size_t count = tx->q3_cvar_count + team_count + map_count;
  qa_app_sim_cvar_name_value *values = checked(calloc(count + 1, sizeof *values));
  keep_text(app, (char *)values);
  size_t at = 0;
  for (size_t i = 0; i < tx->q3_cvar_count; i++) values[at++] = tx->q3_cvars[i];
  for (size_t i = 0; i < team_count; i++) { values[at].name = team[i].name; values[at++].value = team[i].value; }
  for (size_t i = 0; i < map_count; i++) { values[at].name = tx->q3_map->cvars[i].name; values[at++].value = tx->q3_map->cvars[i].value; }
  *out_count = count;
  return values;
}
static void stage_candidate_inputs(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_application_previous_world *before = tx_before(tx);
  if (tx->save == NULL) {
    size_t count = 0;
    qa_app_sim_cvar_name_value *values = destination_q3_cvars(app, tx, &count);
    qa_app_preflight_application_match(preflight_content(tx->content), tx->options->mode, (const qa_app_source_value *)values, count);
  }
  if (before->graphical != NULL && app->image_settings != NULL) {
    tx->candidate_images = qa_app_image_settings_prepare_client_settings(app->image_settings);
    tx->has_candidate_images = true;
  }
  qa_cvar_registry *q1_source_registry = tx->q1_cvars_source == NULL ? NULL
      : qa_app_clone_q1_source_cvars(tx->q1_cvars_source, host_print, app);
  if (tx->q1_cvars_source != NULL && q1_source_registry == NULL) qa_range_error("Out of memory");
  if (tx->save != NULL) {
    qa_app_override_seat *seats = checked(calloc(app->local_seat_count + 1, sizeof *seats));
    for (size_t i = 0; i < app->local_seat_count; i++) {
      seats[i].seat = (double)qa_session_seat_id(app->local_seats[i]).index;
      seats[i].client = (double)qa_session_client_id(qa_session_seat_client(app->local_seats[i])).slot;
    }
    qa_app_team_arena_overrides *read = checked(calloc(1, sizeof *read));
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, seats);
    bool present = qa_app_read_team_arena_overrides(tx->save, seats, app->local_seat_count, read);
    qa_cleanup_pop(&cleanup);
    free(seats);
    if (present) { keep_record(app, read, (void (*)(void *))qa_app_team_arena_overrides_free); tx->next_overrides = read; }
    else { free(read); tx->next_overrides = NULL; }
  }
  if (tx->has_settings) {
    qa_app_options next = qa_app_application_options_for_recipe(tx->options, qa_app_loaded_content_catalog(tx->content),
        qa_app_loaded_content_recipe(tx->content));
    free(tx->options->product); free(tx->options->map); free(tx->options->map_product);
    free(tx->options->movement_product); free(tx->options->character_model);
    *tx->options = next;
    tx->initial_source_milliseconds = tx->settings.host_milliseconds;
  }
  if (tx->has_settings) {
    size_t connection_count = tx->decoded_bots == NULL ? 0 : tx->decoded_bots->transport.connection_count;
    staged->saved_clients = qa_application_prepare_saved_clients(app, tx->settings.client_slots, tx->settings.client_slot_count,
        connection_count == 0 ? NULL : tx->decoded_bots->transport.connections, connection_count);
  }
  if (staged->saved_clients != NULL) {
    tx->client_count = staged->saved_clients->client_count;
    tx->clients = checked(calloc(tx->client_count + 1, sizeof *tx->clients));
    if (tx->client_count != 0) memcpy(tx->clients, staged->saved_clients->clients, tx->client_count * sizeof *tx->clients);
  } else {
    size_t count = 0;
    const qa_client_id *identities = qa_app_shared_sim_client_identities(app->world_simulation, &count);
    tx->clients = checked(calloc(count + 1, sizeof *tx->clients));
    for (size_t i = 0; i < count; i++) {
      bool bot = false;
      for (size_t j = 0; j < before->bot_client_count && !bot; j++)
        bot = qa_same_client(qa_session_client_id(before->bot_clients[j].client), identities[i]);
      if (tx->preserve_bots || !bot) tx->clients[tx->client_count++] = identities[i];
    }
  }
  tx->q1_source_registry = q1_source_registry; tx->has_q1_source_registry = true;
  tx->has_clients = true;
}

/* ================= createCandidateSinks / stageCandidateCommands ================= */
static qa_command_buffer *mod_commands_buffer(void *ctx) {
  qa_application_world_transition *tx = ctx;
  return tx->candidate_published ? tx->app->source_commands : tx->candidate_commands;
}
/* candidateAction(request): takes over the request's storage. */
static void candidate_action(void *ctx, const qa_application_command_request *request) {
  qa_application_world_transition *tx = ctx;
  if (tx->candidate_published) { (void)qa_event_ring_admit(&tx->app->requested_commands, request); return; }
  grow((void **)&tx->candidate_actions, &tx->candidate_action_capacity, tx->candidate_action_count + 1, sizeof *tx->candidate_actions);
  tx->candidate_actions[tx->candidate_action_count++] = *request;
}
/* nativeCommand(text): modCommands.append(text, nativeContext(), Application.contentDialect(content)). */
static void native_command(void *ctx, const char *text) {
  qa_application_world_transition *tx = ctx;
  qa_command_context context = qa_application_native_command_context(tx->content, qa_engine_session_id(tx->app->session));
  qa_mod_commands_append(tx->mod_commands, text, context, qa_application_content_dialect(tx->content));
}
/* guestCommands(): the published or candidate source buffer. */
static qa_command_buffer *guest_commands(void *ctx) {
  qa_application_world_transition *tx = ctx;
  qa_command_buffer *commands = tx->candidate_published ? tx->app->source_commands : tx->candidate_commands;
  if (commands == NULL) qa_raise("Error", 0, 0, "Q3 guest candidate console is unavailable");
  return commands;
}
static void mod_commands_release(void *record) {
  qa_mod_commands **commands = record;
  qa_mod_commands_destroy(*commands);
}
static void create_candidate_sinks(qa_application *app, qa_application_world_transition *tx) {
  tx->candidate_published = false;
  tx->candidate_commands = NULL;
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = qa_engine_session_id(app->session);
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_mod_commands *commands = qa_mod_commands_create(context, mod_commands_buffer, tx);
  if (commands == NULL) qa_range_error("Out of memory");
  qa_mod_commands **record = checked(malloc(sizeof *record));
  *record = commands;
  keep_record(app, record, mod_commands_release);
  tx->mod_commands = commands; tx->has_mod_commands = true;
  tx->has_candidate_actions = true;
  tx->has_candidate_action = true;
  tx->has_native_command = true;
  tx->has_guest_commands = true;
}
static qa_content_mounted *content_for(void *ctx, qa_content_id id) {
  return (qa_content_mounted *)qa_app_loaded_content_for_content(ctx, id);
}
static qa_decoded_world decoded_world(qa_app_loaded_content *content) {
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_decoded_world out;
  memset(&out, 0, sizeof out);
  if (strcmp(world.kind, "q1-bsp") == 0) { out.kind = QA_DECODED_WORLD_Q1; out.as.q1 = &((const qa_q1_map *)world.value)->world; }
  else if (strcmp(world.kind, "q2-bsp") == 0) { out.kind = QA_DECODED_WORLD_Q2; out.as.q2 = &((const qa_q2_decoded_map *)world.value)->world; }
  else { out.kind = QA_DECODED_WORLD_Q3; out.as.q3 = world.value; }
  return out;
}
static void stage_candidate_commands(qa_application *app, qa_application_world_transition *tx) {
  qa_app_shared_simulation *previous_simulation = tx_before(tx)->simulation;
  create_candidate_sinks(app, tx);
  qa_app_sim_monster_navigation_content navigation;
  memset(&navigation, 0, sizeof navigation);
  navigation.ctx = tx->content;
  navigation.recipe = qa_app_loaded_content_recipe(tx->content);
  navigation.world = decoded_world(tx->content);
  navigation.mounts = (qa_content_mounted *)qa_app_loaded_content_mounts(tx->content);
  navigation.for_content = content_for;
  qa_app_sim_application_monster_navigation *monster_navigation = qa_app_sim_preload_application_monster_navigation(navigation);
  if (monster_navigation != NULL) keep_record(app, monster_navigation, NULL);
  if (tx->save == NULL)
    tx->has_mod_travel = qa_app_shared_sim_checkpoint_mods_for_travel(previous_simulation, &tx->mod_travel, &tx->mod_travel_count);
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(tx->content);
  bool continue_q3_clock = tx->save == NULL && tx->q3 != NULL && strncmp(recipe->map.entities.provider, "q3:", 3) == 0
      && !qvm_server_game(recipe);
  qa_app_q3_source_host *q3_host = tx->q3 == NULL ? NULL : qa_app_q3_source_runtime_host(tx->q3);
  double destination = continue_q3_clock && q3_host != NULL ? q3_host->now(q3_host->ctx) : tx->initial_source_milliseconds;
  tx->monster_navigation = monster_navigation; tx->has_monster_navigation = true;
  tx->continue_q3_clock = continue_q3_clock; tx->has_continue_q3_clock = true;
  tx->destination_source_milliseconds = destination; tx->has_destination_source_milliseconds = true;
}

/* ================= stageNativeMapTravel / stageGuestMapShutdown ================= */
static void owned_policy_release(void *record) { qa_scene_image_policy_owned_destroy(record); }
/* TS createWorldAssets(content, { owner, images }, print, this.imageSettings). */
qa_app_application_assets *qa_application_create_world_assets(qa_application *app, qa_app_loaded_content *content,
    qa_renderer_resource_owner owner, qa_scene_image_registry *root_images) {
  qa_app_world_asset_images images;
  const qa_app_world_asset_images *selected = NULL;
  if (app->image_settings != NULL) {
    qa_scene_image_policy_owned *policy = checked(calloc(1, sizeof *policy));
    keep_record(app, policy, owned_policy_release);
    qa_app_image_settings_policy(app->image_settings, policy);
    images.policy = policy->policy;
    images.model_policy = qa_app_image_settings_model_policy(app->image_settings);
    images.cvars = qa_app_image_settings_cvars(app->image_settings);
    selected = &images;
  }
  qa_app_application_assets *assets = qa_app_create_world_assets(content, owner, root_images, host_print, app, selected);
  if (assets == NULL) qa_range_error("Out of memory");
  return assets;
}
static qa_app_application_assets *create_world_assets(qa_application *app, qa_app_loaded_content *content, qa_app_native_renderer *renderer) {
  return qa_application_create_world_assets(app, content, qa_app_native_renderer_owner(renderer), qa_app_native_renderer_images(renderer));
}
static void stage_native_map_travel(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_application_previous_world *before = tx_before(tx);
  if (tx->native_travel == NULL) return;
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  bool old_native = qa_app_shared_sim_q2_native(before->simulation, &classic, &rerelease);
  const char *edition = NULL;
  const void *prepared = qa_app_loaded_content_prepared_q2_game(tx->content, &edition);
  bool mismatch = tx->save != NULL || tx->carry != NULL || strcmp(tx->native_travel->kind, "map") != 0 || !old_native || prepared == NULL;
  if (!mismatch) {
    qa_module_identity module = classic != NULL ? qa_app_sim_classic_guest_world_module(classic) : qa_app_sim_rerelease_guest_world_module(rerelease);
    const qa_resolved_execution_module *execution = strcmp(edition, "classic") == 0
        ? &((const qa_app_sim_prepared_classic_guest *)prepared)->execution : &((const qa_app_sim_prepared_rerelease_guest *)prepared)->execution;
    const qa_resolved_resource_reference *artifact = execution->artifact;
    double capacity = tx->has_next_rules ? tx->next_rules.max_clients : before->simulation->options.max_clients;
    mismatch = strcmp(module.id, execution->owner.provider) != 0 || strcmp(module.digest, artifact->digest) != 0
        || capacity != before->simulation->options.max_clients;
  }
  if (mismatch) qa_raise("Error", 0, 0, "Native gamemap requires the retained module and client capacity");
  if (before->graphical != NULL) {
    staged->assets = create_world_assets(app, tx->content, before->graphical->renderer);
    (void)qa_app_application_assets_load_world(staged->assets);
    (void)qa_app_application_assets_load_console_font(staged->assets);
    (void)qa_app_application_assets_load_menu_typography(staged->assets);
  }
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
  if (app->closed || app->world_simulation != before->simulation) qa_raise("Error", 0, 0, "Native source changed during map preparation");
  staged->native_committed = true;
  for (size_t i = 0; before->graphical != NULL && i < before->graphical->presentation_count; i++)
    qa_app_seat_ui_close_menus(before->graphical->presentations[i].ui);
  if (app->tools != NULL) qa_app_application_tools_before_world_change(app->tools);
}
static void stage_guest_map_shutdown(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_application_previous_world *before = tx_before(tx);
  qa_app_q3_qvm_server_game *old_guest = qa_app_shared_sim_q3_guest(before->simulation);
  if (tx->save != NULL || old_guest == NULL) return;
  if (qa_app_loaded_content_prepared_q3_game(tx->content) == NULL) qa_raise("Error", 0, 0, "QVM map travel requires a destination qagame");
  if (before->graphical != NULL && staged->assets == NULL) {
    staged->assets = create_world_assets(app, tx->content, before->graphical->renderer);
    (void)qa_app_application_assets_load_world(staged->assets);
    (void)qa_app_application_assets_load_console_font(staged->assets);
    (void)qa_app_application_assets_load_menu_typography(staged->assets);
  }
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
  if (app->closed || app->world_simulation != before->simulation) qa_raise("Error", 0, 0, "Guest source changed during map preparation");
  staged->native_committed = true;
  for (size_t i = 0; before->graphical != NULL && i < before->graphical->q3_count; i++)
    if (before->graphical->q3[i].qvm) qa_app_application_q3_client_close(before->graphical->q3[i].client);
  staged->guest_transition = qa_app_q3_qvm_server_game_shutdown_for_map_change(old_guest, tx->guest_restart);
  staged->has_guest_transition = true;
  /* tx.q3Cvars = guestTransition.cvars.variables (latched ?? value), without mapname and sv_mapname. */
  size_t count = 0;
  guest_variable *rows = guest_variables(app, staged->guest_transition.cvars, &count);
  tx->q3_cvars = checked(calloc(count + 1, sizeof *tx->q3_cvars));
  tx->q3_cvar_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (rows[i].name == NULL || strcmp(rows[i].name, "mapname") == 0 || strcmp(rows[i].name, "sv_mapname") == 0) continue;
    tx->q3_cvars[tx->q3_cvar_count].name = rows[i].name;
    tx->q3_cvars[tx->q3_cvar_count++].value = rows[i].latched_value != NULL ? rows[i].latched_value : rows[i].value;
  }
  tx->has_q3_cvars = true;
}

/* ================= simulationRequest / stageSimulation ================= */
/* The closures simulationRequest stores in the new simulation, over the transaction (TS AUDIT-001: they must not
 * capture the previous world). */
typedef struct simulation_request_record {
  qa_application *app;
  qa_application_world_transition *tx;
  qa_app_application_world world;
  qa_app_sim_navigation_content navigation_content;
  qa_app_sim_navigation_simulation navigation_simulation;
  qa_app_sim_mod_selections enabled_mods;
  qa_mod_travel_checkpoint mod_travel;
  const void **weapon_behaviors;
  qa_mod_user_files *mod_files;
} simulation_request_record;
static void simulation_request_release(void *record) {
  simulation_request_record *request = record;
  free((void *)request->enabled_mods.items);
  free(request->weapon_behaviors);
  if (request->mod_files != NULL) qa_mod_user_files_destroy(request->mod_files);
}
/* prepareRereleaseNavigation: simulation => createApplicationBotNavigation({ content, simulation }). */
static const void *prepare_rerelease_navigation(void *ctx, qa_app_sim_shared_simulation *opaque) {
  simulation_request_record *request = ctx;
  qa_app_shared_simulation *simulation = (qa_app_shared_simulation *)(void *)opaque;
  request->navigation_content = qa_application_navigation_content(request->tx->content);
  request->navigation_simulation = qa_app_shared_sim_navigation_simulation(simulation, qa_app_loaded_content_recipe(request->tx->content));
  qa_app_sim_application_bot_navigation *navigation =
      qa_app_sim_create_application_bot_navigation(request->navigation_content, request->navigation_simulation);
  if (navigation == NULL) qa_range_error("Out of memory");
  return navigation;
}
/* promptSupported: client => !tx.options.dedicated && this.localSeats.has(client). */
static bool prompt_supported(void *ctx, qa_client_id client) {
  simulation_request_record *request = ctx;
  return !request->tx->options->dedicated && qa_application_local_seat(request->app, client) != NULL;
}
/* playerIdentity: client => this.networkPlayerIdentities.get(client) ?? { seat: this.localSeats.get(client)?.id.index ?? 0, socialId: "" }. */
static bool player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  simulation_request_record *request = ctx;
  qa_application *app = request->app;
  for (size_t i = 0; i < app->network_player_identity_count; i++)
    if (qa_same_client(app->network_player_clients[i], client)) {
      *out_seat = app->network_player_identities[i].seat;
      *out_social_id = app->network_player_identities[i].social_id;
      return true;
    }
  qa_session_seat *seat = qa_application_local_seat(app, client);
  *out_seat = seat == NULL ? 0 : (double)qa_session_seat_id(seat).index;
  *out_social_id = "";
  return true;
}
static double clamp_color(bool has, double value) {
  double color = has ? trunc(value) : 0;
  return qa_js_min(13, qa_js_max(0, color));
}
static qa_app_simulation_options simulation_request(qa_application *app, qa_application_world_transition *tx,
    const qa_app_sim_native_q2_travel *retained_native, double max_clients, const qa_app_simulation_options *guest) {
  bool continue_q3_clock = SLOT(tx, continue_q3_clock);
  double destination = SLOT(tx, destination_source_milliseconds);
  qa_cvar_registry *q1_source_registry = SLOT(tx, q1_source_registry);
  slot_read(tx->has_mod_commands, "modCommands");
  slot_read(tx->has_monster_navigation, "monsterNavigation");
  slot_read(tx->has_clients, "clients");
  simulation_request_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, simulation_request_release);
  record->app = app;
  record->tx = tx;
  record->world = qa_app_loaded_content_world(tx->content);
  qa_app_simulation_options options = *guest;
  if (retained_native != NULL) options.native_q2_travel = retained_native;
  options.dedicated = tx->options->dedicated;
  options.nav_ctx = record;
  options.prepare_rerelease_navigation = prepare_rerelease_navigation;
  options.prepared_quakec = qa_app_loaded_content_prepared_quakec(tx->content);
  options.monster_navigation = tx->monster_navigation;
  options.identity = *app->identity;
  size_t behavior_count = 0;
  const qa_app_prepared_weapon_behavior *behaviors = qa_app_loaded_content_prepared_weapon_behaviors(tx->content, &behavior_count);
  record->weapon_behaviors = checked(calloc(behavior_count + 1, sizeof *record->weapon_behaviors));
  for (size_t i = 0; i < behavior_count; i++) record->weapon_behaviors[i] = &behaviors[i];
  options.weapon_behaviors = record->weapon_behaviors;
  options.weapon_behavior_count = behavior_count;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(tx->content);
  options.recipe = *recipe;
  options.world = &record->world;
  options.mounts = qa_app_loaded_content_mounts(tx->content);
  /* content.preparedMods (owned by the content). */
  options.prepared_mods = qa_app_loaded_content_prepared_mods(tx->content, &options.prepared_mod_count);
  qa_mod_selection *selections = checked(calloc(recipe->mod_count + 1, sizeof *selections));
  for (size_t i = 0; i < recipe->mod_count; i++) selections[i] = recipe->mods[i]->selection;
  record->enabled_mods.items = selections;
  record->enabled_mods.count = recipe->mod_count;
  options.enabled_mods = &record->enabled_mods;
  options.mod_commands = tx->mod_commands;
  if (tx->has_mod_travel) {
    record->mod_travel.version = 1;
    record->mod_travel.mods = tx->mod_travel;
    record->mod_travel.mod_count = tx->mod_travel_count;
    options.mod_travel = &record->mod_travel;
  }
  const char *user_root = tx->options->user_content_root;
  char *default_root = user_root == NULL ? qa_default_user_content_root() : NULL;
  record->mod_files = qa_mod_user_files_create(user_root != NULL ? user_root : default_root);
  free(default_root);
  if (record->mod_files == NULL) qa_range_error("Out of memory");
  options.mod_files = record->mod_files;
  options.skill = tx->options->skill;
  options.mode = tx->options->mode;
  options.seed = tx->options->seed;
  options.max_clients = max_clients;
  options.has_player_colors = true;
  options.player_color_top = clamp_color(tx->options->has_character_top_color, tx->options->character_top_color);
  options.player_color_bottom = clamp_color(tx->options->has_character_bottom_color, tx->options->character_bottom_color);
  options.prompt_ctx = record;
  options.prompt_supported = prompt_supported;
  options.identity_ctx = record;
  options.player_identity = player_identity;
  if (tx->save == NULL) {
    options.q2_next_server = tx->q2_next_server;
    if (tx->skirmish == NULL && tx->q3_map == NULL && tx->has_server_profile) options.server_profile = &tx->server_profile;
    options.source_archive = tx->source_archive;
    options.source_archive_count = tx->source_archive_count;
    if (q1_source_registry != NULL) options.source_registry = q1_source_registry;
    if (tx->has_q2_cvars) { options.q2_cvars = tx->q2_cvars; options.q2_cvar_count = tx->q2_cvar_count; }
    if (tx->carry != NULL) { options.has_travel = true; options.travel = *tx->carry; }
    if (tx->has_q3_session && tx->skirmish == NULL) options.q3_session = &tx->q3_session.carry;
    if (tx->guest_restart || continue_q3_clock || (tx->has_q3_session && tx->skirmish == NULL)) {
      options.has_initial_source_milliseconds = true;
      options.initial_source_milliseconds = destination;
    }
    if (tx->has_q3_cvars) options.q3_cvars = destination_q3_cvars(app, tx, &options.q3_cvar_count);
  } else {
    options.restore = tx->save;
    options.restored_clients = tx->clients;
    options.restored_client_count = tx->client_count;
    if (tx->carry != NULL) { options.has_travel = true; options.travel = *tx->carry; }
  }
  return options;
}
/* The candidate's guestCommands / nativeCommand closures as guestOptions takes them. */
static qa_command_buffer *guest_commands_buffer(void *ctx) { return guest_commands(ctx); }
static void stage_simulation(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_source_debug_graph *candidate_graph = SLOT(tx, candidate_graph);
  slot_read(tx->has_clients, "clients");
  slot_read(tx->has_native_command, "nativeCommand");
  slot_read(tx->has_guest_commands, "guestCommands");
  qa_app_shared_simulation *previous_simulation = tx_before(tx)->simulation;
  const qa_app_sim_native_q2_travel *retained_native = tx->native_travel == NULL ? NULL
      : qa_app_shared_sim_capture_native_q2_travel(previous_simulation, tx->native_travel->new_unit, tx->native_travel->spawn_point,
          tx->next_frame, tx->next_frame_ctx);
  double max_clients;
  if (tx->has_settings) max_clients = tx->settings.max_clients;
  else if (tx->q3_map != NULL && tx->q3_map->has_max_clients) {
    if (qa_app_shared_sim_q3_guest(previous_simulation) == NULL) max_clients = tx->q3_map->max_clients;
    else {
      max_clients = tx->q3_map->max_clients;
      for (size_t i = 0; i < tx->client_count; i++) max_clients = qa_js_max(max_clients, (double)tx->clients[i].slot + 1);
    }
  } else if (tx->skirmish != NULL) max_clients = tx->skirmish->max_clients;
  else if (tx->has_next_rules) max_clients = tx->next_rules.max_clients;
  else max_clients = app->world_simulation->options.max_clients;
  qa_app_simulation_options guest;
  memset(&guest, 0, sizeof guest);
  qa_application_guest_options(tx->content, tx->options, &app->host, guest_commands_buffer, tx, candidate_graph, native_command, tx, &guest);
  qa_app_simulation_options request = simulation_request(app, tx, retained_native, max_clients, &guest);
  staged->simulation = qa_app_shared_simulation_load(request, tx->next_frame, tx->next_frame_ctx);
  if (staged->simulation == NULL) qa_range_error("Out of memory");
  qa_app_shared_simulation *next = staged->simulation;
  qa_app_q3_qvm_server_game *next_guest = qa_app_shared_sim_q3_guest(next);
  if (staged->has_guest_transition && next_guest != NULL) {
    qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(next_guest));
    qa_cvar_registry_restore_save_state(cvars, staged->guest_transition.cvars);
    size_t count = 0;
    guest_variable *rows = guest_variables(app, staged->guest_transition.cvars, &count);
    for (size_t i = 0; i < count; i++)
      if (rows[i].name != NULL && rows[i].latched_value != NULL) qa_application_cvar_set(cvars, rows[i].name, rows[i].latched_value, true);
    if (tx->q3_map != NULL) qa_app_apply_q3_map_launch(cvars, tx->q3_map, "spawn");
    char text[40];
    qa_net_js_number(next->options.max_clients, text, sizeof text);
    qa_application_cvar_set(cvars, "sv_maxclients", text, true);
    /* requestedPath.replace(/^maps\//, "").replace(/\.bsp$/, "") */
    const char *path = qa_app_loaded_content_recipe(tx->content)->map.geometry.requested_path;
    if (strncmp(path, "maps/", 5) == 0) path += 5;
    size_t length = strlen(path);
    if (length >= 4 && strcmp(path + length - 4, ".bsp") == 0) length -= 4;
    char *map_name = checked(strndup(path, length));
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, map_name);
    qa_application_cvar_set(cvars, "mapname", map_name, true);
    qa_application_cvar_set(cvars, "sv_mapname", map_name, true);
    qa_cleanup_pop(&cleanup);
    free(map_name);
  }
  tx->next_simulation = next; tx->has_next_simulation = true;
}

/* ================= stageClientState ================= */
static bool is_carried_bot(qa_application_world_transition *tx, qa_client_id client) {
  for (size_t i = 0; i < tx->bot_client_count; i++) if (qa_same_client(qa_session_client_id(tx->bot_clients[i].client), client)) return true;
  return false;
}
static char *userinfo_with_ip(qa_cvar_registry *cvars) {
  char *info = qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO, -1);
  if (info == NULL) qa_range_error("Out of memory");
  size_t n = strlen(info) + sizeof "\\ip\\localhost";
  char *text = checked(malloc(n));
  (void)snprintf(text, n, "%s\\ip\\localhost", info);
  free(info);
  return text;
}
/* The userinfo a restored seat is created with (TS q3Source engine / q2Native clients / sourcePlayerUserinfo). Malloc'd. */
char *qa_application_restored_userinfo(qa_app_shared_simulation *next, qa_session_seat *seat) {
  qa_client_id client = qa_session_client_id(qa_session_seat_client(seat));
  qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(next);
  if (q3 != NULL) {
    qa_app_q3_source_engine engine = qa_app_q3_source_runtime_host(q3)->engine;
    char *userinfo = engine.get_userinfo(engine.ctx, (double)client.slot);
    if (userinfo != NULL) return userinfo;
  }
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  if (qa_app_shared_sim_q2_native(next, &classic, &rerelease)) {
    size_t count = 0;
    const qa_app_sim_classic_guest_client *clients = classic != NULL ? qa_app_sim_classic_guest_world_clients(classic, &count)
        : qa_app_sim_rerelease_guest_world_clients(rerelease, &count);
    for (size_t i = 0; i < count; i++) if (clients[i].slot == (double)client.slot + 1) return dup_text(clients[i].userinfo);
  }
  size_t count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(next, &count);
  for (size_t i = 0; i < count; i++) {
    qa_client_id owner;
    if (!qa_app_shared_sim_player_client(next, players[i], &owner) || !qa_same_client(owner, client)) continue;
    const char *userinfo = qa_app_shared_sim_source_player_userinfo(next, players[i]);
    return userinfo == NULL ? NULL : dup_text(userinfo);
  }
  return NULL;
}
static void stage_client_state(qa_application *app, qa_application_world_transition *tx) {
  slot_read(tx->has_next_client_cvars, "nextClientCvars");
  slot_read(tx->has_clients, "clients");
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  qa_application_previous_world *before = tx_before(tx);
  if (tx->save != NULL && qa_app_shared_sim_q3_source(next) != NULL && tx->saved_bots == NULL)
    qa_raise("Error", 0, 0, "Q3 application restoration requires saved bot service state");
  if (before->graphical != NULL && qa_app_shared_sim_q3_guest(next) == NULL) {
    size_t locals = qa_app_application_input_local_count(before->graphical->input);
    for (size_t i = 0; i < locals; i++) {
      qa_session_seat *seat = qa_app_application_input_local(before->graphical->input, i)->player.seat;
      qa_seat_id id = qa_session_seat_id(seat);
      char *userinfo = tx->save == NULL ? NULL : qa_application_restored_userinfo(next, seat);
      qa_cleanup userinfo_cleanup;
      qa_cleanup_push(&userinfo_cleanup, free, userinfo);
      size_t archive_count = 0;
      qa_cvar_archive_entry *archive = tx->same_client_owner ? NULL
          : qa_application_load_client_archive(app, seat, tx->content, tx->options, &archive_count);
      qa_cvar_registry *cvars = qa_application_create_client_cvars(app, next, tx->content, tx->options, seat, userinfo,
          tx->same_client_owner ? previous_client_cvars(before, id) : NULL, archive, archive_count, tx->skirmish != NULL);
      qa_app_cvar_archive_entries_free(archive, archive_count);
      qa_cleanup_pop(&userinfo_cleanup);
      free(userinfo);
      next_client_cvars_set(tx, id, cvars);
      qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(next);
      if (tx->save == NULL && q3 != NULL) {
        char *value = userinfo_with_ip(cvars);
        qa_app_q3_source_engine engine = qa_app_q3_source_runtime_host(q3)->engine;
        engine.set_userinfo(engine.ctx, (double)qa_session_client_id(qa_session_seat_client(seat)).slot, value);
        free(value);
      }
    }
  }
  if (before->graphical != NULL && qa_app_shared_sim_q3_guest(next) == NULL) {
    if (tx->save != NULL || tx->skirmish != NULL) {
      size_t seat_count = qa_application_override_seats(app, tx->next_client_cvar_seats, tx->next_client_cvars, tx->next_client_cvar_count, NULL, 0);
      qa_app_override_registry *seats = checked(calloc(seat_count + 1, sizeof *seats));
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, seats);
      (void)qa_application_override_seats(app, tx->next_client_cvar_seats, tx->next_client_cvars, tx->next_client_cvar_count, seats, seat_count);
      if (tx->save != NULL) {
        if (tx->next_overrides != NULL) qa_app_apply_team_arena_overrides(tx->next_overrides, seats, seat_count);
      } else {
        qa_app_team_arena_overrides *captured = checked(calloc(1, sizeof *captured));
        *captured = qa_app_capture_team_arena_overrides(seats, seat_count);
        keep_record(app, captured, (void (*)(void *))qa_app_team_arena_overrides_free);
        tx->next_overrides = captured;
      }
      qa_cleanup_pop(&cleanup);
      free(seats);
    } else if (!tx->same_client_owner || qa_app_shared_sim_q3_source(next) == NULL) tx->next_overrides = NULL;
  }
  qa_app_sim_quakec_source *quakec = qa_app_shared_sim_quakec_source(next);
  tx->has_admissions = true;
  if (qa_app_shared_sim_q3_guest(next) != NULL || (quakec != NULL && strcmp(qa_app_sim_quakec_source_kind(quakec), "quakeworld") == 0)) return;
  for (size_t c = 0; c < tx->client_count; c++) {
    qa_client_id client = tx->clients[c];
    if (is_carried_bot(tx, client)) continue;
    if (tx->save == NULL) {
      qa_session_seat *seat = qa_application_local_seat(app, client);
      qa_cvar_registry *cvars = seat == NULL ? NULL : next_client_cvars_get(tx, qa_session_seat_id(seat));
      qa_app_sim_quakec_source *previous_quakec = qa_app_shared_sim_quakec_source(before->simulation);
      if (seat == NULL && app->network != NULL && app->network->kind == QA_APPLICATION_NETWORK_Q1 && quakec != NULL
          && strcmp(qa_app_sim_quakec_source_kind(quakec), "netquake") == 0) {
        qa_app_sim_quakec_info info;
        bool has_info = previous_quakec != NULL;
        if (has_info) info = qa_app_sim_quakec_source_client_info(previous_quakec, client);
        admissions_set(tx, (double)client.slot, qa_app_shared_sim_reserve_net_quake_client_with(next, client, has_info ? &info : NULL));
        continue;
      }
      char *userinfo = NULL;
      if (cvars == NULL) {
        for (size_t i = 0; i < before->native_client_count; i++)
          if (before->native_clients[i].slot == (double)client.slot + 1) { userinfo = dup_text(before->native_clients[i].userinfo); break; }
      } else userinfo = userinfo_with_ip(cvars);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, userinfo);
      bool native = qa_app_shared_sim_q2_native(next, NULL, NULL);
      if (native && userinfo == NULL) qa_raise("Error", 0, 0, "Native carried client has no source userinfo");
      qa_app_sim_player_admission player;
      if (!native || userinfo == NULL) {
        char *player_info = cvars == NULL ? NULL : qa_app_player_userinfo(cvars);
        qa_cleanup info_cleanup;
        qa_cleanup_push(&info_cleanup, free, player_info);
        player = qa_app_shared_sim_admit_player_with(next, client, NULL, player_info);
        qa_cleanup_pop(&info_cleanup);
        free(player_info);
      } else player = qa_app_shared_sim_admit_q2_native_player(next, client, userinfo, seat != NULL);
      qa_cleanup_pop(&cleanup);
      free(userinfo);
      admissions_set(tx, (double)client.slot, player.actor);
      continue;
    }
    size_t count = 0;
    const qa_actor_id *players = qa_app_shared_sim_players(next, &count);
    bool found = false;
    for (size_t i = 0; i < count && !found; i++) {
      qa_client_id owner;
      if (qa_app_shared_sim_player_client(next, players[i], &owner) && qa_same_client(owner, client)) {
        admissions_set(tx, (double)client.slot, players[i]);
        found = true;
      }
    }
    if (!found) qa_raise("Error", 0, 0, "Restore did not bind client %s", QA_JS_NUMBER((double)client.slot));
  }
}

/* ================= stageNetworkHost ================= */
typedef struct unified_host_record {
  qa_app_sim_network_simulation simulation;
  qa_app_sim_network_content content;
} unified_host_record;
static qa_app_unified_composition *build_unified_composition(qa_app_loaded_content *content) {
  size_t count = 0;
  const qa_app_application_map_sidecar *sidecars = qa_app_loaded_content_map_sidecars(content, &count);
  qa_app_unified_map_sidecar *observed = checked(calloc(count + 1, sizeof *observed));
  for (size_t i = 0; i < count; i++) {
    observed[i].content = sidecars[i].content; observed[i].path = sidecars[i].path;
    observed[i].has_resource = sidecars[i].resource != NULL;
    if (observed[i].has_resource) observed[i].resource = qa_app_unified_resource_key_from_reference(sidecars[i].resource);
  }
  qa_save_value *recipe = checked(qa_save_recipe_value(qa_app_loaded_content_recipe(content)));
  qa_app_unified_composition *composition = qa_app_unified_build_composition(recipe, observed, count);
  qa_save_value_free(recipe);
  free(observed);
  return checked(composition);
}
static bool same_protocol(qa_protocol_identity left, qa_protocol_identity right) {
  return strcmp(left.kind, right.kind) == 0 && left.version == right.version && left.flags == right.flags && left.revision == right.revision;
}
typedef struct recording_host_try { qa_application *app; qa_application_world_transition *tx; } recording_host_try;
static void recording_host_body(void *ctx) {
  recording_host_try *t = ctx;
  qa_application *app = t->app;
  qa_application_world_transition *tx = t->tx;
  tx->next_network_host = qa_application_network_host(app, tx->next_simulation, tx->content, app->native_world_count + 1);
  tx->has_next_network_host = true;
  qa_application_validate_network_host(app, &tx->next_network_host);
  const qa_application_native_server_host *previous = app->recording_host;
  const qa_application_native_server_host *next = &tx->next_network_host;
  if (previous == NULL || previous->kind != next->kind
      || (previous->kind == QA_APPLICATION_NETWORK_Q1 && next->kind == QA_APPLICATION_NETWORK_Q1 && !same_protocol(previous->q1.protocol, next->q1.protocol))
      || (previous->kind == QA_APPLICATION_NETWORK_Q2 && next->kind == QA_APPLICATION_NETWORK_Q2 && !same_protocol(previous->q2.protocol, next->q2.protocol)))
    qa_raise("Error", 0, 0, "The destination uses a different demo protocol");
}
static void stage_network_host(qa_application *app, qa_application_world_transition *tx) {
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  tx->has_next_network_host = false;
  bool unified = app->network != NULL && app->network->kind == QA_APPLICATION_NETWORK_UNIFIED;
  tx->unified_host_present = unified;
  if (unified) {
    unified_host_record *record = checked(calloc(1, sizeof *record));
    keep_record(app, record, NULL);
    record->simulation = qa_app_shared_sim_network_simulation(next);
    record->content = qa_app_shared_sim_network_content(next, tx->content);
    qa_app_sim_unified_host_options host;
    memset(&host, 0, sizeof host);
    host.session = app->session; host.simulation = &record->simulation; host.content = &record->content;
    host.print_ctx = app; host.print = host_print;
    tx->next_unified_host = qa_app_sim_create_unified_application_server_host(host);
    tx->next_unified_composition = build_unified_composition(tx->content);
  }
  tx->has_recording_transition_error = false;
  if (app->network != NULL && app->network->kind != QA_APPLICATION_NETWORK_UNIFIED) {
    tx->next_network_host = qa_application_network_host(app, next, tx->content, app->native_world_count + 1);
    tx->has_next_network_host = true;
    qa_application_validate_network_host(app, &tx->next_network_host);
  } else if (qa_app_shared_sim_q2_native(next, NULL, NULL)) {
    /* Offline, this host only seeds future demo recording; wire support is admitted when recording actually starts
     * (as at initial open), so foreign movement/character selections can still load and travel. */
    tx->next_network_host = qa_application_network_host(app, next, tx->content, app->native_world_count + 1);
    tx->has_next_network_host = true;
  } else if (app->local_recording_active) {
    recording_host_try t = { app, tx };
    qa_caught_error caught;
    if (!qa_try(recording_host_body, &t, &caught)) {
      tx->recording_transition_error = caught;
      tx->has_recording_transition_error = true;
      tx->has_next_network_host = false;
    }
  }
  tx->has_next_unified_host = true;
}

/* ================= stageSourceCommands ================= */
static void stage_source_commands(qa_application *app, qa_application_world_transition *tx) {
  qa_mod_commands *mod_commands = SLOT(tx, mod_commands);
  slot_read(tx->has_candidate_action, "candidateAction");
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  qa_application_prepared_source_commands *prepared = qa_application_prepare_source_commands(app, next, tx->content, tx->save != NULL,
      candidate_action, tx);
  tx->candidate_commands = prepared->commands;
  qa_mod_commands_flush(mod_commands);
  qa_application_prepare_guest_bots(app, tx->content, next);
  tx->prepared_commands = prepared; tx->has_prepared_commands = true;
}

/* ================= stageGuestContinuation ================= */
static void local_guest_release(void *record) {
  qa_application_local_q3_guest_world *world = record;
  free(world->seat_ids);
  free(world->seats);
  qa_event_ring_destroy(&world->pending_commands);
}
static qa_application_local_q3_guest_world *create_local_guest(qa_application *app, qa_app_sim_q3_server_authority authority) {
  qa_application_local_q3_guest_world *world = checked(calloc(1, sizeof *world));
  qa_identity_owner owner;
  if (!qa_identity_owner_create(&owner, "local-qvm-world-audio")) qa_range_error("Out of memory");
  world->world_sound = qa_identity_owner_actor(&owner, 1022, 0);
  world->authority = authority;
  if (!qa_event_ring_init(&world->pending_commands, QA_APPLICATION_REQUEST_CAPACITY, sizeof(qa_application_command_request)))
    qa_range_error("Out of memory");
  keep_record(app, world, local_guest_release);
  return world;
}
typedef struct authority_record { qa_app_sim_network_simulation simulation; qa_app_sim_network_content content; } authority_record;
static qa_app_sim_q3_server_authority create_authority(qa_application *app, qa_application_world_transition *tx, const char *mode) {
  authority_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, NULL);
  record->simulation = qa_app_shared_sim_network_simulation(tx->next_simulation);
  record->content = qa_app_shared_sim_network_content(tx->next_simulation, tx->content);
  qa_app_sim_q3_server_binding_options options;
  memset(&options, 0, sizeof options);
  options.session = app->session;
  options.simulation = &record->simulation;
  options.content = &record->content;
  options.mode = mode;
  options.print_ctx = app; options.print = host_print;
  return qa_app_sim_create_q3_application_server_host(options);
}
static bool guest_rejected(qa_application_world_transition *tx, qa_client_id client) {
  for (size_t i = 0; i < tx->guest_rejected_client_count; i++) if (qa_same_client(tx->guest_rejected_clients[i].client, client)) return true;
  return false;
}
static void guest_reject(qa_application *app, qa_application_world_transition *tx, qa_client_id client, const char *reason) {
  grow((void **)&tx->guest_rejected_clients, &tx->guest_rejected_client_capacity, tx->guest_rejected_client_count + 1,
      sizeof *tx->guest_rejected_clients);
  char *kept = dup_text(reason);
  keep_text(app, kept);
  size_t i = 0;
  while (i < tx->guest_rejected_client_count && !qa_same_client(tx->guest_rejected_clients[i].client, client)) i++;
  tx->guest_rejected_clients[i].client = client;
  tx->guest_rejected_clients[i].reason = kept;
  if (i == tx->guest_rejected_client_count) tx->guest_rejected_client_count++;
}
static void stage_guest_continuation(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  slot_read(tx->has_guest_rejected_clients, "guestRejectedClients");
  qa_app_q3_guest_output candidate_output = SLOT(tx, guest_candidate_output);
  slot_read(tx->has_carried_guest_commands, "carriedGuestCommands");
  slot_read(tx->has_next_client_cvars, "nextClientCvars");
  double destination = SLOT(tx, destination_source_milliseconds);
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  slot_read(tx->has_admissions, "admissions");
  qa_application_previous_world *before = tx_before(tx);
  qa_app_q3_qvm_server_game *restored = qa_app_shared_sim_q3_guest(next);
  if (staged->has_guest_transition && restored != NULL) {
    qa_app_sim_q3_server_authority authority = create_authority(app, tx, NULL);
    qa_cvar_registry *guest_cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(restored));
    double server_id = (double)qa_js_to_int32(qa_cvar_registry_variable_value(guest_cvars, "sv_serverid") + 1);
    if (app->network == NULL) {
      staged->local_guest = create_local_guest(app, authority);
      authority.host.prepare(authority.host.ctx, 0, server_id, NULL, NULL);
    }
    qa_app_q3_guest_output output = app->network == NULL ? qa_application_guest_output(app, staged->local_guest, false) : candidate_output;
    qa_app_q3_qvm_server_game_initialize(restored, output, tx->guest_restart ? "map-restart" : "map-change",
        staged->guest_transition.clients, staged->guest_transition.client_count);
    if (tx->q3_map != NULL) qa_app_apply_q3_map_launch(guest_cvars, tx->q3_map, "finish");
    for (size_t c = 0; c < staged->guest_transition.client_count; c++) {
      const qa_app_q3_guest_map_client *carried = &staged->guest_transition.clients[c];
      if (guest_rejected(tx, carried->client)) continue;
      qa_app_q3_admission admission = qa_app_q3_qvm_server_game_reconnect(restored, carried->client);
      if (!admission.accepted) {
        if (app->network == NULL || app->network->kind != QA_APPLICATION_NETWORK_Q3) qa_raise("Error", 0, 0, "%s", admission.reason);
        guest_reject(app, tx, carried->client, admission.reason);
        continue;
      }
      qa_app_local_input *local = NULL;
      for (size_t i = 0; before->graphical != NULL && i < qa_app_application_input_local_count(before->graphical->input) && local == NULL; i++) {
        qa_app_local_input *candidate = qa_app_application_input_local(before->graphical->input, i);
        if (qa_same_client(qa_session_client_id(qa_session_seat_client(candidate->player.seat)), carried->client)) local = candidate;
      }
      if (local != NULL && staged->local_guest != NULL) {
        qa_session_seat *seat = local->player.seat;
        const char *guest_userinfo = qa_app_q3_server_state_get_userinfo(qa_app_q3_qvm_server_game_state(restored), (double)carried->client.slot);
        const char *userinfo = guest_userinfo != NULL ? guest_userinfo : carried->userinfo;
        qa_cvar_registry *cvars = qa_application_create_client_cvars(app, next, tx->content, tx->options, seat, userinfo,
            previous_client_cvars(before, qa_session_seat_id(seat)), NULL, 0, false);
        char *info = userinfo_with_ip(cvars);
        qa_cleanup cleanup;
        qa_cleanup_push(&cleanup, free, info);
        qa_application_prepare_local_guest_seat(app, staged->local_guest, next, seat, cvars, info,
            authority.host.game_state(authority.host.ctx, admission.player, server_id));
        qa_cleanup_pop(&cleanup);
        free(info);
        next_client_cvars_set(tx, qa_session_seat_id(seat), cvars);
      }
      if (app->network == NULL) {
        qa_q3_base_user_command command;
        bool found = false;
        for (size_t i = 0; i < tx->carried_count && !found; i++)
          if (tx->carried_slots[i] == (double)carried->client.slot && tx->carried_present[i]) { command = tx->carried_commands[i]; found = true; }
        if (!found) {
          memset(&command, 0, sizeof command);
          command.server_time = destination;
          command.weapon = 2;
        }
        qa_app_q3_qvm_server_game_begin(restored, admission.player, command);
      }
      admissions_set(tx, (double)carried->client.slot, admission.player.actor);
    }
    qa_application_publish_local_guest_snapshots_with(app, staged->local_guest, next);
    if (before->graphical != NULL && staged->guest_browser == NULL) staged->guest_browser = qa_application_open_guest_browser(app);
  }
  if (tx->save != NULL && restored != NULL) {
    if (before->graphical != NULL) {
      qa_app_sim_q3_server_authority authority = create_authority(app, tx, "restore");
      staged->local_guest = create_local_guest(app, authority);
      double server_id = qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(restored)), "sv_serverid");
      authority.host.prepare(authority.host.ctx, 0, server_id, NULL, NULL);
      size_t locals = qa_app_application_input_local_count(before->graphical->input);
      for (size_t i = 0; i < locals; i++) {
        qa_session_seat *seat = qa_app_application_input_local(before->graphical->input, i)->player.seat;
        size_t archive_count = 0;
        qa_cvar_archive_entry *archive = tx->same_client_owner ? NULL
            : qa_application_load_client_archive(app, seat, tx->content, tx->options, &archive_count);
        qa_actor_id actor = qa_application_prepare_restored_guest_seat(app, staged->local_guest, next, seat, tx->content, tx->options,
            tx->same_client_owner ? previous_client_cvars(before, qa_session_seat_id(seat)) : NULL, archive, archive_count);
        qa_app_cvar_archive_entries_free(archive, archive_count);
        admissions_set(tx, (double)qa_session_client_id(qa_session_seat_client(seat)).slot, actor);
      }
      for (size_t i = 0; i < staged->local_guest->seat_count; i++)
        next_client_cvars_set(tx, staged->local_guest->seat_ids[i], staged->local_guest->seats[i].cvars);
      qa_application_publish_local_guest_snapshots_with(app, staged->local_guest, next);
      if (staged->guest_browser == NULL) staged->guest_browser = qa_application_open_guest_browser(app);
    }
    qa_app_q3_qvm_server_game_complete_restore(restored, qa_application_guest_output(app, staged->local_guest, false));
  }
}

/* ================= stageBots ================= */
/* staged.savedClients.resolveBotClient as createBots' restore.resolveClient. */
static qa_session_client *saved_bot_client(void *ctx, double slot, double generation) {
  qa_application_saved_clients *saved = ctx;
  qa_client_id id;
  memset(&id, 0, sizeof id);
  id.slot = (int64_t)slot;
  id.generation = (int64_t)generation;
  return saved->resolve_bot_client(saved->ctx, id);
}
static void stage_bots(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  slot_read(tx->has_guest_commands, "guestCommands");
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  qa_app_sim_bot_service *previous_bots = tx_before(tx)->bots;
  qa_application_create_bots_setup setup;
  if (tx->save == NULL) {
    memset(&setup, 0, sizeof setup);
    setup.clients = tx->bot_clients;
    setup.client_count = tx->bot_client_count;
    setup.restart = tx->initial_source_milliseconds != 0;
    /* commands: { insert: text => guestCommands().insert(text) }: the candidate buffer, which publication makes the
     * source buffer. */
    setup.has_commands = true;
    setup.commands = tx->candidate_commands;
    staged->bots = qa_application_create_bots(app, tx->content, next, &setup);
  }
  /* Bot settings live in the source registry, whose travel archive only carries archived variables; carry the
   * bot namespace explicitly, as the Q2 snapshot path does for the whole registry. */
  if (tx->save == NULL && previous_bots != NULL && staged->bots != NULL) {
    size_t count = 0;
    qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(previous_bots->configuration(previous_bots->ctx), 0, &count);
    qa_cvar_registry *configuration = staged->bots->configuration(staged->bots->ctx);
    for (size_t i = 0; i < count; i++) {
      if (strncmp(snapshots[i].name, "bot_", 4) != 0) continue;
      const char *value = snapshots[i].latched_value != NULL ? snapshots[i].latched_value : snapshots[i].value;
      if (!cvar_exists(configuration, snapshots[i].name))
        (void)qa_cvar_registry_register(configuration, snapshots[i].name, value, 0, NULL);
      else qa_application_cvar_set(configuration, snapshots[i].name, value, true);
    }
    for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
    free(snapshots);
  } else if (tx->decoded_bots != NULL && staged->saved_clients != NULL) {
    memset(&setup, 0, sizeof setup);
    setup.has_commands = true;
    setup.commands = tx->candidate_commands;
    setup.restore = tx->decoded_bots;
    setup.resolve_client = saved_bot_client;
    setup.resolve_ctx = staged->saved_clients;
    staged->bots = qa_application_create_bots(app, tx->content, next, &setup);
  }
}

/* ================= stageGraphical ================= */
static qa_app_local_player *admitted_players(qa_application_world_transition *tx, qa_application_graphical *previous, size_t *out_count) {
  size_t count = qa_app_application_input_local_count(previous->input);
  qa_app_local_player *players = checked(calloc(count + 1, sizeof *players));
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(previous->input, i);
    double slot = (double)qa_session_client_id(qa_session_seat_client(local->player.seat)).slot;
    bool found = false;
    for (size_t j = 0; j < tx->admission_count && !found; j++)
      if (tx->admission_slots[j] == slot) { players[i].actor = tx->admissions[j]; found = true; }
    if (!found) { free(players); qa_raise("Error", 0, 0, "Travel admission is missing a connected local player"); }
    players[i].seat = local->player.seat;
  }
  *out_count = count;
  return players;
}
/* loadMenuArtImage as loadNativeUiArt's reader. */
static bool menu_art_read(void *ctx, const char *path, qa_image_level *out) {
  (void)ctx; /* the reader closure captures nothing */
  qa_app_load_menu_art_image(path, out);
  return true;
}
static double performance_now(void *ctx) {
  (void)ctx; /* TS performanceNow captures nothing */
  return qa_application_performance_now();
}
static void graphical_release(void *record) {
  qa_application_graphical *graphical = record;
  free(graphical->presentations);
  free(graphical->q3_seats); free(graphical->q3);
  qa_app_rerelease_presentation_destroy(graphical->rerelease); /* the graphical owner holds the world's rerelease presentation */
}
static void stage_graphical(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  slot_read(tx->has_next_client_cvars, "nextClientCvars");
  slot_read(tx->has_candidate_action, "candidateAction");
  double destination = SLOT(tx, destination_source_milliseconds);
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  slot_read(tx->has_admissions, "admissions");
  qa_application_prepared_source_commands *prepared_commands = SLOT(tx, prepared_commands);
  qa_application_graphical *previous = tx_before(tx)->graphical;
  if (previous == NULL) return;
  /* Seat closures below outlive this function; the renderer carries across worlds, the old world does not. */
  qa_app_native_renderer *shared_renderer = previous->renderer;
  if (staged->assets == NULL) {
    staged->assets = create_world_assets(app, tx->content, previous->renderer);
    (void)qa_app_application_assets_load_world(staged->assets);
  }
  qa_text_font_selection font = qa_app_application_assets_load_console_font(staged->assets);
  const qa_app_menu_typography *typography = qa_app_application_assets_load_menu_typography(staged->assets);
  const qa_renderer_image *font_source = &font.classic->picture.image.image;
  if (!font_source->source_is_resource) qa_raise("Error", 0, 0, "Native menu font has no mounted resource identity");
  staged->art = checked(calloc(1, sizeof *staged->art));
  keep_record(app, staged->art, NULL);
  qa_ui_load_native_art_selected(staged->art, font_source->source_resource.id, qa_app_application_assets_images(staged->assets), menu_art_read,
      NULL, false, false); /* { frames: false, mainBackground: false } */
  const qa_q3_character_assets *characters = tx->options->character == QA_GAME_FAMILY_Q3
      ? qa_app_load_q3_character_for_content(tx->content, qa_app_loaded_content_recipe(tx->content)->character.appearance.content,
          tx->options->character_model) : NULL;
  size_t player_count = 0;
  qa_app_local_player *players = admitted_players(tx, previous, &player_count);
  keep_record(app, players, NULL);
  size_t locals = qa_app_application_input_local_count(previous->input);
  qa_app_frontend_preference_values *seat_input = checked(calloc(locals + 1, sizeof *seat_input));
  qa_seat_id *seat_input_ids = checked(calloc(locals + 1, sizeof *seat_input_ids));
  keep_record(app, seat_input, NULL);
  keep_record(app, seat_input_ids, NULL);
  for (size_t i = 0; i < locals; i++) {
    qa_app_local_input *local = qa_app_application_input_local(previous->input, i);
    seat_input_ids[i] = qa_session_seat_id(local->player.seat);
    seat_input[i] = qa_app_read_frontend_input(local);
  }
  qa_ui_preference_values *preferences = checked(calloc(previous->presentation_count + 1, sizeof *preferences));
  keep_record(app, preferences, NULL);
  for (size_t i = 0; i < previous->presentation_count; i++)
    preferences[i] = qa_ui_seat_preferences_values(qa_app_seat_ui_preferences(previous->presentations[i].ui));
  qa_app_save_audio_settings_from(&app->input_config, previous->audio);
  qa_app_application_input_save_settings(previous->input);
  staged->scripts = qa_app_source_console_scripts(tx->content, tx->options, &app->input_config);
  qa_application_input_actions_options actions_options;
  memset(&actions_options, 0, sizeof actions_options);
  actions_options.simulation = next;
  actions_options.content = tx->content;
  actions_options.has_q2_console = true; actions_options.q2_console = prepared_commands->q2_console;
  actions_options.has_local_guest = true; actions_options.local_guest = staged->local_guest;
  actions_options.explicit_client_cvars = true;
  actions_options.client_cvar_seats = tx->next_client_cvar_seats;
  actions_options.client_cvars = tx->next_client_cvars;
  actions_options.client_cvar_count = tx->next_client_cvar_count;
  actions_options.has_shared_cvars = true;
  actions_options.shared_cvars = tx->has_candidate_images ? qa_app_image_settings_cvars(tx->candidate_images.settings) : NULL;
  actions_options.candidate_action = candidate_action; actions_options.candidate_ctx = tx;
  qa_application_input_actions *actions = qa_application_input_actions_create(app, &actions_options);
  keep_record(app, actions, qa_application_input_actions_release);
  qa_app_sim_presentation_access *access = checked(malloc(sizeof *access));
  keep_record(app, access, NULL);
  *access = qa_app_shared_sim_presentation_access(next);
  qa_app_input_command_owner owner;
  memset(&owner, 0, sizeof owner);
  owner.scripts = staged->scripts;
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_application_input_window(previous->input);
  args.players = players; args.player_count = player_count;
  args.options = tx->options;
  args.dialect = qa_app_movement_dialect(tx->options, qa_app_shared_sim_recipe(next));
  args.simulation = access;
  args.actions = actions->commands;
  args.now = performance_now; args.now_ctx = NULL;
  args.settings = &app->input_config;
  args.owner = &owner;
  args.previous = previous->input;
  qa_app_application_input *input = qa_app_application_input_open(&args);
  staged->input = input;
  qa_application_restore_source_input_angles(app, input, next, NULL);
  double sequence = qa_app_application_input_next_command_sequence(previous->input);
  for (size_t i = 0; i < player_count; i++) {
    qa_app_sim_movement_player *movement = qa_app_shared_sim_movement_player(next, players[i].actor);
    sequence = qa_js_max(sequence, (movement == NULL ? -1 : movement->last_sequence) + 1);
  }
  qa_app_application_input_resume_commands(input, sequence);
  qa_application_audio_settings settings;
  memset(&settings, 0, sizeof settings);
  settings.has_values = qa_app_load_audio_settings(&app->input_config, &settings.values);
  if (settings.has_values) {
    keep_text(app, settings.values.device_name);
    keep_text(app, settings.values.menu_track);
  }
  qa_app_application_audio *audio = qa_application_create_world_audio(app, tx->content, tx->options, &settings, true, next);
  qa_app_application_audio_bind_haptics(audio, qa_app_application_input_audio_haptics(input));
  staged->audio = audio;
  qa_app_application_audio_prepare_environment(audio, qa_app_shared_sim_scene_queries(next));
  qa_app_application_effects *effects = qa_application_create_world_effects(app, staged->assets, next, tx->options->seed);
  staged->effects = effects;
  qa_application_preload_world_effects(app, effects);
  qa_app_prepare_world_resources(staged->assets, next, audio, effects, app->host.loading, host_print, app);
  qa_app_application_audio_set_effects_volume(audio, qa_app_application_audio_effects_volume(previous->audio));
  qa_app_application_audio_set_music_volume(audio, qa_app_application_audio_music_volume(previous->audio));
  if (tx->has_candidate_images) {
    qa_app_application_audio_bind_volume_cvars(audio, qa_app_image_settings_cvars(tx->candidate_images.settings));
    qa_app_application_audio_bind_output_cvars(audio, qa_app_image_settings_cvars(tx->candidate_images.settings));
  }
  if (input != previous->input) {
    qa_app_apply_frontend_preferences(tx->frontend_overrides, input, audio);
    for (size_t i = 0; i < qa_app_application_input_local_count(input); i++) {
      qa_app_local_input *local = qa_app_application_input_local(input, i);
      for (size_t j = 0; j < locals; j++)
        if (qa_same_seat(seat_input_ids[j], qa_session_seat_id(local->player.seat))) qa_app_apply_frontend_input(seat_input[j], local);
    }
  }
  qa_app_rerelease_seat *seats = checked(calloc(player_count + 1, sizeof *seats));
  keep_record(app, seats, NULL);
  for (size_t i = 0; i < player_count; i++) { seats[i].seat = qa_session_seat_id(players[i].seat); seats[i].actor = players[i].actor; }
  qa_app_rerelease_presentation *rerelease = qa_app_rerelease_presentation_create(staged->assets, seats, player_count,
      qa_app_application_input_shared_settings(input));
  qa_application_graphical *graphical = checked(calloc(1, sizeof *graphical));
  keep_record(app, graphical, graphical_release);
  size_t input_locals = qa_app_application_input_local_count(input);
  graphical->presentations = checked(calloc(input_locals + 1, sizeof *graphical->presentations));
  graphical->q3_seats = checked(calloc(input_locals + 1, sizeof *graphical->q3_seats));
  graphical->q3 = checked(calloc(input_locals + 1, sizeof *graphical->q3));
  for (size_t index = 0; index < input_locals; index++) {
    qa_app_local_input *local = qa_app_application_input_local(input, index);
    qa_seat_id seat = qa_session_seat_id(local->player.seat);
    qa_application_q3_seat_client source_client;
    bool has_source = qa_application_create_q3_seat_client(app, local, staged->assets, audio, input, previous->renderer, next,
        NULL, 0, staged->local_guest, staged->guest_browser, next_client_cvars_get(tx, seat), &source_client);
    if (has_source) {
      grow((void **)&staged->clients, &staged->client_capacity, staged->client_count + 1, sizeof *staged->clients);
      staged->clients[staged->client_count++] = source_client.client;
      graphical->q3_seats[graphical->q3_count] = seat;
      graphical->q3[graphical->q3_count++] = source_client;
    }
    double override_value = 0;
    if (qa_app_view_settings_override(app->view_settings, &override_value)) {
      double field_of_view = qa_app_view_settings_field_of_view(app->view_settings);
      if (!has_source || !source_client.qvm)
        qa_app_shared_sim_set_player_field_of_view(next, local->player.actor, field_of_view, tx->save == NULL ? "change" : "restore");
      if (has_source) {
        char text[40];
        qa_net_js_number(field_of_view, text, sizeof text);
        qa_application_cvar_set(qa_app_application_q3_client_cvars(source_client.client), "cg_fov", text, false);
      }
    }
    if (has_source && source_client.qvm)
      qa_app_application_q3_client_prepare(source_client.client, app->presentation_frames, NULL, NULL, 0, true, NULL, 0, true, false);
    qa_application_native_q2_seat_client native_seat;
    bool has_native = qa_application_prepare_native_q2_seat(app, local, staged->assets, next, next_client_cvars_get(tx, seat), &native_seat);
    if (has_native) {
      grow((void **)&staged->native_q2_seats, &staged->native_q2_capacity, staged->native_q2_count + 1, sizeof *staged->native_q2_seats);
      staged->native_q2 = checked(realloc(staged->native_q2, staged->native_q2_capacity * sizeof *staged->native_q2));
      staged->native_q2_seats[staged->native_q2_count] = seat;
      staged->native_q2[staged->native_q2_count++] = native_seat;
    }
    qa_app_seat_ui *ui = qa_application_create_seat_ui(app, local, staged->art, input, shared_renderer, next, font, audio, typography,
        rerelease, tx->content, tx->options, has_source && source_client.qvm, tx->next_client_cvar_seats, tx->next_client_cvars,
        tx->next_client_cvar_count);
    if (index < previous->presentation_count) qa_ui_seat_preferences_set_values(qa_app_seat_ui_preferences(ui), preferences[index]);
    qa_application_q3_seat_client *seat_client = has_source ? &graphical->q3[graphical->q3_count - 1] : NULL;
    qa_app_world_seat_presentation *presentation = qa_application_create_world_seat_presentation(app, local, staged->assets,
        previous->renderer, next, tx->options->seats, font, characters, ui, effects, seat_client, rerelease,
        has_native ? &staged->native_q2[staged->native_q2_count - 1] : NULL, tx->content);
    grow((void **)&staged->presentations, &staged->presentation_capacity, staged->presentation_count + 1, sizeof *staged->presentations);
    staged->presentations[staged->presentation_count++] = presentation;
    graphical->presentations[graphical->presentation_count].presentation = presentation;
    graphical->presentations[graphical->presentation_count].local = local;
    graphical->presentations[graphical->presentation_count].ui = ui;
    graphical->presentations[graphical->presentation_count++].q3_client = seat_client;
  }
  graphical->renderer = previous->renderer;
  graphical->input = input;
  graphical->audio = audio;
  graphical->effects = effects;
  graphical->art = staged->art;
  graphical->assets = staged->assets;
  graphical->rerelease = rerelease;
  graphical->native_q2_seats = staged->native_q2_seats;
  graphical->native_q2 = staged->native_q2;
  graphical->native_q2_count = staged->native_q2_count;
  qa_application_create_presentation_services(app, staged->assets, audio, input, next, tx->content, shared_renderer,
      qa_application_staged_clock(app, staged, destination), graphical);
  staged->graphical = graphical;
  if (tx->save != NULL) qa_application_restore_component_clients(app, tx->save, staged->graphical, next);
}

/* ================= createWorldAudio / createPresentationServices / stagedClock ================= */
static double audio_elapsed(void *ctx) { return ((qa_application *)ctx)->elapsed; }
typedef struct world_audio_record { qa_application *app; qa_app_shared_simulation *simulation; } world_audio_record;
/* q3TeamGame: () => (simulation ?? this.simulation).teamGame(). */
static bool audio_team_game(void *ctx) {
  world_audio_record *record = ctx;
  return qa_app_shared_sim_team_game(record->simulation != NULL ? record->simulation : record->app->world_simulation);
}
qa_app_application_audio *qa_application_create_world_audio(qa_application *app, qa_app_loaded_content *content,
    const qa_app_options *options, const qa_application_audio_settings *settings, bool defer_output, qa_app_shared_simulation *simulation) {
  world_audio_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, NULL);
  record->app = app;
  record->simulation = simulation;
  qa_app_audio_options audio;
  memset(&audio, 0, sizeof audio);
  audio.q3_team_game = audio_team_game; audio.q3_team_game_ctx = record;
  audio.music_controls = app->music_controls;
  audio.defer_output = defer_output;
  /* ...settings (loadAudioSettings: absent fields stay undefined). */
  if (settings->has_values) {
    audio.has_output_format = settings->values.has_output_format; audio.output_format = settings->values.output_format;
    audio.device_name = settings->values.device_name;
    audio.has_effects_volume = true; audio.effects_volume = settings->values.effects_volume;
    audio.has_music_volume = true; audio.music_volume = settings->values.music_volume;
  }
  qa_app_application_audio *created = qa_app_application_audio_create(content, audio_elapsed, app, options->seed, options->character_model,
      host_print, app, &audio);
  if (created == NULL) qa_range_error("Out of memory");
  return created;
}
/* The closures createPresentationServices stores for the world's life. */
typedef struct presentation_services_record {
  qa_application *app;
  qa_app_shared_simulation *current;
  qa_app_loaded_content *content;
  qa_app_application_assets *assets;
  qa_app_application_audio *audio;
  qa_app_native_renderer *renderer;
  qa_app_application_input *input;
  qa_application_staged_clock_fn mod_clock; void *mod_clock_ctx;
  qa_app_qvm_display_renderer display;
} presentation_services_record;
static void services_print(void *ctx, const char *text) { qa_application_print(((presentation_services_record *)ctx)->app, text); }
/* nextFrame: this.host.loading?.nextFrame ?? setImmediate (a no-op turn here). */
static void services_next_frame(void *ctx) {
  presentation_services_record *record = ctx;
  const qa_application_loading *loading = record->app->host.loading;
  if (loading != NULL && loading->next_frame != NULL) loading->next_frame(loading->ctx);
}
static double presentation_now(void *ctx) {
  qa_application *app = ((presentation_services_record *)ctx)->app;
  return app->has_presentation_milliseconds ? app->presentation_milliseconds : app->elapsed;
}
static double presentation_frame(void *ctx) { return ((presentation_services_record *)ctx)->app->presentation_frames; }
static double mod_now(void *ctx) {
  presentation_services_record *record = ctx;
  return record->mod_clock(record->mod_clock_ctx);
}
/* hardware: () => q3Hardware(sharedRenderer.driver?.renderer ?? "") === "ragepro". */
static bool services_ragepro(void *ctx) {
  presentation_services_record *record = ctx;
  const char *name = qa_app_native_renderer_driver_name(record->renderer);
  return strcmp(qa_q3_hardware_classify(name == NULL ? "" : name), "ragepro") == 0;
}
/* queueCommand: request => { this.requestedCommands.admit(request); }. */
static void services_queue_command(void *ctx, const qa_app_component_client_command_request *request) {
  presentation_services_record *record = ctx;
  /* The ring's record owns copies of the request's strings and context (qa_application_command_request_free). */
  qa_application_command_request copy;
  memset(&copy, 0, sizeof copy);
  copy.target = QA_APPLICATION_TARGET_COMPONENT_CLIENT;
  copy.consumer = request->consumer;
  copy.mode = strcmp(request->mode, "reliable") == 0 ? "reliable" : "console";
  copy.name = dup_text(request->name);
  copy.argument_count = request->argument_count;
  copy.arguments = checked(calloc(request->argument_count + 1, sizeof *copy.arguments));
  for (size_t i = 0; i < request->argument_count; i++) copy.arguments[i] = dup_text(request->arguments[i]);
  copy.has_seat = true; copy.seat = request->seat;
  copy.has_source = true;
  copy.source = qa_command_context_copy(&request->source);
  (void)qa_event_ring_admit(&record->app->requested_commands, &copy);
}
/* presentationMedia: componentMediaControl(current.events, audio, assets). */
static void services_media(void *ctx, const qa_active_mod_presentation *source, const qa_component_presentation_media_request *request,
    bool initializing, bool (*current)(void *current_ctx), void *current_ctx) {
  presentation_services_record *record = ctx;
  qa_app_component_media_host host = qa_app_component_media_host_of(record->current, record->audio, record->assets);
  const char *error = qa_app_component_media_control(host, source, request, initializing, current, current_ctx);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
/* systemCinematics: (_source, presentation, scope) => this.systemCinematics(current, content, assets, audio, sharedRenderer,
 * presentation.local.player.seat.id, input, scope). */
static const qa_app_system_cinematic_host *services_cinematics(void *ctx, const qa_active_mod_presentation *source,
    const qa_app_viewing_seat *presentation, const qa_app_component_cinematic_scope *scope) {
  presentation_services_record *record = ctx;
  (void)source; /* TS `_source`: the system cinematic host does not read it */
  return qa_application_system_cinematics(record->app, record->current, record->content, record->assets, record->audio,
      record->renderer, presentation->seat(presentation->ctx), record->input, scope);
}
void qa_application_create_presentation_services(qa_application *app, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_application_input *input, qa_app_shared_simulation *current, qa_app_loaded_content *content, qa_app_native_renderer *shared_renderer,
    qa_application_clock clock, qa_application_graphical *out) {
  presentation_services_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, NULL);
  record->app = app; record->current = current; record->content = content; record->assets = assets; record->audio = audio;
  record->renderer = shared_renderer; record->input = input; record->mod_clock = clock.now; record->mod_clock_ctx = clock.ctx;
  record->display = qa_app_native_renderer_qvm_display(shared_renderer);
  qa_app_selected_q3_presentation_options selected;
  memset(&selected, 0, sizeof selected);
  selected.base.assets = assets;
  selected.base.audio = audio;
  selected.base.queries = current->scene;
  selected.base.ctx = record;
  selected.base.print = services_print;
  selected.base.next_frame = services_next_frame;
  selected.base.clock = (qa_app_q3_service_clock){ record, presentation_now, presentation_frame };
  selected.base.hosts = qa_app_application_assets_q3_presentation_hosts(assets, audio);
  selected.hardware_ragepro = services_ragepro;
  out->selected_q3_presentations = qa_app_selected_q3_presentations_create(&selected);
  qa_app_mod_presentations_options mods;
  memset(&mods, 0, sizeof mods);
  mods.assets = assets;
  mods.audio = audio;
  mods.input = input;
  mods.ctx = record;
  mods.queue_command = services_queue_command;
  mods.renderer = &record->display;
  mods.queries = current->scene;
  mods.presentation_media = services_media;
  mods.system_cinematics = services_cinematics;
  mods.print = services_print;
  mods.next_frame = services_next_frame;
  mods.clock = (qa_app_q3_service_clock){ record, mod_now, presentation_frame };
  mods.hosts = selected.base.hosts;
  out->mod_presentations = qa_app_application_mod_presentations_create(&mods);
  if (out->selected_q3_presentations == NULL || out->mod_presentations == NULL) qa_range_error("Out of memory");
}
/* TS stagedClock(staged, destination): () => staged.committed ? (presentationMilliseconds ?? elapsed) : destination. */
typedef struct staged_clock_record { qa_application *app; qa_application_world_replacement *staged; double destination; } staged_clock_record;
static double staged_now(void *ctx) {
  staged_clock_record *record = ctx;
  qa_application *app = record->app;
  return record->staged->committed ? (app->has_presentation_milliseconds ? app->presentation_milliseconds : app->elapsed) : record->destination;
}
qa_application_clock qa_application_staged_clock(qa_application *app, struct qa_application_world_replacement *staged, double destination) {
  staged_clock_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, NULL);
  record->app = app; record->staged = staged; record->destination = destination;
  return (qa_application_clock){ record, staged_now };
}

/* ================= createApplicationCapture / createVideoRestart / hostPrinter ================= */
static const char *capture_map_name(void *ctx) { return ((qa_application *)ctx)->launch_options.map; }
qa_app_capture *qa_application_create_application_capture(qa_application *app, qa_app_application_input *input, qa_app_native_renderer *renderer) {
  char *root = qa_app_application_capture_root(app->launch_options.user_content_root);
  keep_text(app, root);
  return qa_app_application_capture_open(input, root, capture_map_name, app, host_print, app, renderer);
}
static qa_app_capture *video_capture(void *ctx) { return ((qa_application *)ctx)->capture; }
static bool video_render_worker(void *ctx, bool *out) {
  qa_application *app = ctx;
  *out = (app->image_settings == NULL ? 0 : qa_cvar_registry_variable_value(qa_app_image_settings_cvars(app->image_settings), "r_smp")) != 0;
  return true;
}
static bool video_prepare(void *ctx, qa_app_prepared_video_presentation *out) { return qa_application_prepare_video_restart(ctx, out); }
static void video_publish_window(void *ctx, qa_sdl_window *window) {
  qa_application *app = ctx;
  if (app->graphical == NULL) qa_raise("Error", 0, 0, "Video input has retired");
  qa_app_application_input_publish_window(app->graphical->input, qa_app_input_window_from_sdl(window));
}
static void video_published(void *ctx, const char *kind) {
  ((qa_application *)ctx)->launch_options.renderer = kind; /* this.launchOptions = { ...this.launchOptions, renderer } */
}
static void video_settled(void *ctx) {
  (void)ctx; /* TS settled: () => {} */
}
static void video_print(void *ctx, const char *text, const qa_command_context *source) {
  qa_application *app = ctx;
  if (app->graphical == NULL) qa_application_print(app, text);
  else qa_app_application_input_print(app->graphical->input, text, source);
}
/* failed: error => this.closeFailedWorld(error) (raises the error after closing). */
static void video_failed(void *ctx, const qa_caught_error *error) { qa_application_close_failed_world(ctx, error); }
qa_app_video_restart *qa_application_create_video_restart(qa_application *app, qa_app_native_renderer *renderer) {
  qa_app_video_restart_host host = { app, video_capture, video_render_worker, video_prepare, video_publish_window, video_published,
      video_settled, video_print, video_failed };
  qa_app_video_restart *video = qa_app_video_restart_create(renderer, host);
  if (video == NULL) qa_range_error("Out of memory");
  return video;
}
static void printer_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
qa_application_printer qa_application_host_printer(qa_application *app) {
  qa_application_printer printer = { printer_print, app, NULL };
  return printer;
}

/* ================= stageCapture / createCandidateCapture ================= */
static qa_app_capture *create_candidate_capture(qa_application *app, qa_application_world_transition *tx) {
  qa_application_graphical *graphical = SLOT(tx, staged)->graphical;
  if (graphical == NULL) return NULL;
  if (app->ownership.borrowed_client != NULL) return app->ownership.borrowed_client->capture;
  char *root = qa_app_application_capture_root(tx->options->user_content_root);
  keep_text(app, root);
  return qa_app_application_capture_open(graphical->input, root, capture_map_name, app, host_print, app, graphical->renderer);
}
static void stage_capture(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_application_graphical *previous = tx_before(tx)->graphical;
  qa_app_capture *previous_capture = app->capture;
  qa_app_capture *next_capture = create_candidate_capture(app, tx);
  staged->capture = app->ownership.borrowed_client == NULL ? next_capture : NULL;
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
  if (previous != NULL && staged->graphical != NULL) {
    tx->publish_audio = qa_app_application_audio_prepare_output_transfer(previous->audio, staged->graphical->audio);
    tx->has_publish_audio = true;
  }
  if (app->closed) qa_raise("Error", 0, 0, "Application closed during world preparation");
  tx->previous_capture = previous_capture;
  tx->next_capture = next_capture; tx->has_next_capture = true;
}

/* ================= stageReplacementSet ================= */
/* cleanup: () => presentation.close(); the C presentation is freed with it (TS GC). */
static void presentation_cleanup(void *ctx) {
  qa_app_world_seat_presentation_close(ctx);
  qa_app_world_seat_presentation_destroy(ctx);
}
static void stage_replacement_set(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  tx->replacement_presentations = checked(calloc(staged->presentation_count + 1, sizeof *tx->replacement_presentations));
  for (size_t i = 0; i < staged->presentation_count; i++) {
    qa_app_world_seat_presentation *presentation = staged->presentations[i];
    tx->replacement_presentations[i].seat = qa_app_world_seat_presentation_local(presentation)->player.seat;
    tx->replacement_presentations[i].presentation = qa_app_world_seat_presentation_seat(presentation);
    tx->replacement_presentations[i].cleanup = presentation_cleanup;
    tx->replacement_presentations[i].cleanup_ctx = presentation;
  }
  tx->replacement_presentation_count = staged->presentation_count;
  tx->has_replacement_presentations = true;
  memset(&tx->replacement_clients, 0, sizeof tx->replacement_clients);
  if (staged->saved_clients != NULL) {
    tx->replacement_clients.added = (qa_session_client **)staged->saved_clients->added;
    tx->replacement_clients.added_count = staged->saved_clients->added_count;
    tx->replacement_clients.removed = (qa_session_client **)staged->saved_clients->removed;
    tx->replacement_clients.removed_count = staged->saved_clients->removed_count;
  }
  tx->has_replacement_clients = true;
  (void)app; /* the stage reads only the transaction */
}

/* ================= stageHeadlessAdoption ================= */
typedef struct headless_record {
  qa_application *app;
  qa_app_prepared_startup *prepared;
  qa_app_console_routing *routing;
  qa_app_prepared_owners owners;
  qa_cvar_registry *source;
  qa_cvar_registry *movement;
} headless_record;
static qa_command_dialect headless_dialect(void *ctx) { return qa_cvar_registry_dialect(((headless_record *)ctx)->source); }
static bool headless_server(void *ctx, qa_app_console_server *out) {
  memset(out, 0, sizeof *out);
  out->cvars = ((headless_record *)ctx)->source;
  return true;
}
static qa_cvar_registry *headless_seat(void *ctx, qa_seat_id id) {
  (void)id; /* TS seat: () => source */
  return ((headless_record *)ctx)->source;
}
static qa_cvar_registry *headless_input(void *ctx, bool has_seat, qa_seat_id id) {
  (void)has_seat; (void)id; /* TS input: () => movement */
  return ((headless_record *)ctx)->movement;
}
/* (name, args, context) => { let origin = context.origin; while (origin.kind === "script") origin = origin.caller;
 * return this.queueCommand(name, args, origin.kind === "local-seat" ? origin.seat : null, context); } */
static void headless_forward(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  headless_record *record = ctx;
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  qa_seat_id seat = origin->seat;
  qa_application_queue_command_source(record->app, name, args, arg_count, origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &seat : NULL, &source);
}
static void headless_adopt(void *ctx) {
  headless_record *record = ctx;
  qa_app_prepared_startup_adopt_console_routing(record->prepared, record->routing, headless_forward, record, &record->owners);
}
static void headless_release(void *record) {
  headless_record *headless = record;
  if (headless->owners.read_ctx != NULL) qa_application_startup_script_reader_free(headless->owners.read_ctx);
}
static void stage_headless_adoption(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  /* Resolved outside the closure: the adoption handler outlives this call inside the prepared startup. */
  qa_cvar_registry *headless_source = staged->graphical != NULL || app->prepared_startup == NULL ? NULL : qa_application_source_cvars(app, next);
  tx->adopt_headless = NULL;
  qa_app_prepared_startup *prepared = app->prepared_startup;
  if (staged->graphical != NULL || prepared == NULL) return;
  qa_app_console_script_files *scripts = qa_app_source_console_scripts(tx->content, tx->options, &app->input_config);
  qa_cvar_registry *source = headless_source;
  if (source == NULL) qa_raise("Error", 0, 0, "Prepared authority requires a source registry");
  qa_command_dialect dialect = qa_app_movement_dialect(tx->options, qa_app_loaded_content_recipe(tx->content));
  qa_cvar_registry *movement = qa_app_prepared_startup_movement(prepared);
  if (qa_cvar_registry_dialect(movement) != dialect) {
    qa_cvar_registry_options options;
    memset(&options, 0, sizeof options);
    options.dialect = dialect;
    options.context = qa_cvar_registry_context(source);
    options.print = host_print; options.print_ctx = app;
    movement = qa_cvar_registry_create(&options);
    if (movement == NULL) qa_range_error("Out of memory");
  }
  headless_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, headless_release);
  record->app = app;
  record->prepared = prepared;
  record->source = source;
  record->movement = movement;
  qa_app_console_routing_options routing;
  memset(&routing, 0, sizeof routing);
  routing.ctx = record;
  routing.fallback = source;
  routing.source_dialect = headless_dialect;
  routing.server = headless_server;
  routing.seat = headless_seat;
  routing.input = headless_input;
  record->routing = qa_app_console_routing_create(routing);
  if (record->routing == NULL) qa_range_error("Out of memory");
  record->owners.source = source;
  record->owners.movement = movement;
  record->owners.fallback = source;
  record->owners.scripts = scripts;
  if (!qa_application_startup_script_reader(tx->content, tx->options, scripts, &record->owners.read, &record->owners.read_ctx))
    qa_range_error("Out of memory");
  qa_app_prepared_startup_validate_owners(prepared, &record->owners);
  tx->adopt_headless = headless_adopt;
  tx->adopt_headless_ctx = record;
}

/* ================= stageCommit ================= */
static void retire_simulation_view(void *record) { qa_application_simulation_view *view = record; if (view->has_save) qa_app_shared_sim_save_free(&view->save); }
static void stop_recording(qa_application *app) {
  qa_app_client_bootstrap *client = app->ownership.borrowed_client;
  const char *error = NULL;
  if (client != NULL) error = client->stop_recording(client->stop_recording_ctx, client->source_current);
  else if (app->owned_recording != NULL) error = qa_app_client_demo_recording_stop(app->owned_recording);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void stage_commit(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  qa_source_debug_graph *candidate_graph = SLOT(tx, candidate_graph);
  slot_read(tx->has_next_client_cvars, "nextClientCvars");
  double destination = SLOT(tx, destination_source_milliseconds);
  qa_app_shared_simulation *next = SLOT(tx, next_simulation);
  qa_application_prepared_source_commands *prepared = SLOT(tx, prepared_commands);
  qa_app_capture *next_capture = SLOT(tx, next_capture);
  slot_read(tx->has_replacement_presentations, "replacementPresentations");
  slot_read(tx->has_replacement_clients, "replacementClients");
  qa_application_previous_world *before = tx_before(tx);
  if (prepared->options == NULL) qa_raise("Error", 0, 0, "Published world requires source command bindings");
  if (app->source_commands != NULL) qa_command_buffer_validate_profile(app->source_commands, prepared->options->dialect, NULL);
  if (prepared->program != NULL) prepared->program->validate_publication(prepared->program->ctx);
  if (staged->graphical != NULL) {
    qa_app_application_input_validate_startup_adoption(staged->graphical->input);
    qa_app_application_input_validate_candidate_commands(staged->graphical->input);
  }
  if (tx->has_candidate_images) qa_app_image_client_settings_validate_publication(&tx->candidate_images);
  qa_application_simulation_view *view = checked(calloc(1, sizeof *view));
  keep_record(app, view, retire_simulation_view);
  view->simulation = next;
  tx->next_view = view;
  qa_simulation simulation = qa_application_simulation_contract(view);
  qa_session_seat_change seats = { NULL, 0, NULL, 0 };
  qa_engine_session_validate_world_replacement(app->session, simulation, tx->replacement_presentations, tx->replacement_presentation_count,
      tx->replacement_clients, seats);
  if (before->graphical != NULL && staged->graphical != NULL)
    qa_app_application_input_release_into_candidate(staged->graphical->input, before->graphical->input, true);
  qa_world_lifetime *world = NULL;
  qa_session_resource replacement = qa_engine_session_replace_world(app->session, simulation, tx->replacement_presentations,
      tx->replacement_presentation_count, tx->replacement_clients, seats, &world);
  staged->committed = true;
  qa_application_write_game_log(app, before->simulation, "ShutdownGame:\n");
  char rule[62];
  memset(rule, '-', 60); rule[60] = '\n'; rule[61] = '\0';
  qa_application_write_game_log(app, before->simulation, rule);
  app->world_simulation = next;
  /* The previous graph stays with the retired world's closures (TS GC). */
  keep_record(app, app->debug_graph, NULL);
  app->debug_graph = candidate_graph;
  app->loaded_content = tx->content;
  /* TS get captureMap reads this.content: the published lifetime row follows the new content. */
  if (app->source_lifetime.ctx == app) app->source_lifetime.capture_map = qa_application_capture_map(app);
  app->launch_options = *tx->options;
  app->bots = staged->bots;
  app->graphical = staged->graphical;
  free(app->client_cvar_seats); free(app->client_cvars);
  app->client_cvar_seats = checked(calloc(tx->next_client_cvar_count + 1, sizeof *app->client_cvar_seats));
  app->client_cvars = checked(calloc(tx->next_client_cvar_count + 1, sizeof *app->client_cvars));
  if (tx->next_client_cvar_count != 0) {
    memcpy(app->client_cvar_seats, tx->next_client_cvar_seats, tx->next_client_cvar_count * sizeof *app->client_cvar_seats);
    memcpy(app->client_cvars, tx->next_client_cvars, tx->next_client_cvar_count * sizeof *app->client_cvars);
  }
  app->client_cvar_count = app->client_cvar_capacity = tx->next_client_cvar_count;
  app->team_arena_overrides = tx->next_overrides;
  app->local_guest = staged->local_guest;
  if (tx->has_recording_transition_error) {
    stop_recording(app);
    qa_application_printf(app, "Demo recording stopped at world change: %s\n", tx->recording_transition_error.message);
  }
  if (app->network == NULL && !app->local_recording_active && !qa_app_shared_sim_q2_native(app->world_simulation, NULL, NULL))
    app->recording_host = NULL;
  if (tx->has_next_network_host && app->network == NULL) {
    if (tx->next_network_host.kind == QA_APPLICATION_NETWORK_QW) app->native_world_count++;
    qa_application_native_server_host *host = checked(calloc(1, sizeof *host));
    keep_record(app, host, NULL);
    if (staged->local_guest == NULL) *host = tx->next_network_host;
    else { host->kind = QA_APPLICATION_NETWORK_Q3; host->q3 = staged->local_guest->authority; }
    app->recording_host = host;
    if (host->kind == QA_APPLICATION_NETWORK_Q3 && staged->local_guest == NULL) {
      qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
      if (cvars == NULL || !cvar_exists(cvars, "sv_serverid")) qa_raise("Error", 0, 0, "Q3 recording replacement has no published epoch");
      host->q3.host.prepare(host->q3.host.ctx, 0, qa_cvar_registry_variable_value(cvars, "sv_serverid"), NULL, NULL);
    }
  }
  app->guest_browser = staged->guest_browser;
  app->frontend_overrides = tx->frontend_overrides;
  app->capture = next_capture;
  if (app->tools != NULL) qa_app_application_tools_before_world_change(app->tools);
  app->elapsed = destination;
  for (size_t i = 0; i < app->source_event_count; i++) qa_app_sim_presentation_event_release(&app->source_events[i]);
  free(app->source_events);
  app->source_events = NULL; app->source_event_count = 0;
  {
    /* roundPresentationEvents.reset(): the ring's records are owned event copies. */
    size_t count = 0;
    qa_app_sim_presentation_event *events = qa_event_ring_drain_all(&app->round_presentation_events, &count);
    for (size_t i = 0; i < count; i++) qa_app_sim_presentation_event_release(&events[i]);
    free(events);
    qa_event_ring_reset(&app->round_presentation_events);
  }
  app->local_snapshot_server_bit = 0;
  app->client_input_count = 0;
  qa_app_unhandled_effects_free(app->unhandled_effects, app->unhandled_effect_count);
  app->unhandled_effects = NULL;
  app->unhandled_effect_count = 0;
  for (size_t i = 0; i < app->reported_effect_gap_count; i++) free(app->reported_effect_gaps[i]);
  app->reported_effect_gap_count = 0;
  tx->replacement = replacement; tx->has_replacement = true;
  (void)world; /* C-only out parameter: the session holds the published lifetime */
}

/* ================= publishWorld ================= */
static void retire_step_record(qa_application_world_transition *tx, const char *label, const qa_caught_error *error) {
  grow((void **)&tx->retirement_errors, &tx->retirement_error_capacity, tx->retirement_error_count + 1, sizeof *tx->retirement_errors);
  (void)snprintf(tx->retirement_errors[tx->retirement_error_count].label, sizeof tx->retirement_errors[0].label, "%s", label);
  tx->retirement_errors[tx->retirement_error_count++].error = *error;
}
/* TS retireStep(tx, label, close): one retirement step; its failure is recorded instead of aborting the rest. */
static void retire_step(qa_application_world_transition *tx, const char *label, void (*close)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (!qa_try(close, ctx, &caught)) retire_step_record(tx, label, &caught);
}
static qa_audio_output_format target_output_format(void *ctx) { return qa_app_application_audio_output_format(ctx); }
static void target_select_output_format(void *ctx, qa_audio_output_format format) { qa_app_application_audio_select_output_format(ctx, format); }
static qa_app_audio_output_target audio_output_target(qa_app_application_audio *audio) {
  qa_app_audio_output_target target = { audio, target_output_format, target_select_output_format };
  return target;
}
static void unbind_view_cvars(void *ctx) { qa_app_view_settings_unbind_cvars(ctx); }
static void retire_previous_world(qa_application *app, qa_application_world_transition *tx);
static void publish_body(void *ctx) {
  qa_application_world_transition *tx = ctx;
  qa_application *app = tx->app;
  qa_application_world_replacement *staged = tx->staged;
  qa_application_prepared_source_commands *prepared = tx->prepared_commands;
  qa_application_graphical *previous = tx->before.graphical;
  qa_application_publish_source_commands(app, prepared);
  if (prepared->program != NULL) prepared->program->publish(prepared->program->ctx);
  tx->candidate_published = true;
  tx->candidate_commands = NULL;
  if (tx->candidate_action_count != 0) {
    (void)qa_event_ring_admit_all(&app->requested_commands, tx->candidate_actions, tx->candidate_action_count);
    tx->candidate_action_count = 0; /* candidateActions.splice(0): the ring took the records */
  }
  if (staged->graphical != NULL) qa_app_application_input_publish_candidate_commands(staged->graphical->input);
  qa_application_refresh_application_tools(app);
  if (tx->has_candidate_images && app->image_settings != NULL) {
    qa_cvar_registry *candidate = qa_app_image_settings_cvars(tx->candidate_images.settings);
    qa_cvar_registry *current = qa_app_image_settings_cvars(app->image_settings);
    char *before_fov = qa_application_cvar_string(candidate, "fov"), *current_fov = qa_application_cvar_string(current, "fov");
    bool fov_changed = strcmp(before_fov, current_fov) != 0;
    free(before_fov); free(current_fov);
    if (app->release_view_cvars.fn != NULL) app->release_view_cvars.fn(app->release_view_cvars.ctx);
    app->release_view_cvars.fn = NULL; app->release_view_cvars.ctx = NULL;
    qa_app_image_client_settings_publish(&tx->candidate_images);
    if (staged->graphical != NULL) {
      qa_app_application_input_publish_shared_cvars(staged->graphical->input, current);
      qa_app_application_audio_bind_volume_cvars(staged->graphical->audio, current);
      qa_app_application_audio_bind_output_cvars(staged->graphical->audio, current);
    }
    if (fov_changed) {
      char *fov = qa_application_cvar_string(current, "fov");
      qa_app_view_settings_set_field_of_view(app->view_settings, qa_js_string_to_number(fov));
      free(fov);
    }
    qa_app_view_settings_bind_cvars(app->view_settings, current);
    app->release_view_cvars.fn = unbind_view_cvars; app->release_view_cvars.ctx = app->view_settings;
  }
  if (app->ownership.borrowed_client != NULL && staged->graphical != NULL)
    qa_app_application_input_publish_client_seats(staged->graphical->input, app->ownership.borrowed_client, false);
  if (previous != NULL && staged->graphical != NULL) qa_app_application_input_transfer_platform_to(previous->input, staged->graphical->input);
  else if (staged->graphical != NULL) qa_app_application_input_adopt_startup(staged->graphical->input);
  qa_application_publish_keys(app);
  qa_app_console_script_files *published_scripts = world_replacement_take_scripts(staged);
  if (published_scripts != NULL) {
    qa_app_client_bootstrap *client = app->ownership.borrowed_client;
    if (client != NULL) {
      tx->retired_scripts = client->configuration_current.scripts;
      client->configuration_current.scripts = published_scripts;
      client->configuration_current.options = tx->options;
    } else {
      tx->retired_scripts = app->configuration_scripts;
      app->configuration_scripts = published_scripts;
    }
  }
  if (tx->has_publish_audio) qa_unified_audio_transfer_commit(tx->publish_audio);
  if (staged->graphical != NULL) qa_app_application_mod_presentations_publish_restored(staged->graphical->mod_presentations);
  if (staged->graphical != NULL && app->image_settings != NULL)
    qa_app_apply_audio_output_settings(qa_app_image_settings_cvars(app->image_settings), audio_output_target(staged->graphical->audio));
  if (app->ownership.borrowed_client != NULL && staged->graphical != NULL) {
    qa_app_client_bootstrap *client = app->ownership.borrowed_client;
    memset(&client->platform, 0, sizeof client->platform);
    client->platform.kind = QA_APP_CLIENT_INPUT_WORLD;
    client->platform.input = staged->graphical->input;
    qa_app_client_bootstrap_publish_output(client, qa_app_application_audio_engine(staged->graphical->audio));
    client->has_source_profile = true;
    client->source_profile = qa_app_loaded_content_recipe(tx->content)->map.entities;
  }
  if (tx->adopt_headless != NULL) tx->adopt_headless(tx->adopt_headless_ctx);
  if (app->has_owned_demos && app->owned_demos_service != NULL) qa_app_client_demo_commands_refresh(app->owned_demos_service);
  qa_application_enable_startup_persistence(app);
  if (staged->local_guest != NULL) {
    size_t count = 0;
    qa_application_command_request *commands = qa_event_ring_drain_all(&staged->local_guest->pending_commands, &count);
    if (count != 0) (void)qa_event_ring_admit_all(&app->requested_commands, commands, count);
    free(commands);
  }
  if (tx->options->dedicated) app->dedicated_commands = app->source_commands;
  /* The scoreboard refresh is a post-publication notification: its failure is reported to the caller without
   * discarding the successfully published world. */
  if (tx->save != NULL) {
    qa_caught_error caught;
    if (!qa_try((void (*)(void *))qa_application_request_restored_scores, app, &caught)) {
      tx->post_publication_error = caught;
      tx->has_post_publication_error = true;
    }
  }
}
static void close_retired_scripts(void *ctx) {
  qa_application_world_transition *tx = ctx;
  if (tx->retired_scripts != NULL) qa_app_console_script_files_close(tx->retired_scripts);
}
static void close_unpublished_scripts(void *ctx) {
  qa_application_world_transition *tx = ctx;
  if (tx->staged->scripts != NULL) qa_app_console_script_files_close(tx->staged->scripts);
}
static void publish_world(qa_application *app, qa_application_world_transition *tx) {
  slot_read(tx->has_staged, "staged");
  slot_read(tx->has_candidate_actions, "candidateActions");
  slot_read(tx->has_prepared_commands, "preparedCommands");
  qa_app_capture *next_capture = SLOT(tx, next_capture);
  (void)tx_before(tx);
  qa_caught_error caught;
  bool ok = qa_try(publish_body, tx, &caught);
  /* finally */
  if (!tx->has_replacement) qa_raise("Error", 0, 0, "The world was not committed before its previous world was retired");
  retire_previous_world(app, tx);
  retire_step(tx, "configuration retirement", close_retired_scripts, tx);
  retire_step(tx, "unpublished configuration retirement", close_unpublished_scripts, tx);
  if (app->ownership.borrowed_client == NULL && next_capture != NULL) qa_app_capture_activate(next_capture);
  if (!ok) qa_rethrow(&caught);
}

/* ================= finishPublication ================= */
static void start_world_music(void *ctx) { qa_app_application_audio_start_world_music(ctx); }
typedef struct network_change { qa_application *app; qa_application_native_server_host host; } network_change;
static void change_network(void *ctx) {
  network_change *change = ctx;
  qa_application_change_network_world(change->app, &change->host);
}
static void finish_publication(qa_application *app, qa_application_world_transition *tx) {
  qa_application_world_replacement *staged = SLOT(tx, staged);
  slot_read(tx->has_guest_network_commands, "guestNetworkCommands");
  slot_read(tx->has_guest_rejected_clients, "guestRejectedClients");
  slot_read(tx->has_next_unified_host, "nextUnifiedHost");
  if (staged->graphical != NULL) {
    app->has_frontend_baseline = qa_app_read_frontend_preferences(staged->graphical->input, staged->graphical->audio, &app->frontend_baseline);
    if (staged->graphical->q3_count == 0) retire_step(tx, "world music", start_world_music, staged->graphical->audio);
  }
  if (tx->has_next_network_host && app->network != NULL) {
    if (!staged->has_guest_transition) {
      network_change change = { app, tx->next_network_host };
      retire_step(tx, "network publication", change_network, &change);
    } else {
      if (app->network->kind != QA_APPLICATION_NETWORK_Q3 || tx->next_network_host.kind != QA_APPLICATION_NETWORK_Q3)
        qa_raise("Error", 0, 0, "Guest map publication requires the Q3 network owner");
      qa_application_native_server_host *host = checked(calloc(1, sizeof *host));
      keep_record(app, host, NULL);
      *host = tx->next_network_host;
      app->recording_host = host;
      qa_app_q3_server_network_change_world(app->network->q3, host->q3.host, tx->guest_rejected_clients, tx->guest_rejected_client_count);
      tx->guest_network_published = true;
      size_t count = 0;
      guest_network_command *commands = qa_event_ring_drain_all(&tx->guest_network_commands, &count);
      qa_app_q3_guest_output output = qa_application_guest_output(app, NULL, true);
      for (size_t i = 0; i < count; i++) {
        output.send_server_command(output.ctx, commands[i].slot, commands[i].text);
        free(commands[i].text);
      }
      free(commands);
    }
  }
  if (tx->unified_host_present) {
    if (app->network == NULL || app->network->kind != QA_APPLICATION_NETWORK_UNIFIED) qa_raise("Error", 0, 0, "Mixed-game travel lost its retained server");
    qa_app_unified_server_network_change_world(app->network->unified, tx->next_unified_host, tx->next_unified_composition);
  }
  if (tx->skirmish != NULL) qa_application_start_team_arena_skirmish(app, tx->skirmish);
  for (size_t i = 0; i < tx->retirement_error_count; i++) {
    char text[QA_CAUGHT_MESSAGE_CAPACITY + 64];
    error_text(&tx->retirement_errors[i].error, text, sizeof text);
    qa_application_printf(app, "Entered world; %s failed: %s\n", tx->retirement_errors[i].label, text);
  }
  qa_application_ranking_frame(app);
  qa_application_printf(app, "Entered %s.\n", qa_app_loaded_content_recipe(tx->content)->map.geometry.requested_path);
}

/* ================= retirePreviousWorld ================= */
typedef struct retire_ctx { qa_application *app; qa_application_world_transition *tx; size_t index; } retire_ctx;
static void rp_rankings(void *ctx) { retire_ctx *r = ctx; qa_application_close_rankings(r->app, r->tx->before.simulation); }
static void rp_source_archive(void *ctx) {
  retire_ctx *r = ctx;
  qa_application_save_source_archive(r->app, &r->tx->before.options, r->tx->before.content, r->tx->before.simulation, r->tx->before.overrides);
}
static void rp_client_archives(void *ctx) {
  retire_ctx *r = ctx;
  qa_application_previous_world *before = &r->tx->before;
  qa_application_save_client_archives(r->app, &before->options, before->content, before->client_cvar_seats, before->client_cvars,
      before->client_cvar_count, before->overrides);
}
static void rp_capture(void *ctx) { retire_ctx *r = ctx; if (r->tx->previous_capture != NULL) qa_app_capture_destroy(r->tx->previous_capture); r->tx->previous_capture = NULL; }
static void rp_bots(void *ctx) {
  retire_ctx *r = ctx;
  qa_app_sim_bot_service *bots = r->tx->before.bots;
  if (bots != NULL) {
    bots->close(bots->ctx, r->tx->initial_source_milliseconds != 0);
    qa_application_bots_release(r->app, bots);
  }
}
static void rp_guest_seat(void *ctx) { retire_ctx *r = ctx; qa_app_local_q3_client_state_retire(r->tx->before.local_guest->seats[r->index].state); }
static void rp_guest_client(void *ctx) { retire_ctx *r = ctx; qa_app_application_q3_client_close(r->tx->before.graphical->q3[r->index].client); }
static void rp_mod_presentations(void *ctx) {
  retire_ctx *r = ctx;
  if (r->tx->before.graphical != NULL) qa_app_application_mod_presentations_close(r->tx->before.graphical->mod_presentations);
}
static void rp_selected_presentations(void *ctx) {
  retire_ctx *r = ctx;
  if (r->tx->before.graphical != NULL) qa_app_selected_q3_presentations_close(r->tx->before.graphical->selected_q3_presentations);
}
static void rp_guest_shutdown(void *ctx) { retire_ctx *r = ctx; qa_app_shared_sim_shutdown_q3_guest(r->tx->before.simulation); }
static void rp_world(void *ctx) { retire_ctx *r = ctx; r->tx->replacement.close(r->tx->replacement.ctx); }
static void rp_bot_disconnect(void *ctx) {
  retire_ctx *r = ctx;
  qa_engine_session_close_client(r->app->session, qa_session_client_id(r->tx->before.bot_clients[r->index].client));
}
static void rp_audio(void *ctx) { retire_ctx *r = ctx; qa_app_application_audio_close(r->tx->before.graphical->audio); }
static void rp_effects(void *ctx) { retire_ctx *r = ctx; qa_app_application_effects_close(r->tx->before.graphical->effects); }
static void rp_art(void *ctx) { retire_ctx *r = ctx; qa_native_ui_art_close(r->tx->before.graphical->art); }
static void rp_assets(void *ctx) { retire_ctx *r = ctx; qa_app_application_assets_close(r->tx->before.graphical->assets); }
static void rp_input(void *ctx) { retire_ctx *r = ctx; qa_app_application_input_close(r->tx->before.graphical->input); }
static void rp_renderer(void *ctx) {
  retire_ctx *r = ctx;
  qa_application_graphical *retiring = r->tx->before.graphical;
  size_t count = 0;
  qa_image_resource_operation *operations = qa_scene_image_registry_drain_operations(qa_app_application_assets_images(retiring->assets), &count);
  qa_render_command *commands = checked(calloc(count + 1, sizeof *commands));
  for (size_t i = 0; i < count; i++) { commands[i].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; commands[i].operation = operations[i]; }
  /* The drained array owns its update levels: it lives until the frame has executed. */
  qa_cleanup operations_cleanup, cleanup;
  qa_cleanup_push(&operations_cleanup, free, operations);
  qa_cleanup_push(&cleanup, free, commands);
  qa_render_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.owner = qa_app_native_renderer_owner(retiring->renderer);
  frame.sequence = r->app->presentation_frames;
  frame.commands = commands;
  frame.command_count = count;
  qa_app_native_renderer_execute(retiring->renderer, frame);
  qa_cleanup_pop(&cleanup);
  qa_cleanup_pop(&operations_cleanup);
  free(commands);
  free(operations);
}
static void rp_content(void *ctx) { retire_ctx *r = ctx; qa_app_loaded_content_close(r->tx->before.content); }
static void retire_previous_world(qa_application *app, qa_application_world_transition *tx) {
  qa_application_previous_world *before = tx_before(tx);
  retire_ctx r = { app, tx, 0 };
  retire_step(tx, "ranking retirement", rp_rankings, &r);
  bool pending = app->prepared_startup != NULL && qa_app_prepared_startup_pending(app->prepared_startup);
  if (app->archive_persistence && !pending && !tx->same_source_owner) retire_step(tx, "previous source archive", rp_source_archive, &r);
  if (app->archive_persistence && !pending && !tx->same_client_owner) retire_step(tx, "previous client archives", rp_client_archives, &r);
  if (app->ownership.borrowed_client == NULL) retire_step(tx, "capture retirement", rp_capture, &r);
  retire_step(tx, "bot retirement", rp_bots, &r);
  for (r.index = 0; before->local_guest != NULL && r.index < before->local_guest->seat_count; r.index++)
    retire_step(tx, "guest seat retirement", rp_guest_seat, &r);
  if (before->local_guest != NULL) before->local_guest->seat_count = 0;
  for (r.index = 0; before->graphical != NULL && r.index < before->graphical->q3_count; r.index++)
    if (before->graphical->q3[r.index].qvm) retire_step(tx, "guest client retirement", rp_guest_client, &r);
  retire_step(tx, "component presentation retirement", rp_mod_presentations, &r);
  retire_step(tx, "selected source presentation retirement", rp_selected_presentations, &r);
  retire_step(tx, "guest shutdown", rp_guest_shutdown, &r);
  retire_step(tx, "world retirement", rp_world, &r);
  if (tx->save == NULL && !tx->preserve_bots)
    for (r.index = 0; r.index < before->bot_client_count; r.index++) retire_step(tx, "bot disconnect", rp_bot_disconnect, &r);
  if (before->graphical != NULL) {
    retire_step(tx, "audio retirement", rp_audio, &r);
    retire_step(tx, "effects retirement", rp_effects, &r);
    retire_step(tx, "art retirement", rp_art, &r);
    retire_step(tx, "asset retirement", rp_assets, &r);
    retire_step(tx, "input retirement", rp_input, &r);
    retire_step(tx, "renderer resource retirement", rp_renderer, &r);
  }
  retire_step(tx, "content retirement", rp_content, &r);
}

/* ================= changeLevel(map, spawnPoint = "") ================= */
void qa_application_change_level(qa_application *app, const char *map, const char *spawn_point) {
  if (spawn_point == NULL) spawn_point = "";
  if (app->closed || app->stepping) qa_raise("Error", 0, 0, "World travel requires an idle open application");
  bool native = qa_app_shared_sim_q2_native(app->world_simulation, NULL, NULL);
  bool campaign = qa_app_shared_sim_q2_source(app->world_simulation) != NULL && app->network == NULL
      && strcmp(app->launch_options.network_kind, "offline") == 0
      && (strcmp(app->launch_options.mode, "singleplayer") == 0 || strcmp(app->launch_options.mode, "coop") == 0);
  if (native || campaign) {
    qa_app_q2_travel_target target;
    memset(&target, 0, sizeof target);
    target.kind = "map";
    target.name = (char *)map;
    target.spawn_point = (char *)spawn_point;
    qa_app_simulation_travel carry;
    if (!native) carry = qa_app_shared_sim_capture_travel(app->world_simulation, spawn_point);
    qa_application_advance_q2_travel(app, &target, native ? NULL : &carry);
    return;
  }
  qa_app_simulation_travel carry = qa_app_shared_sim_capture_travel(app->world_simulation, spawn_point);
  qa_application_replace_world(app, map, &carry);
}

/* ================= restoreComponentClients(image, graphical, simulation) ================= */
static qa_actor_id reference_saved(void *ctx, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(qa_app_shared_sim_actors(ctx), saved, "checkpoint");
}
void qa_application_restore_component_clients(qa_application *app, const qa_save_image *image, qa_application_graphical *graphical,
    qa_app_shared_simulation *simulation) {
  qa_save_value *checkpoint = qa_app_read_component_clients(image);
  size_t source_count = 0;
  const qa_active_mod_presentation *sources = qa_app_shared_sim_mod_presentation_sources(simulation, &source_count);
  if (checkpoint == NULL) {
    if (source_count != 0)
      qa_application_print(app, "This save predates component client continuation; original component clients will initialize from the restored world.\n");
    return;
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, (void (*)(void *))qa_save_value_free, checkpoint);
  qa_app_viewing_seat *seats = checked(calloc(graphical->presentation_count + 1, sizeof *seats));
  qa_cleanup seats_cleanup;
  qa_cleanup_push(&seats_cleanup, free, seats);
  for (size_t i = 0; i < graphical->presentation_count; i++) seats[i] = qa_app_world_seat_presentation_viewing_seat(graphical->presentations[i].presentation);
  const qa_active_mod_presentation **pointers = checked(calloc(source_count + 1, sizeof *pointers));
  qa_cleanup pointers_cleanup;
  qa_cleanup_push(&pointers_cleanup, free, (void *)pointers);
  for (size_t i = 0; i < source_count; i++) pointers[i] = &sources[i];
  qa_app_application_mod_presentations_restore_checkpoint(graphical->mod_presentations, checkpoint, seats, graphical->presentation_count,
      pointers, source_count, reference_saved, simulation);
  qa_cleanup_pop(&pointers_cleanup);
  free((void *)pointers);
  qa_cleanup_pop(&seats_cleanup);
  free(seats);
  qa_cleanup_pop(&cleanup);
  qa_save_value_free(checkpoint);
}

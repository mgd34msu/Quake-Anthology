/* Port of src/app/bootstrap/q3-client.ts. The typescript/QVM
 * cgame backends (q3-client/ subdir slices) ride the host below;
 * lifecycle guards, cvar-owner identity, restart gating, event
 * routing checks and body-pose bookkeeping are ported here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/core/cvars/index.h"
/* TS ApplicationQ3LocalRound (fields read by this file). */
typedef struct qa_app_q3_local_round {
  const void *movement_timing; /* borrowed identity */
} qa_app_q3_local_round;
/* TS ApplicationQ3ClientSource. */
typedef struct qa_app_q3_client_source {
  void *ctx;
  size_t (*game_state)(void *ctx, const char *const **strings);
  /* Returns false for the TS null. */
  bool (*server_command)(void *ctx, double sequence, const char *const **strings, size_t *out_count);
  qa_actor_id (*actor_at)(void *ctx, double number);
} qa_app_q3_client_source;
/* Backend game (typescript Q3ClientPresentation or QVM game). */
typedef struct qa_app_q3_client_game {
  void *ctx;
  bool (*command)(void *ctx, const char *const *argv, size_t argc);
  bool (*handles)(void *ctx, const char *name);      /* typescript console */
  bool (*captures_input)(void *ctx);                  /* qvm */
  const char *(*key_event)(void *ctx, double key, bool down);
  const char *(*mouse_event)(void *ctx, double x, double y);
  const char *(*event_handling)(void *ctx, double type);
  const char *(*shutdown)(void *ctx);                 /* qvm */
  bool shared_equipment_hud;                          /* qvm field */
  const void *(*weapon_hud)(void *ctx);               /* typescript state getter adjunct */
  bool (*weapon_view)(void *ctx, bool *visible, bool *aggregate_warning);
  const char *(*consume_weapon_selection)(void *ctx, bool *has_value, double *out);
} qa_app_q3_client_game;
typedef struct qa_app_q3_client_options {
  const char *kind; /* "typescript"|"qvm"|... */
  qa_cvar_registry *cvars;          /* cvarOwner at construction */
  qa_cvar_registry *time_cvars;     /* NULL = TS undefined */
  qa_seat_id seat;
  const qa_app_q3_client_source *local_source; /* NULL = remote */
  const char *input_kind;           /* movement input kind */
} qa_app_q3_client_options;
typedef struct qa_app_q3_client_host {
  void *ctx;
  qa_app_q3_client_options options;
  qa_app_q3_client_game game;
  const char *(*initialize)(void *ctx);
  const char *(*prepare)(void *ctx, double frame_number);
  const char *(*frame)(void *ctx);
  const char *(*close)(void *ctx);
  const char *(*server_setting)(void *ctx, double index);
} qa_app_q3_client_host;
typedef struct qa_app_application_q3_client qa_app_application_q3_client;
/* Aborts with the TS Error text for foreign movement / missing
 * local snapshot source. initialize runs synchronously. */
qa_app_application_q3_client *qa_app_application_q3_client_create(qa_app_q3_client_host host);
qa_cvar_registry *qa_app_application_q3_client_cvars(const qa_app_application_q3_client *client);
qa_cvar_registry *qa_app_application_q3_client_time_cvars(const qa_app_application_q3_client *client);
/* Aborts when the owner identity changed (TS Error). */
void qa_app_application_q3_client_adopt_cvars(qa_app_application_q3_client *client, qa_cvar_registry *cvars);
double qa_app_application_q3_client_presented_events(const qa_app_application_q3_client *client);
void qa_app_application_q3_client_assert_can_restart_round(qa_app_application_q3_client *client);
void qa_app_application_q3_client_rebind_round(qa_app_application_q3_client *client, qa_app_q3_local_round binding);
/* Aborts for remote seats (TS Error texts). */
void qa_app_application_q3_client_receive_events(qa_app_application_q3_client *client, const void *events, size_t count);
void qa_app_application_q3_client_receive(qa_app_application_q3_client *client, const void *events, size_t count);
bool qa_app_application_q3_client_command(qa_app_application_q3_client *client, const char *const *argv, size_t argc);
bool qa_app_application_q3_client_handles_command(qa_app_application_q3_client *client, const char *name);
void qa_app_application_q3_client_key_event(qa_app_application_q3_client *client, double key, bool down);
void qa_app_application_q3_client_mouse_event(qa_app_application_q3_client *client, double x, double y);
/* Aborts (RangeError) outside 0..3 integral modes. */
void qa_app_application_q3_client_event_handling(qa_app_application_q3_client *client, double type);
bool qa_app_application_q3_client_captures_input(const qa_app_application_q3_client *client);
bool qa_app_application_q3_client_weapon_hud_view(qa_app_application_q3_client *client, bool *visible, bool *aggregate_warning);
bool qa_app_application_q3_client_consume_weapon_selection(qa_app_application_q3_client *client, double *out);
qa_scene_camera qa_app_application_q3_client_camera(const qa_app_application_q3_client *client);
void qa_app_application_q3_client_set_camera(qa_app_application_q3_client *client, qa_scene_camera camera);
bool qa_app_application_q3_client_body_pose(const qa_app_application_q3_client *client,
    qa_actor_id actor, qa_vec3 *origin_out, qa_vec3 *angles_out);
void qa_app_application_q3_client_set_body_pose(qa_app_application_q3_client *client,
    qa_actor_id actor, qa_vec3 origin, qa_vec3 angles);
/* Aborts for another seat's input (TS Error). */
void qa_app_application_q3_client_assert_seat(qa_app_application_q3_client *client, qa_seat_id seat);
void qa_app_application_q3_client_refresh_system_info(qa_app_application_q3_client *client);
const char *qa_app_application_q3_client_prepare(qa_app_application_q3_client *client, double frame_number);
const char *qa_app_application_q3_client_frame(qa_app_application_q3_client *client);
void qa_app_application_q3_client_shutdown(qa_app_application_q3_client *client);
void qa_app_application_q3_client_close(qa_app_application_q3_client *client);
#endif

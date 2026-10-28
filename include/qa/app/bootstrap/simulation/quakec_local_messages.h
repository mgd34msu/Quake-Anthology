/* Port of src/app/bootstrap/simulation/quakec-local-messages.ts.
 * Retained messages are stored by value; their text pointers borrow
 * the caller's decoded messages. A mirror of this class exists in
 * include/qa/compat/qc/mod_messages.h. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_LOCAL_MESSAGES_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_LOCAL_MESSAGES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/math.h"
#include "qa/network/q1/netquake.h"
typedef struct qa_app_sim_quakec_local_messages qa_app_sim_quakec_local_messages;
qa_app_sim_quakec_local_messages *qa_app_sim_quakec_local_messages_create(void);
void qa_app_sim_quakec_local_messages_destroy(qa_app_sim_quakec_local_messages *state);
void qa_app_sim_quakec_local_messages_admit(qa_app_sim_quakec_local_messages *state, qa_actor_id actor);
bool qa_app_sim_quakec_local_messages_has_client(qa_app_sim_quakec_local_messages *state, qa_actor_id actor);
void qa_app_sim_quakec_local_messages_retire(qa_app_sim_quakec_local_messages *state, qa_actor_id actor);
/* actor NULL = baseline broadcast. view_target: has_view false is TS
 * undefined; has_actor false with has_view true is TS null target. */
void qa_app_sim_quakec_local_messages_receive(qa_app_sim_quakec_local_messages *state,
    const qa_netquake_message *messages, size_t message_count,
    const qa_actor_id *actor, bool has_view_target, const qa_actor_id *view_target);
bool qa_app_sim_quakec_local_messages_stat(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, double index, double *out);
bool qa_app_sim_quakec_local_messages_angles(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, qa_vec3 *out);
bool qa_app_sim_quakec_local_messages_view(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, double *out);
bool qa_app_sim_quakec_local_messages_view_target(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, qa_actor_id *out);
typedef struct qa_app_sim_quakec_view_entry { qa_actor_id actor, target; } qa_app_sim_quakec_view_entry;
typedef struct qa_app_sim_quakec_captured_views {
  bool has_baseline; qa_actor_id baseline;
  qa_app_sim_quakec_view_entry *clients; size_t client_count;
} qa_app_sim_quakec_captured_views;
qa_app_sim_quakec_captured_views qa_app_sim_quakec_local_messages_capture_views(qa_app_sim_quakec_local_messages *state);
void qa_app_sim_quakec_local_messages_restore_views(qa_app_sim_quakec_local_messages *state,
    const qa_actor_id *baseline, const qa_app_sim_quakec_view_entry *clients, size_t client_count);
typedef struct qa_app_sim_quakec_session_state {
  const char *server_vars; double views, sequence; bool level_completed, back_to_lobby;
} qa_app_sim_quakec_session_state;
qa_app_sim_quakec_session_state qa_app_sim_quakec_local_messages_session_state(qa_app_sim_quakec_local_messages *state, qa_actor_id actor);
typedef struct qa_app_sim_quakec_prompt {
  const char *kind; /* "prompt"|"clear-prompt" */
  qa_actor_id actor; const char *title;
  const char **labels; double *impulses; size_t choice_count; /* malloc'd */
} qa_app_sim_quakec_prompt;
qa_app_sim_quakec_prompt qa_app_sim_quakec_local_messages_prompt(qa_app_sim_quakec_local_messages *state, qa_actor_id actor);
void qa_app_sim_quakec_prompt_free(qa_app_sim_quakec_prompt *prompt);
bool qa_app_sim_quakec_local_messages_answer_prompt(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, double impulse);
bool qa_app_sim_quakec_local_messages_intermission(qa_app_sim_quakec_local_messages *state, qa_actor_id actor);
/* Borrowed scratch owned by state. */
const qa_netquake_message *qa_app_sim_quakec_local_messages_presentation(qa_app_sim_quakec_local_messages *state, qa_actor_id actor, size_t *out_count);
typedef struct qa_app_sim_quakec_captured_client { qa_actor_id actor; const qa_netquake_message *messages; size_t message_count; } qa_app_sim_quakec_captured_client;
typedef struct qa_app_sim_quakec_capture {
  qa_netquake_message *baseline; size_t baseline_count;
  qa_app_sim_quakec_captured_client *clients; size_t client_count;
  qa_netquake_message *storage; /* owned backing for client messages */
} qa_app_sim_quakec_capture;
qa_app_sim_quakec_capture qa_app_sim_quakec_local_messages_capture(qa_app_sim_quakec_local_messages *state);
void qa_app_sim_quakec_capture_free(qa_app_sim_quakec_capture *capture);
void qa_app_sim_quakec_local_messages_restore(qa_app_sim_quakec_local_messages *state,
    const qa_netquake_message *baseline, size_t baseline_count,
    const qa_app_sim_quakec_captured_client *clients, size_t client_count);

typedef struct qa_app_sim_quakec_local_view_source {
  void *ctx;
  bool (*read)(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
  qa_vec3 (*offset)(void *ctx, qa_actor_id actor);
} qa_app_sim_quakec_local_view_source;
bool qa_app_sim_quakec_local_view(qa_actor_id actor, qa_app_sim_quakec_local_messages *state,
    qa_app_sim_quakec_local_view_source source, qa_app_sim_player_view *out);

/* Event payloads are carried as qa_app_sim_presentation_event views
 * (kind/event_kind + flat fields) for emit/message callbacks. */
typedef struct qa_app_sim_quakec_local_message_host {
  void *ctx;
  qa_actor_id (*actor)(void *ctx, double slot);
  const qa_actor_id *recipients; size_t recipient_count;
  qa_actor_id source_actor;
  const char *map; double seconds;
  void (*camera)(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
  const char *(*sound)(void *ctx, double index);
  const char *(*model)(void *ctx, double index);
  void (*emit)(void *ctx, qa_app_sim_presentation_event event, const qa_actor_id *recipient);
  void (*message)(void *ctx, qa_app_sim_presentation_event event, const qa_actor_id *actor);
  void (*music)(void *ctx, double track);
  void (*angles)(void *ctx, qa_actor_id actor, qa_vec3 angles);
  void (*pause)(void *ctx, bool paused);
  void (*sky)(void *ctx, const char *name, const qa_actor_id *recipient);
  void (*client_metadata)(void *ctx, qa_netquake_message event, const qa_actor_id *recipient);
  void (*session)(void *ctx, const char *kind, const qa_actor_id *recipient);
  void (*prompt)(void *ctx, qa_app_sim_quakec_prompt value);
  void (*fog)(void *ctx, qa_netquake_message value, const qa_actor_id *recipient);
} qa_app_sim_quakec_local_message_host;
void qa_app_sim_present_quakec_local_message(qa_netquake_message message, const qa_actor_id *target,
    qa_app_sim_quakec_local_messages *state, qa_app_sim_quakec_local_message_host host);
#endif

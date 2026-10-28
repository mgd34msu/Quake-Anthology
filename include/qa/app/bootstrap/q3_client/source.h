/* Port of src/app/bootstrap/q3-client/source.ts.
 * Q3SourcePresentationState (TS owner:
 * app/bootstrap/simulation/q3/presentation.ts, sibling slice) is
 * mirrored below; SimulationPresentationEvent rows are the subset
 * this file reads. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_SOURCE_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/visibility.h"
/* TS Q3SourcePresentationState (mirror; see header note). */
typedef struct qa_app_q3_source_entity_row {
  qa_actor_id actor;
  const qa_q3_entity_state_record *state;
  qa_vec3 origin;
  bool linked;
  double server_flags;
  double single_client;
} qa_app_q3_source_entity_row;
typedef struct qa_app_q3_source_client_row {
  qa_actor_id actor;
  double slot;
  const qa_q3_base_player_state_record *state;
} qa_app_q3_source_client_row;
typedef struct qa_app_q3_source_configstring { double index; const char *value; } qa_app_q3_source_configstring;
typedef struct qa_app_q3_source_presentation_state_mirror {
  qa_q3_base_product product;
  double time;
  const qa_app_q3_source_entity_row *entities; size_t entity_count;
  const qa_app_q3_source_client_row *clients; size_t client_count;
  const qa_app_q3_source_configstring *configstrings; size_t configstring_count;
} qa_app_q3_source_presentation_state_mirror;
/* TS SimulationPresentationEvent subset read by receiveEvents:
 * kind "q3-source" with a "server-command" payload. */
typedef struct qa_app_q3_source_presentation_event {
  double sequence;
  const char *kind;       /* "q3-source" | ... */
  const char *event_kind; /* "server-command" | ... */
  double client;          /* server-command */
  const char *text;       /* server-command */
} qa_app_q3_source_presentation_event;
/* TS ApplicationQ3Source. */
typedef struct qa_app_application_q3_source qa_app_application_q3_source;
typedef struct qa_app_q3_source_callbacks {
  void *ctx;
  /* select(player, source): fills *out (caller-destroyed visible set
   * storage is callback-owned until the next call). */
  void (*select)(void *ctx, const qa_q3_player_state_record *player,
                 const qa_app_q3_source_presentation_state_mirror *source, qa_q3_visible_entities *out);
  bool (*source_actor)(void *ctx, double number, qa_actor_id *out); /* false = null */
  /* predictionCommand?: NULL = TS undefined. */
  qa_q3_base_user_command (*prediction_command)(void *ctx, qa_actor_command command, double source_time_ms);
  void (*level_shot)(void *ctx); /* NULL = TS undefined */
} qa_app_q3_source_callbacks;
qa_app_application_q3_source *qa_app_application_q3_source_create(
    qa_actor_id current_actor, const qa_app_q3_source_presentation_state_mirror *initial,
    qa_app_q3_source_callbacks callbacks);
void qa_app_application_q3_source_destroy(qa_app_application_q3_source *source);
const char *qa_app_application_q3_source_mode(const qa_app_application_q3_source *source); /* "live" */
qa_actor_id qa_app_application_q3_source_actor(const qa_app_application_q3_source *source);
double qa_app_application_q3_source_client_number(const qa_app_application_q3_source *source);
double qa_app_application_q3_source_time(const qa_app_application_q3_source *source);
qa_q3_pres_client_command_history *qa_app_application_q3_source_commands(qa_app_application_q3_source *source);
void qa_app_application_q3_source_assert_can_restart_round(qa_app_application_q3_source *source);
/* bit: TS 0 | 4. */
void qa_app_application_q3_source_begin_round_restart(qa_app_application_q3_source *source, double bit,
    const qa_app_q3_source_presentation_state_mirror *state);
void qa_app_application_q3_source_validate_round_actor(qa_app_application_q3_source *source, qa_actor_id actor,
    const qa_app_q3_source_presentation_state_mirror *state);
void qa_app_application_q3_source_rebind_round(qa_app_application_q3_source *source, qa_actor_id actor,
    const qa_app_q3_source_presentation_state_mirror *state);
/* SnapshotSource surface. */
void qa_app_application_q3_source_current(const qa_app_application_q3_source *source,
                                          double *out_number, double *out_server_time);
/* read(): false is the TS null; *out borrows the stored snapshot. */
bool qa_app_application_q3_source_read(qa_app_application_q3_source *source, double number,
                                       const qa_q3_wire_snapshot **out);
/* getGameState(): 1024 borrowed strings. */
const char *const *qa_app_application_q3_source_game_state(qa_app_application_q3_source *source, size_t *out_count);
/* getServerCommand(): false is the TS null; argv borrowed. */
bool qa_app_application_q3_source_get_server_command(qa_app_application_q3_source *source, double sequence,
                                                     const char *const **argv_out, size_t *argc_out);
qa_actor_id qa_app_application_q3_source_actor_at(qa_app_application_q3_source *source, double number);
bool qa_app_application_q3_source_sound_actor(qa_app_application_q3_source *source, double number, qa_actor_id *out);
void qa_app_application_q3_source_receive_events(qa_app_application_q3_source *source,
    const qa_app_q3_source_presentation_event *events, size_t event_count);
void qa_app_application_q3_source_receive(qa_app_application_q3_source *source,
    const qa_app_q3_source_presentation_state_mirror *state,
    const qa_app_q3_source_presentation_event *events, size_t event_count,
    const qa_actor_command *commands, size_t command_count);
#endif

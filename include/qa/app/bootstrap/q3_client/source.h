/* Port of src/app/bootstrap/q3-client/source.ts. Q3SourcePresentationState
 * is the simulation/q3/presentation.h type; SimulationPresentationEvent is
 * simulation/types.h. GPL-2.0-or-later. */
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
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
/* TS ApplicationQ3Source. */
typedef struct qa_app_application_q3_source qa_app_application_q3_source;
typedef struct qa_app_q3_source_callbacks {
  void *ctx;
  /* select(player, source): fills *out (caller-destroyed visible set
   * storage is callback-owned until the next call). */
  void (*select)(void *ctx, const qa_q3_player_state_record *player,
                 const qa_app_q3_source_presentation_state *source, qa_q3_visible_entities *out);
  bool (*source_actor)(void *ctx, double number, qa_actor_id *out); /* false = null */
  /* predictionCommand?: NULL = TS undefined. */
  qa_q3_base_user_command (*prediction_command)(void *ctx, qa_actor_command command, double source_time_ms);
  void (*level_shot)(void *ctx); /* NULL = TS undefined */
} qa_app_q3_source_callbacks;
qa_app_application_q3_source *qa_app_application_q3_source_create(
    qa_actor_id current_actor, const qa_app_q3_source_presentation_state *initial,
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
    const qa_app_q3_source_presentation_state *state);
void qa_app_application_q3_source_validate_round_actor(qa_app_application_q3_source *source, qa_actor_id actor,
    const qa_app_q3_source_presentation_state *state);
void qa_app_application_q3_source_rebind_round(qa_app_application_q3_source *source, qa_actor_id actor,
    const qa_app_q3_source_presentation_state *state);
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
    const qa_app_sim_presentation_event *events, size_t event_count);
void qa_app_application_q3_source_receive(qa_app_application_q3_source *source,
    const qa_app_q3_source_presentation_state *state,
    const qa_app_sim_presentation_event *events, size_t event_count,
    const qa_actor_command *commands, size_t command_count);
#endif

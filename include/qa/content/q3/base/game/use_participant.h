/* Port of src/content/q3/base/game/use-participant.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_USE_PARTICIPANT_H
#define QA_CONTENT_Q3_BASE_GAME_USE_PARTICIPANT_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
typedef struct qa_q3_use_participant_services {
  void *ctx; bool (*live)(void *ctx, qa_actor_id actor); bool (*is_player)(void *ctx, qa_actor_id actor);
  qa_q3_game_entity *(*native)(void *ctx, qa_actor_id actor);
  void (*event)(void *ctx, qa_actor_id actor, double event, double parameter);
} qa_q3_use_participant_services;
qa_actor_id qa_q3_use_actor(qa_q3_use_participant participant);
qa_q3_use_participant qa_q3_require_use_participant(const qa_q3_use_participant *participant);
/* participant NULL mirrors TS null (returns NULL); services NULL mirrors TS undefined. */
qa_q3_game_entity *qa_q3_use_client(const qa_q3_use_participant *participant, const qa_q3_use_participant_services *services);
#endif

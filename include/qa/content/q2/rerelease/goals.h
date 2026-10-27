/* Port of src/content/q2/rerelease/goals.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_GOALS_H
#define QA_CONTENT_Q2_RERELEASE_GOALS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/types.h"

typedef struct qa_q2_rerelease_goals_checkpoint { const char *goals; double goal_number; } qa_q2_rerelease_goals_checkpoint;
struct qa_q2_rerelease_players;
typedef struct qa_q2_rerelease_goals {
  struct qa_q2_rerelease_players *players;
  qa_q2_rerelease_campaign_state *campaign;
  qa_q2_rerelease_hooks hooks;
  qa_q2_use set_poi;
  char *goals; /* owned, NULL = TS null */
  double goal_number;
  qa_q2_use help_use, goal_use;
  qa_q2_think verify_target;
} qa_q2_rerelease_goals;
void qa_q2_rerelease_goals_init(qa_q2_rerelease_goals *g, struct qa_q2_rerelease_players *p,
                                qa_q2_rerelease_campaign_state *c, qa_q2_rerelease_hooks hooks, qa_q2_use set_poi);
void qa_q2_rerelease_goals_destroy(qa_q2_rerelease_goals *g);
bool qa_q2_rerelease_goals_spawn(qa_q2_rerelease_goals *g, qa_q2_entity *e, qa_q2_game_services *game);
void qa_q2_rerelease_goals_notify(qa_q2_rerelease_goals *g, qa_q2_entity *e, qa_q2_game_services *game);
void qa_q2_rerelease_goals_end_player_frame(qa_q2_rerelease_goals *g, qa_q2_entity *e, qa_q2_game_services *game);
void qa_q2_rerelease_goals_help(qa_q2_rerelease_goals *g, qa_q2_entity *e, qa_q2_game_services *game);
qa_q2_rerelease_goals_checkpoint qa_q2_rerelease_goals_capture(qa_q2_rerelease_goals *g);
void qa_q2_rerelease_goals_restore(qa_q2_rerelease_goals *g, qa_q2_rerelease_goals_checkpoint cp);
qa_q2_callback_definitions qa_q2_rerelease_goals_callbacks(qa_q2_rerelease_goals *g);
qa_q2_spawn_module qa_q2_rerelease_goals_as_spawn(qa_q2_rerelease_goals *g);
#endif

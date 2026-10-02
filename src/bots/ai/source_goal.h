#ifndef QA_BOT_SOURCE_GOAL_H
#define QA_BOT_SOURCE_GOAL_H

#include "qa/bot_goals.h"

struct qa_bots;
struct bot_ai_state;
bool bot_ai_source_long_term_goal(struct qa_bots *, struct bot_ai_state *, bool retreat,
                                   qa_bot_goal *, bool *found, qa_error *);
bool bot_ai_source_wants_camp(struct qa_bots *, struct bot_ai_state *, bool *, qa_error *);
bool bot_ai_source_roam_goal(struct qa_bots *, struct bot_ai_state *, qa_vec3 *, qa_error *);
bool bot_ai_source_entity_visible(struct qa_bots *, struct bot_ai_state *, int32_t source_entity,
                                  float *, qa_error *);

/* Existing decision-stack owners supply these operations without another goal
 * stack, movement owner, or item chooser. */
bool bot_ai_source_item_goal(struct qa_bots *, struct bot_ai_state *, qa_bot_goal *, bool *,
                              qa_error *);
bool bot_ai_source_reached_goal(struct qa_bots *, struct bot_ai_state *, const qa_bot_goal *,
                                 bool *, qa_error *);
bool bot_ai_source_go_for_air(struct qa_bots *, struct bot_ai_state *, const qa_bot_goal *,
                              float range, bool *, qa_error *);

#endif

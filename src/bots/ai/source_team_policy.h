#ifndef QA_BOT_SOURCE_TEAM_POLICY_H
#define QA_BOT_SOURCE_TEAM_POLICY_H

#include "qa/bot_goals.h"
#include "qa/bot_navigation.h"

enum { BOT_SOURCE_ALTERNATE_ROUTES = 32 };
typedef struct bot_source_team_policy_globals {
    int32_t num_team_mates_maxclients, sort_team_mates_maxclients, team_orders_maxclients;
    bool routes_setup;
    size_t red_route_count, blue_route_count;
    qa_bot_alternative_goal red_routes[BOT_SOURCE_ALTERNATE_ROUTES];
    qa_bot_alternative_goal blue_routes[BOT_SOURCE_ALTERNATE_ROUTES];
} bot_source_team_policy_globals;

struct qa_bots;
struct bot_ai_state;
bool bot_ai_source_team_policy(struct qa_bots *, struct bot_ai_state *, qa_error *);
bool bot_ai_source_team_goals(struct qa_bots *, struct bot_ai_state *, bool retreat, qa_error *);
bool bot_ai_source_set_last_order(struct qa_bots *, struct bot_ai_state *, bool *, qa_error *);
bool bot_ai_source_routes_setup(struct qa_bots *, bool team_arena, qa_error *);
bool bot_ai_source_alternate_route(struct qa_bots *, struct bot_ai_state *, int32_t base, qa_error *);
bool bot_ai_source_route_goal(struct qa_bots *, struct bot_ai_state *, qa_bot_goal *, qa_error *);
bool bot_ai_source_flag_carrier(struct qa_bots *, struct bot_ai_state *, bool teammate,
                                bool visible, bool cubes, int32_t *, qa_error *);
bool bot_ai_source_go_harvest(struct qa_bots *, struct bot_ai_state *, qa_error *);
bool bot_ai_source_task_preference(struct qa_bots *, struct bot_ai_state *,
                                    const int32_t *old_inventory, qa_error *);
bool bot_ai_source_print_team_goal(struct qa_bots *, struct bot_ai_state *, qa_error *);

#endif

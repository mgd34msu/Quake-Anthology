#ifndef QA_BOT_SOURCE_ORDERS_H
#define QA_BOT_SOURCE_ORDERS_H

#include "qa/bot_goals.h"
#include "qa/bot_chat.h"

enum { BOT_SOURCE_WAYPOINTS = 128 };
typedef struct bot_source_waypoint {
    bool inuse;
    char name[32];
    qa_bot_goal goal;
    int32_t next, prev;
} bot_source_waypoint;
typedef struct bot_source_orders_state {
    bot_source_waypoint points[BOT_SOURCE_WAYPOINTS];
    int32_t free_point;
    int32_t find_client_maxclients, find_enemy_maxclients, same_team_maxclients;
    int32_t client_name_maxclients, team_name_maxclients;
} bot_source_orders_state;
typedef struct bot_source_order_state {
    int32_t checkpoints, patrol_points, current_patrol_point;
} bot_source_order_state;

struct qa_bots;
struct bot_ai_state;
void bot_ai_source_orders_init(bot_source_orders_state *);
void bot_ai_source_order_init(bot_source_order_state *);
void bot_ai_source_order_clear(struct qa_bots *, struct bot_ai_state *);
bool bot_ai_source_order_message(struct qa_bots *, struct bot_ai_state *, const char *,
                                  bool *matched, qa_error *);
bool bot_ai_source_team(struct qa_bots *, int32_t client, int32_t *, qa_error *);
bool bot_ai_source_same_team(struct qa_bots *, struct bot_ai_state *, int32_t client,
                            bool *, qa_error *);
bool bot_ai_source_client_from_name(struct qa_bots *, const char *, int32_t *, qa_error *);
bool bot_ai_source_initial_chat(struct qa_bots *, struct bot_ai_state *, const char *type,
                                const char *const variables[8], qa_error *);
bool bot_ai_source_synonym_context(struct qa_bots *, struct bot_ai_state *, uint32_t *, qa_error *);
bool bot_ai_source_voice(struct qa_bots *, struct bot_ai_state *, int32_t recipient,
                         const char *voice, bool only, qa_error *);
bool bot_ai_source_locate(struct qa_bots *, struct bot_ai_state *, int32_t client,
                          uint32_t goal_offset, qa_error *);
bool bot_ai_source_message_goal(struct qa_bots *, struct bot_ai_state *, const char *,
                                qa_bot_goal *, bool *, qa_error *);
/* The policy owner implements this from the retained AAS alternative routes. */
bool bot_ai_source_alternate_route(struct qa_bots *, struct bot_ai_state *, int32_t team,
                                    qa_error *);
bool bot_ai_source_print(struct qa_bots *, const char *, qa_error *);

#endif

#ifndef QA_BOT_SOURCE_SETUP_H
#define QA_BOT_SOURCE_SETUP_H

#include "qa/common.h"

typedef enum bot_source_setup_stage {
    BOT_SOURCE_SETUP_ALLOCATED, BOT_SOURCE_SETUP_CHARACTER,
    BOT_SOURCE_SETUP_SETTINGS, BOT_SOURCE_SETUP_GOAL_STATE,
    BOT_SOURCE_SETUP_ITEM_WEIGHTS, BOT_SOURCE_SETUP_WEAPON_STATE,
    BOT_SOURCE_SETUP_WEAPON_WEIGHTS, BOT_SOURCE_SETUP_CHAT_STATE,
    BOT_SOURCE_SETUP_CHAT_FILE, BOT_SOURCE_SETUP_CHAT_GENDER,
    BOT_SOURCE_SETUP_PUBLISHED, BOT_SOURCE_SETUP_MOVE_STATE,
    BOT_SOURCE_SETUP_WALKER, BOT_SOURCE_SETUP_COUNTED,
    BOT_SOURCE_SETUP_SCHEDULED, BOT_SOURCE_SETUP_INTERBRED,
    BOT_SOURCE_SETUP_SESSION
} bot_source_setup_stage;
typedef enum bot_source_setup_failure {
    BOT_SOURCE_SETUP_FAILED_AAS, BOT_SOURCE_SETUP_FAILED_CHARACTER,
    BOT_SOURCE_SETUP_FAILED_ITEM_WEIGHTS, BOT_SOURCE_SETUP_FAILED_WEAPON_WEIGHTS,
    BOT_SOURCE_SETUP_FAILED_CHAT_FILE
} bot_source_setup_failure;
typedef enum bot_source_setup_kind {
    BOT_SOURCE_SETUP_EMPTY, BOT_SOURCE_SETUP_SETTING_UP,
    BOT_SOURCE_SETUP_FAILED, BOT_SOURCE_SETUP_COMPLETE
} bot_source_setup_kind;
typedef struct bot_source_setup_progress {
    bot_source_setup_kind kind;
    union {
        bot_source_setup_stage stage;
        struct { bot_source_setup_failure stage; int32_t error; } failure;
    } value;
} bot_source_setup_progress;
typedef struct bot_source_setup_state {
    char team[144];
    bool map_restart;
    bot_source_setup_progress progress;
} bot_source_setup_state;

struct qa_bots;
struct bot_ai_state;
void bot_ai_source_setup_init(bot_source_setup_state *);
void bot_ai_source_setup_team(bot_source_setup_state *, const char *);
void bot_ai_source_setup_stage(bot_source_setup_state *, bot_source_setup_stage);
void bot_ai_source_setup_failed(bot_source_setup_state *, bot_source_setup_failure, int32_t);
bool bot_ai_source_setup_gender(struct qa_bots *, struct bot_ai_state *, qa_error *);
/* The caller publishes the actual source slot, inuse, setupCount and enter time
 * before this helper. Published failures retain that actual state and stage. */
bool bot_ai_source_setup_published(struct qa_bots *, struct bot_ai_state *,
                                    bool restart, bool interbreed, qa_error *);
/* ready is false only while the source four-think setup delay remains. */
bool bot_ai_source_setup_frame(struct qa_bots *, struct bot_ai_state *, bool *ready, qa_error *);

#endif

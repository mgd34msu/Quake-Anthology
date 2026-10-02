#ifndef QA_BOT_SOURCE_CHAT_H
#define QA_BOT_SOURCE_CHAT_H

#include "qa/bot_chat.h"

typedef struct bot_source_chat_globals {
    int32_t active_maxclients, first_maxclients, last_maxclients;
    int32_t first_name_maxclients, last_name_maxclients, opponent_maxclients;
} bot_source_chat_globals;

struct qa_bots;
struct bot_ai_state;
void bot_ai_source_chat_globals_init(bot_source_chat_globals *);
bool bot_ai_source_valid_chat_position(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_visible_enemies(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_enter_game(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_exit_game(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_start_level(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_end_level(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_death(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_kill(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_enemy_suicide(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_hit_talking(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_hit_no_death(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_hit_no_kill(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_random(struct qa_bots *,struct bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_chat_time(struct qa_bots *,struct bot_ai_state *,float *,qa_error *);
/* This is the donor's explicit bot_testichat gameplay diagnostic. */
bool bot_ai_source_chat_test(struct qa_bots *,struct bot_ai_state *,qa_error *);

#endif

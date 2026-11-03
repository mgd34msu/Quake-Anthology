/*
 * Port of src/bots/behavior/q3/arsenal-knowledge.ts. GPL-2.0-or-later.
 *
 * createBotArsenalKnowledge's closure becomes qa_bot_arsenal_knowledge_bots_a:
 * `knowledge` is the returned BotArsenalKnowledge table, whose ctx is the
 * struct itself, so the struct must not move after creation. The data table is
 * copied; its leaves keep their own ctx. The refreshed candidate array is owned
 * (data.candidates returns a malloc'd array) and released by destroy.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_ARSENAL_KNOWLEDGE_H
#define QA_BOTS_BEHAVIOR_Q3_ARSENAL_KNOWLEDGE_H
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/game_host.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_arsenal_knowledge_bots_a {
  qa_bot_arsenal_interface_bots_a knowledge;
  qa_bot_arsenal_data_bots_a data;
  qa_bot_weapon_knowledge_bots_a *candidates;
  size_t candidate_count;
} qa_bot_arsenal_knowledge_bots_a;
void qa_create_bot_arsenal_knowledge_bots_a(qa_bot_arsenal_knowledge_bots_a *k,
                                            const qa_bot_arsenal_data_bots_a *data);
void qa_bot_arsenal_knowledge_bots_a_destroy(qa_bot_arsenal_knowledge_bots_a *k);
#endif

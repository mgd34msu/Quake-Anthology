/* Port of src/bots/behavior/q3/library.ts (composition). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_LIBRARY_H
#define QA_BOTS_BEHAVIOR_Q3_LIBRARY_H
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/character.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/library/genetic.h"
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/library/libvars.h"
#include "qa/bots/behavior/library/log.h"
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/bots/behavior/library/weapons.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include <stdbool.h>
typedef struct qa_bot_library_bots_a {
  qa_bot_memory memory;
  qa_bot_lib_vars variables;
  qa_bot_script_sources sources;
  qa_bot_character_library characters;
  qa_bot_action_buffer actions;
  qa_bot_chat_library chat;
  qa_weight_config_store_bots_a weights;
  qa_weapon_ai_bots_a weapons;
  qa_bot_goal_library goals;
  qa_bot_log log;
  qa_bot_move_state_store move_states;
  double log_time;
  bool initialized, closed;
  qa_bot_random random;
  double (*milliseconds)(void *ctx);
  void *ms_ctx;
} qa_bot_library_bots_a;
void qa_bot_library_bots_a_init(qa_bot_library_bots_a *l);
void qa_bot_library_bots_a_destroy(qa_bot_library_bots_a *l);
double qa_bot_library_time_bots_a(qa_bot_library_bots_a *l);
double qa_bot_library_capacity_bots_a(qa_bot_library_bots_a *l, const char *name, const char *def);
int64_t qa_bot_library_setup_bots_a(qa_bot_library_bots_a *l, int64_t max_clients);
void qa_bot_library_shutdown_bots_a(qa_bot_library_bots_a *l);
#endif

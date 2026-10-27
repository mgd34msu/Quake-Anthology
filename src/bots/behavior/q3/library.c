/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/library.h"
#include <string.h>
void qa_bot_library_bots_a_init(qa_bot_library_bots_a *l) {
  memset(l, 0, sizeof *l);
  qa_bot_memory_init(&l->memory);
  qa_bot_lib_vars_init(&l->variables);
  qa_bot_script_sources_init(&l->sources);
  qa_bot_character_library_init(&l->characters);
  qa_bot_action_buffer_init(&l->actions);
  qa_bot_chat_library_init(&l->chat);
  qa_weight_config_store_bots_a_init(&l->weights);
  qa_weapon_ai_bots_a_init(&l->weapons);
  qa_bot_goal_library_init(&l->goals);
  qa_bot_log_init(&l->log, &l->variables);
  qa_bot_move_state_store_init(&l->move_states);
}
void qa_bot_library_bots_a_destroy(qa_bot_library_bots_a *l) {
  qa_bot_memory_destroy(&l->memory);
  qa_bot_lib_vars_destroy(&l->variables);
  qa_bot_script_sources_destroy(&l->sources);
  qa_bot_character_library_destroy(&l->characters);
  qa_bot_action_buffer_destroy(&l->actions);
  qa_bot_chat_library_destroy(&l->chat);
  qa_weight_config_store_bots_a_destroy(&l->weights);
  qa_weapon_ai_bots_a_destroy(&l->weapons);
  qa_bot_goal_library_destroy(&l->goals);
  qa_bot_log_destroy(&l->log);
}
double qa_bot_library_time_bots_a(qa_bot_library_bots_a *l) {
  return l->milliseconds ? l->milliseconds(l->ms_ctx) / 1000.0 : l->log_time;
}
double qa_bot_library_capacity_bots_a(qa_bot_library_bots_a *l, const char *name, const char *def) {
  return qa_bot_lib_vars_value(&l->variables, name, def);
}
int64_t qa_bot_library_setup_bots_a(qa_bot_library_bots_a *l, int64_t max_clients) {
  qa_bot_action_buffer_setup(&l->actions, max_clients);
  (void)qa_bot_move_state_store_setup(&l->move_states);
  l->initialized = true;
  return 0;
}
void qa_bot_library_shutdown_bots_a(qa_bot_library_bots_a *l) {
  qa_bot_action_buffer_shutdown(&l->actions);
  qa_bot_move_state_store_shutdown(&l->move_states);
  (void)qa_bot_log_close(&l->log);
  l->closed = true;
}

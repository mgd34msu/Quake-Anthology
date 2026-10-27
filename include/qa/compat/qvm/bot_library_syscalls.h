/*
 * Port of src/compat/qvm/bot-library-syscalls.ts.
 * qa_qvmx_bot_library mirrors bots/behavior/q3/library.ts BotLibrary
 * (bots module unported); sub-tables keep the TS grouping.
 * qa_qvm_string_contains / qa_qvm_unify_white_spaces_in_place /
 * qa_qvm_touching_goal port the chat.ts/goals.ts helpers used here.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_BOT_LIBRARY_SYSCALLS_H
#define QA_COMPAT_QVM_BOT_LIBRARY_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/bot_navigation_records.h"
#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/script_record.h"
#include "qa/compat/qvm/syscalls.h"

typedef struct qa_qvmx_chat_span { int64_t byte_offset, byte_length; } qa_qvmx_chat_span;

typedef struct qa_qvmx_chat_buffer_writes {
  void *ctx;
  qa_qvm_writable_view (*view)(void *ctx, qa_qvmx_chat_span span);
  void (*copy)(void *ctx, qa_qvmx_chat_span destination, qa_qvmx_chat_span source);
  void (*clear)(void *ctx, qa_qvmx_chat_span destination, int64_t start);
} qa_qvmx_chat_buffer_writes;

typedef struct qa_qvmx_chat_match_buffer {
  qa_qvm_writable_view view; /* 328-byte record */
  qa_qvmx_chat_span string;
  qa_qvmx_chat_buffer_writes writes;
} qa_qvmx_chat_match_buffer;

/* ChatTextSource thunks become borrowed strings read by the dispatcher. */
typedef struct qa_qvm_bot_inventory_fn {
  void *ctx;
  double (*get)(void *ctx, double item);
} qa_qvm_bot_inventory_fn;

typedef struct qa_qvm_bot_span_fn {
  void *ctx;
  qa_qvm_memory_span (*get)(void *ctx);
} qa_qvm_bot_span_fn;

typedef struct qa_qvm_bot_text_fn {
  void *ctx;
  const char *(*get)(void *ctx);
} qa_qvm_bot_text_fn;

typedef struct qa_qvmx_console_chat_message {
  double handle, time, type;
  const char *message; /* borrowed */
} qa_qvmx_console_chat_message;

typedef struct qa_qvmx_genetic_selection {
  double count;
  void *ctx;
  float (*rank)(void *ctx, double index);
  /* target: "parent1" | "parent2" | "child" */
  void (*write)(void *ctx, const char *target, double value);
} qa_qvmx_genetic_selection;

typedef struct qa_qvmx_bot_library {
  void *ctx;
  /* variables */
  void (*variables_set)(void *ctx, const char *name, const char *value);
  const char *(*variables_get_string)(void *ctx, const char *name);
  /* globals (ScriptGlobalDefines.add) */
  bool (*globals_add)(void *ctx, const char *text);
  double (*start_frame)(void *ctx, float time);
  void (*print)(void *ctx, double kind, const char *text);
  /* actions */
  void (*actions_say)(void *ctx, double client, const char *text);
  void (*actions_say_team)(void *ctx, double client, const char *text);
  void (*actions_command)(void *ctx, double client, const char *text);
  void (*actions_action)(void *ctx, double client, double flags);
  void (*actions_gesture)(void *ctx, double client);
  void (*actions_talk)(void *ctx, double client);
  void (*actions_attack)(void *ctx, double client);
  void (*actions_use)(void *ctx, double client);
  void (*actions_respawn)(void *ctx, double client);
  void (*actions_crouch)(void *ctx, double client);
  void (*actions_move_up)(void *ctx, double client);
  void (*actions_move_down)(void *ctx, double client);
  void (*actions_move_forward)(void *ctx, double client);
  void (*actions_move_back)(void *ctx, double client);
  void (*actions_move_left)(void *ctx, double client);
  void (*actions_move_right)(void *ctx, double client);
  void (*actions_select_weapon)(void *ctx, double client, double weapon);
  void (*actions_jump)(void *ctx, double client);
  void (*actions_delayed_jump)(void *ctx, double client);
  void (*actions_move)(void *ctx, double client, qa_vec3 direction, float speed);
  void (*actions_view)(void *ctx, double client, qa_vec3 angles);
  void (*actions_end_regular)(void *ctx, double client, float time);
  void (*actions_get_input_bytes)(void *ctx, double client, float time, uint8_t out[40]);
  void (*actions_reset_input)(void *ctx, double client);
  /* actions used only by legacy-bot-syscalls.ts */
  void (*actions_use_item)(void *ctx, double client, const char *item);
  void (*actions_drop_item)(void *ctx, double client, const char *item);
  void (*actions_use_inventory)(void *ctx, double client, const char *item);
  void (*actions_drop_inventory)(void *ctx, double client, const char *item);
  /* characters */
  double (*characters_load)(void *ctx, const char *name, float skill);
  void (*characters_free)(void *ctx, double handle);
  double (*characters_float)(void *ctx, double handle, double characteristic);
  double (*characters_bounded_float)(void *ctx, double handle, double characteristic, float min,
                                     float max);
  double (*characters_integer)(void *ctx, double handle, double characteristic);
  double (*characters_bounded_integer)(void *ctx, double handle, double characteristic, double min,
                                       double max);
  const char *(*characters_string)(void *ctx, double handle, double characteristic);
  /* chat */
  double (*chat_allocate)(void *ctx);
  void (*chat_free)(void *ctx, double state);
  void (*chat_queue_console_message)(void *ctx, double state, double type, const char *message);
  void (*chat_remove_console_message)(void *ctx, double state, double type);
  double (*chat_num_console_messages)(void *ctx, double state);
  bool (*chat_next_console_message)(void *ctx, double state, qa_qvmx_console_chat_message *out);
  void (*chat_initial_chat)(void *ctx, double state, const char *type, double subtype,
                            const char *const variables[8]);
  bool (*chat_reply_chat)(void *ctx, double state, const char *message, double type, double subtype,
                          const char *const variables[8]);
  double (*chat_num_initial_chats)(void *ctx, double state, const char *type);
  double (*chat_chat_length)(void *ctx, double state);
  void (*chat_enter_chat)(void *ctx, double state, double client, double send_to);
  void (*chat_write_chat_message)(void *ctx, double state,
                                  void (*write)(void *write_ctx, const char *message),
                                  void *write_ctx);
  void (*chat_replace_synonyms_in_place)(void *ctx, qa_qvm_bot_span_fn source, double gender,
                                         qa_qvmx_chat_buffer_writes writes);
  bool (*chat_load_chat_file)(void *ctx, double state, const char *file, const char *name);
  void (*chat_set_gender)(void *ctx, double state, double gender);
  void (*chat_set_name)(void *ctx, double state, const char *name, double client);
  bool (*chat_find_match_into)(void *ctx, const char *text, double type,
                               qa_qvmx_chat_match_buffer *buffer);
  void (*chat_write_match_variable)(
      void *ctx, void (*capture)(void *capture_ctx, double index, double *out_offset,
                                 double *out_length),
      void *capture_ctx, double variable, double size,
      void (*write)(void *write_ctx, double offset, double capacity), void *write_ctx,
      void (*clear)(void *clear_ctx), void *clear_ctx);
  /* legacy-only enterChat arity: (state, type, team, subtype) */
  void (*chat_enter_chat_legacy)(void *ctx, double state, double type, double team, double subtype);
  /* goals */
  double (*goals_alloc_goal_state)(void *ctx, double client);
  void (*goals_free_goal_state)(void *ctx, double state);
  void (*goals_reset_goal_state)(void *ctx, double state);
  void (*goals_reset_avoid_goals)(void *ctx, double state);
  void (*goals_push_goal)(void *ctx, double state, qa_qvm_bot_span_fn goal);
  void (*goals_pop_goal)(void *ctx, double state);
  void (*goals_empty_goal_stack)(void *ctx, double state);
  bool (*goals_get_top_goal_bytes)(void *ctx, double state, uint8_t out[56]);
  bool (*goals_get_second_goal_bytes)(void *ctx, double state, uint8_t out[56]);
  const char *(*goals_goal_name)(void *ctx, double state);
  double (*goals_avoid_goal_time)(void *ctx, double state, double goal);
  void (*goals_set_avoid_goal_time)(void *ctx, double state, double goal, float time);
  void (*goals_remove_from_avoid_goals)(void *ctx, double state, double goal);
  double (*goals_load_item_weights)(void *ctx, double state, qa_qvm_bot_text_fn filename);
  void (*goals_free_item_weights)(void *ctx, double state);
  bool (*goals_choose_ltg_item)(void *ctx, double state, qa_vec3 origin,
                                qa_qvm_bot_inventory_fn inventory, double goal_time);
  bool (*goals_choose_nbg_item)(void *ctx, double state, qa_vec3 origin,
                                qa_qvm_bot_inventory_fn inventory, double goal_time,
                                const qa_qvmx_bot_goal *ltg, float avoid_time);
  bool (*goals_item_goal_in_vis_but_not_visible)(void *ctx, double state, qa_vec3 eye,
                                                  qa_vec3 view_angles, qa_qvmx_bot_goal goal);
  void (*goals_init_level_items)(void *ctx);
  void (*goals_update_entity_items)(void *ctx);
  bool (*goals_get_level_item_goal)(void *ctx, double index, const char *classname,
                                    qa_qvmx_bot_goal *out);
  bool (*goals_get_map_location_goal)(void *ctx, const char *name, qa_qvmx_bot_goal *out);
  bool (*goals_get_next_camp_spot_goal)(void *ctx, double number, qa_qvmx_bot_goal *out_goal,
                                        double *out_next);
  void (*goals_dump_avoid_goals)(void *ctx, double state);
  void (*goals_dump_goal_stack)(void *ctx, double state);
  void (*goals_interbreed_goal_fuzzy_logic)(void *ctx, double state1, double state2, double state3);
  void (*goals_mutate_goal_fuzzy_logic)(void *ctx, double state, float range);
  void (*goals_save_goal_fuzzy_logic)(void *ctx, double state, const char *filename);
  /* weapons */
  double (*weapons_allocate_state)(void *ctx);
  void (*weapons_free_state)(void *ctx, double state);
  void (*weapons_reset_state)(void *ctx, double state);
  double (*weapons_load_weights)(void *ctx, double state, qa_qvm_bot_text_fn filename);
  double (*weapons_choose_best_fight_weapon)(void *ctx, double state,
                                             qa_qvm_bot_inventory_fn inventory);
  /* False is the TS undefined bytes. */
  bool (*weapons_weapon_info_bytes)(void *ctx, double state, double weapon, const uint8_t **out,
                                    size_t *out_length);
  /* move states (read by bot-navigation-syscalls) */
  void (*move_states_reset)(void *ctx, double state);
  void (*move_states_reset_avoid_reach)(void *ctx, double state);
  void (*move_states_reset_last_avoid_reach)(void *ctx, double state);
  double (*move_states_allocate)(void *ctx);
  void (*move_states_free)(void *ctx, double state);
  void (*move_states_initialize)(void *ctx, double state, qa_qvmx_bot_init_move move);
  void (*move_states_add_avoid_spot)(void *ctx, double state, qa_vec3 origin, float radius,
                                     double type);
  /* sources */
  double (*sources_load_source_handle)(void *ctx, const char *filename);
  bool (*sources_free_source_handle)(void *ctx, double handle);
  bool (*sources_read_token)(void *ctx, double handle, qa_qvmy_script_token_record *out_token,
                             bool *out_read);
  bool (*sources_source_file_and_line)(void *ctx, double handle, const char **out_filename,
                                       double *out_line);
  /* geneticSelection: kind "selected" returns true */
  bool (*genetic_selection)(void *ctx, qa_qvmx_genetic_selection selection);
} qa_qvmx_bot_library;

typedef struct qa_qvm_bot_library_services {
  qa_qvmx_bot_library library;
  void *ctx;
  double (*setup)(void *ctx);
  double (*shutdown)(void *ctx);
  double (*load_map)(void *ctx, const char *name);
  double (*update_entity)(void *ctx, double number, const qa_qvm_host_call *call, double pointer);
  double (*snapshot_entity)(void *ctx, double client, double sequence);
  const char *(*console_message)(void *ctx, double client); /* NULL is TS null */
  void (*user_command)(void *ctx, double client, qa_qvmx_wire_user_command command);
  double (*allocate_client)(void *ctx);
  void (*free_client)(void *ctx, double client);
} qa_qvm_bot_library_services;

/* Helpers from bots/behavior/library/chat.ts and goals.ts (mirrors). */
double qa_qvm_string_contains(const char *text, const char *part, bool case_sensitive);
void qa_qvm_unify_white_spaces_in_place(qa_qvm_memory *memory, bool has_bytes,
                                        qa_qvm_memory_span bytes,
                                        qa_qvmx_chat_buffer_writes writes);
bool qa_qvm_touching_goal(qa_vec3 origin, qa_qvmx_bot_goal goal);

bool qa_qvm_bot_library_syscall(const qa_qvm_host_call *call, qa_qvm_bot_library_services *services,
                                qa_qvm_host_result *out);

#endif

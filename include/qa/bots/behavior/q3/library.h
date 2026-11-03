/*
 * Port of src/bots/behavior/q3/library.ts: botlib behavior composition; the
 * shared session owns actors, navigation and movement.
 * qa_bot_library_bots_a_init is the TS constructor. Options and everything they
 * point to are borrowed. The struct must not move: its members bind to each
 * other and to it. qa_bot_library_bots_a_destroy releases C bookkeeping and may
 * follow qa_bot_library_shutdown (TS memory.dispose) or stand alone.
 * GPL-2.0-or-later.
 */
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
#include "qa/ui/common/legacy/script/preprocessor.h"
#include <stdbool.h>
/* TS BotSourceFiles read: borrowed bytes, NULL is TS null. Every member receives ctx. */
typedef struct qa_source_bot_library_options_bots_a {
  void *ctx;
  const uint8_t *(*read_file)(void *ctx, const char *path, size_t *out_len);
  qa_bot_random random;
  bool debug;
  double (*milliseconds)(void *ctx);
  void (*print)(void *ctx, int severity, const char *text); /* 1|2|3|4 */
  void (*client_command)(void *ctx, int64_t client, const char *command);
  qa_bot_log_open_result (*open_log)(void *ctx, const char *filename);
  /* NULL is the absent TS resumeLog. */
  qa_bot_log_open_result (*resume_log)(void *ctx, const char *filename, int64_t position);
} qa_source_bot_library_options_bots_a;
typedef struct qa_bot_library_bots_a {
  qa_source_bot_library_options_bots_a options;
  qa_bot_memory memory;
  qa_bot_lib_vars variables;
  qa_ui_script_global_defines *globals;
  qa_bot_script_sources sources;
  qa_bot_script_reader reader;
  qa_bot_character_library characters;
  qa_bot_action_buffer actions;
  qa_bot_chat_library chat;
  qa_weight_config_store_bots_a weights;
  qa_weapon_ai_bots_a weapons;
  qa_bot_goal_library goals;
  qa_bot_log log;
  qa_bot_move_state_store move_states;
  bool initialized, closed;
} qa_bot_library_bots_a;
void qa_bot_library_bots_a_init(qa_bot_library_bots_a *l,
                                const qa_source_bot_library_options_bots_a *options);
void qa_bot_library_bots_a_destroy(qa_bot_library_bots_a *l);
bool qa_bot_library_debug_build(const qa_bot_library_bots_a *l);
double qa_bot_library_time_bots_a(const qa_bot_library_bots_a *l);
int64_t qa_bot_library_setup_bots_a(qa_bot_library_bots_a *l);
int64_t qa_bot_library_load_map_bots_a(qa_bot_library_bots_a *l, const qa_goal_world *world);
int64_t qa_bot_library_start_frame_bots_a(qa_bot_library_bots_a *l, double time);
qa_genetic_selection_result qa_bot_library_genetic_selection_bots_a(qa_bot_library_bots_a *l,
                                                                    const double *ranks,
                                                                    size_t count,
                                                                    qa_genetic_write_fn write,
                                                                    void *write_ctx);
int64_t qa_bot_library_shutdown_bots_a(qa_bot_library_bots_a *l);
/* Owned checkpoint tree. */
qa_save_value *qa_bot_library_capture_save_state(qa_bot_library_bots_a *l,
                                                 qa_bot_memory_capture *memory);
void qa_bot_library_restore_save_state(qa_bot_library_bots_a *l, const qa_save_value *value,
                                       qa_bot_memory_restored *memory, const qa_goal_world *world,
                                       qa_goal_restore_actor_fn actor, void *actor_ctx,
                                       qa_bot_move_edge_fn edge, void *edge_ctx);
#endif

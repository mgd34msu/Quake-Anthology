/*
 * Port of src/bots/behavior/q3/ai-main.ts (game/ai_main.c lifecycle and
 * scheduling). GPL-2.0-or-later.
 *
 * qa_game_ai_bots_a is the TS GameAi: a fresh instance represents a fresh game
 * VM, while its library survives fast restart. It embeds the context, whose
 * address must stay stable, so the struct must not move after init.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_MAIN_H
#define QA_BOTS_BEHAVIOR_Q3_AI_MAIN_H
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Output stops at out_len-1 bytes; strlen(text)+1 always fits. */
void qa_remove_color_escape_sequences_bots_a(const char *text, char *out, size_t out_len);
void qa_bot_report_status_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_teamplay_report_bots_a(qa_game_ai_context_bots_a *ctx);
void qa_bot_test_aas_bots_a(qa_game_ai_context_bots_a *ctx, qa_vec3 origin);
void qa_bot_update_info_config_strings_bots_a(qa_game_ai_context_bots_a *ctx);
bool qa_bot_team_leader_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
typedef struct qa_game_ai_bots_a {
  qa_game_ai_context_bots_a context;
  int32_t local_time, botlib_residual, last_bot_think_time;
} qa_game_ai_bots_a;
/* game, library and host are borrowed. False only on OOM. */
bool qa_game_ai_bots_a_init(qa_game_ai_bots_a *g, qa_source_bot_game_bots_a *game,
                            qa_bot_library_bots_a *library, const qa_game_ai_host_bots_a *host);
void qa_game_ai_bots_a_destroy(qa_game_ai_bots_a *g);
/* Owned checkpoint tree. */
qa_save_value *qa_game_ai_capture_save_state_bots_a(qa_game_ai_bots_a *g);
void qa_game_ai_restore_save_state_bots_a(qa_game_ai_bots_a *g, const qa_save_value *value,
                                          qa_bot_remap_observation_fn remap, void *remap_ctx);
void qa_game_ai_test_aas_bots_a(qa_game_ai_bots_a *g, qa_vec3 origin);
bool qa_game_ai_setup_bots_a(qa_game_ai_bots_a *g, bool restart);
bool qa_game_ai_load_map_bots_a(qa_game_ai_bots_a *g, bool restart);
bool qa_game_ai_setup_client_bots_a(qa_game_ai_bots_a *g, int64_t client,
                                    const qa_bot_settings_bots_a *settings, bool restart);
bool qa_game_ai_shutdown_client_bots_a(qa_game_ai_bots_a *g, int64_t client, bool restart);
bool qa_game_ai_shutdown_bots_a(qa_game_ai_bots_a *g, bool restart);
bool qa_game_ai_start_frame_bots_a(qa_game_ai_bots_a *g, double time);
void qa_game_ai_interbreed_end_match_bots_a(qa_game_ai_bots_a *g);
#endif

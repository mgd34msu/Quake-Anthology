/* Port of src/content/q3/presentation/console.ts. GPL-2.0-or-later.
 * From id Software's cg_consolecmds.c and cg_main.c helpers.
 * TS async execute is synchronous (serialized by call order). */
#ifndef QA_CONTENT_Q3_PRESENTATION_CONSOLE_H
#define QA_CONTENT_Q3_PRESENTATION_CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
typedef enum qa_q3_pres_console_cvar {
  QA_Q3_PRES_CONSOLE_CVAR_VIEWSIZE, QA_Q3_PRES_CONSOLE_CVAR_CAMERA_ORBIT,
  QA_Q3_PRES_CONSOLE_CVAR_CURRENT_SELECTED_PLAYER
} qa_q3_pres_console_cvar;
const char *qa_q3_pres_console_cvar_name(qa_q3_pres_console_cvar cvar);
typedef enum qa_q3_pres_console_sound {
  QA_Q3_PRES_CONSOLE_SOUND_WINNER, QA_Q3_PRES_CONSOLE_SOUND_LOSER
} qa_q3_pres_console_sound;
/* TS ClientConsoleHost. hud/orders availability is `available` +
 * reason (borrowed). view/weapons/clients picks are fn+ctx tables. */
typedef struct qa_q3_pres_client_console_host {
  void *cvars_ctx;
  bool (*cvar_get)(void *ctx, const char *name, const char **out_value);
  void (*cvar_set)(void *ctx, const char *name, const char *value);
  void *view_ctx;
  bool (*test_gun)(void *ctx, const char *name, const double *back_lerp);
  bool (*test_model)(void *ctx, const char *name, const double *back_lerp);
  void (*next_model_frame)(void *ctx);
  void (*previous_model_frame)(void *ctx);
  void (*next_model_skin)(void *ctx);
  void (*previous_model_skin)(void *ctx);
  void (*zoom_down)(void *ctx);
  void (*zoom_up)(void *ctx);
  bool (*clear_test_model)(void *ctx);
  void *weapons_ctx;
  void (*next_weapon)(void *ctx);
  void (*previous_weapon)(void *ctx);
  void (*select_weapon)(void *ctx, double weapon);
  void *clients_ctx;
  bool (*load_deferred_players)(void *ctx, void (*reset)(void *rctx, qa_q3_pres_client_entity *entity), void *rctx);
  bool (*reset_clients)(void *ctx);
  void *commands_ctx;
  bool (*build_spectator_string)(void *ctx);
  bool hud_available;
  const char *hud_reason;
  void *hud_ctx;
  void (*hud_reset_strings)(void *ctx);
  void (*hud_reset_menus)(void *ctx);
  bool (*hud_load_menus)(void *ctx, const char *path);
  void (*hud_clear_scoreboard)(void *ctx);
  bool (*hud_menu_scoreboard)(void *ctx); /* true when a menu is captured */
  bool (*hud_scroll_feeder)(void *ctx, double feeder, bool down);
  bool orders_available;
  const char *orders_reason;
  void *orders_ctx;
  void (*select_next_player)(void *ctx);
  void (*select_previous_player)(void *ctx);
  bool (*other_team_has_flag)(void *ctx);
  bool (*your_team_has_flag)(void *ctx);
  void *ctx;
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, qa_q3_pres_console_cvar name);
  void (*reset_player_entity)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*add_command)(void *ctx, const char *name);
  void (*send_client_command)(void *ctx, const char *text);
  void (*send_console_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  void (*center_print)(void *ctx, const char *text, double y, double char_width);
  qa_pcm_sound *(*sound)(void *ctx, qa_q3_pres_console_sound name);
  void (*add_buffered_sound)(void *ctx, qa_pcm_sound *sound);
} qa_q3_pres_client_console_host;
/* TS clientConsoleCommandNames: malloc'd array of borrowed literals. */
const char *const *qa_q3_pres_client_console_command_names(qa_q3_base_product product, size_t *out_count);
typedef struct qa_q3_pres_client_console_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_client_console_host host;
  bool closed;
} qa_q3_pres_client_console_runtime;
/* Canonical-sharing is checked through view/weapons state pointers. */
void qa_q3_pres_client_console_runtime_init(qa_q3_pres_client_console_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_console_host host, qa_q3_pres_client_game_state *view_state,
    qa_q3_pres_client_game_state *weapons_state);
void qa_q3_pres_client_console_runtime_initialize_commands(qa_q3_pres_client_console_runtime *runtime);
void qa_q3_pres_client_console_runtime_dispose(qa_q3_pres_client_console_runtime *runtime);
bool qa_q3_pres_client_console_runtime_handles(qa_q3_pres_client_console_runtime *runtime, const char *name);
/* Returns false for forwarded/unhandled commands (and on OOM in a
 * host registration). Oversize tokenization aborts as the TS rejects. */
bool qa_q3_pres_client_console_runtime_execute(qa_q3_pres_client_console_runtime *runtime,
                                               const char *const *argv, size_t argc);
double qa_q3_pres_client_console_runtime_crosshair_player(qa_q3_pres_client_console_runtime *runtime);
double qa_q3_pres_client_console_runtime_last_attacker(qa_q3_pres_client_console_runtime *runtime);
#endif

/* Port of src/content/q3/presentation/scoreboard.ts. GPL-2.0-or-later.
 * Classic scoreboard from id Software's cg_scoreboard.c (baseq3). */
#ifndef QA_CONTENT_Q3_PRESENTATION_SCOREBOARD_H
#define QA_CONTENT_Q3_PRESENTATION_SCOREBOARD_H
#include <stdbool.h>
#include "qa/content/q3/presentation/draw_icons.h"
#include "qa/content/q3/presentation/players.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
typedef enum qa_q3_pres_scoreboard_cvar {
  QA_Q3_PRES_SCOREBOARD_CVAR_PAUSED, QA_Q3_PRES_SCOREBOARD_CVAR_DRAW_ICONS
} qa_q3_pres_scoreboard_cvar;
const char *qa_q3_pres_scoreboard_cvar_name(qa_q3_pres_scoreboard_cvar cvar);
typedef struct qa_q3_pres_base_scoreboard_host {
  qa_q3_pres_client_draw_icons *icons;
  qa_q3_pres_client_info_store *clients;
  qa_q3_pres_player_presenter *players;
  void *ctx;
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, qa_q3_pres_scoreboard_cvar name);
  const char *(*config_string)(void *ctx, double index);
  void (*send_client_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
} qa_q3_pres_base_scoreboard_host;
typedef struct qa_q3_pres_base_scoreboard {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_base_scoreboard_host host;
  bool local_client;
} qa_q3_pres_base_scoreboard;
/* Product/store mismatches abort with the TS Error texts. */
void qa_q3_pres_base_scoreboard_init(qa_q3_pres_base_scoreboard *scoreboard,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_base_scoreboard_host host);
/* Returns false when the scoreboard should not show (TS boolean). */
bool qa_q3_pres_base_scoreboard_draw(qa_q3_pres_base_scoreboard *scoreboard);
bool qa_q3_pres_base_scoreboard_draw_tourney(qa_q3_pres_base_scoreboard *scoreboard);
#endif

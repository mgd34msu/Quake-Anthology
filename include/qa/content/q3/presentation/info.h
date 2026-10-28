/* Port of src/content/q3/presentation/info.ts. GPL-2.0-or-later.
 * From id Software's code/cgame/cg_info.c. TS async loading hooks are
 * synchronous; methods return false only on OOM. The two
 * ClientDrawTools methods used here (draw-tools.ts, unported) are
 * implemented inline against the landed text/q3-font + draw2d APIs,
 * exactly as draw-tools.ts defines them. */
#ifndef QA_CONTENT_Q3_PRESENTATION_INFO_H
#define QA_CONTENT_Q3_PRESENTATION_INFO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/presentation/media.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
#include "qa/text/draw2d.h"
/* TS ClientLoadingImports. config_string returns a borrowed string. */
typedef struct qa_q3_pres_client_loading_imports {
  void *ctx;
  const char *(*config_string)(void *ctx, double index);
  void (*update_screen)(void *ctx);
} qa_q3_pres_client_loading_imports;
typedef struct qa_q3_pres_client_loading_screen {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_media *media;
  qa_cvar_registry *cvars;
  qa_q3_pres_client_loading_imports imports;
  const qa_q3_pres_scene_shader *player_icons[16]; size_t player_icon_count;
  const qa_q3_pres_scene_shader *item_icons[26]; size_t item_icon_count;
} qa_q3_pres_client_loading_screen;
/* Product mismatch aborts with the TS Error text. */
void qa_q3_pres_client_loading_screen_init(qa_q3_pres_client_loading_screen *screen,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_media *media, qa_cvar_registry *cvars,
    qa_q3_pres_client_loading_imports imports);
/* False only on OOM. */
bool qa_q3_pres_client_loading_screen_loading_string(qa_q3_pres_client_loading_screen *screen,
    const char *text);
bool qa_q3_pres_client_loading_screen_loading_item(qa_q3_pres_client_loading_screen *screen, double index);
bool qa_q3_pres_client_loading_screen_loading_client(qa_q3_pres_client_loading_screen *screen,
    double client_num);
/* False only on OOM; non-stretch-640 draws abort as in ClientDrawTools. */
bool qa_q3_pres_client_loading_screen_draw_information(qa_q3_pres_client_loading_screen *screen,
    qa_draw2d *draw);
#endif

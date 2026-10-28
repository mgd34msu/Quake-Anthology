/* Port of src/ui/hud/powerups.ts. measure_text NULL is the TS
 * undefined optional (fallback text.length * 8 * scale, counting
 * UTF-8 code points); localize returns a borrowed string.
 * GPL-2.0-or-later. */
#ifndef QA_UI_HUD_POWERUPS_H
#define QA_UI_HUD_POWERUPS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/ui/common/skin.h"

typedef double (*qa_ui_hud_measure_fn)(void *ctx, const char *text,
                                       double scale);
typedef const char *(*qa_ui_hud_localize_fn)(void *ctx, const char *text);

/** Owned result of qa_ui_draw_powerup_timers; free with _free. */
typedef struct qa_ui_powerup_commands {
  qa_ui_draw_command *commands;
  size_t count;
  char **owned;
  size_t owned_count;
} qa_ui_powerup_commands;

void qa_ui_powerup_commands_free(qa_ui_powerup_commands *result);

/* TS drawPowerupTimers. Returns false only on OOM. */
bool qa_ui_draw_powerup_timers(const qa_active_powerup_timer *timers,
                               size_t timer_count, qa_rect area,
                               double bottom, qa_ui_skin skin,
                               double requested_scale,
                               qa_ui_hud_measure_fn measure_text,
                               void *measure_ctx,
                               qa_ui_hud_localize_fn localize,
                               void *localize_ctx,
                               qa_ui_powerup_commands *out);

#endif

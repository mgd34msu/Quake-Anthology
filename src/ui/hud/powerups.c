/* Port of src/ui/hud/powerups.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/powerups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UTF-8 code-point count (TS Array.from / string length in units is
 * approximated by code points; ASCII matches exactly). */
static size_t cp_count(const char *text) {
  size_t count = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if ((*p & 0xc0) != 0x80) count++;
  return count;
}

/* Byte index just past the first `n` code points. */
static size_t cp_prefix_bytes(const char *text, size_t n) {
  size_t seen = 0, i = 0;
  while (text[i] != '\0' && seen < n) {
    i++;
    while ((text[i] & 0xc0) == 0x80) i++;
    seen++;
  }
  return i;
}

static char *dup_range(const char *first, size_t length) {
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, first, length);
  out[length] = '\0';
  return out;
}

void qa_ui_powerup_commands_free(qa_ui_powerup_commands *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->owned_count; i++) free(result->owned[i]);
  free(result->owned);
  free(result->commands);
  memset(result, 0, sizeof *result);
}

bool qa_ui_draw_powerup_timers(const qa_active_powerup_timer *timers,
                               size_t timer_count, qa_rect area,
                               double bottom, qa_ui_skin skin,
                               double requested_scale,
                               qa_ui_hud_measure_fn measure_text,
                               void *measure_ctx,
                               qa_ui_hud_localize_fn localize,
                               void *localize_ctx,
                               qa_ui_powerup_commands *out) {
  memset(out, 0, sizeof *out);
  size_t active_count = 0;
  for (size_t i = 0; i < timer_count; i++)
    if (timers[i].remaining_seconds > 0) active_count++;
  if (active_count == 0) return true;
  double cap_height = skin.has_cap_ink ? skin.cap_ink_height : 8;
  double available_height = fmax(0, bottom - area.y - 8);
  double rows_d = fmin((double)active_count,
                       fmax(1, floor(available_height / 12)));
  size_t rows = (size_t)rows_d;
  double columns = ceil((double)active_count / (double)rows);
  double scale = fmin(fmax(8 / cap_height, requested_scale),
                      fmax(1, available_height / (double)rows - 4)
                          / cap_height);
  double row_height = cap_height * scale + 4;
  double width = fmin(200, area.width / fmax(2, columns) - 8);
  out->commands = malloc(active_count * 2 * sizeof *out->commands);
  out->owned = calloc(active_count, sizeof *out->owned);
  if (out->commands == NULL || out->owned == NULL) {
    qa_ui_powerup_commands_free(out);
    return false;
  }
  size_t index = 0;
  for (size_t ti = 0; ti < timer_count; ti++) {
    const qa_active_powerup_timer *timer = &timers[ti];
    if (!(timer->remaining_seconds > 0)) continue;
    size_t column = index / rows, row = index % rows;
    double x = area.x + area.width - (double)(column + 1) * (width + 8) + 4;
    double y = bottom - (double)(rows - row) * row_height;
    char value[64];
    snprintf(value, sizeof value, "%.0fs", ceil(timer->remaining_seconds));
    const char *localized = localize(localize_ctx, timer->label);
    /* label is an array of code points; pop until it fits. */
    size_t keep = cp_count(localized);
    for (;;) {
      size_t bytes = cp_prefix_bytes(localized, keep);
      char *joined = malloc(bytes + 1 + strlen(value) + 1);
      if (joined == NULL) {
        qa_ui_powerup_commands_free(out);
        return false;
      }
      memcpy(joined, localized, bytes);
      joined[bytes] = ' ';
      strcpy(joined + bytes + 1, value);
      double measured = measure_text != NULL
          ? measure_text(measure_ctx, joined, scale)
          : (double)cp_count(joined) * 8 * scale;
      if (keep == 0 || measured <= width - 8) {
        /* `.trimStart()` on the final text. */
        const char *text = joined;
        while (*text == ' ' || *text == '\t' || *text == '\n'
               || *text == '\r' || *text == '\v' || *text == '\f')
          text++;
        char *owned = dup_range(text, strlen(text));
        free(joined);
        if (owned == NULL) {
          qa_ui_powerup_commands_free(out);
          return false;
        }
        out->owned[out->owned_count++] = owned;
        qa_ui_draw_command *fill = &out->commands[out->count++];
        memset(fill, 0, sizeof *fill);
        fill->kind = QA_UI_DRAW_FILL;
        fill->rect.x = x; fill->rect.y = y;
        fill->rect.width = width; fill->rect.height = row_height;
        fill->color = skin.colors.panel;
        qa_ui_draw_command *cmd = &out->commands[out->count++];
        memset(cmd, 0, sizeof *cmd);
        cmd->kind = QA_UI_DRAW_TEXT;
        cmd->origin.x = x + width - 4;
        cmd->origin.y = y + 2
            - (skin.has_cap_ink ? skin.cap_ink_top : 0) * scale;
        cmd->text = owned;
        cmd->font = skin.font;
        cmd->scale = scale;
        cmd->color = skin.colors.text;
        cmd->align = "right";
        cmd->shadow = true;
        break;
      }
      free(joined);
      keep--;
    }
    index++;
  }
  return true;
}

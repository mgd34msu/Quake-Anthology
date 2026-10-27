/*
 * Port of src/ui/common/captions.ts: captionCommands. Captions use
 * the same text command renderer as menus and HUD, within a
 * caller-owned safe region.
 * C accommodation: the returned array and its line strings are owned
 * by the result; free with qa_ui_caption_commands_free. Word
 * splitting follows JS \s over bytes (non-ASCII whitespace differs).
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_CAPTIONS_H
#define QA_UI_COMMON_CAPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/text/captions.h"

typedef double (*qa_ui_measure_text_fn)(void *ctx, const char *text,
                                        double scale);

/** TS captionCommands result (owned). */
typedef struct qa_ui_caption_commands {
  qa_ui_draw_command *commands;
  size_t count;
  char **owned; /* line strings referenced by text commands */
  size_t owned_count;
} qa_ui_caption_commands;

void qa_ui_caption_commands_free(qa_ui_caption_commands *result);

/* TS captionCommands(captions, area, font, scale, measure,
 * capInk = {top: 0, height: 8}). Returns false only on OOM. */
bool qa_ui_get_caption_commands(const qa_active_caption *captions,
                                size_t caption_count, qa_rect area,
                                qa_resource_id font, double scale,
                                qa_ui_measure_text_fn measure, void *ctx,
                                double cap_top, double cap_height,
                                qa_ui_caption_commands *out);

#endif

/*
 * Port of src/text/ui.ts: seat-bound UI text drawing (UiTextRenderer).
 * C accommodations: the font registry is an owned entry array keyed by
 * ResourceId strings (copied on bind; the TS Map compares id strings
 * by value). draw returns the layout in *out (caller destroys) and
 * returns false only on OOM; seat/font errors abort with the TS Error
 * messages. align "center"/"right" are the exact TS strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_UI_H
#define QA_TEXT_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
#include "qa/text/atlas.h"
#include "qa/text/draw2d.h"
#include "qa/text/layout.h"

typedef struct qa_ui_font_entry {
  char *resource; /* owned copy of the ResourceId */
  qa_text_font_selection font;
} qa_ui_font_entry;

/* TS UiTextRenderer. */
typedef struct qa_ui_text_renderer {
  qa_seat_id seat;
  qa_ui_font_entry *fonts; /* owned */
  size_t font_count;
  size_t font_capacity;
} qa_ui_text_renderer;

void qa_ui_text_renderer_init(qa_ui_text_renderer *renderer, qa_seat_id seat);
void qa_ui_text_renderer_destroy(qa_ui_text_renderer *renderer);
/* TS bind/release/clear. bind returns false only on OOM. */
bool qa_ui_text_renderer_bind(qa_ui_text_renderer *renderer,
                              qa_resource_id resource,
                              qa_text_font_selection font);
void qa_ui_text_renderer_release(qa_ui_text_renderer *renderer,
                                 qa_resource_id resource);
void qa_ui_text_renderer_clear(qa_ui_text_renderer *renderer);
/* TS draw: lays out and draws the text command; the layout is
 * returned in *out (destroy with qa_text_layout_destroy). */
bool qa_ui_text_renderer_draw(qa_ui_text_renderer *renderer,
                              const qa_ui_draw_context *context,
                              const qa_ui_draw_command *command,
                              const qa_draw2d *draw, qa_text_layout *out);

#endif

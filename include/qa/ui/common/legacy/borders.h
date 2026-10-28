/* Port of src/ui/common/legacy/borders.ts: CG_DrawRect,
 * CG_DrawTopBottom and CG_DrawSides from code/cgame/cg_drawtools.c.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later. */
#ifndef QA_UI_COMMON_LEGACY_BORDERS_H
#define QA_UI_COMMON_LEGACY_BORDERS_H

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/text/draw2d.h"

/** TS drawCgTopBottom(draw, rect, size, picture). */
void qa_ui_draw_cg_top_bottom(const qa_draw2d *draw, qa_rect rect,
                              double size,
                              const qa_picture_asset *picture);
/** TS drawCgSides(draw, rect, size, picture). */
void qa_ui_draw_cg_sides(const qa_draw2d *draw, qa_rect rect, double size,
                         const qa_picture_asset *picture);
/** TS drawCgRect(draw, rect, size, color, picture). */
void qa_ui_draw_cg_rect(const qa_draw2d *draw, qa_rect rect, double size,
                        qa_vec4 color, const qa_picture_asset *picture);

#endif

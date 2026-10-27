/*
 * Port of src/ui/common/accessibility.ts: high-contrast and color-mode
 * projection of UiSkin colors. color_mode is a TS
 * UiPreferenceValues["colorMode"] string ("standard" | "blue-yellow"
 * | "monochrome"); NULL is a TS undefined colorMode.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_ACCESSIBILITY_H
#define QA_UI_COMMON_ACCESSIBILITY_H

#include <stdbool.h>

#include "qa/ui/common/skin.h"

/** TS accessibleColors(colors, appearance). */
qa_ui_skin_colors qa_ui_accessible_colors(qa_ui_skin_colors colors,
                                          bool high_contrast,
                                          const char *color_mode);

#endif

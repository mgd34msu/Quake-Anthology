/* Port of src/ui/hud/weapon.ts. measure_text NULL is the TS absent
 * optional; has_icon_aspect/has_ammo_aspect mark TS undefined.
 * GPL-2.0-or-later. */
#ifndef QA_UI_HUD_WEAPON_H
#define QA_UI_HUD_WEAPON_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/ui/common/skin.h"
#include "qa/ui/hud/powerups.h"

/** TS CommonWeaponHud. native_status is carried but unread here. */
typedef struct qa_ui_common_weapon_hud {
  qa_weapon_hud_status status;
  qa_arsenal_ammo_warning warning;
  qa_resource_id weapon_icon; /* nullable */
  qa_resource_id ammo_icon;   /* nullable */
  bool has_icon_aspect;
  double icon_aspect;
  bool has_ammo_aspect;
  double ammo_aspect;
  bool native_status;
  qa_ui_hud_measure_fn measure_text; /* optional */
  void *measure_ctx;
} qa_ui_common_weapon_hud;

/** TS hudStatusRows result. */
typedef struct qa_ui_hud_status_rows {
  double label_top, height;
} qa_ui_hud_status_rows;

/** TS hudStatusRows(textScale, capHeight = 8). */
qa_ui_hud_status_rows qa_ui_hud_status_rows_for(double text_scale,
                                                double cap_height);

/** Owned result of qa_ui_draw_weapon_hud; free with _free. */
typedef struct qa_ui_weapon_commands {
  qa_ui_draw_command *commands;
  size_t count;
  char **owned;
  size_t owned_count, owned_capacity;
} qa_ui_weapon_commands;

void qa_ui_weapon_commands_free(qa_ui_weapon_commands *result);

/* TS drawWeaponHud(data, rect, skin, textScale, minimumTextScale = 0).
 * Returns false only on OOM. */
bool qa_ui_draw_weapon_hud(const qa_ui_common_weapon_hud *data, qa_rect rect,
                           qa_ui_skin skin, double text_scale,
                           double minimum_text_scale,
                           qa_ui_weapon_commands *out);

#endif

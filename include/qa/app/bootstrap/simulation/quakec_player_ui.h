/* Port of src/app/bootstrap/simulation/quakec-player-ui.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_PLAYER_UI_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_PLAYER_UI_H
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/gameplay.h"
typedef struct qa_app_sim_quakec_weapon_ui_binding { qa_item_id item; const char *label; double bit; double impulse; } qa_app_sim_quakec_weapon_ui_binding;
typedef struct qa_app_sim_quakec_weapon_ui {
  qa_item_id active_weapon; /* NULL = TS null */
  bool has_ammo; qa_item_id ammo_item; double ammo_count;
  /* weapon_status is TS null; arsenal_warning is "none". */
  qa_app_sim_player_ui_item *items; size_t item_count; /* caller frees array */
} qa_app_sim_quakec_weapon_ui;
qa_app_sim_quakec_weapon_ui qa_app_sim_quakec_weapon_ui_of(double items, double weapon, double current_ammo,
    const qa_app_sim_quakec_weapon_ui_binding *bindings, size_t binding_count);
#endif

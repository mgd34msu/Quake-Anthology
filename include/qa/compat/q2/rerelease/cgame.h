/* Port of src/compat/q2/rerelease/cgame.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_CGAME_H
#define QA_COMPAT_Q2_RERELEASE_CGAME_H
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/ui.h"

typedef struct qa_compat_q2_rerelease_cgame_options {
  qa_compat_q2_rerelease_guest_module *module;
  void *ctx;
  double (*split_index)(void *ctx, qa_seat_id seat);
  /* TS withSeat(seat, callback): runs callback(cb_ctx) with the seat bound. */
  void (*with_seat)(void *ctx, qa_seat_id seat, void *cb_ctx, void (*callback)(void *));
  void (*refresh_cvars)(void *ctx);
} qa_compat_q2_rerelease_cgame_options;

typedef struct qa_compat_q2_rerelease_cgame qa_compat_q2_rerelease_cgame;
/* NULL only on OOM. */
qa_compat_q2_rerelease_cgame *qa_compat_q2_rerelease_cgame_create(qa_compat_q2_rerelease_cgame_options options);
void qa_compat_q2_rerelease_cgame_destroy(qa_compat_q2_rerelease_cgame *g);
const qa_compat_q2_rerelease_cgame_options *qa_compat_q2_rerelease_cgame_get_options(const qa_compat_q2_rerelease_cgame *g);
/* TS api = { kind: "q2-rerelease-cgame", version: 2022 }. */
qa_q2_cgame_api_identity qa_compat_q2_rerelease_cgame_api(const qa_compat_q2_rerelease_cgame *g);
void qa_compat_q2_rerelease_cgame_init(qa_compat_q2_rerelease_cgame *g);
void qa_compat_q2_rerelease_cgame_shutdown(qa_compat_q2_rerelease_cgame *g);
void qa_compat_q2_rerelease_cgame_touch_pictures(qa_compat_q2_rerelease_cgame *g);
void qa_compat_q2_rerelease_cgame_draw_hud(qa_compat_q2_rerelease_cgame *g, qa_q2_hud_draw frame);
double qa_compat_q2_rerelease_cgame_layout_flags(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *player);
double qa_compat_q2_rerelease_cgame_active_weapon_wheel_weapon(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *player);
double qa_compat_q2_rerelease_cgame_owned_weapon_wheel_weapons(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *player);
double qa_compat_q2_rerelease_cgame_weapon_wheel_ammo_count(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *player, double ammo_id);
double qa_compat_q2_rerelease_cgame_powerup_wheel_count(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *player, double powerup_id);
double qa_compat_q2_rerelease_cgame_hit_marker_damage(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *player);
void qa_compat_q2_rerelease_cgame_pmove_raw(qa_compat_q2_rerelease_cgame *g, qa_guest_address address);
void qa_compat_q2_rerelease_cgame_parse_config_string(qa_compat_q2_rerelease_cgame *g, double index, const char *value);
void qa_compat_q2_rerelease_cgame_parse_center_print(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat, const char *text, bool instant);
void qa_compat_q2_rerelease_cgame_clear_notify(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat);
void qa_compat_q2_rerelease_cgame_clear_center_print(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat);
void qa_compat_q2_rerelease_cgame_notify_message(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat, const char *text, bool chat);
qa_vec3 qa_compat_q2_rerelease_cgame_monster_flash_offset(qa_compat_q2_rerelease_cgame *g, double flash_id);
#endif

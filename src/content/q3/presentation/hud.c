/* Port of src/content/q3/presentation/hud.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/hud.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/mission_hud.h"
#include "qa/content/q3/presentation/scoreboard.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/text/draw2d.h"
#include "qa/foundation/js.h"
#define PI_F ((float)3.14159265358979323846)
enum { ICON = 48, DAMAGE_TIME = 500, BIG_BUFFER_BYTES = 32000 };
static double f32(double value) { return (float)value; }
static const qa_vec4 WHITE = {1, 1, 1, 1}, RED = {1, 0, 0, 1};
static const qa_vec4 NORMAL = {1, (float)0.69, 0, 1}, LOW = {1, (float)0.2, (float)0.2, 1}, FIRING = {0.5, 0.5, 0.5, 1};
const char *qa_q3_pres_hud_cvar_name(qa_q3_pres_hud_cvar cvar) {
  switch (cvar) {
    case QA_Q3_PRES_HUD_CVAR_DRAW_2D: return "cg_draw2D";
    case QA_Q3_PRES_HUD_CVAR_DRAW_STATUS: return "cg_drawStatus";
    case QA_Q3_PRES_HUD_CVAR_DRAW_ICONS: return "cg_drawIcons";
    case QA_Q3_PRES_HUD_CVAR_DRAW_3D_ICONS: return "cg_draw3dIcons";
    case QA_Q3_PRES_HUD_CVAR_DRAW_REWARDS: return "cg_drawRewards";
    case QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR: return "cg_drawCrosshair";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_HEALTH: return "cg_crosshairHealth";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_SIZE: return "cg_crosshairSize";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_X: return "cg_crosshairX";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_Y: return "cg_crosshairY";
    case QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR_NAMES: return "cg_drawCrosshairNames";
    case QA_Q3_PRES_HUD_CVAR_DRAW_AMMO_WARNING: return "cg_drawAmmoWarning";
    case QA_Q3_PRES_HUD_CVAR_PAUSED: return "cg_paused";
  }
  return "cg_draw2D";
}
static double cvar_integer(qa_q3_pres_client_hud *hud, qa_q3_pres_hud_cvar name) {
  qa_cvar_snapshot snapshot = hud->host.read_vm_cvar(hud->host.ctx, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static double cvar_numeric(qa_q3_pres_client_hud *hud, qa_q3_pres_hud_cvar name) {
  qa_cvar_snapshot snapshot = hud->host.read_vm_cvar(hud->host.ctx, name);
  double value = snapshot.numeric_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static bool enabled(qa_q3_pres_client_hud *hud, qa_q3_pres_hud_cvar name) { return cvar_integer(hud, name) != 0; }
static qa_q3_pres_client_draw_tools *tools_of(qa_q3_pres_client_hud *hud) { return hud->host.icons->tools; }
static qa_q3_base_source_player_state *snapshot_of(qa_q3_pres_client_hud *hud) {
  if (!hud->state->snap) qa_range_error("CG_Draw2D requires a current snapshot");
  return &hud->state->snap->player_state;
}
static qa_q3_pres_client_info *client_at(qa_q3_pres_client_hud *hud, double index) {
  if (index != trunc(index) || index < 0 || index >= QA_Q3_PRES_MAX_CLIENTS)
    qa_range_error("HUD client index outside source array: %s", QA_JS_NUMBER(index));
  return &hud->static_state->client_info[(size_t)index];
}
static qa_q3_pres_client_reward *reward_at(qa_q3_pres_client_hud *hud, double index) {
  if (index != trunc(index) || index < 0 || index >= QA_Q3_PRES_REWARD_COUNT)
    qa_range_error("HUD reward index outside source array: %s", QA_JS_NUMBER(index));
  return &hud->state->rewards[(size_t)index];
}
static void base_only(qa_q3_pres_client_hud *hud, const char *operation) {
  if (hud->variant.kind != QA_Q3_BASE_PRODUCT_BASEQ3) qa_range_error("%s is excluded from the missionpack build", operation);
}
static void set_color(qa_q3_pres_client_hud *hud, const qa_vec4 *color) { qa_draw2d_set_color(&tools_of(hud)->draw, color); }
static qa_rect make_rect(double x, double y, double width, double height) {
  qa_rect rect = {x, y, width, height};
  return rect;
}
void qa_q3_pres_client_hud_init(qa_q3_pres_client_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_client_hud_host host,
    qa_q3_pres_client_hud_variant variant) {
  if (state->product != static_state->product || variant.kind != state->product || host.icons->state != state ||
      host.media->static_state != static_state || host.status->state != state ||
      host.status->tools != host.icons->tools || host.corners->state != state ||
      host.corners->static_state != static_state || host.corners->icons != host.icons ||
      host.prediction->state != state || host.weapons->selection.state != state ||
      host.weapons->registry != host.weapon_registry)
    qa_range_error("HUD services must share canonical cgame state, drawing and weapon media");
  if (variant.kind == QA_Q3_BASE_PRODUCT_BASEQ3 &&
      (variant.scoreboard->state != state || variant.scoreboard->host.icons != host.icons))
    qa_range_error("HUD scoreboard must share canonical drawing and state");
  if (variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK && variant.fonts->profile != QA_Q3_FONT_CGAME)
    qa_range_error("Missionpack HUD requires cgame fonts");
  if (variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      (variant.menus->state != state || variant.menus->static_state != static_state ||
       variant.menus->host.icons != host.icons || variant.fonts != &variant.menus->fonts))
    qa_range_error("Missionpack HUD must share its menu, font and drawing owners");
  memset(hud, 0, sizeof *hud);
  hud->state = state;
  hud->static_state = static_state;
  hud->host = host;
  hud->variant = variant;
}
static double head_cos(qa_game_random *random) {
  return f32(cos(f32(qa_game_random_crandom(random) * (double)PI_F)));
}
static double head_end_time(qa_q3_pres_client_game_state *state, qa_game_random *random) {
  return qa_int32(trunc(f32(f32(qa_int32(state->time + 100)) + f32(qa_game_random_random(random) * 2000.0))));
}
void qa_q3_pres_client_hud_draw_status_bar_head(qa_q3_pres_client_hud *hud, double x) {
  base_only(hud, "CG_DrawStatusBarHead");
  x = f32(x);
  qa_q3_pres_client_game_state *state = hud->state;
  qa_game_random *random = hud->host.random;
  double size = 60;
  if (state->damage_time != 0 && f32(f32(state->time) - state->damage_time) < DAMAGE_TIME) {
    double frac = f32(f32(f32(state->time) - state->damage_time) / DAMAGE_TIME);
    size = f32(60 * f32(1.5 - f32(frac * 0.5)));
    double stretch = f32(size - 60);
    x = f32(x - f32(f32(stretch * 0.5) + f32(f32(state->damage_x * stretch) * 0.5)));
    state->head_start_yaw = f32(180 + f32(state->damage_x * 45));
    state->head_end_yaw = f32(180 + f32(20 * head_cos(random)));
    state->head_end_pitch = f32(5 * head_cos(random));
    state->head_start_time = state->time;
    state->head_end_time = head_end_time(state, random);
  } else if (state->time >= state->head_end_time) {
    state->head_start_yaw = state->head_end_yaw;
    state->head_start_pitch = state->head_end_pitch;
    state->head_start_time = state->head_end_time;
    state->head_end_time = head_end_time(state, random);
    state->head_end_yaw = f32(180 + f32(20 * head_cos(random)));
    state->head_end_pitch = f32(5 * head_cos(random));
  }
  if (state->head_start_time > state->time) state->head_start_time = state->time;
  double frac = f32(f32(qa_int32(state->time - state->head_start_time)) /
                    f32(qa_int32(state->head_end_time - state->head_start_time)));
  frac = f32(f32(frac * frac) * f32(3 - f32(2 * frac)));
  qa_vec3 angles = {f32(state->head_start_pitch + f32(f32(state->head_end_pitch - state->head_start_pitch) * frac)),
                    f32(state->head_start_yaw + f32(f32(state->head_end_yaw - state->head_start_yaw) * frac)), 0};
  qa_q3_pres_client_draw_icons_draw_head(hud->host.icons, make_rect(x, f32(480 - size), size, size),
                                         snapshot_of(hud)->client_num, angles);
}
void qa_q3_pres_client_hud_draw_status_bar_flag(qa_q3_pres_client_hud *hud, double x, double team) {
  base_only(hud, "CG_DrawStatusBarFlag");
  qa_q3_pres_client_draw_icons_draw_flag_model(hud->host.icons, make_rect(x, 432, ICON, ICON), team, false);
}
void qa_q3_pres_client_hud_draw_status_bar(qa_q3_pres_client_hud *hud) {
  base_only(hud, "CG_DrawStatusBar");
  if (!enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_STATUS)) return;
  qa_q3_pres_client_game_state *state = hud->state;
  qa_q3_base_source_player_state *ps = snapshot_of(hud), *predicted = &state->predicted_player_state;
  qa_q3_pres_client_draw_icons *icons = hud->host.icons;
  qa_q3_pres_client_draw_tools *tools = tools_of(hud);
  const qa_q3_pres_client_media_graphics *graphics = &hud->host.media->graphics;
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps));
  qa_q3_pres_client_draw_icons_draw_team_background(icons, make_rect(0, 420, 640, 60), (float)0.33,
      qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM));
  double weapon = qa_q3_pres_client_game_state_entity_at(state, ps->client_num)->current_state.weapon;
  const qa_q3_pres_client_weapon_info *info = qa_q3_pres_client_weapon_media_registry_weapon_or_empty(hud->host.weapon_registry, weapon);
  if (!hud->host.weapon_hud && weapon != 0 && info->ammo_model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_vec3 origin = {70, 0, 0}, angles = {0, f32(90 + f32(20 * f32(sin(f32(f32(state->time) / 1000))))), 0};
    qa_q3_pres_client_draw_icons_draw_3d_model(icons, make_rect(100, 432, ICON, ICON), info->ammo_model, NULL, origin, angles);
  }
  qa_q3_pres_client_hud_draw_status_bar_head(hud, 285);
  if (qa_q3_base_player_state_slots_get(&predicted->powerups, QA_Q3_PW_REDFLAG) != 0)
    qa_q3_pres_client_hud_draw_status_bar_flag(hud, 333, QA_Q3_BASE_TEAM_RED);
  else if (qa_q3_base_player_state_slots_get(&predicted->powerups, QA_Q3_PW_BLUEFLAG) != 0)
    qa_q3_pres_client_hud_draw_status_bar_flag(hud, 333, QA_Q3_BASE_TEAM_BLUE);
  else if (qa_q3_base_player_state_slots_get(&predicted->powerups, QA_Q3_PW_NEUTRALFLAG) != 0)
    qa_q3_pres_client_hud_draw_status_bar_flag(hud, 333, QA_Q3_BASE_TEAM_FREE);
  if (qa_q3_base_player_state_slots_get(&ps->stats, stats->armor) != 0) {
    qa_vec3 origin = {90, 0, -10}, angles = {0, f32((double)(qa_int32(state->time) & 2047) * 360 / 2048), 0};
    qa_q3_pres_client_draw_icons_draw_3d_model(icons, make_rect(470, 432, ICON, ICON), graphics->armor_model, NULL, origin, angles);
  }
  if (!hud->host.weapon_hud && weapon != 0) {
    double ammo = qa_q3_base_player_state_slots_get(&ps->ammo, weapon);
    if (ammo > -1) {
      set_color(hud, predicted->weapon_state == QA_Q3_WEAPON_FIRING && predicted->weapon_time > 100 ? &FIRING : &NORMAL);
      qa_q3_pres_client_hud_corners_draw_field(hud->host.corners, 0, 432, 3, ammo);
      set_color(hud, NULL);
      if (!enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_3D_ICONS) && enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_ICONS)) {
        const qa_q3_pres_scene_shader *icon =
            qa_q3_pres_client_weapon_media_registry_weapon(hud->host.weapon_registry, predicted->weapon)->ammo_icon;
        if (icon) qa_q3_pres_client_draw_tools_draw_pic(tools, make_rect(100, 432, ICON, ICON), icon);
      }
    }
  }
  double health = qa_q3_base_player_state_slots_get(&ps->stats, stats->health);
  set_color(hud, health > 100 ? &WHITE : health > 25 ? &NORMAL : health > 0 ? (((qa_int32(state->time) >> 8) & 1) ? &LOW : &NORMAL) : &LOW);
  qa_q3_pres_client_hud_corners_draw_field(hud->host.corners, 185, 432, 3, health);
  qa_vec4 health_color = qa_q3_pres_color_for_health(state);
  set_color(hud, &health_color);
  double armor = qa_q3_base_player_state_slots_get(&ps->stats, stats->armor);
  if (armor > 0) {
    set_color(hud, &NORMAL);
    qa_q3_pres_client_hud_corners_draw_field(hud->host.corners, 370, 432, 3, armor);
    set_color(hud, NULL);
    if (!enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_3D_ICONS) && enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_ICONS))
      qa_q3_pres_client_draw_tools_draw_pic(tools, make_rect(470, 432, ICON, ICON), graphics->armor_icon);
  }
}
bool qa_q3_pres_client_hud_draw_holdable_item(qa_q3_pres_client_hud *hud) {
  base_only(hud, "CG_DrawHoldableItem");
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(hud->state->product);
  double value = qa_q3_base_player_state_slots_get(&snapshot_of(hud)->stats, stats->holdable_item);
  if (value == 0) return true;
  if (!qa_q3_pres_client_weapon_media_registry_register_item_visuals(hud->host.weapon_registry, value)) return false;
  size_t count = 0;
  const qa_q3_pres_packet_item_visual *items = qa_q3_pres_client_weapon_media_registry_items(hud->host.weapon_registry, &count);
  if (value != trunc(value) || value < 0 || value >= (double)count)
    qa_range_error("HUD holdable item outside source array: %s", QA_JS_NUMBER(value));
  qa_q3_pres_client_draw_tools_draw_pic(tools_of(hud), make_rect(592, 216, ICON, ICON), items[(size_t)value].icon);
  return true;
}
static void fixed_text(qa_q3_pres_client_hud *hud, double x, double y, const char *text, double char_width,
                       double char_height, qa_vec4 color, bool force_color) {
  qa_q3_fixed_text_options options;
  memset(&options, 0, sizeof options);
  options.x = trunc(x);
  options.y = y;
  options.text = text;
  options.color = color;
  options.char_width = char_width;
  options.char_height = char_height;
  options.max_chars = 0;
  options.force_color = force_color;
  options.shadow = true;
  qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(hud), options);
}
static void proportional(qa_q3_pres_client_hud *hud, const char *text, double y, double scale, qa_vec4 color,
                         int32_t style, bool integer_width) {
  if (hud->variant.kind != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("Proportional HUD text requires missionpack fonts");
  const qa_q3_font_set *fonts = qa_q3_pres_mission_hud_fonts(hud->variant.menus);
  double width = qa_q3_text_width(fonts, text, scale, 0);
  double half = integer_width ? trunc(width / 2) : f32(width / 2);
  qa_q3_text_paint_options options;
  memset(&options, 0, sizeof options);
  options.x = f32(320 - half);
  options.y = y;
  options.scale = scale;
  options.color = color;
  options.text = text;
  options.adjust = 0;
  options.limit = 0;
  options.style = style;
  qa_q3_text_paint(&tools_of(hud)->draw, fonts, &options);
}
static char *format_alloc(const char *format, const qa_q3_game_format_arg *args, size_t count, double max_bytes) {
  char *text = qa_q3_game_format(format, args, count, max_bytes);
  if (!text) qa_range_error("HUD text allocation failed");
  return text;
}
void qa_q3_pres_client_hud_draw_reward(qa_q3_pres_client_hud *hud) {
  if (!enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_REWARDS)) return;
  qa_q3_pres_client_game_state *state = hud->state;
  qa_vec4 color;
  bool has_color = qa_q3_pres_fade_color(state->time, state->reward_time, 3000, &color);
  if (!has_color) {
    if (state->reward_stack <= 0) return;
    for (double index = 0; index < state->reward_stack; index++) *reward_at(hud, index) = *reward_at(hud, index + 1);
    state->reward_time = state->time;
    state->reward_stack--;
    has_color = qa_q3_pres_fade_color(state->time, state->reward_time, 3000, &color);
    hud->host.start_local_sound(hud->host.ctx, reward_at(hud, 0)->sound, 7);
  }
  set_color(hud, has_color ? &color : NULL);
  qa_q3_pres_client_reward *reward = reward_at(hud, 0);
  if (reward->count >= 10) {
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(hud), make_rect(296, 56, 44, 44), reward->shader);
    qa_q3_game_format_arg args[] = {qa_q3_game_format_number(reward->count)};
    char *text = format_alloc("%d", args, 1, 32);
    if (!has_color) qa_range_error("CG_DrawReward: source null text color at zero reward time");
    fixed_text(hud, (640 - 8 * qa_q3_pres_draw_strlen(text)) / 2, 104, text, 8, 16, color, false);
    free(text);
  } else {
    double x = 320 - reward->count * 24;
    for (double index = 0; index < reward->count; index++, x += ICON)
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(hud), make_rect(x, 56, 44, 44), reward->shader);
  }
  set_color(hud, NULL);
}
void qa_q3_pres_client_hud_draw_crosshair(qa_q3_pres_client_hud *hud) {
  if (!enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR) ||
      qa_q3_base_player_state_slots_get(&snapshot_of(hud)->persistant, QA_Q3_BASE_PERS_TEAM) == QA_Q3_BASE_TEAM_SPECTATOR ||
      hud->state->rendering_third_person)
    return;
  qa_q3_pres_client_draw_tools *tools = tools_of(hud);
  if (enabled(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_HEALTH)) {
    qa_vec4 color = qa_q3_pres_color_for_health(hud->state);
    set_color(hud, &color);
  } else {
    set_color(hud, NULL);
  }
  double size = f32(cvar_numeric(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_SIZE));
  double elapsed = f32(qa_int32(hud->state->time - hud->state->item_pickup_blend_time));
  if (elapsed > 0 && elapsed < 200) size = f32(size * f32(1 + f32(elapsed / 200)));
  double x2 = cvar_integer(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_X);
  double y2 = cvar_integer(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_Y);
  qa_rect rect = qa_q3_pres_client_draw_tools_adjust_from_640(tools,
      make_rect(x2, y2, size, size));
  double index = fmod(qa_js_max(0, cvar_integer(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR)), 10);
  const qa_q3_pres_scene_shader *shader = hud->host.media->graphics.crosshair_shader[(size_t)index];
  const qa_q3_pres_refdef *view = &hud->state->refdef;
  qa_rect place = rect;
  place.x = f32(f32(rect.x + f32(view->x)) + f32(0.5 * f32(f32(view->width) - rect.width)));
  place.y = f32(f32(rect.y + f32(view->y)) + f32(0.5 * f32(f32(view->height) - rect.height)));
  qa_text_texture_rect uv = {0, 0, 1, 1};
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_PICTURE_MATERIAL;
  picture.material = hud->host.media->resources.picture(hud->host.media->resources.ctx, shader);
  qa_draw2d_stretch_pixels(&tools->draw, place, uv, &picture);
}
void qa_q3_pres_client_hud_scan_for_crosshair_entity(qa_q3_pres_client_hud *hud) {
  const qa_q3_pres_refdef *view = &hud->state->refdef;
  qa_vec3 start = view->view_origin, axis = view->view_axis.vectors[0];
  qa_vec3 end = {f32(start.x + f32(131072 * axis.x)), f32(start.y + f32(131072 * axis.y)), f32(start.z + f32(131072 * axis.z))};
  qa_vec3 zero = {0, 0, 0};
  qa_bounds bounds = {zero, zero};
  qa_q3_pres_prediction_runtime *prediction = hud->host.prediction;
  qa_q3_pres_movement_trace trace = qa_q3_pres_prediction_runtime_trace(prediction, start, end, bounds,
      snapshot_of(hud)->client_num, 1 | 0x2000000);
  if (trace.entity_num >= 64) return;
  if (qa_int32(prediction->collision.point_contents(prediction->collision.ctx, trace.result.end, 0)) & 64) return;
  if (qa_int32(qa_q3_pres_client_game_state_entity_at(hud->state, trace.entity_num)->current_state.powerups) & (1 << QA_Q3_PW_INVIS)) return;
  hud->state->crosshair_client_num = trace.entity_num;
  hud->state->crosshair_client_time = hud->state->time;
}
void qa_q3_pres_client_hud_draw_crosshair_names(qa_q3_pres_client_hud *hud) {
  if (!enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR) || !enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR_NAMES) ||
      hud->state->rendering_third_person)
    return;
  qa_q3_pres_client_hud_scan_for_crosshair_entity(hud);
  qa_vec4 color;
  if (!qa_q3_pres_fade_color(hud->state->time, hud->state->crosshair_client_time, 1000, &color)) {
    set_color(hud, NULL);
    return;
  }
  const char *stored = client_at(hud, hud->state->crosshair_client_num)->name;
  const char *name = stored ? stored : "";
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    qa_vec4 faded = color;
    faded.w = f32(color.w * 0.5);
    proportional(hud, name, 190, (float)0.3, faded, 3, false);
  } else {
    qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 320 - qa_q3_pres_draw_strlen(name) * 8, 170, name, f32(color.w * 0.5));
  }
  set_color(hud, NULL);
}
void qa_q3_pres_client_hud_draw_spectator(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_draw_tools *tools = tools_of(hud);
  qa_q3_pres_client_draw_tools_draw_big_string(tools, 248, 440, "SPECTATOR", 1);
  if (hud->static_state->game_type == QA_Q3_BASE_GT_TOURNAMENT)
    qa_q3_pres_client_draw_tools_draw_big_string(tools, 200, 460, "waiting to play", 1);
  else if (hud->static_state->game_type >= QA_Q3_BASE_GT_TEAM)
    qa_q3_pres_client_draw_tools_draw_big_string(tools, 8, 460, "press ESC and use the JOIN menu to play", 1);
}
static double vote_seconds(double time, double vote_time) {
  return qa_js_max(0, trunc((double)qa_int32(30000 - (double)qa_int32(time - vote_time)) / 1000));
}
static void draw_vote_line(qa_q3_pres_client_hud *hud, const char *format, double y, double sec, const char *text, double yes, double no) {
  qa_q3_game_format_arg args[] = {qa_q3_game_format_number(sec), qa_q3_game_format_string(text ? text : ""),
                                  qa_q3_game_format_number(yes), qa_q3_game_format_number(no)};
  char *line = format_alloc(format, args, 4, BIG_BUFFER_BYTES);
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(hud), 0, y, line, 1);
  free(line);
}
void qa_q3_pres_client_hud_draw_vote(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  if (cgs->vote_time == 0) return;
  if (cgs->vote_modified) {
    cgs->vote_modified = false;
    hud->host.start_local_sound(hud->host.ctx, hud->host.media->sounds.talk_sound, 6);
  }
  draw_vote_line(hud, "VOTE(%i):%s yes:%i no:%i", 58, vote_seconds(hud->state->time, cgs->vote_time), cgs->vote_string, cgs->vote_yes, cgs->vote_no);
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    qa_q3_pres_client_draw_tools_draw_small_string(tools_of(hud), 0, 76, "or press ESC then click Vote", 1);
}
void qa_q3_pres_client_hud_draw_team_vote(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  qa_q3_base_team team = client_at(hud, 0)->team;
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) return;
  int index = team == QA_Q3_BASE_TEAM_RED ? 0 : 1;
  if (cgs->team_vote_time[index] == 0) return;
  if (cgs->team_vote_modified[index]) {
    cgs->team_vote_modified[index] = false;
    hud->host.start_local_sound(hud->host.ctx, hud->host.media->sounds.talk_sound, 6);
  }
  draw_vote_line(hud, "TEAMVOTE(%i):%s yes:%i no:%i", 90, vote_seconds(hud->state->time, cgs->team_vote_time[index]),
                 cgs->team_vote_string[index], cgs->team_vote_yes[index], cgs->team_vote_no[index]);
}
bool qa_q3_pres_client_hud_draw_follow(qa_q3_pres_client_hud *hud) {
  qa_q3_base_source_player_state *ps = snapshot_of(hud);
  if (!(qa_int32(ps->pm_flags) & QA_Q3_FLAG_FOLLOW)) return false;
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 248, 24, "following", 1);
  const char *stored = client_at(hud, ps->client_num)->name;
  const char *name = stored ? stored : "";
  fixed_text(hud, 0.5 * (640 - 32 * qa_q3_pres_draw_strlen(name)), 40, name, 32, 48, WHITE, true);
  return true;
}
void qa_q3_pres_client_hud_draw_ammo_warning(qa_q3_pres_client_hud *hud) {
  if (hud->host.weapon_hud || !enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_AMMO_WARNING) || hud->state->low_ammo_warning == 0) return;
  const char *text = hud->state->low_ammo_warning == 2 ? "OUT OF AMMO" : "LOW AMMO WARNING";
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 320 - qa_q3_pres_draw_strlen(text) * 8, 64, text, 1);
}
void qa_q3_pres_client_hud_draw_prox_warning(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("CG_DrawProxWarning is excluded from the baseq3 build");
  if (!(qa_int32(snapshot_of(hud)->e_flags) & 2)) {
    hud->prox_time = 0;
    return;
  }
  if (hud->prox_time == 0) {
    hud->prox_time = qa_int32(hud->state->time + 5000);
    hud->prox_counter = 5;
    hud->prox_tick = 0;
  }
  if (hud->state->time > hud->prox_time) {
    hud->prox_tick = hud->prox_counter;
    hud->prox_counter = qa_int32(hud->prox_counter - 1);
    hud->prox_time = qa_int32(hud->state->time + 1000);
  }
  char *text = NULL;
  if (hud->prox_tick != 0) {
    qa_q3_game_format_arg args[] = {qa_q3_game_format_number(hud->prox_tick)};
    text = format_alloc("INTERNAL COMBUSTION IN: %i", args, 1, 32);
  }
  const char *line = text ? text : "YOU HAVE BEEN MINED";
  qa_q3_pres_client_draw_tools_draw_big_string_color(tools_of(hud), 320 - qa_q3_pres_draw_strlen(line) * 8, 80, line, RED);
  free(text);
}
void qa_q3_pres_client_hud_draw_warmup(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_game_state *state = hud->state;
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  bool mission = hud->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  if (state->warmup == 0) return;
  if (state->warmup < 0) {
    const char *text = "Waiting for players";
    qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 320 - qa_q3_pres_draw_strlen(text) * 8, 24, text, 1);
    state->warmup_count = 0;
    return;
  }
  const char *heading = "";
  char *owned_heading = NULL;
  bool draw_heading = true;
  if (cgs->game_type == QA_Q3_BASE_GT_TOURNAMENT) {
    qa_q3_pres_client_info *first = NULL, *second = NULL;
    for (double index = 0; index < cgs->maxclients; index++) {
      qa_q3_pres_client_info *client = client_at(hud, index);
      if (client->info_valid && client->team == QA_Q3_BASE_TEAM_FREE) {
        if (!first) first = client;
        else second = client;
      }
    }
    if (first && second) {
      qa_q3_game_format_arg args[] = {qa_q3_game_format_string(first->name ? first->name : ""),
                                      qa_q3_game_format_string(second->name ? second->name : "")};
      owned_heading = format_alloc("%s vs %s", args, 2, BIG_BUFFER_BYTES);
      heading = owned_heading;
    } else {
      draw_heading = false;
    }
  } else if (cgs->game_type == QA_Q3_BASE_GT_FFA) heading = "Free For All";
  else if (cgs->game_type == QA_Q3_BASE_GT_TEAM) heading = "Team Deathmatch";
  else if (cgs->game_type == QA_Q3_BASE_GT_CTF) heading = "Capture the Flag";
  else if (mission) {
    if (cgs->game_type == QA_Q3_BASE_GT_1FCTF) heading = "One Flag CTF";
    else if (cgs->game_type == QA_Q3_BASE_GT_OBELISK) heading = "Overload";
    else if (cgs->game_type == QA_Q3_BASE_GT_HARVESTER) heading = "Harvester";
  }
  if (draw_heading) {
    bool tournament = cgs->game_type == QA_Q3_BASE_GT_TOURNAMENT;
    if (mission) {
      proportional(hud, heading, tournament ? 60 : 90, (float)0.6, WHITE, 6, true);
    } else {
      double width = qa_q3_pres_draw_strlen(heading), cw = width > 20 ? trunc(640 / width) : 32;
      fixed_text(hud, 320 - trunc(width * cw / 2), tournament ? 20 : 25, heading, cw,
                 trunc(f32(cw * f32(tournament ? 1.5 : 1.1))), WHITE, false);
    }
  }
  free(owned_heading);
  double sec = trunc((double)qa_int32(state->warmup - state->time) / 1000);
  if (sec < 0) {
    state->warmup = 0;
    sec = 0;
  }
  qa_q3_game_format_arg args[] = {qa_q3_game_format_number(qa_int32(sec + 1))};
  char *text = format_alloc("Starts in: %i", args, 1, BIG_BUFFER_BYTES);
  if (sec != state->warmup_count) {
    state->warmup_count = sec;
    const qa_q3_pres_client_media_sounds *sounds = &hud->host.media->sounds;
    if (sec == 0) hud->host.start_local_sound(hud->host.ctx, sounds->count1_sound, 7);
    else if (sec == 1) hud->host.start_local_sound(hud->host.ctx, sounds->count2_sound, 7);
    else if (sec == 2) hud->host.start_local_sound(hud->host.ctx, sounds->count3_sound, 7);
  }
  double count = state->warmup_count, cw = count == 0 ? 28 : count == 1 ? 24 : count == 2 ? 20 : 16;
  if (mission) proportional(hud, text, 125, (float)(count == 0 ? 0.54 : count == 1 ? 0.51 : count == 2 ? 0.48 : 0.45), WHITE, 6, true);
  else fixed_text(hud, 320 - trunc(qa_q3_pres_draw_strlen(text) * cw / 2), 70, text, cw, trunc(cw * 1.5), WHITE, false);
  free(text);
}
void qa_q3_pres_client_hud_draw_timed_menus(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("CG_DrawTimedMenus is excluded from baseq3");
  qa_q3_pres_mission_hud_draw_timed_menus(hud->variant.menus);
}
bool qa_q3_pres_client_hud_draw_scoreboard(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK) return qa_q3_pres_mission_hud_draw_scoreboard(hud->variant.menus);
  return qa_q3_pres_base_scoreboard_draw(hud->variant.scoreboard);
}
void qa_q3_pres_client_hud_draw_intermission(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_BASEQ3 && hud->static_state->game_type == QA_Q3_BASE_GT_SINGLE_PLAYER) {
    qa_q3_pres_client_draw_status_draw_center_string(hud->host.status);
    return;
  }
  hud->state->score_fade_time = hud->state->time;
  hud->state->score_board_showing = qa_q3_pres_client_hud_draw_scoreboard(hud);
}
bool qa_q3_pres_client_hud_draw_tourney_scoreboard(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_BASEQ3) return qa_q3_pres_base_scoreboard_draw_tourney(hud->variant.scoreboard);
  return false;
}
void qa_q3_pres_client_hud_draw_2d(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_game_state *state = hud->state;
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  bool mission = hud->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  if (mission && cgs->order_pending && state->time > cgs->order_time)
    qa_q3_pres_mission_hud_check_order_pending(hud->variant.menus);
  if (state->level_shot || !enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_2D)) return;
  qa_q3_base_source_player_state *ps = snapshot_of(hud);
  if (ps->pm_type == QA_Q3_PM_INTERMISSION) {
    qa_q3_pres_client_hud_draw_intermission(hud);
    return;
  }
  if (qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) == QA_Q3_BASE_TEAM_SPECTATOR) {
    qa_q3_pres_client_hud_draw_spectator(hud);
    qa_q3_pres_client_hud_draw_crosshair(hud);
    qa_q3_pres_client_hud_draw_crosshair_names(hud);
  } else {
    const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps));
    if (!state->show_scores && qa_q3_base_player_state_slots_get(&ps->stats, stats->health) > 0) {
      if (mission) {
        if (enabled(hud, QA_Q3_PRES_HUD_CVAR_DRAW_STATUS)) {
          qa_q3_pres_mission_hud_paint_all(hud->variant.menus);
          qa_q3_pres_client_hud_draw_timed_menus(hud);
        }
      } else {
        qa_q3_pres_client_hud_draw_status_bar(hud);
      }
      qa_q3_pres_client_hud_draw_ammo_warning(hud);
      if (mission) qa_q3_pres_client_hud_draw_prox_warning(hud);
      qa_q3_pres_client_hud_draw_crosshair(hud);
      qa_q3_pres_client_hud_draw_crosshair_names(hud);
      if (!hud->host.weapon_hud) qa_q3_pres_client_weapon_runtime_draw_weapon_select(hud->host.weapons);
      if (!mission && !qa_q3_pres_client_hud_draw_holdable_item(hud)) qa_range_error("HUD item registration allocation failed");
      qa_q3_pres_client_hud_draw_reward(hud);
    }
    if (cgs->game_type >= QA_Q3_BASE_GT_TEAM && !mission) qa_q3_pres_client_hud_corners_draw_team_info(hud->host.corners);
  }
  qa_q3_pres_client_hud_draw_vote(hud);
  qa_q3_pres_client_hud_draw_team_vote(hud);
  if (!qa_q3_pres_client_draw_status_draw_lagometer(hud->host.status)) qa_range_error("HUD lagometer allocation failed");
  if (!mission || !enabled(hud, QA_Q3_PRES_HUD_CVAR_PAUSED)) qa_q3_pres_client_hud_corners_draw_upper_right(hud->host.corners);
  if (!mission) {
    qa_q3_pres_client_hud_corners_draw_lower_right(hud->host.corners);
    qa_q3_pres_client_hud_corners_draw_lower_left(hud->host.corners);
  }
  if (!qa_q3_pres_client_hud_draw_follow(hud)) qa_q3_pres_client_hud_draw_warmup(hud);
  state->score_board_showing = qa_q3_pres_client_hud_draw_scoreboard(hud);
  if (!state->score_board_showing) qa_q3_pres_client_draw_status_draw_center_string(hud->host.status);
}

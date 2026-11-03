/* Port of src/content/q3/presentation/mission-hud.ts. GPL-2.0-or-later.
 * Team Arena HUD from id Software's cg_newdraw.c, cg_main.c and cg_draw.c:
 * one cgDC/menu lifetime, sharing canonical cg/cgs and the engine
 * registration caches. TS async flows are synchronous. Plain TS Error and
 * RangeError throws abort with the TS text. bool results are false only
 * on OOM unless the declaration says otherwise. */
#ifndef QA_CONTENT_Q3_PRESENTATION_MISSION_HUD_H
#define QA_CONTENT_Q3_PRESENTATION_MISSION_HUD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/contracts/common.h"
#include "qa/contracts/math.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/core/game_numeric.h"
#include "qa/content/q3/presentation/config.h"
#include "qa/content/q3/presentation/draw_icons.h"
#include "qa/content/q3/presentation/media.h"
#include "qa/content/q3/presentation/mission_owner_draw.h"
#include "qa/content/q3/presentation/player_state.h"
#include "qa/content/q3/presentation/players.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/content/q3/presentation/ui_adapters.h"
#include "qa/content/q3/presentation/weapons.h"
#include "qa/text/q3_font.h"
#include "qa/ui/common/legacy/menu.h"
#include "qa/ui/common/legacy/runtime.h"
#include "qa/ui/common/legacy/team_arena/memory.h"
typedef enum qa_q3_pres_mission_score_feeder {
  QA_Q3_PRES_FEEDER_RED = 5, QA_Q3_PRES_FEEDER_BLUE = 6, QA_Q3_PRES_FEEDER_SCOREBOARD = 11
} qa_q3_pres_mission_score_feeder;
/* TS MissionHudHost. weapon_hud NULL is the TS absent optional. append is
 * TS commands.append; ui_commands is the concrete buffer qa_ui_runtime
 * requires and must be the buffer append feeds. */
typedef struct qa_q3_pres_mission_hud_host {
  void *weapon_hud_ctx;
  qa_q3_pres_weapon_hud_reading (*weapon_hud)(void *ctx);
  qa_q3_pres_sound_asset_reader assets;
  qa_q3_ui_asset_registry *font_registry;
  qa_q3_pres_client_draw_icons *icons;
  qa_q3_pres_client_configuration *configuration;
  qa_cvar_registry *cvars;
  qa_command_buffer *ui_commands;
  qa_command_context command_context;
  qa_q3_pres_client_info_store *clients;
  qa_game_random *random;
  qa_q3_pres_engine_ui_cinematics cinematics;
  qa_q3_pres_engine_ui_model_painter *model_painter;
  qa_ui_runtime_audio audio;
  qa_q3_pres_client_weapon_media_registry *weapon_registry; /* TS media.weaponRegistry */
  void *ctx;
  void (*append)(void *ctx, const char *text);
  const char *(*config_string)(void *ctx, double index);
  void (*reset_player_entity)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*print)(void *ctx, const char *text);
  double (*milliseconds)(void *ctx);
  void (*set_key_catcher)(void *ctx, double mask);
} qa_q3_pres_mission_hud_host;
typedef struct qa_q3_pres_mission_hud_map qa_q3_pres_mission_hud_map;
typedef struct qa_q3_pres_mission_hud_font_map qa_q3_pres_mission_hud_font_map;
/* TS chat() result; strings stay owned by the HUD. */
typedef struct qa_q3_pres_mission_hud_chat {
  const char *system, *team1, *team2;
} qa_q3_pres_mission_hud_chat;
/* TS cachedAssets() result. */
typedef struct qa_q3_pres_mission_hud_cached_assets {
  const qa_q3_pres_scene_shader *fx_base;
  const qa_q3_pres_scene_shader *fx_colors[7];
  qa_ui_widget_assets widgets;
} qa_q3_pres_mission_hud_cached_assets;
typedef struct qa_q3_pres_mission_hud {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_client_media *media;
  qa_q3_pres_mission_hud_host host;
  qa_q3_pres_mission_owner_draw owner_draw;
  char system_chat[256], team_chat1[256], team_chat2[256];
  qa_q3_registered_font small_font, normal_font, big_font; /* stable storage fonts.* points at */
  qa_q3_font_set fonts;
  qa_ui_menu_definitions **definitions; /* every loaded generation; the runtime borrows the newest */
  size_t definition_count;
  qa_ui_runtime *runtime; /* NULL is TS loaded === null */
  qa_ui_captured_menu *scoreboard, *captured_menu;
  bool scoreboard_first_time, closed, loading;
  double generation;
  qa_ui_widget_assets widget_assets;
  const qa_q3_pres_scene_shader *fx_base;
  const qa_q3_pres_scene_shader *fx_colors[7];
  qa_q3_pres_mission_hud_map *pictures, *sounds, *models;
  qa_q3_pres_mission_hud_font_map *registered_fonts;
  const qa_ui_global_assets *asset_definitions; /* NULL is TS undefined */
  qa_ui_team_arena_memory *memory;
  char feeder_text[64];
  /* Per-load lifetime values for the resolver and sink callbacks. */
  void **load_buffers;
  size_t load_buffer_count;
  const char *load_path;
  qa_ui_script_source load_root;
  double load_generation;
} qa_q3_pres_mission_hud;
/* Mixed owners abort with the TS Error text. */
bool qa_q3_pres_mission_hud_init(qa_q3_pres_mission_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_client_media *media,
    qa_q3_pres_mission_hud_host host);
/* Releases storage after dispose (calls dispose when still open). */
void qa_q3_pres_mission_hud_destroy(qa_q3_pres_mission_hud *hud);
/* TS fonts getter object: refreshes the live cvar thresholds. */
const qa_q3_font_set *qa_q3_pres_mission_hud_fonts(qa_q3_pres_mission_hud *hud);
qa_q3_pres_mission_hud_cached_assets qa_q3_pres_mission_hud_cached(const qa_q3_pres_mission_hud *hud);
/* TS menuState(); false is TS null. out owned (qa_ui_runtime_snapshot_destroy). */
bool qa_q3_pres_mission_hud_menu_state(qa_q3_pres_mission_hud *hud, qa_ui_runtime_snapshot *out);
bool qa_q3_pres_mission_hud_asset_cache(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_other_team_has_flag(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_your_team_has_flag(qa_q3_pres_mission_hud *hud);
/* Malloc'd; NULL is TS null after printing the TS diagnostic. */
char *qa_q3_pres_mission_hud_get_menu_buffer(qa_q3_pres_mission_hud *hud, const char *filename);
void qa_q3_pres_mission_hud_reset_strings(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_reset_menus(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_load_hud_menu(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_load_menus(qa_q3_pres_mission_hud *hud, const char *path);
void qa_q3_pres_mission_hud_dispose(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_init_team_chat(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_set_print_string(qa_q3_pres_mission_hud *hud, double type, const char *value);
qa_q3_pres_mission_hud_chat qa_q3_pres_mission_hud_chat_text(const qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_check_order_pending(qa_q3_pres_mission_hud *hud);
double qa_q3_pres_mission_hud_get_selected_player(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_select_next_player(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_select_previous_player(qa_q3_pres_mission_hud *hud);
double qa_q3_pres_mission_hud_feeder_count(qa_q3_pres_mission_hud *hud, double feeder);
/* out->text is borrowed until the next feeder call. */
void qa_q3_pres_mission_hud_feeder_item(qa_q3_pres_mission_hud *hud, double feeder, double index, double column,
                                        qa_ui_runtime_feeder_item *out);
void qa_q3_pres_mission_hud_feeder_selection(qa_q3_pres_mission_hud *hud, double feeder, double index);
/* TS setScoreSelection() and setScoreSelection(menu). */
void qa_q3_pres_mission_hud_set_score_selection(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_set_score_selection_menu(qa_q3_pres_mission_hud *hud, qa_ui_captured_menu *menu);
void qa_q3_pres_mission_hud_clear_scoreboard(qa_q3_pres_mission_hud *hud);
qa_ui_captured_menu *qa_q3_pres_mission_hud_menu_scoreboard(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_scroll_feeder(qa_q3_pres_mission_hud *hud, qa_ui_captured_menu *menu, double feeder, bool down);
void qa_q3_pres_mission_hud_paint_all(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_draw_scoreboard(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_close_by_name(qa_q3_pres_mission_hud *hud, const char *name);
void qa_q3_pres_mission_hud_show_response_head(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_draw_timed_menus(qa_q3_pres_mission_hud *hud);
double qa_q3_pres_mission_hud_client_num_from_name(qa_q3_pres_mission_hud *hud, const char *name);
void qa_q3_pres_mission_hud_hide_team_menu(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_show_team_menu(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_event_handling(qa_q3_pres_mission_hud *hud, double type);
void qa_q3_pres_mission_hud_mouse_event(qa_q3_pres_mission_hud *hud, double x, double y);
void qa_q3_pres_mission_hud_key_event(qa_q3_pres_mission_hud *hud, double key, bool down);
#endif

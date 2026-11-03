/* Port of src/app/bootstrap/ui.ts (ApplicationSeatUi). GPL-2.0-or-later. */
#include "qa/app/bootstrap/ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/player_death.h"
#include "qa/app/bootstrap/q2_match_ui.h"
#include "qa/app/bootstrap/seat_hud_state.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/weapon_hud.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"
#include "qa/platform/sdl.h"
#include "qa/text/layout.h"
#include "qa/text/ui.h"
#include "qa/ui/common/captions.h"
#include "qa/ui/common/draw.h"
#include "qa/ui/common/layout.h"
#include "qa/ui/common/menu_theme.h"
#include "qa/ui/common/skin.h"
#include "qa/ui/hud/weapon.h"
#include "qa/ui/hud/wheel.h"
#include "qa/ui/library/inventory.h"
#include "qa/ui/library/match.h"
#include "qa/ui/settings/action_catalog.h"
#include "qa/ui/settings/bindings.h"
#include "qa/ui/settings/console.h"
#include "qa/ui/settings/gyro.h"
#include "qa/ui/settings/images.h"
#include "qa/ui/settings/input_routing.h"
#include "qa/ui/settings/language.h"
#include "qa/ui/settings/ranking_account.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

static void *xcalloc(size_t count, size_t size) {
  void *value = calloc(count == 0 ? 1 : count, size);
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static char *xdup(const char *text) {
  if (text == NULL) return NULL;
  char *copy = qa_input_dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}

/* ---- ui.ts menuCatalogs ---- */
static qa_ui_menu_catalog *menu_catalogs; static size_t menu_catalog_count;
void qa_app_seat_ui_register_menu_catalog(qa_ui_menu_catalog catalog) {
  for (size_t i = 0; i < menu_catalog_count; i++)
    if (is(menu_catalogs[i].language, catalog.language)) { menu_catalogs[i] = catalog; return; }
  qa_ui_menu_catalog *grown = realloc(menu_catalogs, (menu_catalog_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  menu_catalogs = grown;
  menu_catalogs[menu_catalog_count++] = catalog;
}
static qa_ui_menu_catalogs current_catalogs(void *ctx) {
  (void)ctx; /* the module-level menuCatalogs */
  qa_ui_menu_catalogs catalogs = {menu_catalogs, menu_catalog_count};
  return catalogs;
}

/* Pause-menu and settings button ids. */
enum { PAUSE_RESUME, PAUSE_SAVE, PAUSE_LOAD, PAUSE_SETTINGS, PAUSE_CONSOLE, PAUSE_PROGRESS, PAUSE_MATCH, PAUSE_LOBBY, PAUSE_QUIT,
       PAUSE_COUNT };
enum { SETTING_BINDINGS, SETTING_GYRO, SETTING_SERVER, SETTING_RANKING, SETTING_JOIN, SETTING_DROP, SETTING_BUTTON_COUNT };

typedef struct ui_button { qa_app_seat_ui *ui; int which; } ui_button;
typedef struct wheel_icon { char *item; char *resource; } wheel_icon;
typedef struct owned_bindings { qa_ui_setting_bindings bindings; } owned_bindings;

struct qa_app_seat_ui {
  qa_app_seat_ui_options options;
  qa_app_local_input *local;
  const qa_native_ui_art *art;
  qa_app_application_input *input;
  qa_seat_id seat;
  qa_native_ui_controller *controller;
  bool manual_pause;
  qa_app_seat_player_death *death;
  qa_app_q2_native_hud *native_q2_hud;
  qa_ui_settings_menus inventory_menu;
  bool has_match_menu; qa_ui_settings_menus match_menu;
  qa_app_base_arena_menus *base_arena;
  qa_app_seat_sound_captions *sound_captions;
  qa_app_seat_source_hud *source_hud;
  qa_app_application_q1_wheel *authored_wheel;
  qa_app_team_arena_results *team_arena;
  qa_ui_saved_game_menus saves;
  qa_ui_seat_preferences preferences;
  qa_seat_hud_messages *messages;
  qa_seat_weapon_wheel *weapon_wheel;
  qa_ui_text_renderer text, menu_text;
  qa_app_q2_match_ui *match;
  qa_app_seat_game_prompt *prompt;
  qa_ui_settings_menus settings, gyro_settings;
  bool has_server_settings; qa_ui_settings_menus server_settings;
  bool has_ranking_menu; qa_ui_settings_menus ranking_menu;
  bool has_llm_menu; qa_ui_settings_menus llm_menu;
  qa_ui_settings_menus bindings;
  qa_app_input_ui_attachment *dispose_input;
  int dispose_menu;
  wheel_icon *wheel_icons; size_t wheel_icon_count, wheel_icon_capacity;
  qa_app_weapon_hud_assets *weapon_assets;
  qa_resource_id weapon_icon, ammo_icon; /* NULL is TS null */
  bool has_native_item;
  char *native_item; qa_provider_reference native_source; char *native_source_content, *native_source_provider;
  char *native_label, *native_localized_label; bool native_has_icon; qa_resource_id native_icon; double native_aspect;
  qa_app_rerelease_presentation *native_hud_localizations;
  qa_text_font_selection font;
  const qa_app_menu_typography *typography;
  /* closure state */
  ui_button pause_buttons[PAUSE_COUNT];
  qa_ui_control pause_controls[PAUSE_COUNT];
  ui_button setting_buttons[SETTING_BUTTON_COUNT];
  owned_bindings *owned; size_t owned_count, owned_capacity;
  qa_ui_setting_binding *settings_items; size_t settings_item_count;
  qa_ui_gyro_settings gyro;
  /* services handed to the settings ports by pointer */
  qa_ui_binding_reset binding_reset;
  qa_ui_controller_vibration_service vibration;
  qa_ui_audio_settings_service audio_service;
  qa_ui_audio_output_settings audio_output;
  qa_ui_renderer_settings_options renderer;
  qa_ui_settings_reset settings_reset;
  qa_ui_weapon_wheel_services wheel_services;
  qa_ui_binding_actions binding_actions; bool has_binding_actions;
  qa_weapon_binding_item *binding_items;
  qa_ui_wheel_item *wheel_items; size_t wheel_item_count;
  qa_app_q1_wheel_items_result q1_items; bool has_q1_items;
  char **output_names; size_t output_name_count;
  qa_controller_device *devices; size_t device_count;
  char *language_text, *localized_text, *prompt_language, *clipboard_text;
  qa_app_q2_match_setting match_settings[8]; char match_setting_text[3][32];
  bool closed;
};

/* ---- simulation views ---- */
static qa_app_sim_player_ui player_ui(const qa_app_seat_ui *ui) {
  const qa_app_sim_presentation_access *simulation = ui->options.simulation;
  return simulation->player_ui(simulation->ctx, ui->local->player.actor);
}

static qa_seat_input_focus local_focus(const qa_app_seat_ui *ui) { return qa_seat_input_get_focus(ui->local->input); }
static bool focus_game(const qa_app_seat_ui *ui) { return is(local_focus(ui).kind, "game"); }

/* readLanguage: the seat language of the shared settings, else "english". */
static char *read_language(qa_app_seat_ui *ui) {
  qa_cvar_registry *shared = qa_app_application_input_shared_settings(ui->input);
  char *language = shared == NULL ? xdup("english") : qa_ui_read_seat_language(shared, ui->seat.index);
  if (language == NULL) qa_range_error("Out of memory");
  return language;
}
static char *localizer_language(void *ctx) { return read_language(ctx); }
static const char *borrowed_language(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  free(ui->language_text);
  ui->language_text = read_language(ui);
  return ui->language_text;
}

static qa_ui_preference_values preferences(const qa_app_seat_ui *ui) {
  return qa_ui_seat_preferences_values(&ui->preferences);
}
static bool bold(const qa_app_seat_ui *ui) { return is(preferences(ui).typeface, "bold"); }
/* TS hudFont / menuFont getters. */
static qa_text_font_selection hud_font(const qa_app_seat_ui *ui) {
  return bold(ui) ? qa_app_menu_typography_title(ui->typography) : ui->font;
}
static qa_text_font_selection menu_font(const qa_app_seat_ui *ui) {
  return bold(ui) ? qa_app_menu_typography_title(ui->typography) : qa_app_menu_typography_body(ui->typography);
}

/* layoutText({ text, font, scale, color: white }). */
static qa_vec2 measure(qa_text_font_selection font, const char *text, double scale) {
  qa_text_layout_options options;
  memset(&options, 0, sizeof options);
  options.text = text; options.text_length = strlen(text); options.font = font; options.scale = scale;
  options.color.x = 1; options.color.y = 1; options.color.z = 1; options.color.w = 1;
  qa_text_layout layout;
  if (!qa_layout_text(&options, &layout)) qa_range_error("Out of memory");
  qa_vec2 size = {layout.width, layout.height};
  qa_text_layout_destroy(&layout);
  return size;
}
static double measure_hud_text(void *ctx, const char *text, double scale) { return measure(hud_font(ctx), text, scale).x; }
static double measure_menu_text(void *ctx, const char *text, double scale) { return measure(menu_font(ctx), text, scale).x; }

static void console_print(qa_app_seat_ui *ui, const char *text) { qa_seat_console_print(ui->local->console, text); }
/* SeatSoundCaptions warn: text => local.console.print(text). */
static void caption_warn(void *ctx, const char *text) { console_print(ctx, text); }
static void console_line(void *ctx, const char *text) {
  qa_app_seat_ui *ui = ctx;
  size_t length = strlen(text) + 2;
  char *line = xcalloc(length, 1);
  snprintf(line, length, "%s\n", text);
  console_print(ui, line);
  free(line);
}

static void command(qa_app_seat_ui *ui, const char *name, const char *const *args, size_t count) {
  ui->options.command(ui->options.command_ctx, name, args, count);
}
static void command_fn(void *ctx, const char *name, const char *const *args, size_t count) { command(ctx, name, args, count); }

static void own_bindings(qa_app_seat_ui *ui, qa_ui_setting_bindings bindings) {
  if (ui->owned_count == ui->owned_capacity) {
    size_t next = ui->owned_capacity == 0 ? 16 : ui->owned_capacity * 2;
    owned_bindings *grown = realloc(ui->owned, next * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    ui->owned = grown; ui->owned_capacity = next;
  }
  ui->owned[ui->owned_count++].bindings = bindings;
}

/* ---- NativeUiController options ---- */
static qa_ui_skin controller_skin(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  qa_ui_skin skin = qa_ui_menu_skin(ui->art->skin.font);
  if (is(qa_native_ui_controller_active_menu(ui->controller), qa_app_game_prompt_menu)) {
    skin.has_title_font = true; skin.title_font = ui->art->skin.font;
    skin.has_title_scale = true; skin.title_scale = 2.6;
  }
  return skin;
}
static double controller_now(void *ctx) { qa_app_seat_ui *ui = ctx; return qa_app_application_input_now(ui->input); }
static const qa_input_binding *controller_bindings(void *ctx, size_t *out_count) {
  qa_app_seat_ui *ui = ctx;
  return qa_seat_input_bindings(ui->local->input, out_count);
}
static qa_native_ui_appearance controller_appearance(void *ctx) {
  qa_ui_preference_values values = preferences(ctx);
  qa_native_ui_appearance appearance = {values.menu_scale, values.text_scale, values.high_contrast, values.color_mode};
  return appearance;
}
static const char *controller_localize(void *ctx, const char *text) {
  qa_app_seat_ui *ui = ctx;
  qa_ui_menu_localizer localizer = {ui, current_catalogs, localizer_language};
  return qa_ui_menu_localize(&localizer, text);
}
static void controller_focus(void *ctx, qa_seat_input_focus focus, double time) {
  qa_app_seat_ui *ui = ctx;
  qa_seat_input_set_focus(ui->local->input, focus, time);
  qa_seat_haptics_set_active(ui->local->haptics, qa_seat_input_focused(ui->local->input) && is(focus.kind, "game"));
  qa_input_router_update_capture(qa_app_application_input_router(ui->input));
}
static const char *controller_clipboard(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  size_t length = 0;
  uint8_t *bytes = qa_read_sdl_clipboard(&length);
  if (bytes == NULL) return NULL;
  free(ui->clipboard_text);
  ui->clipboard_text = xcalloc(length + 1, 1);
  memcpy(ui->clipboard_text, bytes, length);
  free(bytes);
  return ui->clipboard_text;
}
static void controller_sound(void *ctx, qa_ui_sound sound, qa_seat_id owner) {
  qa_app_seat_ui *ui = ctx;
  qa_app_application_audio_ui_sound(ui->options.audio, sound, owner);
}
static void controller_script(void *ctx, qa_legacy_ui_script script, qa_seat_id seat) {
  (void)ctx; (void)seat;
  qa_raise("Error", 0, 0, "Legacy UI module %s is not attached to this native menu", script.module);
}

/* ---- source HUD / prompt / match hosts ---- */
static char *source_hud_localize(void *ctx, qa_content_id content, const char *text, const char *const *args, size_t count) {
  qa_app_seat_ui *ui = ctx;
  return ui->options.localize(ui->options.localize_ctx, content, text, args, count);
}
static const qa_ui_hud_inventory_item *inventory_items(void *ctx, size_t *out_count) {
  qa_app_seat_ui *ui = ctx;
  const qa_ui_hud_inventory_item *items = NULL;
  if (!qa_app_seat_source_hud_inventory_items(ui->source_hud, &items, out_count)) { *out_count = 0; return NULL; }
  return items;
}
static qa_actor_id prompt_actor(void *ctx) { qa_app_seat_ui *ui = ctx; return ui->local->player.actor; }
static void prompt_impulse(void *ctx, double value) { qa_app_seat_ui *ui = ctx; qa_seat_input_set_impulse(ui->local->input, value); }
static const char *prompt_language(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  free(ui->prompt_language);
  ui->prompt_language = read_language(ui);
  return ui->prompt_language;
}
static const char *prompt_focus_kind(void *ctx) { return local_focus(ctx).kind; }

/* ---- weapon wheel services ---- */
static void clear_wheel_items(qa_app_seat_ui *ui) {
  free(ui->wheel_items); ui->wheel_items = NULL; ui->wheel_item_count = 0;
  if (ui->has_q1_items) { qa_app_q1_wheel_items_result_destroy(&ui->q1_items); ui->has_q1_items = false; }
}
static qa_resource_id wheel_icon_for(const qa_app_seat_ui *ui, const char *item) {
  for (size_t i = 0; i < ui->wheel_icon_count; i++) if (is(ui->wheel_icons[i].item, item)) return ui->wheel_icons[i].resource;
  return NULL;
}
static const qa_ui_wheel_item *wheel_items(void *ctx, qa_ui_wheel_mode mode, size_t *out_count) {
  qa_app_seat_ui *ui = ctx;
  clear_wheel_items(ui);
  bool weapons = is(mode, "weapons");
  qa_app_sim_player_ui player = player_ui(ui);
  const qa_ui_wheel_item *authored = NULL; size_t authored_count = 0;
  if (weapons && !ui->options.guest_ui) {
    if (qa_app_application_q1_wheel_items(ui->authored_wheel, &player, &ui->q1_items)) {
      ui->has_q1_items = true; authored = ui->q1_items.items; authored_count = ui->q1_items.count;
    }
  }
  const char *kind = weapons ? "weapon" : "powerup";
  /* TS `authored ?? (guest ? [] : items).filter(...).map(...)`: the
   * authored wheel's items are returned as built. */
  if (ui->has_q1_items) {
    *out_count = authored_count;
    return authored;
  } else if (!ui->options.guest_ui) {
    ui->wheel_items = xcalloc(player.item_count, sizeof *ui->wheel_items);
    for (size_t i = 0; i < player.item_count; i++) {
      const qa_app_sim_player_ui_item *item = &player.items[i];
      if (!is(item->kind, kind)) continue;
      qa_ui_wheel_item *out = &ui->wheel_items[ui->wheel_item_count++];
      memset(out, 0, sizeof *out);
      out->id = item->id; out->source_ordinal = item->source_ordinal; out->sort_order = item->source_ordinal;
      out->label = item->label; out->owned = item->owned; out->has_ammo = item->has_ammo;
      out->has_count = item->has_count; out->count = item->count; out->warning_count = item->warning_count;
      out->icon = wheel_icon_for(ui, item->id); out->selected_icon = wheel_icon_for(ui, item->id);
    }
  }
  *out_count = ui->wheel_item_count;
  return ui->wheel_items;
}
static const char *wheel_active(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  if (ui->options.guest_ui) return NULL;
  return player_ui(ui).active_weapon;
}
static void wheel_select(void *ctx, const char *id, qa_ui_wheel_mode mode, qa_seat_id seat) {
  (void)mode; (void)seat;
  const char *args[1] = {id};
  command(ctx, "use", args, 1);
}
static double wheel_now(void *ctx) { return controller_now(ctx); }
static void wheel_changed(void *ctx, qa_seat_id owner) { controller_sound(ctx, "move", owner); }

/* ---- binding menus ---- */
static const qa_ui_binding_action *binding_source(void *ctx, size_t *out_count) {
  qa_app_seat_ui *ui = ctx;
  if (ui->has_binding_actions) { qa_ui_binding_actions_free(&ui->binding_actions); ui->has_binding_actions = false; }
  free(ui->binding_items); ui->binding_items = NULL;
  size_t count = 0;
  if (!ui->options.guest_ui) {
    qa_app_sim_player_ui player = player_ui(ui);
    ui->binding_items = xcalloc(player.item_count, sizeof *ui->binding_items);
    for (size_t i = 0; i < player.item_count; i++) {
      ui->binding_items[i].id = player.items[i].id; ui->binding_items[i].label = player.items[i].label;
      ui->binding_items[i].kind = is(player.items[i].kind, "powerup") ? QA_WEAPON_BINDING_POWERUP : QA_WEAPON_BINDING_WEAPON;
    }
    count = player.item_count;
  }
  ui->binding_actions = qa_ui_shared_binding_actions(qa_input_command_builder_dialect(ui->local->builder), ui->binding_items,
                                                     count, qa_app_application_input_binding_capabilities(ui->input));
  ui->has_binding_actions = true;
  *out_count = ui->binding_actions.count;
  return ui->binding_actions.items;
}
static bool reset_available(void *ctx) { qa_app_seat_ui *ui = ctx; return qa_app_application_input_can_reset_bindings(ui->input, ui->seat); }
static void reset_apply(void *ctx) { qa_app_seat_ui *ui = ctx; qa_app_application_input_reset_bindings(ui->input, ui->seat); }

/* ---- settings button bindings ---- */
static bool setting_enabled(void *ctx) {
  ui_button *button = ctx;
  qa_app_seat_ui *ui = button->ui;
  switch (button->which) {
    case SETTING_SERVER: {
      size_t count = 0;
      const qa_ui_host_server_settings *host = ui->options.host_settings;
      if (host != NULL) (void)host->bindings(host->bindings_ctx, &count);
      return count > 0;
    }
    case SETTING_RANKING: return ui->options.rankings->current(ui->options.rankings->ctx) != NULL;
    case SETTING_JOIN: {
      double capacity = qa_app_application_input_local_player_capacity(ui->input);
      return (double)qa_app_application_input_local_count(ui->input) < (capacity < 4 ? capacity : 4);
    }
    case SETTING_DROP: return qa_app_application_input_can_remove_local_player(ui->input, ui->seat);
    default: return true;
  }
}
static void setting_activate(void *ctx) {
  ui_button *button = ctx;
  qa_app_seat_ui *ui = button->ui;
  switch (button->which) {
    case SETTING_BINDINGS: qa_native_ui_controller_open_menu(ui->controller, ui->bindings.root); return;
    case SETTING_GYRO: qa_native_ui_controller_open_menu(ui->controller, ui->gyro_settings.root); return;
    case SETTING_SERVER: if (ui->has_server_settings) qa_native_ui_controller_open_menu(ui->controller, ui->server_settings.root); return;
    case SETTING_RANKING: if (ui->has_ranking_menu) qa_native_ui_controller_open_menu(ui->controller, ui->ranking_menu.root); return;
    case SETTING_JOIN: command(ui, "local_join", NULL, 0); return;
    case SETTING_DROP: {
      char index[32];
      snprintf(index, sizeof index, "%s", QA_JS_NUMBER((double)ui->seat.index + 1));
      const char *args[1] = {index};
      command(ui, "local_drop", args, 1);
      return;
    }
    default: return;
  }
}
static qa_ui_setting_binding setting_button(qa_app_seat_ui *ui, int which, const char *id, const char *label,
                                            const char *category) {
  ui->setting_buttons[which].ui = ui; ui->setting_buttons[which].which = which;
  qa_ui_setting_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.id = id; binding.label = label; binding.kind = "button"; binding.category = category;
  binding.ctx = &ui->setting_buttons[which];
  binding.enabled = setting_enabled; binding.activate = setting_activate;
  return binding;
}

/* ---- input settings (vibration) ---- */
static qa_ui_controller_vibration_settings vibration_read(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  qa_ui_controller_vibration_settings values = {qa_seat_haptics_enabled(ui->local->haptics), qa_seat_haptics_strength(ui->local->haptics)};
  return values;
}
static void vibration_write(void *ctx, qa_ui_controller_vibration_settings values, unsigned mask) {
  qa_app_seat_ui *ui = ctx;
  if ((mask & QA_UI_VIBRATION_STRENGTH) != 0) (void)qa_seat_haptics_set_strength(ui->local->haptics, values.controller_vibration_strength);
  if ((mask & QA_UI_VIBRATION_ON) != 0) (void)qa_seat_haptics_set_enabled(ui->local->haptics, values.controller_vibration);
}

/* ---- audio settings ---- */
static qa_ui_audio_settings audio_read(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  qa_ui_audio_settings values = {qa_app_application_audio_effects_volume(ui->options.audio),
                                 qa_app_application_audio_music_volume(ui->options.audio)};
  return values;
}
static void audio_write(void *ctx, qa_ui_audio_settings values, unsigned mask) {
  qa_app_seat_ui *ui = ctx;
  if ((mask & QA_UI_AUDIO_EFFECTS) != 0) qa_app_application_audio_set_effects_volume(ui->options.audio, values.effects_volume);
  if ((mask & QA_UI_AUDIO_MUSIC) != 0) qa_app_application_audio_set_music_volume(ui->options.audio, values.music_volume);
}
static qa_audio_output_format format_read(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  return qa_app_application_audio_output_format(ui->options.audio);
}
static const char *format_select(void *ctx, qa_audio_output_format format) {
  qa_app_seat_ui *ui = ctx;
  qa_app_application_audio_select_output_format(ui->options.audio, format);
  return NULL;
}
static const char *output_selected(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  return qa_app_application_audio_selected_output(ui->options.audio);
}
static void free_output_names(qa_app_seat_ui *ui) {
  for (size_t i = 0; i < ui->output_name_count; i++) free(ui->output_names[i]);
  free(ui->output_names);
  ui->output_names = NULL; ui->output_name_count = 0;
}
static const char *const *output_devices(void *ctx, size_t *out_count) {
  qa_app_seat_ui *ui = ctx;
  free_output_names(ui);
  ui->output_names = qa_app_application_audio_output_device_names(ui->options.audio, &ui->output_name_count);
  *out_count = ui->output_name_count;
  return (const char *const *)ui->output_names;
}
static const char *output_select(void *ctx, const char *name) {
  qa_app_seat_ui *ui = ctx;
  qa_app_application_audio_select_output(ui->options.audio, name);
  return NULL;
}

/* ---- display settings ---- */
static const char *renderer_current(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  qa_sdl_window *window = qa_app_application_input_window(ui->input).sdl;
  return window != NULL && qa_sdl_window_backend(window) == QA_SDL_BACKEND_GL ? "gl" : "cpu";
}
static bool worker_read(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  return qa_cvar_registry_variable_value(qa_app_application_input_shared_settings(ui->input), "r_smp") != 0;
}
static const char *worker_write(void *ctx, bool value) {
  qa_app_seat_ui *ui = ctx;
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(qa_app_application_input_shared_settings(ui->input), "r_smp", value ? "1" : "0", false, &snapshot))
    qa_cvar_read_free(&snapshot);
  return NULL;
}
static const char *renderer_apply(void *ctx, const char *backend) {
  qa_app_seat_ui *ui = ctx;
  char text[64];
  snprintf(text, sizeof text, "vid_restart %s\n", backend);
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = ui->seat.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = ui->seat;
  context.origin.client = qa_session_client_id(qa_session_seat_client(ui->local->player.seat));
  qa_command_buffer_append(qa_app_application_input_commands(ui->input), text, &context, -1);
  return NULL;
}
static qa_sdl_window *current_window(void *ctx) { qa_app_seat_ui *ui = ctx; return qa_app_application_input_window(ui->input).sdl; }
static void mutate_window(void *ctx, const qa_ui_window_mutation *mutation) {
  qa_app_seat_ui *ui = ctx;
  if (ui->options.mutate_window != NULL) ui->options.mutate_window(ui->options.mutate_ctx, mutation);
  else qa_ui_apply_window_mutation(mutation);
}
static qa_input_router *routing_router(void *ctx) { qa_app_seat_ui *ui = ctx; return qa_app_application_input_router(ui->input); }
static const qa_controller_device *routing_devices(void *ctx, size_t *out_count) {
  qa_app_seat_ui *ui = ctx;
  qa_controller_devices_free(ui->devices, ui->device_count);
  ui->devices = qa_sdl_controllers_devices(qa_app_application_input_controllers(ui->input), &ui->device_count);
  if (ui->devices == NULL) ui->device_count = 0;
  *out_count = ui->device_count;
  return ui->devices;
}
static const qa_ui_ranking_account_actions *ranking_current(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  return ui->options.rankings->current(ui->options.rankings->ctx);
}
static void gameplay_reset(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  if (ui->options.gameplay != NULL) qa_ui_reset_gameplay_settings(ui->options.gameplay);
}
static void llm_dispose(void *menus) { qa_ui_settings_menus_dispose(menus); }

/* ---- pause menu ---- */
static void pause_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  ui_button *button = ctx;
  qa_app_seat_ui *ui = button->ui;
  switch (button->which) {
    case PAUSE_RESUME: qa_native_ui_controller_close_all(ui->controller); return;
    case PAUSE_SAVE: qa_native_ui_controller_open_menu(ui->controller, ui->saves.save); return;
    case PAUSE_LOAD: qa_native_ui_controller_open_menu(ui->controller, ui->saves.load); return;
    case PAUSE_SETTINGS: qa_native_ui_controller_open_menu(ui->controller, ui->settings.root); return;
    case PAUSE_CONSOLE: qa_native_ui_controller_close_all(ui->controller); qa_seat_console_toggle(ui->local->console); return;
    case PAUSE_PROGRESS: qa_native_ui_controller_open_menu(ui->controller, qa_app_arena_progress_root); return;
    case PAUSE_MATCH: qa_native_ui_controller_open_menu(ui->controller, "menu:application:match"); return;
    case PAUSE_LOBBY: ui->options.lobby->return_to_lobby(ui->options.lobby->ctx); return;
    default: ui->options.quit(ui->options.quit_ctx); return;
  }
}
static void pause_open(void *ctx, qa_seat_id seat) { (void)seat; ((qa_app_seat_ui *)ctx)->manual_pause = true; }
static void pause_close(void *ctx, qa_seat_id seat) { (void)seat; ((qa_app_seat_ui *)ctx)->manual_pause = false; }
static qa_ui_control pause_button(qa_app_seat_ui *ui, int which, qa_ui_control_id id, const char *label, double row) {
  ui->pause_buttons[which].ui = ui; ui->pause_buttons[which].which = which;
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id; control.kind = "button"; control.label = label; control.rect = qa_ui_menu_row_default(row);
  control.enabled = true; control.visible = true;
  control.ctx = &ui->pause_buttons[which]; control.activate_seat = pause_activate;
  return control;
}
static qa_ui_menu pause_menu(void *ctx) {
  qa_app_seat_ui *ui = ctx;
  size_t count = 0;
  qa_ui_control *controls = ui->pause_controls;
  controls[count++] = pause_button(ui, PAUSE_RESUME, "ui:application:resume", "Resume game", 1);
  controls[count++] = pause_button(ui, PAUSE_SAVE, "ui:application:save", "Save game", 2);
  controls[count++] = pause_button(ui, PAUSE_LOAD, "ui:application:load", "Load game", 3);
  controls[count++] = pause_button(ui, PAUSE_SETTINGS, "ui:application:settings", "Options", 4);
  controls[count++] = pause_button(ui, PAUSE_CONSOLE, "ui:application:console", "Console", 5);
  if (ui->base_arena != NULL) controls[count++] = pause_button(ui, PAUSE_PROGRESS, "ui:application:progress", "Arena progress", 6);
  if (ui->has_match_menu) controls[count++] = pause_button(ui, PAUSE_MATCH, "ui:application:match", "Match controls", 7);
  if (ui->options.lobby != NULL) controls[count++] = pause_button(ui, PAUSE_LOBBY, "ui:application:lobby", "Return to lobby", 8);
  controls[count++] = pause_button(ui, PAUSE_QUIT, "ui:application:quit", "End game", ui->options.lobby == NULL ? 8 : 9);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:application:game"; menu.title = "Paused"; menu.full_screen = true;
  menu.controls = controls; menu.control_count = count;
  menu.ctx = ui; menu.open = pause_open; menu.close = pause_close;
  return menu;
}

/* ---- ApplicationInputUi adapters ---- */
static bool input_ui_input(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  return qa_app_seat_ui_input(ctx, event, focus);
}
static void input_ui_close_menus(void *ctx) { qa_app_seat_ui_close_menus(ctx); }
static void input_ui_clear_prompt(void *ctx) { qa_app_seat_ui_clear_prompt(ctx); }
static void input_ui_sample(void *ctx, qa_seat_input_sample *sample) { qa_app_seat_ui_sample(ctx, sample); }
static void input_ui_wheel(void *ctx, qa_wheel_mode mode, bool down) { qa_app_seat_ui_wheel(ctx, mode, down); }
static bool input_ui_cycle(void *ctx, int direction) { return qa_app_seat_ui_cycle_weapon(ctx, direction); }
static bool input_ui_switch(void *ctx, double first, double second) { return qa_app_seat_ui_switch_weapon(ctx, first, second); }
static void quit_fn(void *ctx) { qa_app_seat_ui *ui = ctx; ui->options.quit(ui->options.quit_ctx); }
static void match_print(void *ctx, const char *text) { console_print(ctx, text); }

/* ---- constructor ---- */
static void append_items(qa_ui_setting_binding **items, size_t *count, size_t *capacity,
                         const qa_ui_setting_binding *more, size_t more_count) {
  if (*count + more_count > *capacity) {
    size_t next = (*count + more_count) * 2;
    qa_ui_setting_binding *grown = realloc(*items, next * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    *items = grown; *capacity = next;
  }
  if (more_count != 0) memcpy(*items + *count, more, more_count * sizeof *more);
  *count += more_count;
}
static void append_owned(qa_app_seat_ui *ui, qa_ui_setting_binding **items, size_t *count, size_t *capacity,
                         qa_ui_setting_bindings bindings) {
  own_bindings(ui, bindings);
  append_items(items, count, capacity, bindings.items, bindings.count);
}

qa_app_seat_ui *qa_app_seat_ui_create(const qa_app_seat_ui_options *options) {
  qa_app_seat_ui *ui = xcalloc(1, sizeof *ui);
  ui->options = *options;
  ui->local = options->local; ui->art = options->art; ui->input = options->input;
  ui->seat = qa_session_seat_id(options->local->player.seat);
  ui->native_q2_hud = qa_app_q2_native_hud_create();
  ui->authored_wheel = qa_app_application_q1_wheel_create();
  ui->font = options->font; ui->typography = options->typography;
  qa_ui_seat_preferences_init(&ui->preferences, ui->seat, qa_app_application_input_shared_settings(ui->input));
  ui->messages = qa_seat_hud_messages_create(ui->seat);
  qa_ui_text_renderer_init(&ui->text, ui->seat);
  if (!qa_ui_text_renderer_bind(&ui->text, ui->art->skin.font, ui->font)) qa_range_error("Out of memory");
  qa_ui_text_renderer_init(&ui->menu_text, ui->seat);
  if (!qa_ui_text_renderer_bind(&ui->menu_text, ui->art->skin.font, qa_app_menu_typography_body(ui->typography))
      || !qa_ui_text_renderer_bind(&ui->menu_text, qa_ui_menu_title_font, qa_app_menu_typography_title(ui->typography)))
    qa_range_error("Out of memory");
  qa_native_ui_options controller;
  memset(&controller, 0, sizeof controller);
  controller.ctx = ui; controller.seat = ui->seat;
  controller.skin = controller_skin; controller.measure_text = measure_menu_text; controller.now = controller_now;
  controller.bindings = controller_bindings; controller.appearance = controller_appearance;
  controller.localize = controller_localize; controller.focus = controller_focus;
  controller.clipboard = controller_clipboard; controller.sound = controller_sound;
  controller.execute_script = controller_script;
  ui->controller = qa_native_ui_controller_create(&controller);
  if (ui->messages == NULL || ui->native_q2_hud == NULL || ui->authored_wheel == NULL || ui->controller == NULL)
    qa_range_error("Out of memory");
  ui->sound_captions = qa_app_seat_sound_captions_create(ui->seat, qa_app_application_audio_engine(options->audio),
                                                         borrowed_language, ui, caption_warn, ui);
  ui->source_hud = qa_app_seat_source_hud_create(options->local->player.actor,
                                                 options->localize != NULL ? source_hud_localize : NULL, ui);
  ui->inventory_menu = qa_ui_register_inventory_menu(ui->controller, inventory_items, ui, command_fn, ui);
  if (qa_input_command_builder_dialect(ui->local->builder) == QA_COMMAND_DIALECT_Q3) {
    ui->match_menu = qa_ui_register_match_menu(ui->controller, command_fn, ui);
    ui->has_match_menu = true;
  }
  if (options->base_arena != NULL) ui->base_arena = qa_app_base_arena_menus_create(ui->controller, *options->base_arena);
  if (options->team_arena != NULL) ui->team_arena = qa_app_team_arena_results_create(ui->controller, *options->team_arena);
  qa_app_seat_game_prompt_host prompt = {ui, prompt_actor, prompt_impulse, prompt_language};
  ui->prompt = qa_app_seat_game_prompt_create(ui->seat, ui->controller, prompt);
  ui->match = qa_app_q2_match_ui_create(options->local->player.actor, ui->controller, command_fn, ui, match_print, ui);
  ui->wheel_services.ctx = ui; ui->wheel_services.seat = ui->seat;
  ui->wheel_services.items = wheel_items; ui->wheel_services.active_item = wheel_active;
  ui->wheel_services.select = wheel_select; ui->wheel_services.now = wheel_now; ui->wheel_services.changed = wheel_changed;
  ui->weapon_wheel = qa_seat_weapon_wheel_create(&ui->wheel_services, NULL);
  if (ui->sound_captions == NULL || ui->source_hud == NULL || ui->prompt == NULL || ui->match == NULL || ui->weapon_wheel == NULL)
    qa_range_error("Out of memory");
  ui->binding_reset.ctx = ui; ui->binding_reset.available = reset_available; ui->binding_reset.reset = reset_apply;
  ui->bindings = qa_ui_register_binding_menus(ui->controller, ui->local->input, NULL, 0, binding_source, ui, &ui->binding_reset);
  qa_ui_setting_binding binding_menu = setting_button(ui, SETTING_BINDINGS, "ui:input:bindings", "Key and controller bindings", "input");
  ui->audio_service.ctx = ui; ui->audio_service.read = audio_read; ui->audio_service.write = audio_write;
  ui->audio_output.ctx = ui; ui->audio_output.has_format = true;
  ui->audio_output.format_read = format_read; ui->audio_output.format_select = format_select;
  ui->audio_output.selected = output_selected; ui->audio_output.devices = output_devices;
  ui->audio_output.select = output_select; ui->audio_output.report = console_line;
  qa_ui_setting_bindings volumes = qa_ui_bind_audio_settings(&ui->audio_service, &ui->audio_output);
  qa_cvar_registry *shared = qa_app_application_input_shared_settings(ui->input);
  ui->renderer.ctx = ui; ui->renderer.current = renderer_current; ui->renderer.has_worker = shared != NULL;
  ui->renderer.worker_read = worker_read; ui->renderer.worker_write = worker_write;
  ui->renderer.apply = renderer_apply; ui->renderer.report = console_line;
  qa_ui_setting_binding *items = NULL;
  size_t count = 0, capacity = 0;
  append_owned(ui, &items, &count, &capacity, qa_ui_bind_renderer_settings(&ui->renderer));
  append_owned(ui, &items, &count, &capacity,
               qa_ui_bind_native_video_settings(current_window, ui, shared, console_line, ui, mutate_window, ui));
  if (options->view_setting != NULL) append_items(&items, &count, &capacity, options->view_setting, 1);
  if (shared != NULL) {
    append_owned(ui, &items, &count, &capacity, qa_ui_bind_image_settings(shared));
    append_owned(ui, &items, &count, &capacity, qa_ui_bind_model_settings(shared));
    append_owned(ui, &items, &count, &capacity, qa_ui_bind_console_settings(shared));
  }
  if (options->language != NULL) append_items(&items, &count, &capacity, options->language, 1);
  append_items(&items, &count, &capacity, &binding_menu, 1);
  if (options->rankings != NULL) {
    ui->ranking_menu = qa_ui_register_ranking_account_menu(ui->controller, ranking_current, ui);
    ui->has_ranking_menu = true;
  }
  if (options->host_settings != NULL) {
    ui->server_settings = qa_ui_register_server_settings_menu(ui->controller, options->host_settings);
    ui->has_server_settings = true;
  }
  ui->gyro = qa_app_controller_settings_ui(qa_app_application_input_controller_settings(ui->input),
                                          qa_seat_input_seat(ui->local->input));
  ui->gyro_settings = qa_ui_register_gyro_settings_menu(ui->controller, &ui->gyro, NULL);
  qa_ui_setting_binding gyro = setting_button(ui, SETTING_GYRO, "ui:settings:gyro", "Gyro controls", "input");
  append_items(&items, &count, &capacity, &gyro, 1);
  if (ui->has_server_settings) {
    qa_ui_setting_binding server = setting_button(ui, SETTING_SERVER, "ui:network:server-settings", "Server settings", "network");
    append_items(&items, &count, &capacity, &server, 1);
  }
  if (ui->has_ranking_menu) {
    qa_ui_setting_binding ranking = setting_button(ui, SETTING_RANKING, "ui:network:rankings", "Ranking account", "network");
    append_items(&items, &count, &capacity, &ranking, 1);
  }
  append_owned(ui, &items, &count, &capacity, qa_app_input_devices_bindings(qa_app_application_input_input_devices(ui->input)));
  append_owned(ui, &items, &count, &capacity, qa_ui_bind_input_routing_settings(routing_router, ui, routing_devices, ui));
  qa_ui_setting_binding join = setting_button(ui, SETTING_JOIN, "ui:input:local-join", "Add local player", "input");
  qa_ui_setting_binding drop = setting_button(ui, SETTING_DROP, "ui:input:local-drop", "Remove this player", "input");
  append_items(&items, &count, &capacity, &join, 1);
  append_items(&items, &count, &capacity, &drop, 1);
  ui->vibration.ctx = ui; ui->vibration.read = vibration_read; ui->vibration.write = vibration_write;
  append_owned(ui, &items, &count, &capacity, qa_ui_bind_input_settings(ui->local->input, ui->local->builder, &ui->vibration));
  append_owned(ui, &items, &count, &capacity, volumes);
  append_owned(ui, &items, &count, &capacity, qa_ui_bind_audio_geometry_settings(shared));
  append_owned(ui, &items, &count, &capacity, qa_ui_bind_music_playlist_settings(shared, NULL, NULL));
  size_t preference_count = 0;
  const qa_ui_setting_binding *preference_items = qa_ui_seat_preferences_bindings(&ui->preferences, &preference_count);
  append_items(&items, &count, &capacity, preference_items, preference_count);
  if (options->gameplay != NULL) append_owned(ui, &items, &count, &capacity, qa_ui_bind_gameplay_settings(options->gameplay));
  ui->settings_items = items; ui->settings_item_count = count;
  qa_ui_menu_id llm_root = NULL;
  if (options->llm != NULL) {
    ui->llm_menu = qa_ui_register_llm_settings_menu(ui->controller, options->llm);
    ui->has_llm_menu = true;
    llm_root = ui->llm_menu.root;
  }
  ui->settings_reset.ctx = ui; ui->settings_reset.label = "Reset player preferences..."; ui->settings_reset.apply = gameplay_reset;
  ui->settings = qa_ui_register_settings_menus(ui->controller, items, count, llm_root,
      ui->has_llm_menu ? &ui->llm_menu : NULL, ui->has_llm_menu ? llm_dispose : NULL,
      options->gameplay != NULL ? &ui->settings_reset : NULL);
  ui->saves = qa_ui_register_saved_game_menus(ui->controller, options->saves);
  ui->death = qa_app_seat_player_death_create(ui->controller, options->saves, ui->saves.load, quit_fn, ui);
  ui->dispose_menu = qa_native_ui_controller_register(ui->controller, "menu:application:game", pause_menu, ui);
  qa_app_input_ui input_ui = {ui, input_ui_input, input_ui_close_menus, input_ui_clear_prompt, input_ui_sample,
                              input_ui_wheel, input_ui_cycle, input_ui_switch};
  ui->dispose_input = qa_app_application_input_attach_ui(ui->input, ui->seat, input_ui);
  return ui;
}

/* ---- accessors ---- */
qa_native_ui_controller *qa_app_seat_ui_controller(qa_app_seat_ui *ui) { return ui->controller; }
bool qa_app_seat_ui_pause_menu_open(const qa_app_seat_ui *ui) {
  return ui->manual_pause || (ui->team_arena != NULL && qa_app_team_arena_results_active(ui->team_arena))
      || (ui->death != NULL && qa_app_seat_player_death_active(ui->death)
          && !is(qa_native_ui_controller_active_menu(ui->controller), qa_app_player_death_menu));
}
qa_ui_seat_preferences *qa_app_seat_ui_preferences(qa_app_seat_ui *ui) { return &ui->preferences; }
qa_seat_hud_messages *qa_app_seat_ui_messages(qa_app_seat_ui *ui) { return ui->messages; }
qa_seat_weapon_wheel *qa_app_seat_ui_weapon_wheel(qa_app_seat_ui *ui) { return ui->weapon_wheel; }
qa_ui_text_renderer *qa_app_seat_ui_text(qa_app_seat_ui *ui) { return &ui->text; }
qa_app_local_input *qa_app_seat_ui_local(const qa_app_seat_ui *ui) { return ui->local; }
const qa_native_ui_art *qa_app_seat_ui_art(const qa_app_seat_ui *ui) { return ui->art; }

void qa_app_seat_ui_refresh_images(qa_app_seat_ui *ui, qa_text_font_selection font, const qa_app_menu_typography *typography) {
  ui->font = font; ui->typography = typography;
  if (!qa_ui_text_renderer_bind(&ui->text, ui->art->skin.font, font)
      || !qa_ui_text_renderer_bind(&ui->menu_text, ui->art->skin.font, qa_app_menu_typography_body(typography))
      || !qa_ui_text_renderer_bind(&ui->menu_text, qa_ui_menu_title_font, qa_app_menu_typography_title(typography)))
    qa_range_error("Out of memory");
}

struct qa_app_seat_ui_image_refresh { qa_app_seat_ui *ui; qa_app_weapon_hud_refresh *weapon; };
qa_app_seat_ui_image_refresh *qa_app_seat_ui_prepare_image_refresh(qa_app_seat_ui *ui, qa_app_prepared_images *providers) {
  qa_app_weapon_hud_refresh *weapon =
      ui->weapon_assets != NULL ? qa_app_weapon_hud_assets_prepare_image_refresh(ui->weapon_assets, providers) : NULL;
  qa_app_seat_ui_image_refresh *refresh = calloc(1, sizeof *refresh);
  if (refresh == NULL) { qa_app_weapon_hud_refresh_discard(weapon); qa_range_error("Out of memory"); }
  refresh->ui = ui;
  refresh->weapon = weapon;
  return refresh;
}
void qa_app_seat_ui_image_refresh_commit(qa_app_seat_ui_image_refresh *refresh) {
  if (refresh->weapon != NULL) qa_app_weapon_hud_refresh_publish(refresh->weapon);
  qa_app_q2_native_hud_clear(refresh->ui->native_q2_hud);
  free(refresh);
}

/* weaponAssets ??= new ApplicationWeaponHudAssets(assets). */
static qa_app_weapon_hud_assets *weapon_assets(qa_app_seat_ui *ui, qa_app_application_assets *assets) {
  if (ui->weapon_assets == NULL) {
    ui->weapon_assets = qa_app_weapon_hud_assets_create(assets);
    if (ui->weapon_assets == NULL) qa_range_error("Out of memory");
  }
  return ui->weapon_assets;
}
static void set_weapon_icons(qa_app_seat_ui *ui, qa_app_weapon_hud_prepared prepared) {
  ui->weapon_icon = prepared.has_weapon ? prepared.weapon : NULL;
  ui->ammo_icon = prepared.has_ammo ? prepared.ammo : NULL;
}
static void set_wheel_icon(qa_app_seat_ui *ui, const char *item, qa_resource_id resource) {
  for (size_t i = 0; i < ui->wheel_icon_count; i++)
    if (is(ui->wheel_icons[i].item, item)) { free(ui->wheel_icons[i].resource); ui->wheel_icons[i].resource = xdup(resource); return; }
  if (ui->wheel_icon_count == ui->wheel_icon_capacity) {
    size_t next = ui->wheel_icon_capacity == 0 ? 8 : ui->wheel_icon_capacity * 2;
    wheel_icon *grown = realloc(ui->wheel_icons, next * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    ui->wheel_icons = grown; ui->wheel_icon_capacity = next;
  }
  ui->wheel_icons[ui->wheel_icon_count].item = xdup(item);
  ui->wheel_icons[ui->wheel_icon_count].resource = xdup(resource);
  ui->wheel_icon_count++;
}

/* TS prepare(assets), synchronous. */
void qa_app_seat_ui_prepare(qa_app_seat_ui *ui, qa_app_application_assets *assets) {
  if (ui->options.guest_ui) {
    qa_app_weapon_hud_assets *owner = weapon_assets(ui, assets);
    qa_app_sim_player_ui player = player_ui(ui);
    set_weapon_icons(ui, qa_app_weapon_hud_assets_prepare(owner, player.weapon_status));
    return;
  }
  if (ui->options.rankings != NULL && ui->options.rankings->take_menu_request(ui->options.rankings->ctx) && ui->has_ranking_menu)
    qa_native_ui_controller_open_menu(ui->controller, ui->ranking_menu.root);
  if (ui->team_arena != NULL) qa_app_team_arena_results_update(ui->team_arena);
  if (ui->base_arena != NULL) qa_app_base_arena_menus_update(ui->base_arena);
  if (ui->team_arena == NULL) qa_app_seat_player_death_observe(ui->death, player_ui(ui).health);
  if (qa_app_seat_player_death_active(ui->death)) qa_seat_weapon_wheel_close(ui->weapon_wheel, false);
  qa_app_seat_game_prompt_prepare(ui->prompt, assets, prompt_focus_kind, ui);
  qa_app_weapon_hud_assets *owner = weapon_assets(ui, assets);
  qa_app_sim_player_ui player = player_ui(ui);
  set_weapon_icons(ui, qa_app_weapon_hud_assets_prepare(owner, player.weapon_status));
  qa_app_application_q1_wheel_prepare(ui->authored_wheel, assets, owner, &player);
  qa_app_seat_source_hud_prepare(ui->source_hud, owner);
  size_t print_count = 0;
  const qa_app_objective_print *prints = qa_app_seat_source_hud_drain_objective_prints(ui->source_hud, &print_count);
  for (size_t i = 0; i < print_count; i++) {
    qa_source_time starts = {QA_SOURCE_TIME_SECONDS, prints[i].seconds}, duration = {QA_SOURCE_TIME_SECONDS, 5};
    qa_seat_hud_messages_center_print(ui->messages, ui->seat, prints[i].text, starts, duration, false, 40);
  }
  qa_app_seat_sound_captions_prepare(ui->sound_captions, qa_app_application_assets_content(assets));
  if (player.weapon_status != NULL) {
    qa_weapon_hud_status status = *player.weapon_status;
    for (size_t i = 0; i < player.item_count; i++) {
      if (!is(player.items[i].kind, "weapon")) continue;
      status.item = player.items[i].id;
      qa_app_weapon_hud_prepared icons = qa_app_weapon_hud_assets_prepare(owner, &status);
      if (icons.has_weapon && icons.weapon != NULL) set_wheel_icon(ui, player.items[i].id, icons.weapon);
    }
  }
}

/* ---- ApplicationInputUi ---- */
bool qa_app_seat_ui_input(qa_app_seat_ui *ui, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  if (qa_app_seat_player_death_input(ui->death, *event)) return true;
  if (is(focus.kind, "console") || is(focus.kind, "chat")) return false;
  if (qa_app_seat_game_prompt_input(ui->prompt, event)) return true;
  bool key = is(event->kind, "key"), button = is(event->kind, "controller-button");
  bool escape = key && event->code == 27 && event->down && !event->repeat;
  if (is(qa_native_ui_controller_active_menu(ui->controller), qa_app_game_prompt_menu)
      && (escape || (button && (event->button == 1 || event->button == 6) && event->down)
          || (is(event->kind, "mouse-button") && event->button == 3 && event->down))) {
    qa_native_ui_controller_open_menu(ui->controller, "menu:application:game");
    return true;
  }
  if (qa_native_ui_controller_active_menu(ui->controller) != NULL) return qa_native_ui_controller_input(ui->controller, *event);
  if (escape || (button && event->button == 6 && event->down)) {
    qa_seat_weapon_wheel_close(ui->weapon_wheel, false);
    qa_native_ui_controller_open_menu(ui->controller, "menu:application:game");
    return true;
  }
  return qa_seat_weapon_wheel_input(ui->weapon_wheel, *event);
}

void qa_app_seat_ui_sample(qa_app_seat_ui *ui, qa_seat_input_sample *input) {
  qa_seat_weapon_wheel_update(ui->weapon_wheel, input->now_milliseconds);
  bool scores = false, attack = false;
  for (size_t i = 0; i < input->button_count; i++) {
    const qa_button_sample *button = &input->buttons[i];
    if (is(button->action, "scores") && button->active) scores = true;
    if (is(button->action, "attack") && (button->active || button->pressed)) attack = true;
  }
  qa_app_seat_source_hud_scores(ui->source_hud, scores);
  qa_ui_wheel_command wheel = qa_seat_weapon_wheel_command(ui->weapon_wheel, attack, input->now_milliseconds);
  if (!wheel.holster && !wheel.consume_attack
      && input->now_milliseconds >= qa_seat_weapon_wheel_weapon_lock_until(ui->weapon_wheel)) return;
  for (size_t i = 0; i < input->button_count; i++) {
    qa_button_sample *button = &input->buttons[i];
    if (!is(button->action, "attack")) continue;
    button->active = false; button->pressed = false; button->fraction = 0;
  }
}

void qa_app_seat_ui_wheel(qa_app_seat_ui *ui, qa_wheel_mode mode, bool down) {
  if (down) (void)qa_seat_weapon_wheel_open(ui->weapon_wheel, mode == QA_WHEEL_WEAPONS ? "weapons" : "powerups");
  else qa_seat_weapon_wheel_close(ui->weapon_wheel, true);
}

bool qa_app_seat_ui_switch_weapon(qa_app_seat_ui *ui, double first, double second) {
  if (ui->options.guest_ui) return false;
  qa_app_sim_player_ui player = player_ui(ui);
  if (player.active_weapon == NULL || strncmp(player.active_weapon, "q1:", 3) != 0) return false;
  qa_item_id selected = qa_app_application_q1_wheel_switch_weapon(ui->authored_wheel, &player, first, second);
  if (selected != NULL) { const char *args[1] = {selected}; command(ui, "use", args, 1); }
  return true;
}

bool qa_app_seat_ui_cycle_weapon(qa_app_seat_ui *ui, int direction) {
  if (ui->options.guest_ui || qa_input_command_builder_dialect(ui->local->builder) != QA_COMMAND_DIALECT_Q2_RERELEASE) return false;
  qa_seat_weapon_wheel_cycle(ui->weapon_wheel, direction);
  return true;
}

void qa_app_seat_ui_close_menus(qa_app_seat_ui *ui) {
  qa_seat_weapon_wheel_close(ui->weapon_wheel, false);
  qa_native_ui_controller_close_all(ui->controller);
}

void qa_app_seat_ui_clear_prompt(qa_app_seat_ui *ui) { qa_app_seat_game_prompt_clear(ui->prompt); }

void qa_app_seat_ui_center_print(qa_app_seat_ui *ui, const char *text, double time_milliseconds, double duration_milliseconds) {
  qa_source_time starts = {QA_SOURCE_TIME_MILLISECONDS, time_milliseconds};
  qa_source_time duration = {QA_SOURCE_TIME_MILLISECONDS, duration_milliseconds};
  qa_seat_hud_messages_center_print(ui->messages, ui->seat, text, starts, duration, true, 125);
}

/* ---- receive ---- */
static const char *comp_q1_kind(qa_comp_q1_event_kind kind) {
  switch (kind) {
    case QA_COMP_Q1_EVENT_ADDON: return "addon";
    case QA_COMP_Q1_EVENT_CLIENT: return "client";
    case QA_COMP_Q1_EVENT_CLIENT_LEFT: return "client-left";
    case QA_COMP_Q1_EVENT_CTF_STATUS: return "ctf-status";
    case QA_COMP_Q1_EVENT_CTF_CAPTURE: return "ctf-capture";
    case QA_COMP_Q1_EVENT_PROMPT: return "prompt";
    case QA_COMP_Q1_EVENT_CLEAR_PROMPT: return "clear-prompt";
    case QA_COMP_Q1_EVENT_SOURCE_LOG: return "source-log";
    case QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE: return "developer-message";
    default: return "level-presentation";
  }
}

/* The C Q2MatchUi reads one flat view of a ctf/lmctf event. */
static bool match_event(qa_app_seat_ui *ui, const qa_q2_composition_event *source, qa_app_q2_match_event *view,
                        qa_app_q2_match_score_row **rows, qa_app_q2_match_menu_entry **entries) {
  memset(view, 0, sizeof *view);
  if (source->kind == QA_Q2_COMPOSITION_EVENT_CTF && source->ctf != NULL) {
    const qa_q2_ctf_event *event = source->ctf;
    view->kind = event->kind;
    view->has_actor = !is(event->kind, "match-status"); view->actor = event->actor;
    view->text = event->text;
    view->team = event->team;
    if (is(event->kind, "hud") || is(event->kind, "scoreboard")) {
      view->has_captures = true; view->captures[0] = event->captures[0]; view->captures[1] = event->captures[1];
    }
    view->flag_states[0] = event->flag_states[0]; view->flag_states[1] = event->flag_states[1];
    view->tech = event->tech; view->match = event->match;
    view->has_id_target = event->has_id_target; view->id_target = event->id_target;
    view->has_blink_team = event->has_blink_team; view->blink_team = event->blink_team;
    view->carried_flag = event->has_carried_flag;
    size_t total = event->red_count + event->blue_count + event->spectator_count;
    *rows = xcalloc(total, sizeof **rows);
    const qa_q2_ctf_score_row *groups[3] = {event->red, event->blue, event->spectators};
    size_t counts[3] = {event->red_count, event->blue_count, event->spectator_count}, at = 0;
    for (size_t g = 0; g < 3; g++)
      for (size_t i = 0; i < counts[g]; i++, at++) {
        (*rows)[at].name = groups[g][i].name; (*rows)[at].score = groups[g][i].score; (*rows)[at].ping = groups[g][i].ping;
      }
    view->red = *rows; view->red_count = event->red_count;
    view->blue = *rows + event->red_count; view->blue_count = event->blue_count;
    view->spectators = *rows + event->red_count + event->blue_count; view->spectator_count = event->spectator_count;
    view->title = event->title;
    *entries = xcalloc(event->entry_count, sizeof **entries);
    for (size_t i = 0; i < event->entry_count; i++) {
      (*entries)[i].label = event->entries[i].label; (*entries)[i].has_action = true; (*entries)[i].action = event->entries[i].action;
    }
    view->entries = *entries; view->entry_count = event->entry_count;
    /* Object.entries(Q2CtfAdminSettings) in declaration order. */
    static const char *const keys[8] = {"matchMinutes", "setupMinutes", "startSeconds", "weaponsStay", "instantItems",
                                        "quadDrop", "instantWeapons", "matchLock"};
    const qa_q2_ctf_admin_settings *settings = &event->settings;
    double numbers[3] = {settings->match_minutes, settings->setup_minutes, settings->start_seconds};
    bool flags[5] = {settings->weapons_stay, settings->instant_items, settings->quad_drop, settings->instant_weapons,
                     settings->match_lock};
    for (size_t i = 0; i < 8; i++) {
      ui->match_settings[i].key = keys[i];
      ui->match_settings[i].is_boolean = i >= 3;
      if (i < 3) {
        snprintf(ui->match_setting_text[i], sizeof ui->match_setting_text[i], "%s", QA_JS_NUMBER(numbers[i]));
        ui->match_settings[i].text_value = ui->match_setting_text[i];
      } else ui->match_settings[i].bool_value = flags[i - 3];
    }
    view->settings = ui->match_settings; view->setting_count = 8;
    return true;
  }
  if (source->kind == QA_Q2_COMPOSITION_EVENT_LMCTF && source->lmctf != NULL) {
    const qa_lmctf_event *event = source->lmctf;
    view->kind = event->kind; view->has_actor = true; view->actor = event->actor;
    view->name = event->name; view->amount = event->amount; view->team = event->team;
    view->rune = event->rune; view->carried_flag = event->carried_flag;
    *rows = xcalloc(event->row_count, sizeof **rows);
    for (size_t i = 0; i < event->row_count; i++) {
      (*rows)[i].name = event->rows[i].name; (*rows)[i].score = event->rows[i].score; (*rows)[i].ping = event->rows[i].ping;
    }
    view->has_rows = true; view->rows = *rows; view->row_count = event->row_count;
    view->title = event->title;
    *entries = xcalloc(event->entry_count, sizeof **entries);
    for (size_t i = 0; i < event->entry_count; i++) {
      (*entries)[i].label = event->entries[i].label; (*entries)[i].command = event->entries[i].command;
    }
    view->entries = *entries; view->entry_count = event->entry_count;
    return true;
  }
  return false;
}

void qa_app_seat_ui_receive(qa_app_seat_ui *ui, const qa_app_sim_presentation_event *events, size_t count) {
  /* prompt.receive(events): q1-composition prompt/clear-prompt events. */
  qa_app_prompt_event *prompts = xcalloc(count, sizeof *prompts);
  qa_app_prompt_choice **choices = xcalloc(count, sizeof *choices);
  size_t prompt_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (!is(events[i].kind, "q1-composition") || events[i].q1_composition == NULL) continue;
    const qa_comp_q1_event *event = events[i].q1_composition;
    if (event->kind != QA_COMP_Q1_EVENT_PROMPT && event->kind != QA_COMP_Q1_EVENT_CLEAR_PROMPT) continue;
    qa_app_prompt_event *prompt = &prompts[prompt_count];
    prompt->kind = comp_q1_kind(event->kind); prompt->actor = event->actor; prompt->title = event->title;
    prompt->content = events[i].content;
    choices[prompt_count] = xcalloc(event->choice_count, sizeof **choices);
    for (size_t c = 0; c < event->choice_count; c++) {
      choices[prompt_count][c].label = event->choices[c].label; choices[prompt_count][c].impulse = event->choices[c].impulse;
    }
    prompt->choices = choices[prompt_count]; prompt->choice_count = event->choice_count;
    prompt_count++;
  }
  qa_app_seat_game_prompt_receive(ui->prompt, prompts, prompt_count);
  for (size_t i = 0; i < prompt_count; i++) free(choices[i]);
  free(choices); free(prompts);
  qa_actor_id actor = ui->local->player.actor;
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *source = &events[i];
    qa_app_seat_source_hud_receive(ui->source_hud, source);
    if (is(source->kind, "q2-player") && source->q2_player != NULL) {
      const qa_q2_player_event *event = source->q2_player;
      if (is(event->kind, "inventory") && qa_same_actor(event->actor, actor) && event->has_visible) {
        qa_ui_menu_id active = qa_native_ui_controller_active_menu(ui->controller);
        if (event->visible && !is(active, ui->inventory_menu.root))
          qa_native_ui_controller_open_menu(ui->controller, ui->inventory_menu.root);
        else if (!event->visible && is(active, ui->inventory_menu.root)) qa_native_ui_controller_close_menu(ui->controller);
      }
      if (is(event->kind, "userinfo")) qa_app_q2_match_ui_name(ui->match, event->actor, event->name);
    }
    if (is(source->kind, "q2-composition") && source->q2_composition != NULL) {
      const qa_q2_composition_event *event = source->q2_composition;
      qa_app_q2_match_event view;
      qa_app_q2_match_score_row *rows = NULL;
      qa_app_q2_match_menu_entry *entries = NULL;
      if (match_event(ui, event, &view, &rows, &entries)) qa_app_q2_match_ui_receive(ui->match, view);
      free(rows); free(entries);
    }
    qa_source_time duration = {QA_SOURCE_TIME_SECONDS, 3};
    qa_source_time starts = {QA_SOURCE_TIME_SECONDS, source->seconds};
    if (is(source->kind, "q1") && source->q1 != NULL && is((source->q1)->kind, "message")
        && qa_same_actor((source->q1)->player, actor)) {
      const qa_q1_event *event = source->q1;
      if (event->center) qa_seat_hud_messages_center_print(ui->messages, ui->seat, event->text, starts, duration, true, 125);
      else qa_seat_hud_messages_notify(ui->messages, ui->seat, event->text, false, starts, duration);
    } else if (is(source->kind, "q2") && source->q2 != NULL && is((source->q2)->kind, "centerprint")
               && qa_same_actor((source->q2)->actor, actor)) {
      const qa_q2_presentation_event *event = source->q2;
      qa_source_time shown = {QA_SOURCE_TIME_SECONDS, event->has_duration_seconds ? event->duration_seconds : 5};
      qa_seat_hud_messages_center_print(ui->messages, ui->seat, event->text, starts, shown,
                                        event->has_instant ? event->instant : true, 40);
    } else if (is(source->kind, "q3-source") && source->q3_source != NULL && (source->q3_source)->kind == QA_APP_Q3_EVENT_SERVER_COMMAND
               && ((source->q3_source)->client < 0
                   || (source->q3_source)->client
                          == (double)qa_session_client_id(qa_session_seat_client(ui->local->player.seat)).slot)) {
      const qa_app_q3_source_event *event = source->q3_source;
      qa_command_tokens tokens;
      if (!qa_tokenize_command(event->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
      if (tokens.argc >= 2) {
        if (is(tokens.argv[0], "cp"))
          qa_seat_hud_messages_center_print(ui->messages, ui->seat, tokens.argv[1], starts, duration, true, 125);
        else if (is(tokens.argv[0], "chat") || is(tokens.argv[0], "tchat"))
          qa_seat_hud_messages_notify(ui->messages, ui->seat, tokens.argv[1], true, starts, duration);
      }
      qa_command_tokens_destroy(&tokens);
    } else if (is(source->kind, "q2-player") && source->q2_player != NULL && is((source->q2_player)->kind, "print")
               && is((source->q2_player)->level, "chat")
               && (!(source->q2_player)->has_target
                   || qa_same_actor((source->q2_player)->target, actor))) {
      qa_seat_hud_messages_notify(ui->messages, ui->seat, (source->q2_player)->text, true, starts, duration);
    } else if (is(source->kind, "q2") && source->q2 != NULL && is((source->q2)->kind, "print")
               && is((source->q2)->level, "chat")
               && (!(source->q2)->has_actor
                   || qa_same_actor((source->q2)->actor, actor))) {
      qa_seat_hud_messages_notify(ui->messages, ui->seat, (source->q2)->text, true, starts, duration);
    }
  }
}

/* ---- drawing ---- */
static qa_ui_skin hud_skin(const qa_app_seat_ui *ui) { return qa_ui_hud_skin_font(ui->art->skin, hud_font(ui)); }

bool qa_app_seat_ui_weapon_occlusion(qa_app_seat_ui *ui, qa_ui_draw_context context, bool game_visible, qa_ui_hud_rects *out) {
  memset(out, 0, sizeof *out);
  if (ui->options.guest_ui || !game_visible || !focus_game(ui)) return true;
  qa_app_sim_player_ui player = player_ui(ui);
  qa_ui_preference_values values = preferences(ui);
  qa_ui_skin skin = hud_skin(ui);
  return qa_ui_hud_vital_occupied_rects(context, player.weapon_status == NULL ? 2 : 3, values.hud_scale,
                                        ui->art->skin.font_scale * values.text_scale,
                                        skin.has_cap_ink ? skin.cap_ink_height : 8, out);
}

qa_app_seat_ui_draw_flags qa_app_seat_ui_default_draw_flags(void) {
  qa_app_seat_ui_draw_flags flags = {true, true, false, true, false};
  return flags;
}

typedef struct render_ctx {
  qa_app_seat_ui *ui;
  qa_app_seat_ui_sinks sinks;
  qa_app_q2_native_hud *native; /* drawNativeQ2Hud's renderer picture first */
  bool art_only;
  qa_render_command *collected; size_t collected_count, collected_capacity; /* captionCommands */
} render_ctx;

static qa_picture_asset image_asset(qa_image_picture image) {
  qa_picture_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_IMAGE; asset.image = image;
  return asset;
}
static qa_picture_asset render_picture(void *ctx, qa_resource_id resource) {
  render_ctx *render = ctx;
  qa_app_seat_ui *ui = render->ui;
  if (render->native != NULL) {
    const qa_picture_asset *picture = qa_app_q2_native_hud_picture(render->native, resource);
    if (picture != NULL) return *picture;
  }
  if (!render->art_only && ui->weapon_assets != NULL) {
    const qa_picture_asset *picture = qa_app_weapon_hud_assets_picture(ui->weapon_assets, resource);
    if (picture != NULL) return *picture;
  }
  return image_asset(qa_native_ui_art_picture(ui->art, resource));
}
static void render_emit(void *ctx, const qa_render_command *command) {
  render_ctx *render = ctx;
  render->sinks.emit(render->sinks.ctx, command);
}
static void render_material(void *ctx, const qa_material_text_draw *draw) {
  render_ctx *render = ctx;
  render->sinks.material(render->sinks.ctx, draw);
}
static void render(render_ctx *render_state, qa_ui_text_renderer *text, qa_ui_draw_context context,
                   const qa_ui_draw_command *commands, size_t count) {
  qa_ui_render_services services;
  memset(&services, 0, sizeof services);
  services.ctx = render_state; services.text = text; services.white = image_asset(render_state->ui->art->white);
  services.picture = render_picture; services.emit = render_emit; services.material = render_material;
  qa_ui_render_commands(context, commands, count, &services);
}
static const char *identity_localize(void *ctx, const char *text) { (void)ctx; return text; }

static qa_ui_common_hud_draw_options hud_options(qa_app_seat_ui *ui, const qa_scene_camera *camera) {
  qa_ui_common_hud_draw_options options;
  memset(&options, 0, sizeof options);
  options.skin = hud_skin(ui); options.measure_text = measure_hud_text; options.measure_ctx = ui;
  options.preferences = preferences(ui); options.messages = ui->messages; options.camera = camera;
  options.localize = identity_localize; options.localize_ctx = ui;
  return options;
}

static void weapon_hud(qa_app_seat_ui *ui, const qa_weapon_hud_status *status, qa_arsenal_ammo_warning warning,
                       bool native_status, bool with_ammo_aspect, qa_ui_common_weapon_hud *out) {
  memset(out, 0, sizeof *out);
  out->status = *status; out->warning = warning;
  out->weapon_icon = ui->weapon_icon; out->ammo_icon = ui->ammo_icon;
  qa_resource_id first = ui->weapon_icon != NULL ? ui->weapon_icon : ui->ammo_icon;
  out->has_icon_aspect = true;
  out->icon_aspect = ui->weapon_assets != NULL ? qa_app_weapon_hud_assets_aspect(ui->weapon_assets, first != NULL ? &first : NULL) : 1;
  if (with_ammo_aspect) {
    out->has_ammo_aspect = true;
    out->ammo_aspect = ui->weapon_assets != NULL
        ? qa_app_weapon_hud_assets_aspect(ui->weapon_assets, ui->ammo_icon != NULL ? &ui->ammo_icon : NULL) : 1;
  }
  out->native_status = native_status;
  out->measure_text = measure_hud_text; out->measure_ctx = ui;
}

void qa_app_seat_ui_draw(qa_app_seat_ui *ui, qa_ui_draw_context context, const qa_scene_camera *camera,
                         qa_app_seat_ui_sinks sinks, qa_app_seat_ui_draw_flags flags,
                         const qa_app_seat_ui_source_status *source_status) {
  bool native_source = source_status != NULL && is(source_status->kind, "native");
  const qa_app_seat_ui_source_status *vitals = source_status != NULL && is(source_status->kind, "vitals") ? source_status : NULL;
  if (!qa_ui_text_renderer_bind(&ui->text, ui->art->skin.font, hud_font(ui))
      || !qa_ui_text_renderer_bind(&ui->menu_text, ui->art->skin.font, menu_font(ui)))
    qa_range_error("Out of memory");
  render_ctx state = {ui, sinks, NULL, false, NULL, 0, 0};
  if (ui->options.guest_ui && !native_source && flags.game_visible && focus_game(ui)) {
    qa_app_sim_player_ui player = player_ui(ui);
    if (player.weapon_status != NULL) {
      qa_ui_common_hud_data hud = qa_ui_empty_hud_data(ui->seat);
      hud.visible = true; hud.crosshair_visible = false;
      qa_ui_common_weapon_hud weapon;
      weapon_hud(ui, player.weapon_status, "none", true, false, &weapon);
      hud.weapon = &weapon;
      qa_ui_common_hud_draw_options options = hud_options(ui, camera);
      qa_ui_hud_commands commands;
      if (!qa_ui_draw_common_hud(context, &hud, &options, &commands)) qa_range_error("Out of memory");
      render(&state, &ui->text, context, commands.commands, commands.count);
      qa_ui_hud_commands_free(&commands);
    }
  }
  if (!ui->options.guest_ui || vitals != NULL) {
    qa_app_sim_player_ui player = player_ui(ui);
    size_t point_count = 0;
    const qa_ui_hud_point_of_interest *points = qa_app_seat_source_hud_points(ui->source_hud, &point_count);
    qa_seat_hud_messages_set_source_points(ui->messages, ui->seat, points, point_count);
    qa_app_hud_presentation source = qa_app_seat_source_hud_presentation(ui->source_hud, context.time_milliseconds);
    double armor = player.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : player.armor.regular.points;
    qa_ui_common_hud_data hud = qa_ui_empty_hud_data(ui->seat);
    hud.health_bars = source.health_bars; hud.health_bar_count = source.health_bar_count;
    hud.help = source.help;
    hud.inventory_is_null = source.inventory_is_null; hud.inventory = source.inventory; hud.inventory_count = source.inventory_count;
    hud.has_pickup = source.pickup != NULL;
    if (source.pickup != NULL) {
      hud.pickup_name = source.pickup->name; hud.pickup_icon = source.pickup->icon;
      hud.pickup_expires_milliseconds = source.pickup->expires_milliseconds;
    }
    hud.damage_indicators = source.damage_indicators; hud.damage_indicator_count = source.damage_indicator_count;
    hud.has_help_path = source.has_help_path;
    hud.help_path_origin = source.help_path_origin; hud.help_path_direction = source.help_path_direction;
    qa_caption_preferences caption_preferences = {true, preferences(ui).captions, true};
    size_t caption_count = 0;
    qa_active_caption *captions = qa_app_seat_sound_captions_active(ui->sound_captions, caption_preferences, &caption_count);
    hud.captions = captions; hud.caption_count = caption_count;
    hud.powerups = player.powerups; hud.powerup_count = player.powerup_count;
    size_t match_count = 0;
    const qa_ui_hud_prompt *match_prompts = qa_app_q2_match_ui_prompts(ui->match, &match_count);
    qa_ui_hud_prompt *prompts = xcalloc(match_count + source.prompt_count + 1, sizeof *prompts);
    size_t prompt_count = 0;
    for (size_t i = 0; i < match_count; i++) prompts[prompt_count++] = match_prompts[i];
    for (size_t i = 0; i < source.prompt_count; i++) prompts[prompt_count++] = source.prompts[i];
    char power[96];
    if (player.armor.powered.kind != NULL && !is(player.armor.powered.kind, "none")) {
      snprintf(power, sizeof power, "Power %s %s", player.armor.powered.kind, QA_JS_NUMBER(player.armor.powered.cells));
      prompts[prompt_count].action = power; prompts[prompt_count].binding = ""; prompts[prompt_count].icon = NULL;
      prompt_count++;
    }
    hud.prompts = prompts; hud.prompt_count = prompt_count;
    qa_ui_wheel_draw_state wheel = qa_seat_weapon_wheel_draw_state(ui->weapon_wheel);
    hud.wheel = wheel.has_wheel ? &wheel.wheel : NULL;
    hud.carousel = wheel.has_carousel ? &wheel.carousel : NULL;
    hud.visible = flags.game_visible && focus_game(ui);
    hud.crosshair_visible = flags.crosshair_visible && !flags.native_crosshair;
    qa_ui_common_weapon_hud weapon;
    if (!(player.weapon_status == NULL || native_source || ui->options.guest_ui || (flags.native_status && player.selected_arsenal))) {
      weapon_hud(ui, player.weapon_status, flags.show_aggregate_warning ? player.arsenal_warning : "none",
                 flags.native_status, true, &weapon);
      hud.weapon = &weapon;
    }
    qa_ui_hud_value values[2];
    memset(values, 0, sizeof values);
    if (!(flags.native_status && vitals == NULL)) {
      double health = vitals != NULL ? vitals->health : player.health;
      values[0].label = "Health"; values[0].value = health; values[0].warning = health <= 25;
      values[1].label = "Armor"; values[1].value = vitals != NULL ? vitals->armor : armor; values[1].warning = false;
      hud.vitals = values; hud.vital_count = 2;
    } else { hud.vitals = NULL; hud.vital_count = 0; }
    qa_ui_common_hud_draw_options options = hud_options(ui, camera);
    qa_ui_hud_commands commands;
    if (!qa_ui_draw_common_hud(context, &hud, &options, &commands)) qa_range_error("Out of memory");
    render(&state, &ui->text, context, commands.commands, commands.count);
    qa_ui_hud_commands_free(&commands);
    free(prompts);
    qa_active_captions_free(captions, caption_count);
  }
  qa_ui_draw_command panel = qa_ui_menu_panel_scaled(context, false, preferences(ui).menu_scale);
  qa_ui_draw_command backdrop = panel;
  if (is(qa_native_ui_controller_active_menu(ui->controller), qa_app_player_death_menu) && panel.kind == QA_UI_DRAW_FILL)
    backdrop.color.w = 0.45;
  if (qa_native_ui_controller_active_menu(ui->controller) == NULL) return;
  qa_ui_draw_context menu_context = context;
  menu_context.time_milliseconds = controller_now(ui);
  size_t menu_count = 0;
  const qa_ui_draw_command *menu = qa_native_ui_controller_draw(ui->controller, menu_context, &menu_count);
  qa_ui_draw_command *commands = xcalloc(menu_count + 1, sizeof *commands);
  commands[0] = backdrop;
  if (menu_count != 0) memcpy(commands + 1, menu, menu_count * sizeof *menu);
  render_ctx menus = {ui, sinks, NULL, true, NULL, 0, 0};
  render(&menus, &ui->menu_text, context, commands, menu_count + 1);
  free(commands);
}

/* ---- native Q2 HUD ---- */
static void clear_native_item(qa_app_seat_ui *ui) {
  ui->has_native_item = false;
  free(ui->native_item); free(ui->native_source_content); free(ui->native_source_provider);
  free(ui->native_label); free(ui->native_localized_label);
  ui->native_item = ui->native_source_content = ui->native_source_provider = NULL;
  ui->native_label = ui->native_localized_label = NULL;
  ui->native_has_icon = false; ui->native_icon = NULL;
}

/* TS nativeQ2Arsenal(). */
static bool native_q2_arsenal(qa_app_seat_ui *ui, qa_native_q2_hud_arsenal *out) {
  memset(out, 0, sizeof *out);
  qa_app_sim_player_ui player = player_ui(ui);
  const qa_compat_q2_native_inventory_readout *selected = player.native_inventory;
  if (!player.selected_arsenal && selected == NULL) return false;
  qa_resource_id icon = ui->ammo_icon;
  if (ui->has_native_item && selected != NULL && is(ui->native_item, selected->selected) && selected->has_presentation
      && is(ui->native_source_content, selected->presentation.source.content)
      && is(ui->native_source_provider, selected->presentation.source.provider)) {
    out->has_selected_item = true;
    out->selected_label = ui->native_label; out->selected_localized_label = ui->native_localized_label;
    out->selected_has_icon = ui->native_has_icon; out->selected_icon_resource = ui->native_icon;
    out->selected_icon_aspect = ui->native_aspect;
  }
  if (selected != NULL) { out->has_inventory = true; out->inventory = *selected; }
  if (player.selected_arsenal) {
    out->has_ammunition = true;
    out->ammo_has_count = player.has_ammo; out->ammo_count = player.ammo_count;
    out->ammo_has_icon = icon != NULL;
    if (icon != NULL) {
      out->ammo_icon_resource = icon;
      out->ammo_icon_aspect = ui->weapon_assets != NULL ? qa_app_weapon_hud_assets_aspect(ui->weapon_assets, &icon) : 1;
    }
  }
  return true;
}

/* TS nativeSourceLocalizer(content, assets) applied to (text, args). */
static void ensure_native_localizer(qa_app_seat_ui *ui, qa_app_application_assets *assets) {
  if (ui->options.native_hud_localizer != NULL || ui->native_hud_localizations != NULL) return;
  qa_app_rerelease_seat seat = {ui->seat, ui->local->player.actor, NULL};
  ui->native_hud_localizations = qa_app_rerelease_presentation_create(assets, &seat, 1,
                                                                      qa_app_application_input_shared_settings(ui->input));
  if (ui->native_hud_localizations == NULL) qa_range_error("Out of memory");
}
static char *native_localize(qa_app_seat_ui *ui, qa_app_application_assets *assets, qa_content_id content, const char *text,
    const char *const *args, size_t count) {
  ensure_native_localizer(ui, assets);
  if (ui->options.native_hud_localizer != NULL)
    return ui->options.native_hud_localizer(ui->options.native_hud_localizer_ctx, content, text, args, count);
  return qa_app_rerelease_source_localize(ui->native_hud_localizations, ui->seat, content, text, args, count);
}

/* TS prepareNativeInventory(assets). */
static void prepare_native_inventory(qa_app_seat_ui *ui, qa_app_application_assets *assets) {
  clear_native_item(ui);
  qa_app_sim_player_ui player = player_ui(ui);
  const qa_compat_q2_native_inventory_readout *inventory = player.native_inventory;
  if (inventory == NULL || !inventory->has_presentation) return;
  const qa_compat_q2_native_inventory_presentation *presentation = &inventory->presentation;
  const qa_compat_q2_native_inventory_readout_item *item = NULL;
  for (size_t i = 0; i < inventory->item_count && item == NULL; i++)
    if (is(inventory->items[i].item, inventory->selected)) item = &inventory->items[i];
  if (item == NULL) return;
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(qa_app_application_assets_content(assets));
  const qa_catalog_product_expectation *product = &qa_content_catalog_product(catalog, presentation->source.content)->expectation;
  qa_weapon_hud_icons icons;
  memset(&icons, 0, sizeof icons);
  bool found = false;
  if (!is(presentation->kind, "item")
      && !qa_weapon_hud_icons_for(presentation->source, product, presentation->weapon, &icons, &found))
    qa_range_error("Out of memory");
  const qa_weapon_hud_icon *icon = NULL;
  if (is(presentation->kind, "item")) icon = presentation->has_icon ? presentation->icon : NULL;
  else if (found && is(presentation->kind, "weapon"))
    icon = icons.has_selected_weapon ? &icons.selected_weapon : icons.has_weapon ? &icons.weapon : NULL;
  else if (found) icon = icons.has_ammo ? &icons.ammo : NULL;
  qa_app_weapon_hud_assets *owner = weapon_assets(ui, assets);
  qa_resource_id resource = icon == NULL ? NULL : qa_app_weapon_hud_assets_load(owner, *icon);
  qa_weapon_hud_icons_free(&icons);
  char *localized = is(product->edition, "rerelease")
      ? native_localize(ui, assets, presentation->source.content, item->label, NULL, 0) : xdup(item->label);
  ui->has_native_item = true;
  ui->native_item = xdup(item->item);
  ui->native_source_content = xdup(presentation->source.content);
  ui->native_source_provider = xdup(presentation->source.provider);
  ui->native_label = xdup(item->label);
  ui->native_localized_label = localized;
  ui->native_has_icon = resource != NULL;
  ui->native_icon = resource;
  ui->native_aspect = resource != NULL ? qa_app_weapon_hud_assets_aspect(owner, &resource) : 1;
}

typedef struct native_environment { qa_app_seat_ui *ui; qa_app_application_assets *assets; qa_content_id content; qa_text_font_selection font; } native_environment;
static qa_vec2 native_measure(void *ctx, const char *text) {
  native_environment *environment = ctx;
  return measure(environment->font, text, 1.25);
}
static const char *native_environment_localize(void *ctx, const char *text, const char *const *args, size_t count) {
  native_environment *environment = ctx;
  qa_app_seat_ui *ui = environment->ui;
  free(ui->localized_text);
  ui->localized_text = native_localize(ui, environment->assets, environment->content, text, args, count);
  return ui->localized_text;
}

static bool rerelease_protocol(qa_q2_protocol_identity protocol) {
  return is(protocol.kind, "q2-rerelease") || is(protocol.kind, "q2-kex") || is(protocol.kind, "q2-kex-demo");
}

void qa_app_seat_ui_prepare_native_q2_hud(qa_app_seat_ui *ui, const qa_native_q2_hud_frame *frame, qa_content_id content,
    qa_app_application_assets *assets, qa_ui_draw_context context, const qa_app_seat_ui_q2_component *component) {
  if (component == NULL) prepare_native_inventory(ui, assets);
  qa_app_native_q2_hud_rendering rendering;
  memset(&rendering, 0, sizeof rendering);
  native_environment environment = {ui, assets, content, hud_font(ui)};
  bool has_environment = false;
  if (rerelease_protocol(frame->protocol)) {
    qa_cvar_registry *registry = qa_app_application_input_cvars(ui->input);
    qa_cvar_read use_font;
    if (!qa_cvar_registry_find(registry, "scr_usekfont", &use_font)
        && !qa_cvar_registry_register(registry, "scr_usekfont", "1", 0, &use_font))
      qa_raise("Error", 0, 0, "Could not register rerelease HUD font setting");
    ensure_native_localizer(ui, assets);
    if (component != NULL && component->assert_current != NULL) component->assert_current(component->assert_ctx);
    rendering.font = ui->art->skin.font; rendering.font_scale = 1.25;
    rendering.environment.use_font = use_font.integer_value != 0;
    rendering.environment.font_line_height = 8 * 1.25;
    rendering.environment.ctx = &environment;
    rendering.environment.measure = native_measure; rendering.environment.localize = native_environment_localize;
    qa_cvar_read_free(&use_font);
    has_environment = true;
  }
  qa_native_q2_hud_arsenal arsenal;
  bool has_arsenal = component == NULL && native_q2_arsenal(ui, &arsenal);
  qa_app_q2_native_hud_prepare(component != NULL ? component->renderer : ui->native_q2_hud, content, assets,
      frame, context, preferences(ui).hud_scale * context.binding.hud_scale, component != NULL ? component->mode : NULL,
      component != NULL ? component->assert_current : NULL, component != NULL ? component->assert_ctx : NULL,
      has_arsenal ? &arsenal : NULL, has_environment ? &rendering : NULL);
}

void qa_app_seat_ui_draw_native_q2_hud(qa_app_seat_ui *ui, const qa_native_q2_hud_frame *frame, qa_ui_draw_context context,
    qa_app_seat_ui_sinks sinks, qa_q2_hud_binding_fn binding, void *binding_ctx, const qa_app_seat_ui_q2_component *component) {
  qa_app_q2_native_hud *renderer = component != NULL ? component->renderer : ui->native_q2_hud;
  if (!qa_ui_text_renderer_bind(&ui->text, ui->art->skin.font, hud_font(ui))) qa_range_error("Out of memory");
  qa_native_q2_hud_arsenal arsenal;
  bool has_arsenal = component == NULL && native_q2_arsenal(ui, &arsenal);
  qa_ui_draw_command *commands = NULL;
  size_t count = qa_app_q2_native_hud_commands(renderer, frame, context, preferences(ui).hud_scale * context.binding.hud_scale,
      binding, binding_ctx, component != NULL ? component->mode : NULL, has_arsenal ? &arsenal : NULL, &commands);
  render_ctx state = {ui, sinks, renderer, false, NULL, 0, 0};
  render(&state, &ui->text, context, commands, count);
  free(commands);
}

/* ---- captions ---- */
static void caption_emit(void *ctx, const qa_render_command *command) {
  render_ctx *render_state = ctx;
  if (render_state->collected_count == render_state->collected_capacity) {
    size_t next = render_state->collected_capacity == 0 ? 16 : render_state->collected_capacity * 2;
    qa_render_command *grown = realloc(render_state->collected, next * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    render_state->collected = grown; render_state->collected_capacity = next;
  }
  render_state->collected[render_state->collected_count++] = *command;
}
static void caption_material(void *ctx, const qa_material_text_draw *draw) {
  (void)ctx; (void)draw;
  qa_raise("Error", 0, 0, "Caption text requested a material draw");
}

qa_render_command *qa_app_seat_ui_caption_commands(qa_app_seat_ui *ui, const qa_active_caption *captions,
    size_t caption_count, qa_ui_draw_context context, size_t *out_count) {
  *out_count = 0;
  if (!preferences(ui).captions) return NULL;
  if (!qa_ui_text_renderer_bind(&ui->menu_text, ui->art->skin.font, menu_font(ui))) qa_range_error("Out of memory");
  qa_rect area = context.binding.safe_area;
  qa_rect region = {area.x + 8, area.y + area.height * 0.65, area.width - 16, area.height * 0.3};
  double fit = area.width / 640 < area.height / 480 ? area.width / 640 : area.height / 480;
  qa_ui_caption_commands draws;
  if (!qa_ui_get_caption_commands(captions, caption_count, region, ui->art->skin.font, preferences(ui).text_scale * fit,
                                  measure_menu_text, ui, 0, 8, &draws))
    qa_range_error("Out of memory");
  render_ctx state;
  memset(&state, 0, sizeof state);
  state.ui = ui; state.art_only = true;
  qa_ui_render_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &state; services.text = &ui->menu_text; services.white = image_asset(ui->art->white);
  services.picture = render_picture; services.emit = caption_emit; services.material = caption_material;
  qa_ui_render_commands(context, draws.commands, draws.count, &services);
  qa_ui_caption_commands_free(&draws);
  *out_count = state.collected_count;
  return state.collected;
}

/* ---- close ---- */
void qa_app_seat_ui_close(qa_app_seat_ui *ui) {
  if (ui->closed) return;
  ui->closed = true;
  qa_ui_settings_menus_dispose(&ui->inventory_menu);
  qa_app_seat_sound_captions_close(ui->sound_captions);
  if (ui->has_match_menu) qa_ui_settings_menus_dispose(&ui->match_menu);
  if (ui->base_arena != NULL) qa_app_base_arena_menus_close(ui->base_arena);
  if (ui->team_arena != NULL) qa_app_team_arena_results_close(ui->team_arena);
  qa_app_seat_player_death_close(ui->death); /* frees the death owner */
  ui->death = NULL;
  qa_app_seat_game_prompt_close(ui->prompt);
  qa_app_q2_match_ui_close(ui->match);
  qa_app_input_ui_attachment_detach(ui->dispose_input);
  ui->dispose_input = NULL;
  qa_native_ui_controller_close_all(ui->controller);
  qa_native_ui_controller_unregister(ui->controller, ui->dispose_menu);
  qa_ui_saved_game_menus_dispose(&ui->saves);
  qa_ui_settings_menus_dispose(&ui->settings);
  qa_ui_settings_menus_dispose(&ui->gyro_settings);
  if (ui->has_server_settings) qa_ui_settings_menus_dispose(&ui->server_settings);
  if (ui->has_ranking_menu) qa_ui_settings_menus_dispose(&ui->ranking_menu);
  qa_ui_settings_menus_dispose(&ui->bindings);
  qa_ui_text_renderer_clear(&ui->text);
  qa_ui_text_renderer_clear(&ui->menu_text);
  qa_seat_hud_messages_clear(ui->messages);
}

void qa_app_seat_ui_destroy(qa_app_seat_ui *ui) {
  if (ui == NULL) return;
  qa_app_seat_ui_close(ui);
  for (size_t i = 0; i < ui->owned_count; i++) qa_ui_setting_bindings_free(&ui->owned[i].bindings);
  free(ui->owned); free(ui->settings_items);
  if (ui->has_binding_actions) qa_ui_binding_actions_free(&ui->binding_actions);
  free(ui->binding_items);
  clear_wheel_items(ui);
  free_output_names(ui);
  qa_controller_devices_free(ui->devices, ui->device_count);
  for (size_t i = 0; i < ui->wheel_icon_count; i++) { free(ui->wheel_icons[i].item); free(ui->wheel_icons[i].resource); }
  free(ui->wheel_icons);
  clear_native_item(ui);
  free(ui->language_text); free(ui->localized_text); free(ui->prompt_language); free(ui->clipboard_text);
  if (ui->native_hud_localizations != NULL) qa_app_rerelease_presentation_destroy(ui->native_hud_localizations);
  if (ui->weapon_assets != NULL) qa_app_weapon_hud_assets_destroy(ui->weapon_assets);
  qa_app_q2_match_ui_destroy(ui->match);
  qa_app_seat_game_prompt_destroy(ui->prompt);
  if (ui->team_arena != NULL) qa_app_team_arena_results_destroy(ui->team_arena);
  if (ui->base_arena != NULL) qa_app_base_arena_menus_destroy(ui->base_arena);
  qa_app_seat_source_hud_destroy(ui->source_hud);
  qa_app_seat_sound_captions_destroy(ui->sound_captions);
  qa_seat_weapon_wheel_destroy(ui->weapon_wheel);
  qa_app_application_q1_wheel_destroy(ui->authored_wheel);
  qa_app_q2_native_hud_destroy(ui->native_q2_hud);
  qa_seat_hud_messages_destroy(ui->messages);
  /* SeatUiPreferences binding contexts (ui/settings has no destroy for them). */
  for (size_t i = 0; i < QA_UI_PREFERENCE_BINDING_COUNT; i++) free(ui->preferences.binding_ctx[i]);
  qa_ui_text_renderer_destroy(&ui->text);
  qa_ui_text_renderer_destroy(&ui->menu_text);
  qa_native_ui_controller_destroy(ui->controller);
  free(ui);
}

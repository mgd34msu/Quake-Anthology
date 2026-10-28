/* Port of src/content/q3/presentation/info.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"
#include "qa/text/q3_font.h"

typedef qa_q3_pres_client_loading_screen screen_t;
#define STR(text) ((text) != NULL ? (text) : "")

/* TS sourceText: stop at NUL (C strings already do), slice size - 1.
 * The charCode > 255 rejection cannot occur for byte strings. */
static char *source_text(const char *input, size_t size) {
  size_t length = strlen(input);
  if (length > size - 1) length = size - 1;
  char *copy = malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, input, length);
  copy[length] = '\0';
  return copy;
}
static char *clean_text(const char *text) {
  size_t length = strlen(text);
  char *result = malloc(length + 1);
  if (result == NULL) return NULL;
  size_t written = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char byte = (unsigned char)text[i];
    if (byte == 94 && i + 1 < length && text[i + 1] != '^') i++;
    else if (byte >= 32 && byte <= 126) result[written++] = text[i];
  }
  result[written] = '\0';
  return result;
}

void qa_q3_pres_client_loading_screen_init(screen_t *screen, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_media *media, qa_cvar_registry *cvars, qa_q3_pres_client_loading_imports imports) {
  if (state->product != media->product) {
    qa_range_error("Loading screen product differs from cgame media");
  }
  memset(screen, 0, sizeof *screen);
  screen->state = state;
  screen->media = media;
  screen->cvars = cvars;
  screen->imports = imports;
}

bool qa_q3_pres_client_loading_screen_loading_string(screen_t *screen, const char *text) {
  char *sliced = source_text(text, 1024);
  if (sliced == NULL) return false;
  bool ok = qa_q3_pres_set_string(&screen->state->info_screen_text, sliced);
  free(sliced);
  if (!ok) return false;
  screen->imports.update_screen(screen->imports.ctx);
  return true;
}

bool qa_q3_pres_client_loading_screen_loading_item(screen_t *screen, double index) {
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(screen->media->product, index);
  if (item->pickup_name == NULL) qa_range_error("CG_LoadingItem requires a named item");
  if (item->icon != NULL && screen->item_icon_count < 26) {
    const qa_q3_pres_scene_shader *icon = NULL;
    if (!screen->media->resources.register_shader_no_mip(screen->media->resources.ctx, item->icon, &icon)) {
      return false;
    }
    screen->item_icons[screen->item_icon_count++] = icon;
  }
  return qa_q3_pres_client_loading_screen_loading_string(screen, item->pickup_name);
}

bool qa_q3_pres_client_loading_screen_loading_client(screen_t *screen, double client_num) {
  if (client_num != (double)(int64_t)client_num || client_num < 0 || client_num >= 64) {
    qa_range_error("CG_LoadingClient: bad client number");
  }
  const char *info = STR(screen->imports.config_string(screen->imports.ctx, 544 + client_num));
  bool ok = true;
  if (screen->player_icon_count < 16) {
    char *raw_value = qa_info_value_for_key(info, "model");
    char *value = raw_value != NULL ? source_text(raw_value, 64) : NULL;
    free(raw_value);
    if (value == NULL) return false;
    char *slash = strrchr(value, '/');
    char model[64], skin[64];
    if (slash == NULL) {
      snprintf(model, sizeof model, "%.63s", value);
      snprintf(skin, sizeof skin, "default");
    } else {
      size_t model_length = (size_t)(slash - value);
      if (model_length >= sizeof model) model_length = sizeof model - 1;
      memcpy(model, value, model_length);
      model[model_length] = '\0';
      snprintf(skin, sizeof skin, "%.63s", slash + 1);
    }
    free(value);
    const qa_q3_pres_scene_shader *icon = NULL;
    char combined[256], path[64];
    snprintf(combined, sizeof combined, "models/players/%s/icon_%s.tga", model, skin);
    char *sliced = source_text(combined, 64);
    if (sliced == NULL) return false;
    snprintf(path, sizeof path, "%.63s", sliced);
    free(sliced);
    if (!screen->media->resources.register_shader_no_mip(screen->media->resources.ctx, path, &icon)) {
      return false;
    }
    if (icon == NULL) {
      snprintf(combined, sizeof combined, "models/players/characters/%s/icon_%s.tga", model, skin);
      sliced = source_text(combined, 64);
      if (sliced == NULL) return false;
      snprintf(path, sizeof path, "%.63s", sliced);
      free(sliced);
      if (!screen->media->resources.register_shader_no_mip(screen->media->resources.ctx, path, &icon)) {
        return false;
      }
    }
    if (icon == NULL) {
      if (!screen->media->resources.register_shader_no_mip(screen->media->resources.ctx,
              "models/players/sarge/icon_default.tga", &icon)) {
        return false;
      }
    }
    if (icon != NULL) screen->player_icons[screen->player_icon_count++] = icon;
  }
  char *raw_name = qa_info_value_for_key(info, "n");
  char *sliced_name = raw_name != NULL ? source_text(raw_name, 64) : NULL;
  free(raw_name);
  if (sliced_name == NULL) return false;
  char *personality = clean_text(sliced_name);
  free(sliced_name);
  if (personality == NULL) return false;
  if (screen->media->static_state->game_type == QA_Q3_BASE_GT_SINGLE_PLAYER) {
    char path[128];
    snprintf(path, sizeof path, "sound/player/announce/%.100s.wav", personality);
    qa_pcm_sound *ignored = NULL;
    ok = screen->media->sound_bank.register_sound(screen->media->sound_bank.ctx, path, true, &ignored);
  }
  if (ok) ok = qa_q3_pres_client_loading_screen_loading_string(screen, personality);
  free(personality);
  return ok;
}

/* ClientDrawTools (draw-tools.ts) pieces used by cg_info. */
static qa_picture_asset picture_asset(screen_t *screen, const qa_q3_pres_scene_shader *shader) {
  qa_picture_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_MATERIAL;
  asset.material = screen->media->resources.picture(screen->media->resources.ctx, shader);
  return asset;
}
static void draw_pic(screen_t *screen, qa_draw2d *draw, qa_rect rect, const qa_q3_pres_scene_shader *shader) {
  qa_picture_asset picture = picture_asset(screen, shader);
  qa_draw2d_draw_pic(draw, rect, &picture);
}
static void draw_loading_icons(screen_t *screen, qa_draw2d *draw) {
  for (size_t n = 0; n < screen->player_icon_count; n++) {
    qa_rect rect = {16 + (double)n * 78, 284, 64, 64};
    draw_pic(screen, draw, rect, screen->player_icons[n]);
  }
  for (size_t n = 0; n < screen->item_icon_count; n++) {
    qa_rect rect = {16 + (double)(n % 13) * 48, 360 + (n >= 13 ? 40 : 0), 32, 32};
    draw_pic(screen, draw, rect, screen->item_icons[n]);
  }
}
static const char *game_type_name(screen_t *screen) {
  switch (screen->media->static_state->game_type) {
    case QA_Q3_BASE_GT_FFA: return "Free For All";
    case QA_Q3_BASE_GT_SINGLE_PLAYER: return "Single Player";
    case QA_Q3_BASE_GT_TOURNAMENT: return "Tournament";
    case QA_Q3_BASE_GT_TEAM: return "Team Deathmatch";
    case QA_Q3_BASE_GT_CTF: return "Capture The Flag";
    case QA_Q3_BASE_GT_1FCTF:
      return screen->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? "One Flag CTF" : "Unknown Gametype";
    case QA_Q3_BASE_GT_OBELISK:
      return screen->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? "Overload" : "Unknown Gametype";
    case QA_Q3_BASE_GT_HARVESTER:
      return screen->media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? "Harvester" : "Unknown Gametype";
    default: return "Unknown Gametype";
  }
}
static void draw_text_line(screen_t *screen, qa_draw2d *draw, const qa_q3_legacy_fonts *fonts,
    double y, const char *value) {
  qa_q3_ui_text_options options = {320, y, value, {1, 1, 1, 1},
                                   QA_UI_CENTER | QA_UI_SMALLFONT | QA_UI_DROPSHADOW, screen->state->time};
  qa_q3_draw_cg_proportional_string(draw, fonts, &options);
}

bool qa_q3_pres_client_loading_screen_draw_information(screen_t *screen, qa_draw2d *draw) {
  if (draw->space != QA_COORD_STRETCH_640) {
    qa_range_error("Cgame drawing requires stretch-640 coordinates");
  }
  const char *info = STR(screen->imports.config_string(screen->imports.ctx, 0));
  const char *system = STR(screen->imports.config_string(screen->imports.ctx, 1));
  qa_q3_pres_renderer_resources *resources = &screen->media->resources;
  char *map = qa_info_value_for_key(info, "mapname");
  if (map == NULL) return false;
  char levelshot_path[256];
  snprintf(levelshot_path, sizeof levelshot_path, "levelshots/%.200s.tga", map);
  free(map);
  const qa_q3_pres_scene_shader *levelshot = NULL;
  if (!resources->register_shader_no_mip(resources->ctx, levelshot_path, &levelshot)) return false;
  if (levelshot == NULL) {
    if (!resources->register_shader_no_mip(resources->ctx, "menu/art/unknownmap", &levelshot)) return false;
  }
  qa_q3_legacy_fonts fonts;
  fonts.charset = picture_asset(screen, screen->media->graphics.charset_shader);
  fonts.proportional = picture_asset(screen, screen->media->graphics.charset_prop);
  fonts.glow = picture_asset(screen, screen->media->graphics.charset_prop_glow);
  fonts.banner = picture_asset(screen, screen->media->graphics.charset_prop_b);
  qa_draw2d_set_color(draw, NULL);
  qa_rect full = {0, 0, 640, 480};
  draw_pic(screen, draw, full, levelshot);
  const qa_q3_pres_scene_shader *detail = NULL;
  if (!resources->register_shader(resources->ctx, "levelShotDetail", &detail)) return false;
  qa_picture_asset detail_picture = picture_asset(screen, detail);
  qa_rect pixels = {0, 0, qa_draw2d_width(draw), qa_draw2d_height(draw)};
  qa_text_texture_rect uv = {0, 0, 2.5, 2};
  qa_draw2d_stretch_pixels(draw, pixels, uv, &detail_picture);
  draw_loading_icons(screen, draw);
  bool ok = true;
  char *value = NULL;
  char loading_line[1100];
  if (STR(screen->state->info_screen_text)[0] != '\0') {
    snprintf(loading_line, sizeof loading_line, "Loading... %.1050s", STR(screen->state->info_screen_text));
  } else {
    snprintf(loading_line, sizeof loading_line, "Awaiting snapshot...");
  }
  draw_text_line(screen, draw, &fonts, 96, loading_line);
  double y = 148;
  /* Cvar_VariableStringBuffer returns the empty string for an unregistered cvar. */
  qa_cvar_snapshot server;
  bool has_server = qa_cvar_registry_get(screen->cvars, "sv_running", &server);
  char *server_text = has_server ? source_text(STR(server.value), 1024) : NULL;
  if (has_server && server_text == NULL) {
    qa_cvar_read_free(&server);
    return false;
  }
  int32_t running = qa_game_atoi(has_server ? server_text : "");
  free(server_text);
  if (has_server) qa_cvar_read_free(&server);
  if (running == 0) {
    value = qa_info_value_for_key(info, "sv_hostname");
    char *sliced = value != NULL ? source_text(value, 1024) : NULL;
    free(value);
    char *cleaned = sliced != NULL ? clean_text(sliced) : NULL;
    free(sliced);
    if (cleaned == NULL) return false;
    draw_text_line(screen, draw, &fonts, y, cleaned);
    free(cleaned);
    y += 27;
    value = qa_info_value_for_key(system, "sv_pure");
    if (value != NULL && value[0] == '1') {
      draw_text_line(screen, draw, &fonts, y, "Pure Server");
      y += 27;
    }
    free(value);
    char *motd = source_text(STR(screen->imports.config_string(screen->imports.ctx, 4)), 16000);
    if (motd == NULL) return false;
    if (motd[0] != '\0') {
      draw_text_line(screen, draw, &fonts, y, motd);
      y += 27;
    }
    free(motd);
    y += 10;
  }
  char *message = source_text(STR(screen->imports.config_string(screen->imports.ctx, 3)), 16000);
  if (message == NULL) return false;
  if (message[0] != '\0') {
    draw_text_line(screen, draw, &fonts, y, message);
    y += 27;
  }
  free(message);
  value = qa_info_value_for_key(system, "sv_cheats");
  if (value != NULL && value[0] == '1') {
    draw_text_line(screen, draw, &fonts, y, "CHEATS ARE ENABLED");
    y += 27;
  }
  free(value);
  draw_text_line(screen, draw, &fonts, y, game_type_name(screen));
  y += 27;
  value = qa_info_value_for_key(info, "timelimit");
  int32_t time_limit = qa_game_atoi(STR(value));
  free(value);
  if (time_limit != 0) {
    char line[48];
    snprintf(line, sizeof line, "timelimit %lld", (long long)time_limit);
    draw_text_line(screen, draw, &fonts, y, line);
    y += 27;
  }
  if (screen->media->static_state->game_type < QA_Q3_BASE_GT_CTF) {
    value = qa_info_value_for_key(info, "fraglimit");
    int32_t frag_limit = qa_game_atoi(STR(value));
    free(value);
    if (frag_limit != 0) {
      char line[48];
      snprintf(line, sizeof line, "fraglimit %lld", (long long)frag_limit);
      draw_text_line(screen, draw, &fonts, y, line);
    }
  }
  if (screen->media->static_state->game_type >= QA_Q3_BASE_GT_CTF) {
    value = qa_info_value_for_key(info, "capturelimit");
    int32_t capture_limit = qa_game_atoi(STR(value));
    free(value);
    if (capture_limit != 0) {
      char line[48];
      snprintf(line, sizeof line, "capturelimit %lld", (long long)capture_limit);
      draw_text_line(screen, draw, &fonts, y, line);
    }
  }
  return ok;
}

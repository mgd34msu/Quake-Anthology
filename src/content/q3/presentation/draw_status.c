/* Port of src/content/q3/presentation/draw-status.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/draw_status.h"
#include <math.h>
#include <string.h>
#include "qa/content/q3/presentation/client_info.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
enum { LAG_SAMPLES = 128, MAX_LAGOMETER_PING = 900, MAX_LAGOMETER_RANGE = 300 };
static float f32(double v) { return (float)v; }
static const qa_vec4 YELLOW_C = {1, 1, 0, 1}, BLUE_C = {0, 0, 1, 1}, GREEN_C = {0, 1, 0, 1}, RED_C = {1, 0, 0, 1};
const char *qa_q3_pres_draw_status_cvar_name(qa_q3_pres_draw_status_cvar cvar) {
  switch (cvar) {
    case QA_Q3_PRES_STATUS_CVAR_CENTER_TIME: return "cg_centertime";
    case QA_Q3_PRES_STATUS_CVAR_LAGOMETER: return "cg_lagometer";
    case QA_Q3_PRES_STATUS_CVAR_NO_PREDICT: return "cg_nopredict";
    case QA_Q3_PRES_STATUS_CVAR_SYNCHRONOUS_CLIENTS: return "g_synchronousClients";
  }
  return "cg_centertime";
}
void qa_q3_pres_client_draw_status_init(qa_q3_pres_client_draw_status *status,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_draw_tools *tools, qa_q3_pres_client_game_static_state *tools_static_state,
    qa_q3_pres_draw_status_variant variant, qa_q3_pres_draw_status_host host) {
  if (state->product != static_state->product || tools_static_state != static_state)
    qa_range_error("Draw status services must share canonical cgame state");
  if (variant.kind != state->product) qa_range_error("Draw status product variant differs from cgame state");
  if (variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      (!variant.fonts || variant.fonts->profile != QA_Q3_FONT_CGAME))
    qa_range_error("Missionpack center print requires cgame fonts");
  memset(status, 0, sizeof *status);
  status->state = state;
  status->static_state = static_state;
  status->tools = tools;
  status->variant = variant;
  status->host = host;
}
bool qa_q3_pres_client_draw_status_center_print(qa_q3_pres_client_draw_status *status, const char *text,
                                                double y, double char_width) {
  qa_q3_pres_client_game_state *state = status->state;
  char truncated[1024];
  size_t length = strlen(text);
  if (length > 1023) length = 1023;
  memcpy(truncated, text, length);
  truncated[length] = '\0';
  if (!qa_q3_pres_set_string(&state->center_print, truncated)) return false;
  state->center_print_time = state->time;
  state->center_print_y = qa_int32(y);
  state->center_print_char_width = qa_int32(char_width);
  state->center_print_lines = 1;
  for (const char *p = state->center_print; p && *p; p++)
    if (*p == '\n') state->center_print_lines++;
  return true;
}
static int32_t imul32(double a, double b) {
  uint32_t product = (uint32_t)qa_int32(a) * (uint32_t)qa_int32(b);
  int32_t result;
  memcpy(&result, &product, sizeof result);
  return result;
}
void qa_q3_pres_client_draw_status_draw_center_string(qa_q3_pres_client_draw_status *status) {
  qa_q3_pres_client_game_state *state = status->state;
  if (state->center_print_time == 0) return;
  qa_cvar_snapshot snapshot = status->host.read_vm_cvar(status->host.ctx, QA_Q3_PRES_STATUS_CVAR_CENTER_TIME);
  double duration = f32(1000 * f32(snapshot.numeric_value));
  qa_cvar_read_free(&snapshot);
  qa_vec4 color;
  if (!qa_q3_pres_fade_color(state->time, state->center_print_time, duration, &color)) return;
  const char *text = state->center_print ? state->center_print : "";
  size_t text_length = strlen(text);
  double y = qa_int32(state->center_print_y - trunc(imul32(state->center_print_lines, 16) / 2.0));
  size_t start = 0;
  for (;;) {
    size_t end = start;
    while (end < text_length && text[end] != '\n') end++;
    size_t line_length = end - start;
    if (line_length > 50) line_length = 50;
    char line[51];
    memcpy(line, text + start, line_length);
    line[line_length] = '\0';
    if (status->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      double width = qa_q3_text_width(status->variant.fonts, line, 0.5, 0);
      double height = qa_q3_text_height(status->variant.fonts, line, 0.5, 0);
      double x = trunc((640 - width) / 2);
      qa_q3_text_paint_options options = {x, qa_int32(y + height), 0.5, color, line, 0, 0, 6};
      qa_q3_text_paint(&status->tools->draw, status->variant.fonts, &options);
      y = qa_int32(y + height + 6);
    } else {
      double width = imul32(state->center_print_char_width, qa_q3_pres_draw_strlen(line));
      double x = trunc((640 - width) / 2);
      double height = trunc(f32(f32(state->center_print_char_width) * f32(1.5)));
      qa_q3_fixed_text_options options = {x, y, line, color, state->center_print_char_width, height, 0, false, true};
      qa_q3_pres_client_draw_tools_draw_string_ext(status->tools, options);
      y = trunc(f32(f32(y) + f32(f32(state->center_print_char_width) * f32(1.5))));
    }
    if (end == text_length) break;
    start = end + 1;
  }
  qa_draw2d_set_color(&status->tools->draw, NULL);
}
void qa_q3_pres_client_draw_status_add_lagometer_frame_info(qa_q3_pres_client_draw_status *status) {
  status->frame_samples[status->frame_count & (LAG_SAMPLES - 1)] =
      qa_int32(status->state->time - status->state->latest_snapshot_time);
  status->frame_count = qa_int32((double)status->frame_count + 1);
}
void qa_q3_pres_client_draw_status_add_lagometer_snapshot_info(qa_q3_pres_client_draw_status *status,
    const qa_q3_pres_lagometer_snapshot_sample *sample) {
  int index = status->snapshot_count & (LAG_SAMPLES - 1);
  if (!sample) status->snapshot_samples[index] = -1;
  else {
    status->snapshot_samples[index] = qa_int32(sample->ping);
    status->snapshot_flags[index] = qa_int32(sample->flags);
  }
  status->snapshot_count = qa_int32((double)status->snapshot_count + 1);
}
bool qa_q3_pres_client_draw_status_draw_disconnect(qa_q3_pres_client_draw_status *status) {
  if (!status->state->snap) qa_range_error("CG_DrawDisconnect requires a current snapshot");
  double number = qa_int32(status->host.commands.current_number(status->host.commands.ctx) - 64 + 1);
  qa_q3_base_user_command command;
  if (!status->host.commands.read(status->host.commands.ctx, number, &command))
    qa_range_error("CG_DrawDisconnect command fell outside CMD_BACKUP");
  if (command.server_time <= status->state->snap->player_state.command_time ||
      command.server_time > status->state->time) return true;
  const char *message = "Connection Interrupted";
  double width = imul32(qa_q3_pres_draw_strlen(message), 16);
  qa_q3_pres_client_draw_tools_draw_big_string(status->tools, trunc(320 - width / 2), 100, message, 1);
  if (((qa_int32(status->state->time) >> 9) & 1) != 0) return true;
  const qa_q3_pres_scene_shader *shader = NULL;
  if (!status->host.register_shader(status->host.ctx, "gfx/2d/net.tga", &shader)) return false;
  qa_rect rect = {640 - 48, 480 - 48, 48, 48};
  qa_q3_pres_client_draw_tools_draw_pic(status->tools, rect, shader);
  return true;
}
static void stretch(qa_q3_pres_client_draw_status *status, qa_rect rect, const qa_picture_asset *picture) {
  qa_text_texture_rect uv = {0, 0, 0, 0};
  qa_draw2d_stretch_pixels(&status->tools->draw, rect, uv, picture);
}
bool qa_q3_pres_client_draw_status_draw_lagometer(qa_q3_pres_client_draw_status *status) {
  qa_cvar_snapshot lagometer = status->host.read_vm_cvar(status->host.ctx, QA_Q3_PRES_STATUS_CVAR_LAGOMETER);
  double lagometer_value = lagometer.integer_value;
  qa_cvar_read_free(&lagometer);
  if (lagometer_value == 0 || status->static_state->local_server != 0)
    return qa_q3_pres_client_draw_status_draw_disconnect(status);
  double x = 640 - 48;
  double y = status->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? 480 - 144 : 480 - 48;
  qa_draw2d_set_color(&status->tools->draw, NULL);
  qa_rect rect = {x, y, 48, 48};
  qa_q3_pres_client_draw_tools_draw_pic(status->tools, rect, status->host.lagometer_shader);
  qa_rect adjusted = qa_q3_pres_client_draw_tools_adjust_from_640(status->tools, rect);
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_PICTURE_MATERIAL;
  picture.material = status->tools->media.picture(status->tools->media.ctx, status->tools->media.white_shader);
  int color = -1;
  double range = f32(adjusted.height / 3), mid = f32(adjusted.y + range), scale = f32(range / MAX_LAGOMETER_RANGE);
  for (double a = 0; a < adjusted.width; a++) {
    int index = (int)(status->frame_count - 1 - (int32_t)a) & (LAG_SAMPLES - 1);
    double value = f32(f32(status->frame_samples[index]) * scale);
    if (value > 0) {
      if (color != 1) { color = 1; qa_draw2d_set_color(&status->tools->draw, &YELLOW_C); }
      if (value > range) value = range;
      qa_rect cell = {f32(f32(adjusted.x + adjusted.width) - f32(a)), f32(mid - value), 1, value};
      stretch(status, cell, &picture);
    } else if (value < 0) {
      if (color != 2) { color = 2; qa_draw2d_set_color(&status->tools->draw, &BLUE_C); }
      value = f32(-value);
      if (value > range) value = range;
      qa_rect cell = {f32(f32(adjusted.x + adjusted.width) - f32(a)), mid, 1, value};
      stretch(status, cell, &picture);
    }
  }
  range = f32(adjusted.height / 2);
  scale = f32(range / MAX_LAGOMETER_PING);
  for (double a = 0; a < adjusted.width; a++) {
    int index = (int)(status->snapshot_count - 1 - (int32_t)a) & (LAG_SAMPLES - 1);
    double value = f32(status->snapshot_samples[index]);
    if (value > 0) {
      if ((status->snapshot_flags[index] & 1) != 0) {
        if (color != 5) { color = 5; qa_draw2d_set_color(&status->tools->draw, &YELLOW_C); }
      } else if (color != 3) { color = 3; qa_draw2d_set_color(&status->tools->draw, &GREEN_C); }
      value = f32(value * scale);
      if (value > range) value = range;
      qa_rect cell = {f32(f32(adjusted.x + adjusted.width) - f32(a)),
                      f32(f32(adjusted.y + adjusted.height) - value), 1, value};
      stretch(status, cell, &picture);
    } else if (value < 0) {
      if (color != 4) { color = 4; qa_draw2d_set_color(&status->tools->draw, &RED_C); }
      qa_rect cell = {f32(f32(adjusted.x + adjusted.width) - f32(a)),
                      f32(f32(adjusted.y + adjusted.height) - range), 1, range};
      stretch(status, cell, &picture);
    }
  }
  qa_draw2d_set_color(&status->tools->draw, NULL);
  qa_cvar_snapshot no_predict = status->host.read_vm_cvar(status->host.ctx, QA_Q3_PRES_STATUS_CVAR_NO_PREDICT);
  double no_predict_value = no_predict.integer_value;
  qa_cvar_read_free(&no_predict);
  qa_cvar_snapshot synchronous =
      status->host.read_vm_cvar(status->host.ctx, QA_Q3_PRES_STATUS_CVAR_SYNCHRONOUS_CLIENTS);
  double synchronous_value = synchronous.integer_value;
  qa_cvar_read_free(&synchronous);
  if (no_predict_value != 0 || synchronous_value != 0)
    qa_q3_pres_client_draw_tools_draw_big_string(status->tools, trunc(adjusted.x), trunc(adjusted.y), "snc", 1);
  return qa_q3_pres_client_draw_status_draw_disconnect(status);
}

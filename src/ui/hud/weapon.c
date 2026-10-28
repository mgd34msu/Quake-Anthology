/* Port of src/ui/hud/weapon.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/weapon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t cp_count(const char *text) {
  size_t count = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if ((*p & 0xc0) != 0x80) count++;
  return count;
}
static size_t cp_prefix_bytes(const char *text, size_t n) {
  size_t seen = 0, i = 0;
  while (text[i] != '\0' && seen < n) {
    i++;
    while ((text[i] & 0xc0) == 0x80) i++;
    seen++;
  }
  return i;
}
static char *dup_str(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
/* JS String(number) for the ammo count (integers print plainly). */
static void js_number(char *buf, size_t size, double value) {
  if (value == floor(value) && fabs(value) < 1e21)
    snprintf(buf, size, "%.0f", value);
  else
    snprintf(buf, size, "%.17g", value);
}

typedef struct builder {
  qa_ui_weapon_commands *out;
  size_t capacity;
} builder;

static bool push(builder *b, qa_ui_draw_command cmd) {
  if (b->out->count == b->capacity) {
    size_t cap = b->capacity == 0 ? 8 : b->capacity * 2;
    qa_ui_draw_command *items =
        realloc(b->out->commands, cap * sizeof *items);
    if (items == NULL) return false;
    b->out->commands = items;
    b->capacity = cap;
  }
  b->out->commands[b->out->count++] = cmd;
  return true;
}
static char *own(builder *b, char *text) {
  if (text == NULL) return NULL;
  if (b->out->owned_count == b->out->owned_capacity) {
    size_t cap = b->out->owned_capacity == 0 ? 8 : b->out->owned_capacity * 2;
    char **items = realloc(b->out->owned, cap * sizeof *items);
    if (items == NULL) { free(text); return NULL; }
    b->out->owned = items;
    b->out->owned_capacity = cap;
  }
  b->out->owned[b->out->owned_count++] = text;
  return text;
}
static qa_ui_draw_command base_cmd(qa_ui_draw_command_kind kind) {
  qa_ui_draw_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.kind = kind;
  return cmd;
}
static bool push_fill(builder *b, qa_rect rect, qa_vec4 color) {
  qa_ui_draw_command cmd = base_cmd(QA_UI_DRAW_FILL);
  cmd.rect = rect;
  cmd.color = color;
  return push(b, cmd);
}
static bool push_image(builder *b, qa_resource_id resource, qa_rect rect) {
  qa_ui_draw_command cmd = base_cmd(QA_UI_DRAW_IMAGE);
  cmd.resource = resource;
  cmd.rect = rect;
  cmd.tex_coords[0].x = 0; cmd.tex_coords[0].y = 0;
  cmd.tex_coords[1].x = 1; cmd.tex_coords[1].y = 1;
  cmd.color.x = 1; cmd.color.y = 1; cmd.color.z = 1; cmd.color.w = 1;
  return push(b, cmd);
}
static bool push_text(builder *b, double x, double y, const char *text,
                      qa_resource_id font, double scale, qa_vec4 color) {
  char *owned = own(b, dup_str(text));
  if (owned == NULL) return false;
  qa_ui_draw_command cmd = base_cmd(QA_UI_DRAW_TEXT);
  cmd.origin.x = x; cmd.origin.y = y;
  cmd.text = owned;
  cmd.font = font;
  cmd.scale = scale;
  cmd.color = color;
  cmd.align = "left";
  cmd.shadow = true;
  return push(b, cmd);
}

void qa_ui_weapon_commands_free(qa_ui_weapon_commands *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->owned_count; i++) free(result->owned[i]);
  free(result->owned);
  free(result->commands);
  memset(result, 0, sizeof *result);
}

qa_ui_hud_status_rows qa_ui_hud_status_rows_for(double text_scale,
                                                double cap_height) {
  double label_top = fmax(25, 4 + cap_height * text_scale * 1.5 + 4);
  qa_ui_hud_status_rows out = {
    label_top, fmax(42, ceil(label_top + cap_height * text_scale * 0.9 + 4))
  };
  return out;
}

static double measure(const qa_ui_common_weapon_hud *data, const char *text,
                      double scale) {
  if (data->measure_text != NULL)
    return data->measure_text(data->measure_ctx, text, scale);
  return (double)cp_count(text) * 8 * scale;
}
/* TS fallback in the label block measures with Array.from length. */
static double measure_at_1(const qa_ui_common_weapon_hud *data,
                           const char *text) {
  if (data->measure_text != NULL)
    return data->measure_text(data->measure_ctx, text, 1);
  return (double)cp_count(text) * 8;
}

bool qa_ui_draw_weapon_hud(const qa_ui_common_weapon_hud *data, qa_rect rect,
                           qa_ui_skin skin, double text_scale,
                           double minimum_text_scale,
                           qa_ui_weapon_commands *out) {
  memset(out, 0, sizeof *out);
  builder b = { out, 0 };
  bool finite = data->status.ammo_is_finite;
  bool unavailable = finite && !data->status.has_ammo_to_start;
  const char *provider = data->status.source.provider;
  const char *active_warning =
      (provider != NULL && strncmp(provider, "q3:", 3) == 0) ? NULL
      : unavailable ? "NO AMMO"
      : (finite && data->status.low) ? "LOW AMMO" : NULL;
  const char *warning = strcmp(data->warning, "empty") == 0 ? "OUT OF AMMO"
      : strcmp(data->warning, "low") == 0 ? "LOW AMMO WARNING"
      : active_warning;
  qa_vec4 color = (warning == NULL && !unavailable) ? skin.colors.text
                                                    : skin.colors.accent;
  double cap_top = skin.has_cap_ink ? skin.cap_ink_top : 0;
  double cap_height = skin.has_cap_ink ? skin.cap_ink_height : 8;
  qa_resource_id icon = data->weapon_icon != NULL ? data->weapon_icon
                                                  : data->ammo_icon;
  double icon_aspect = data->has_icon_aspect ? data->icon_aspect : 1;
  char count_buf[64];
  js_number(count_buf, sizeof count_buf, data->status.ammo_count);

  if (minimum_text_scale > 0 && rect.height == 32) {
    double scale = minimum_text_scale, top = cap_top * scale;
    if (!push_fill(&b, rect, skin.colors.panel)) goto oom;
    if (icon != NULL) {
      qa_rect r = { rect.x + 4, rect.y + 4, 24, 24 / icon_aspect };
      if (!push_image(&b, icon, r)) goto oom;
    }
    double left = rect.x + (icon == NULL ? 4 : 32);
    double available = rect.x + rect.width - 4 - left;
    const char *labels[2];
    labels[0] = finite ? count_buf
                       : (icon == NULL ? data->status.label : "");
    labels[1] = (warning != NULL && strcmp(warning, "LOW AMMO WARNING") == 0)
        ? "LOW AMMO" : (warning != NULL ? warning : "");
    for (int row = 0; row < 2; row++) {
      const char *label = labels[row];
      size_t keep = cp_count(label);
      char *truncated = NULL;
      for (;;) {
        size_t bytes = cp_prefix_bytes(label, keep);
        truncated = malloc(bytes + 1);
        if (truncated == NULL) goto oom;
        memcpy(truncated, label, bytes);
        truncated[bytes] = '\0';
        if (keep == 0 || measure(data, truncated, scale) <= available)
          break;
        free(truncated);
        truncated = NULL;
        keep--;
      }
      bool ok = true;
      if (keep > 0)
        ok = push_text(&b, left, rect.y + 4 + row * 14 - top, truncated,
                       skin.font, scale, color);
      free(truncated);
      if (!ok) goto oom;
    }
    return true;
  }

  {
    qa_ui_hud_status_rows rows =
        qa_ui_hud_status_rows_for(text_scale, cap_height);
    if (!push_fill(&b, rect, skin.colors.panel)) goto oom;
    double number_width = finite
        ? measure(data, count_buf, text_scale * 1.5) : 0;
    bool secondary_icon = data->ammo_icon != NULL
        && data->weapon_icon != NULL && finite;
    bool stacked_icon = icon != NULL
        && number_width > rect.width - (secondary_icon ? 84 : 66);
    double max_width = warning == NULL ? 48 : 30;
    double max_height = warning == NULL ? 32 : 20;
    double width = fmin(stacked_icon ? 24 : max_width,
                        (stacked_icon
                             ? fmin(24, rect.height - rows.label_top - 4)
                             : max_height)
                            * icon_aspect);
    double height = width / icon_aspect;
    if (icon != NULL) {
      qa_rect r = { rect.x + 4 + ((stacked_icon ? 24 : 48) - width) / 2,
                    rect.y + (stacked_icon ? rows.label_top : 5),
                    width, height };
      if (!push_image(&b, icon, r)) goto oom;
    }
    double x = rect.x + (icon == NULL || stacked_icon ? 8 : 58);
    if (finite) {
      double available = rect.x + rect.width
          - (secondary_icon && !stacked_icon ? 26 : 8) - x;
      double scale = fmin(text_scale * 1.5,
                          available
                              / fmax(1, measure(data, count_buf, 1)));
      if (!push_text(&b, x, rect.y + 4 - cap_top * scale, count_buf,
                     skin.font, scale, color))
        goto oom;
    }
    if (warning != NULL || icon == NULL) {
      const char *original = warning != NULL ? warning : data->status.label;
      double left = stacked_icon ? rect.x + 32
                                 : (warning == NULL ? x : rect.x + 4);
      double available = rect.x + rect.width
          - (stacked_icon && secondary_icon ? 26 : 4) - left;
      const char *text =
          (warning != NULL && strcmp(warning, "LOW AMMO WARNING") == 0
           && measure_at_1(data, original) * minimum_text_scale > available)
              ? "LOW AMMO" : original;
      double text_width = measure_at_1(data, text);
      double scale = fmax(minimum_text_scale,
                          fmin(text_scale * 0.9,
                               available / fmax(1, text_width)));
      /* Word wrap on single spaces (TS text.split(" ")). */
      char *copy = dup_str(text);
      if (copy == NULL) goto oom;
      char **lines = NULL;
      size_t line_count = 0, line_cap = 0;
      char *line = dup_str("");
      bool ok = line != NULL;
      char *p = copy;
      while (ok) {
        char *space = strchr(p, ' ');
        const char *word = p;
        if (space != NULL) *space = '\0';
        char *next;
        if (line[0] == '\0') {
          next = dup_str(word);
        } else {
          next = malloc(strlen(line) + 1 + strlen(word) + 1);
          if (next != NULL) {
            strcpy(next, line);
            strcat(next, " ");
            strcat(next, word);
          }
        }
        if (next == NULL) { ok = false; break; }
        if (line[0] != '\0'
            && measure_at_1(data, next) * scale > available) {
          if (line_count == line_cap) {
            size_t cap = line_cap == 0 ? 4 : line_cap * 2;
            char **grown = realloc(lines, cap * sizeof *grown);
            if (grown == NULL) { free(next); ok = false; break; }
            lines = grown;
            line_cap = cap;
          }
          lines[line_count++] = line;
          line = dup_str(word);
          free(next);
          if (line == NULL) { ok = false; break; }
        } else {
          free(line);
          line = next;
        }
        if (space == NULL) break;
        p = space + 1;
      }
      if (ok && line[0] != '\0') {
        if (line_count == line_cap) {
          size_t cap = line_cap == 0 ? 4 : line_cap * 2;
          char **grown = realloc(lines, cap * sizeof *grown);
          if (grown == NULL) ok = false;
          else { lines = grown; line_cap = cap; }
        }
        if (ok) { lines[line_count++] = line; line = NULL; }
      }
      double max_lines_d = fmax(1, floor((rect.height - rows.label_top - 4)
                                         / (cap_height * scale)));
      size_t maximum_lines = (size_t)max_lines_d;
      size_t shown = line_count < maximum_lines ? line_count : maximum_lines;
      for (size_t i = 0; ok && i < shown; i++) {
        const char *visible = lines[i];
        char *ellipsized = NULL;
        bool truncated = (i == maximum_lines - 1 && line_count > maximum_lines)
            || measure_at_1(data, visible) * scale > available;
        if (truncated) {
          size_t keep = cp_count(visible);
          for (;;) {
            size_t bytes = cp_prefix_bytes(visible, keep);
            ellipsized = malloc(bytes + 4);
            if (ellipsized == NULL) { ok = false; break; }
            memcpy(ellipsized, visible, bytes);
            memcpy(ellipsized + bytes, "\xe2\x80\xa6", 4);
            if (keep == 0
                || measure_at_1(data, ellipsized) * scale <= available)
              break;
            free(ellipsized);
            ellipsized = NULL;
            keep--;
          }
          if (ok) visible = ellipsized;
        }
        if (ok)
          ok = push_text(&b, left,
                         rect.y + rows.label_top + (double)i * 8 * scale
                             - cap_top * scale,
                         visible, skin.font, scale, color);
        free(ellipsized);
      }
      for (size_t i = 0; i < line_count; i++) free(lines[i]);
      free(lines);
      free(line);
      free(copy);
      if (!ok) goto oom;
    }
    double ammo_aspect = data->has_ammo_aspect ? data->ammo_aspect : 1;
    double ammo_width = fmin(18, 18 * ammo_aspect);
    if (secondary_icon) {
      qa_rect r = { rect.x + rect.width - 22,
                    rect.y + (stacked_icon ? rows.label_top : 4),
                    ammo_width, ammo_width / ammo_aspect };
      if (!push_image(&b, data->ammo_icon, r)) goto oom;
    }
    return true;
  }
oom:
  qa_ui_weapon_commands_free(out);
  return false;
}

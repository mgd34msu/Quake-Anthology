/* Port of src/ui/hud/q2-rerelease-layout.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/q2_rerelease_layout.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"

enum {
  STAT_HEALTH_BARS = 52,
  STAT_ACTIVE_WEAPON = 53,
  CONFIG_HEALTH_BAR_NAME = 12104,
  CONFIG_STORY = 12105,
  CS_WHEEL_WEAPONS = 12350
};

static char *xstrdup(const char *text) {
  char *copy = qa_input_dup(text != NULL ? text : "");
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static char *xdup_range(const char *text, size_t length) {
  char *copy = qa_input_dup_range(text, length);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static char *js_number(double value) {
  char *text = qa_input_js_number(value);
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(value);
  double wrapped = fmod(fabs(truncated), 4294967296.0);
  if (truncated < 0) wrapped = -wrapped;
  if (wrapped >= 2147483648.0) wrapped -= 4294967296.0;
  if (wrapped < -2147483648.0) wrapped += 4294967296.0;
  return (int32_t)wrapped;
}
static uint32_t to_uint32(double value) {
  if (!isfinite(value)) return 0;
  double wrapped = fmod(trunc(value), 4294967296.0);
  if (wrapped < 0) wrapped += 4294967296.0;
  return (uint32_t)wrapped;
}
static char *pad_start_zero(const char *text, size_t target) {
  size_t length = strlen(text);
  if (length >= target) return xstrdup(text);
  char *out = malloc(target + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memset(out, '0', target - length);
  memcpy(out + (target - length), text, length + 1);
  return out;
}
static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
static bool ends_with(const char *text, const char *suffix) {
  size_t length = strlen(text), suffix_length = strlen(suffix);
  return length >= suffix_length &&
         strcmp(text + length - suffix_length, suffix) == 0;
}

/* TS cell(): encode, cut at 23 bytes, decode (lossy). */
static char *cell_text(const char *value) {
  size_t length = strlen(value);
  if (length <= 23) return xstrdup(value);
  length = 23;
  /* Drop a trailing incomplete UTF-8 sequence (see header note). */
  while (length > 0 && ((((unsigned char)value[length - 1]) & 0xC0) == 0x80))
    length--;
  if (length > 0) {
    unsigned char lead = (unsigned char)value[length - 1];
    if (lead >= 0xC2) length--;
  }
  return xdup_range(value, length);
}

void qa_native_q2_hud_table_init(qa_native_q2_hud_table *table) {
  memset(table, 0, sizeof *table);
}
static void table_clear_rows(qa_native_q2_hud_table *table) {
  for (size_t r = 0; r < table->row_count; r++) {
    for (size_t c = 0; c < table->row_cell_counts[r]; c++)
      free(table->rows[r][c]);
    free(table->rows[r]);
  }
  table->row_count = 0;
}
void qa_native_q2_hud_table_destroy(qa_native_q2_hud_table *table) {
  if (table == NULL) return;
  table_clear_rows(table);
  free(table->rows);
  free(table->row_cell_counts);
  free(table->columns);
  memset(table, 0, sizeof *table);
}
static void table_push_row(qa_native_q2_hud_table *table, char **cells,
                           size_t cell_count) {
  if (table->row_count == table->row_capacity) {
    size_t capacity = table->row_capacity != 0 ? table->row_capacity * 2 : 8;
    char ***rows = realloc(table->rows, capacity * sizeof *rows);
    size_t *counts =
        realloc(table->row_cell_counts, capacity * sizeof *counts);
    if (rows == NULL || counts == NULL) qa_range_error("Out of memory");
    table->rows = rows;
    table->row_cell_counts = counts;
    table->row_capacity = capacity;
  }
  table->rows[table->row_count] = cells;
  table->row_cell_counts[table->row_count] = cell_count;
  table->row_count++;
}
static void table_set_columns(qa_native_q2_hud_table *table,
                              const double *values, size_t count) {
  if (count > table->column_capacity) {
    double *grown = realloc(table->columns, count * sizeof *grown);
    if (grown == NULL && count != 0) qa_range_error("Out of memory");
    table->columns = grown;
    table->column_capacity = count;
  }
  if (count != 0) memcpy(table->columns, values, count * sizeof *values);
  table->column_count = count;
}

static char *client_name(const qa_native_q2_hud_frame *frame, double slot) {
  if (slot < 0 || slot >= 256)
    qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD client is outside clientinfo");
  qa_ui_hud_q2_app_layout config =
      qa_ui_hud_q2_application_layout_for(frame->protocol);
  const char *info =
      qa_native_q2_config_string_at(frame, config.player_skins + slot);
  const char *slash = strchr(info, '\\');
  return slash == NULL ? xstrdup(info)
                       : xdup_range(info, (size_t)(slash - info));
}

/* TS localized(): environment.localize, then ##P<n> client names. */
static char *localized_text(const qa_native_q2_hud_frame *frame,
                            qa_native_q2_hud_environment *env,
                            const char *text, const char *const *args,
                            size_t arg_count) {
  const char *base = env->localize(env->ctx, text, args, arg_count);
  if (base == NULL) base = "";
  size_t capacity = strlen(base) + 32, length = 0;
  char *out = malloc(capacity);
  if (out == NULL) qa_range_error("Out of memory");
  const char *cursor = base;
  while (*cursor != '\0') {
    if (cursor[0] == '#' && cursor[1] == '#' && cursor[2] == 'P' &&
        cursor[3] >= '0' && cursor[3] <= '9') {
      const char *digits = cursor + 3;
      /* /##P([0-9]+)/ then Number(slot): strtod also read "12.5" and "1e3". */
      const char *end = digits;
      while (*end >= '0' && *end <= '9') end++;
      double slot = qa_js_parse_int(digits, 10);
      char *name = client_name(frame, slot);
      size_t name_length = strlen(name);
      if (length + name_length + 1 > capacity) {
        capacity = (length + name_length + 1) * 2;
        out = realloc(out, capacity);
        if (out == NULL) qa_range_error("Out of memory");
      }
      memcpy(out + length, name, name_length);
      length += name_length;
      free(name);
      cursor = end;
    } else {
      if (length + 2 > capacity) {
        capacity *= 2;
        out = realloc(out, capacity);
        if (out == NULL) qa_range_error("Out of memory");
      }
      out[length++] = *cursor++;
    }
  }
  out[length] = '\0';
  return out;
}

typedef struct rr_state {
  const qa_native_q2_hud_frame *frame;
  qa_native_q2_hud_environment *env;
  const qa_native_q2_hud_arsenal *arsenal;
  qa_ui_hud_q2_app_layout config;
  qa_native_q2_hud_operations *out;
  qa_native_q2_hud_table *table;
  double width, height, x, y;
  bool ok;
} rr_state;

static void rr_push(rr_state *s, qa_native_q2_hud_operation op) {
  if (!qa_native_q2_hud_ops_push(s->out, op)) s->ok = false;
}
static double rr_stat(rr_state *s, double index) {
  return qa_native_q2_hud_stat(s->frame, index, s->arsenal);
}
static double rr_font_offset(rr_state *s) {
  return (s->env->font_line_height - 8) / 2;
}
static double rr_measure(rr_state *s, const char *value, bool force_font) {
  if (s->env->use_font || force_font)
    return s->env->measure(s->env->ctx, value).x;
  return (double)strlen(value) * 8;
}
static void rr_text(rr_state *s, const char *value, bool alternate, double x,
                    double y, bool force_font) {
  bool font = s->env->use_font || force_font;
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = font ? QA_Q2_HUD_OP_FONT_TEXT : QA_Q2_HUD_OP_TEXT;
  op.x = x;
  op.y = y - (font ? rr_font_offset(s) : 0);
  op.text = qa_native_q2_hud_ops_dup(s->out, value);
  op.alternate = alternate;
  if (!font) {
    op.has_shadow = true;
    op.shadow = true;
    op.has_xor = true;
    op.xor_ = true;
  }
  rr_push(s, op);
}
static void rr_centered(rr_state *s, const char *value, bool alternate,
                        double x, double y) {
  const char *cursor = value;
  for (;;) {
    const char *newline = strchr(cursor, '\n');
    size_t length = newline != NULL ? (size_t)(newline - cursor)
                                    : strlen(cursor);
    char *line = xdup_range(cursor, length);
    rr_text(s, line, alternate, x + (320 - rr_measure(s, line, false)) / 2, y,
            false);
    free(line);
    y += s->env->use_font ? 10 : 8;
    if (newline == NULL) break;
    cursor = newline + 1;
  }
}
static void rr_picture(rr_state *s, const char *name, double x, double y) {
  if (name[0] == '\0') return;
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_PICTURE;
  op.x = x;
  op.y = y;
  op.name = qa_native_q2_hud_ops_dup(s->out, name);
  rr_push(s, op);
}
static void rr_field(rr_state *s, double value, double digits, bool alternate) {
  double count = digits < 5 ? digits : 5;
  if (count < 1) return;
  char *value_text = js_number(trunc(value));
  size_t text_length = strlen(value_text);
  size_t length = text_length < (size_t)count ? text_length : (size_t)count;
  double atx = s->x + 2 + 16 * (count - (double)length);
  for (size_t i = 0; i < length; i++) {
    char name[32];
    if (value_text[i] == '-')
      snprintf(name, sizeof name, "%sminus", alternate ? "anum_" : "num_");
    else
      snprintf(name, sizeof name, "%s%c", alternate ? "anum_" : "num_",
               value_text[i]);
    rr_picture(s, name, atx, s->y);
    atx += 16;
  }
  free(value_text);
}
static const char *rr_config_stat(rr_state *s, double index) {
  double value = rr_stat(s, index);
  if (value < 0 || value >= s->config.max_config_strings)
    qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD stat string is outside configstrings");
  return qa_native_q2_config_string_at(s->frame, value);
}
static int32_t rr_count(int32_t value, int32_t maximum) {
  if (value < 0 || value > maximum)
    qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD argument count exceeds source limits");
  return value;
}
static void rr_fill(rr_state *s, double x, double y, double width,
                    double height, qa_vec4 color) {
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_FILL;
  op.x = x;
  op.y = y;
  op.width = width;
  op.height = height;
  op.color = color;
  rr_push(s, op);
}
static void rr_sized(rr_state *s, double x, double y, double width,
                     double height, const char *name) {
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_SIZED_PICTURE;
  op.x = x;
  op.y = y;
  op.width = width;
  op.height = height;
  op.name = qa_native_q2_hud_ops_dup(s->out, name);
  rr_push(s, op);
}
static void rr_arsenal(rr_state *s, qa_resource_id resource, double aspect) {
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_ARSENAL_PICTURE;
  op.x = s->x;
  op.y = s->y;
  op.resource = resource;
  op.aspect = aspect;
  rr_push(s, op);
}

/* idx-th '|'-separated field of text (malloc'd), or NULL. */
static char *split_field(const char *text, char separator, size_t wanted) {
  const char *cursor = text;
  for (size_t i = 0;; i++) {
    const char *next = strchr(cursor, separator);
    if (i == wanted)
      return next == NULL ? xstrdup(cursor)
                          : xdup_range(cursor, (size_t)(next - cursor));
    if (next == NULL) return NULL;
    cursor = next + 1;
  }
}

typedef struct rr_layout_unwind {
  qa_tokenizer *parser;
  qa_native_q2_hud_table *scratch; /* NULL when the environment's table is used */
  bool **conditions;
  char **command;
} rr_layout_unwind;
static void rr_layout_unwind_run(void *ctx) {
  rr_layout_unwind *unwind = ctx;
  qa_tokenizer_destroy(unwind->parser);
  if (unwind->scratch != NULL) qa_native_q2_hud_table_destroy(unwind->scratch);
  free(*unwind->conditions);
  *unwind->conditions = NULL;
  free(*unwind->command);
  *unwind->command = NULL;
}

bool qa_q2_rerelease_layout_into(const char *source,
                                 const qa_native_q2_hud_frame *frame,
                                 double width, double height,
                                 qa_native_q2_hud_environment *environment,
                                 const qa_native_q2_hud_arsenal *arsenal,
                                 qa_native_q2_hud_operations *out) {
  rr_state s;
  memset(&s, 0, sizeof s);
  s.frame = frame;
  s.env = environment;
  s.arsenal = arsenal;
  s.config = qa_ui_hud_q2_application_layout_for(frame->protocol);
  s.out = out;
  s.width = width;
  s.height = height;
  s.ok = true;
  qa_native_q2_hud_table scratch;
  bool scratch_used = environment->table == NULL;
  if (scratch_used) {
    qa_native_q2_hud_table_init(&scratch);
    s.table = &scratch;
  } else {
    s.table = environment->table;
  }
  qa_tokenizer parser;
  memset(&parser, 0, sizeof parser);
  if (!qa_tokenizer_init(&parser, source, strlen(source),
                         "Q2 rerelease HUD layout")) {
    if (scratch_used) qa_native_q2_hud_table_destroy(&scratch);
    return false;
  }
  bool *conditions = NULL;
  size_t condition_count = 0, condition_capacity = 0;
  bool flash = fmod(frame->time_milliseconds, 1000) < 500;
  qa_text_token token;
  char *command = NULL;
  /* A layout or parse error is caught by the HUD (NativeQ2HudLayoutError); this
   * frame's storage is released when it unwinds. */
  rr_layout_unwind unwind = { &parser, scratch_used ? &scratch : NULL, &conditions, &command };
  qa_cleanup unwind_cleanup;
  qa_cleanup_push(&unwind_cleanup, rr_layout_unwind_run, &unwind);
  while (qa_tokenizer_next(&parser, true, &token)) {
    command = token.value;
    bool draw = condition_count == 0 ? true : conditions[condition_count - 1];
/* Local token helpers as macros to keep the case order readable. */
#define NEXT_STR()                                                        \
  (qa_tokenizer_next(&parser, true, &token) ? token.value : xstrdup(""))
#define NEXT_INT()                                                        \
  (qa_tokenizer_next(&parser, true, &token)                              \
       ? qa_game_atoi(token.value)                                        \
       : qa_game_atoi(""))
    if (strcmp(command, "if") == 0) {
      int32_t index = 0;
      {
        char *text = NEXT_STR();
        index = qa_game_atoi(text);
        free(text);
      }
      bool value = draw && rr_stat(&s, index) != 0;
      if (condition_count == condition_capacity) {
        condition_capacity =
            condition_capacity != 0 ? condition_capacity * 2 : 8;
        conditions = realloc(conditions, condition_capacity * sizeof *conditions);
        if (conditions == NULL) qa_range_error("Out of memory");
      }
      conditions[condition_count++] = value;
    } else if (strcmp(command, "ifgef") == 0) {
      char *text = NEXT_STR();
      int32_t value = qa_game_atoi(text);
      free(text);
      if (condition_count == condition_capacity) {
        condition_capacity =
            condition_capacity != 0 ? condition_capacity * 2 : 8;
        conditions = realloc(conditions, condition_capacity * sizeof *conditions);
        if (conditions == NULL) qa_range_error("Out of memory");
      }
      conditions[condition_count++] = draw && frame->server_frame >= value;
    } else if (strcmp(command, "endif") == 0) {
      if (condition_count == 0)
        qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD endif without matching if");
      condition_count--;
    } else if (strcmp(command, "xl") == 0 || strcmp(command, "xr") == 0 ||
               strcmp(command, "xv") == 0) {
      char *text = NEXT_STR();
      int32_t value = qa_game_atoi(text);
      free(text);
      if (draw)
        s.x = value + (strcmp(command, "xr") == 0
                           ? width
                           : strcmp(command, "xv") == 0 ? trunc(width / 2) - 160
                                                         : 0);
    } else if (strcmp(command, "yt") == 0 || strcmp(command, "yb") == 0 ||
               strcmp(command, "yv") == 0) {
      char *text = NEXT_STR();
      int32_t value = qa_game_atoi(text);
      free(text);
      if (draw)
        s.y = value + (strcmp(command, "yb") == 0
                           ? height
                           : strcmp(command, "yv") == 0
                                 ? trunc(height / 2) - 120
                                 : 0);
    } else if (strcmp(command, "pic") == 0) {
      char *text = NEXT_STR();
      int32_t index = qa_game_atoi(text);
      free(text);
      if (draw) {
        if (index == 2 && arsenal != NULL && arsenal->has_ammunition) {
          if (arsenal->ammo_has_count && arsenal->ammo_has_icon)
            rr_arsenal(&s, arsenal->ammo_icon_resource,
                       arsenal->ammo_icon_aspect);
        } else if (index == 6 && arsenal != NULL &&
                   arsenal->has_selected_item) {
          if (rr_stat(&s, index) != 0 && arsenal->selected_has_icon)
            rr_arsenal(&s, arsenal->selected_icon_resource,
                       arsenal->selected_icon_aspect);
        } else {
          double value = rr_stat(&s, index);
          if (value < 0 || value >= s.config.max_images)
            qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD image is outside configstrings");
          rr_picture(&s,
                     qa_native_q2_config_string_at(frame, s.config.images + value),
                     s.x, s.y);
        }
      }
    } else if (strcmp(command, "picn") == 0) {
      char *value = NEXT_STR();
      if (draw) rr_picture(&s, value, s.x, s.y);
      free(value);
    } else if (strcmp(command, "num") == 0) {
      char *a = NEXT_STR();
      int32_t digits = qa_game_atoi(a);
      free(a);
      char *b = NEXT_STR();
      int32_t index = qa_game_atoi(b);
      free(b);
      if (draw) rr_field(&s, rr_stat(&s, index), digits, false);
    } else if (strcmp(command, "lives_num") == 0) {
      char *a = NEXT_STR();
      int32_t index = qa_game_atoi(a);
      free(a);
      if (draw) {
        double value = rr_stat(&s, index);
        double shown = value - 2 > 0 ? value - 2 : 0;
        rr_field(&s, shown, 1, value <= 2 && flash);
      }
    } else if (strcmp(command, "hnum") == 0 || strcmp(command, "anum") == 0 ||
               strcmp(command, "rnum") == 0) {
      if (draw) {
        double index = strcmp(command, "hnum") == 0 ? 1
                       : strcmp(command, "anum") == 0 ? 3
                                                      : 5;
        double value = rr_stat(&s, index);
        if (!(index != 1 && value < 0)) {
          double weapon = rr_stat(&s, STAT_ACTIVE_WEAPON);
          char *field = split_field(
              qa_native_q2_config_string_at(frame, CS_WHEEL_WEAPONS + weapon),
              '|', 6);
          int32_t warning_raw = qa_game_atoi(field != NULL ? field : "0");
          free(field);
          double warning = warning_raw != 0 ? warning_raw : 5;
          bool alternate =
              index == 1 ? (value <= 0 || (value <= 25 && flash))
                         : (index == 3 && value <= warning && flash);
          int32_t bit = index == 1 ? 1 : index == 3 ? 4 : 2;
          if ((to_int32(rr_stat(&s, 15)) & bit) != 0)
            rr_picture(&s, "field_3", s.x, s.y);
          rr_field(&s, value, 3, alternate);
        }
      }
    } else if (strcmp(command, "stat_string") == 0 ||
               strcmp(command, "loc_stat_string") == 0 ||
               strcmp(command, "loc_stat_rstring") == 0 ||
               strcmp(command, "loc_stat_cstring") == 0 ||
               strcmp(command, "loc_stat_cstring2") == 0) {
      char *a = NEXT_STR();
      int32_t index = qa_game_atoi(a);
      free(a);
      if (draw) {
        bool selected = index == 51 && rr_stat(&s, index) != 0 &&
                        arsenal != NULL && arsenal->has_selected_item;
        const char *raw = selected ? arsenal->selected_label
                                   : rr_config_stat(&s, index);
        char *owned_value = NULL;
        const char *value;
        if (strcmp(command, "stat_string") == 0) {
          value = raw;
        } else if (selected) {
          value = arsenal->selected_localized_label;
        } else {
          owned_value = localized_text(frame, environment, raw, NULL, 0);
          value = owned_value;
        }
        if (starts_with(command, "loc_stat_cstring")) {
          rr_centered(&s, value, ends_with(command, "2"), s.x, s.y);
        } else {
          rr_text(&s, value, false,
                  s.x - (strcmp(command, "loc_stat_rstring") == 0
                             ? rr_measure(&s, value, false)
                             : 0),
                  s.y, false);
        }
        free(owned_value);
      }
    } else if (strcmp(command, "string") == 0 ||
               strcmp(command, "string2") == 0 ||
               strcmp(command, "cstring") == 0 ||
               strcmp(command, "cstring2") == 0) {
      char *value = NEXT_STR();
      if (draw) {
        if (starts_with(command, "cstring"))
          rr_centered(&s, value, ends_with(command, "2"), s.x, s.y);
        else
          rr_text(&s, value, ends_with(command, "2"), s.x, s.y, false);
      }
      free(value);
    } else if (strcmp(command, "loc_string") == 0 ||
               strcmp(command, "loc_string2") == 0 ||
               strcmp(command, "loc_rstring") == 0 ||
               strcmp(command, "loc_rstring2") == 0 ||
               strcmp(command, "loc_cstring") == 0 ||
               strcmp(command, "loc_cstring2") == 0) {
      char *a = NEXT_STR();
      int32_t size = rr_count(qa_game_atoi(a), 7);
      free(a);
      char *base = NEXT_STR();
      char **args = NULL;
      if (size > 0) {
        args = malloc((size_t)size * sizeof *args);
        if (args == NULL) qa_range_error("Out of memory");
        for (int32_t i = 0; i < size; i++) args[i] = NEXT_STR();
      }
      if (draw) {
        char *value = localized_text(frame, environment, base,
                                     (const char *const *)args, (size_t)size);
        bool alternate = ends_with(command, "2");
        if (starts_with(command, "loc_cstring"))
          rr_centered(&s, value, alternate, s.x, s.y);
        else
          rr_text(&s, value, alternate,
                  s.x - (starts_with(command, "loc_rstring")
                             ? rr_measure(&s, value, false)
                             : 0),
                  s.y, false);
        free(value);
      }
      for (int32_t i = 0; i < size; i++) free(args[i]);
      free(args);
      free(base);
    } else if (strcmp(command, "client") == 0) {
      int32_t values[5];
      for (size_t i = 0; i < 5; i++) {
        char *a = NEXT_STR();
        values[i] = qa_game_atoi(a);
        free(a);
      }
      if (draw) {
        s.x = trunc(width / 2) - 160 + values[0] + 8;
        s.y = trunc(height / 2) - 120 + values[1] + 7;
        char *name = client_name(frame, values[2]);
        rr_text(&s, name, false, s.x + 32, s.y, false);
        free(name);
        char *score = js_number(values[3]);
        rr_text(&s, score, !environment->use_font, s.x + 32, s.y + 10, false);
        free(score);
        rr_sized(&s, s.x + 96, s.y + 10, 9, 9, "ping");
        char *ping = js_number(values[4]);
        rr_text(&s, ping, false, s.x + (environment->use_font ? 107 : 105),
                s.y + 10, false);
        free(ping);
      }
    } else if (strcmp(command, "ctf") == 0) {
      int32_t values[5];
      for (size_t i = 0; i < 5; i++) {
        char *a = NEXT_STR();
        values[i] = qa_game_atoi(a);
        free(a);
      }
      if (values[4] > 999) values[4] = 999;
      char *icon = NEXT_STR();
      if (draw) {
        s.x = trunc(width / 2) - 160 + values[0];
        s.y = trunc(height / 2) - 120 + values[1];
        bool mine = (double)values[2] == frame->player_number;
        char *score = js_number(values[3]);
        rr_text(&s, score, mine, s.x, s.y, true);
        free(score);
        s.x += 27;
        char *ping = js_number(values[4]);
        rr_text(&s, ping, mine, s.x, s.y, true);
        free(ping);
        s.x += 27;
        char *name = client_name(frame, values[2]);
        rr_text(&s, name, mine, s.x, s.y, true);
        free(name);
        if (icon[0] != '\0') {
          qa_native_q2_hud_operation op;
          memset(&op, 0, sizeof op);
          op.kind = QA_Q2_HUD_OP_PICTURE;
          op.x = s.x;
          op.y = s.y;
          op.name = qa_native_q2_hud_ops_dup(out, icon);
          op.has_anchor_before = true;
          rr_push(&s, op);
        }
      }
      free(icon);
    } else if (strcmp(command, "time_limit") == 0) {
      char *a = NEXT_STR();
      int32_t end = qa_game_atoi(a);
      free(a);
      if (draw && !(end < frame->server_frame)) {
        double frame_ms = frame->has_frame_time_milliseconds
                              ? frame->frame_time_milliseconds
                              : 25;
        double seconds = trunc(((double)end - frame->server_frame) * frame_ms / 1000);
        char *minutes_raw = js_number(trunc(seconds / 60));
        char *minutes = pad_start_zero(minutes_raw, 2);
        free(minutes_raw);
        char *seconds_raw = js_number(fmod(seconds, 60));
        char *seconds_text = pad_start_zero(seconds_raw, 2);
        free(seconds_raw);
        size_t total = strlen(minutes) + strlen(seconds_text) + 2;
        char *arg = malloc(total);
        if (arg == NULL) qa_range_error("Out of memory");
        snprintf(arg, total, "%s:%s", minutes, seconds_text);
        free(minutes);
        free(seconds_text);
        const char *args[1] = { arg };
        char *value = localized_text(frame, environment, "$g_score_time",
                                     args, 1);
        rr_text(&s, value, true, s.x - rr_measure(&s, value, false), s.y,
                false);
        free(value);
        free(arg);
      }
    } else if (strcmp(command, "dogtag") == 0) {
      char *a = NEXT_STR();
      int32_t slot = qa_game_atoi(a);
      free(a);
      if (draw) {
        char *name = client_name(frame, slot);
        free(name);
        char *tag = split_field(
            qa_native_q2_config_string_at(frame, s.config.player_skins + slot),
            '\\', 2);
        const char *tag_name =
            tag == NULL || tag[0] == '\0' ? "default" : tag;
        size_t total = strlen(tag_name) + 16;
        char *path = malloc(total);
        if (path == NULL) qa_range_error("Out of memory");
        snprintf(path, total, "/tags/%s.pcx", tag_name);
        rr_sized(&s, s.x, s.y, 198, 32, path);
        free(path);
        free(tag);
      }
    } else if (strcmp(command, "start_table") == 0) {
      char *a = NEXT_STR();
      int32_t size = rr_count(qa_game_atoi(a), 5);
      free(a);
      char **headers = NULL;
      if (size > 0) {
        headers = malloc((size_t)size * sizeof *headers);
        if (headers == NULL) qa_range_error("Out of memory");
        for (int32_t i = 0; i < size; i++) headers[i] = NEXT_STR();
      }
      if (draw) {
        char **cells = NULL;
        if (size > 0) {
          cells = malloc((size_t)size * sizeof *cells);
          if (cells == NULL) qa_range_error("Out of memory");
          for (int32_t i = 0; i < size; i++) {
            char *value =
                localized_text(frame, environment, headers[i], NULL, 0);
            cells[i] = cell_text(value);
            free(value);
          }
        }
        table_clear_rows(s.table);
        table_push_row(s.table, cells, (size_t)size);
        double *columns = NULL;
        if (size > 0) {
          columns = malloc((size_t)size * sizeof *columns);
          if (columns == NULL) qa_range_error("Out of memory");
          for (int32_t i = 0; i < size; i++)
            columns[i] = trunc(rr_measure(&s, cells[i], true));
        }
        table_set_columns(s.table, columns, (size_t)size);
        free(columns);
      }
      for (int32_t i = 0; i < size; i++) free(headers[i]);
      free(headers);
    } else if (strcmp(command, "table_row") == 0) {
      char *a = NEXT_STR();
      int32_t size = rr_count(qa_game_atoi(a), 6);
      free(a);
      char **values = NULL;
      if (size > 0) {
        values = malloc((size_t)size * sizeof *values);
        if (values == NULL) qa_range_error("Out of memory");
        for (int32_t i = 0; i < size; i++) values[i] = NEXT_STR();
      }
      if (draw) {
        if (s.table->row_count >= 11)
          qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD table exceeds source dimensions");
        size_t cell_count = (size_t)size > s.table->column_count
                                ? (size_t)size
                                : s.table->column_count;
        char **cells = NULL;
        if (cell_count > 0) {
          cells = malloc(cell_count * sizeof *cells);
          if (cells == NULL) qa_range_error("Out of memory");
          for (size_t i = 0; i < cell_count; i++)
            cells[i] = cell_text(i < (size_t)size ? values[i] : "");
        }
        table_push_row(s.table, cells, cell_count);
        for (size_t i = 0; i < s.table->column_count && i < cell_count; i++) {
          double measured = trunc(rr_measure(&s, cells[i], true));
          if (measured > s.table->columns[i]) s.table->columns[i] = measured;
        }
      }
      for (int32_t i = 0; i < size; i++) free(values[i]);
      free(values);
    } else if (strcmp(command, "draw_table") == 0) {
      if (draw) {
        double space = trunc(environment->measure(environment->ctx, " ").x);
        double table_width = 0;
        for (size_t i = 0; i < s.table->column_count; i++)
          table_width += s.table->columns[i];
        table_width += (s.table->column_count > 0
                            ? (double)(s.table->column_count - 1)
                            : 0) * space;
        double table_height =
            (double)s.table->row_count * (8 + rr_font_offset(&s));
        double left = s.x - trunc(table_width / 2), top = s.y + 8;
        const int corners[4] = { 18, 20, 24, 26 };
        const double corner_x[4] = { left - 8, left + table_width, left - 8,
                                     left + table_width };
        const double corner_y[4] = { top - 8, top - 8, top + table_height,
                                     top + table_height };
        for (size_t i = 0; i < 4; i++) {
          char glyph[2] = { (char)corners[i], '\0' };
          qa_native_q2_hud_operation op;
          memset(&op, 0, sizeof op);
          op.kind = QA_Q2_HUD_OP_TEXT;
          op.x = corner_x[i];
          op.y = corner_y[i];
          op.text = qa_native_q2_hud_ops_dup(out, glyph);
          rr_push(&s, op);
        }
        for (double px = left; px < left + table_width; px += 8) {
          const int codes[2] = { 19, 25 };
          const double ys[2] = { top - 8, top + table_height };
          for (size_t i = 0; i < 2; i++) {
            char glyph[2] = { (char)codes[i], '\0' };
            qa_native_q2_hud_operation op;
            memset(&op, 0, sizeof op);
            op.kind = QA_Q2_HUD_OP_TEXT;
            op.x = px;
            op.y = ys[i];
            op.text = qa_native_q2_hud_ops_dup(out, glyph);
            rr_push(&s, op);
          }
        }
        for (double py = top; py < top + table_height; py += 8) {
          const int codes[2] = { 21, 23 };
          const double xs[2] = { left - 8, left + table_width };
          for (size_t i = 0; i < 2; i++) {
            char glyph[2] = { (char)codes[i], '\0' };
            qa_native_q2_hud_operation op;
            memset(&op, 0, sizeof op);
            op.kind = QA_Q2_HUD_OP_TEXT;
            op.x = xs[i];
            op.y = py;
            op.text = qa_native_q2_hud_ops_dup(out, glyph);
            rr_push(&s, op);
          }
        }
        rr_fill(&s, left, top, table_width, table_height,
                (qa_vec4){ 0, 0, 0, 1 });
        double px = left;
        for (size_t c = 0; c < s.table->column_count; c++) {
          double column = s.table->columns[c];
          for (size_t r = 0; r < s.table->row_count; r++) {
            const char *value =
                c < s.table->row_cell_counts[r] ? s.table->rows[r][c] : "";
            double offset = r == 0
                                ? (column - rr_measure(&s, value, true)) / 2
                                : c == 0 ? 0
                                         : column - rr_measure(&s, value, true);
            rr_text(&s, value, r == 0, px + offset,
                    top + (double)r * (8 + rr_font_offset(&s)), true);
          }
          px += column + space;
        }
      }
    } else if (strcmp(command, "stat_pname") == 0) {
      char *a = NEXT_STR();
      int32_t index = qa_game_atoi(a);
      free(a);
      if (draw) {
        char *name = client_name(frame, rr_stat(&s, index) - 1);
        rr_text(&s, name, false, s.x, s.y, false);
        free(name);
      }
    } else if (strcmp(command, "health_bars") == 0) {
      if (draw) {
        char *label = localized_text(
            frame, environment,
            qa_native_q2_config_string_at(frame, CONFIG_HEALTH_BAR_NAME), NULL,
            0);
        rr_centered(&s, label, false, trunc(width / 2) - 160, s.y);
        free(label);
        s.y += environment->font_line_height;
        double value = rr_stat(&s, STAT_HEALTH_BARS);
        double bar_width = width * 0.5;
        double left = width * 0.5 - bar_width * 0.5;
        for (int index = 0; index < 2; index++) {
          uint32_t packed = (to_uint32(value) >> (index * 8)) & 255;
          if ((packed & 128) == 0) continue;
          double fraction = (double)(packed & 127) / 127;
          rr_fill(&s, left, s.y, bar_width + 1, 5, (qa_vec4){ 0, 0, 0, 1 });
          if (fraction > 0)
            rr_fill(&s, left, s.y, bar_width * fraction, 4,
                    (qa_vec4){ 1, 0, 0, 1 });
          if (fraction < 1)
            rr_fill(&s, left + bar_width * fraction, s.y,
                    bar_width * (1 - fraction), 4,
                    (qa_vec4){ 80.0 / 255, 80.0 / 255, 80.0 / 255, 1 });
          s.y += 12;
        }
      }
    } else if (strcmp(command, "story") == 0) {
      const char *raw = qa_native_q2_config_string_at(frame, CONFIG_STORY);
      if (raw[0] != '\0') {
        char *value = localized_text(frame, environment, raw, NULL, 0);
        qa_vec2 size = environment->measure(environment->ctx, value);
        const char *cursor = value;
        size_t line_index = 0;
        for (;;) {
          const char *newline = strchr(cursor, '\n');
          size_t length = newline != NULL ? (size_t)(newline - cursor)
                                          : strlen(cursor);
          char *line = xdup_range(cursor, length);
          qa_native_q2_hud_operation op;
          memset(&op, 0, sizeof op);
          op.kind = QA_Q2_HUD_OP_FONT_TEXT;
          op.x = (width - environment->measure(environment->ctx, line).x) / 2;
          op.y = (height - size.y) / 2 +
                 (double)line_index * environment->font_line_height;
          op.text = qa_native_q2_hud_ops_own(out, line);
          op.alternate = false;
          rr_push(&s, op);
          line_index++;
          if (newline == NULL) break;
          cursor = newline + 1;
        }
        free(value);
      }
    }
#undef NEXT_STR
#undef NEXT_INT
    free(command);
    command = NULL;
    if (!s.ok) break;
  }
  qa_cleanup_pop(&unwind_cleanup);
  free(conditions);
  qa_tokenizer_destroy(&parser);
  if (scratch_used) qa_native_q2_hud_table_destroy(&scratch);
  if (condition_count != 0)
    qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD if without matching endif");
  return s.ok;
}

bool qa_q2_rerelease_layout(const char *source,
                            const qa_native_q2_hud_frame *frame, double width,
                            double height,
                            qa_native_q2_hud_environment *environment,
                            const qa_native_q2_hud_arsenal *arsenal,
                            qa_native_q2_hud_operations *out) {
  memset(out, 0, sizeof *out);
  return qa_q2_rerelease_layout_into(source, frame, width, height, environment,
                                     arsenal, out);
}

bool qa_q2_rerelease_inventory_into(
    const qa_native_q2_hud_frame *frame, double width, double height,
    qa_native_q2_hud_environment *environment,
    const qa_native_q2_inventory_readout *inventory,
    qa_native_q2_hud_operations *out) {
  rr_state s;
  memset(&s, 0, sizeof s);
  s.frame = frame;
  s.env = environment;
  s.config = qa_ui_hud_q2_application_layout_for(frame->protocol);
  s.out = out;
  s.ok = true;
  double selected = frame->stat_count > 12 ? frame->stats[12] : 0;
  typedef struct inv_row {
    const char *name;
    double count;
    bool selected;
  } inv_row;
  inv_row *items = NULL;
  size_t item_count = 0, item_capacity = 0;
  if (inventory != NULL) {
    item_count = inventory->item_count;
    items = malloc((item_count != 0 ? item_count : 1) * sizeof *items);
    if (items == NULL) qa_range_error("Out of memory");
    item_capacity = item_count;
    for (size_t i = 0; i < item_count; i++) {
      items[i].name = inventory->items[i].label;
      items[i].count = inventory->items[i].count;
      items[i].selected = inventory->selected != NULL &&
                          inventory->items[i].item != NULL &&
                          strcmp(inventory->items[i].item,
                                 inventory->selected) == 0;
    }
  } else {
    for (size_t i = 0; i < frame->inventory_count; i++) {
      if (frame->inventory[i] == 0) continue;
      if (item_count == item_capacity) {
        item_capacity = item_capacity != 0 ? item_capacity * 2 : 16;
        items = realloc(items, item_capacity * sizeof *items);
        if (items == NULL) qa_range_error("Out of memory");
      }
      items[item_count].name =
          qa_native_q2_config_string_at(frame, s.config.items + (double)i);
      items[item_count].count = frame->inventory[i];
      items[item_count].selected = (double)i == selected;
      item_count++;
    }
  }
  double selected_row;
  if (inventory == NULL) {
    if (selected >= 0 && selected < (double)frame->inventory_count) {
      double count = 0;
      for (size_t i = 0; i < (size_t)selected; i++)
        if (frame->inventory[i] != 0) count++;
      selected_row = count;
    } else {
      selected_row = 0;
    }
  } else {
    double found = -1;
    for (size_t i = 0; i < item_count; i++)
      if (items[i].selected) {
        found = (double)i;
        break;
      }
    selected_row = found > 0 ? found : 0;
  }
  double top_d = (double)item_count - 19;
  if (selected_row - 9 < top_d) top_d = selected_row - 9;
  if (top_d < 0) top_d = 0;
  size_t top = (size_t)top_d;
  double x = trunc(width / 2) - 128, y = trunc(height / 2) - 108;
  rr_picture(&s, "inventory", x, y + 8);
  size_t end = top + 19;
  if (end > item_count) end = item_count;
  for (size_t i = top; i < end; i++) {
    char *name = localized_text(frame, environment, items[i].name, NULL, 0);
    double py = y + 27 + (double)(i - top) * 8;
    bool alternate = items[i].selected;
    if (alternate && (to_int32(frame->time_milliseconds * 10) & 1) != 0) {
      qa_native_q2_hud_operation op;
      memset(&op, 0, sizeof op);
      op.kind = QA_Q2_HUD_OP_TEXT;
      op.x = x + 14;
      op.y = py;
      op.text = qa_native_q2_hud_ops_dup(out, "\x0f");
      rr_push(&s, op);
    }
    if (environment->use_font) {
      char *count = js_number(items[i].count);
      double offset = (environment->font_line_height - 8) / 2;
      qa_native_q2_hud_operation op;
      memset(&op, 0, sizeof op);
      op.kind = QA_Q2_HUD_OP_FONT_TEXT;
      op.x = x + 222 - environment->measure(environment->ctx, count).x;
      op.y = py - offset;
      op.text = qa_native_q2_hud_ops_own(out, count);
      op.alternate = alternate;
      rr_push(&s, op);
      memset(&op, 0, sizeof op);
      op.kind = QA_Q2_HUD_OP_FONT_TEXT;
      op.x = x + 38;
      op.y = py - offset;
      op.text = qa_native_q2_hud_ops_dup(out, name);
      op.alternate = alternate;
      rr_push(&s, op);
    } else {
      char *count_raw = js_number(items[i].count);
      size_t count_length = strlen(count_raw);
      char count_padded[64];
      if (count_length < 3) {
        memset(count_padded, ' ', 3 - count_length);
        memcpy(count_padded + (3 - count_length), count_raw, count_length);
        count_padded[3] = '\0';
      } else {
        snprintf(count_padded, sizeof count_padded, "%s", count_raw);
      }
      free(count_raw);
      size_t total = strlen(count_padded) + strlen(name) + 2;
      char *line = malloc(total);
      if (line == NULL) qa_range_error("Out of memory");
      snprintf(line, total, "%s %s", count_padded, name);
      qa_native_q2_hud_operation op;
      memset(&op, 0, sizeof op);
      op.kind = QA_Q2_HUD_OP_TEXT;
      op.x = x + 22;
      op.y = py;
      op.text = qa_native_q2_hud_ops_own(out, line);
      op.alternate = alternate;
      op.has_xor = true;
      op.xor_ = true;
      rr_push(&s, op);
    }
    free(name);
  }
  free(items);
  return s.ok;
}

bool qa_q2_rerelease_inventory(const qa_native_q2_hud_frame *frame,
                               double width, double height,
                               qa_native_q2_hud_environment *environment,
                               const qa_native_q2_inventory_readout *inventory,
                               qa_native_q2_hud_operations *out) {
  memset(out, 0, sizeof *out);
  return qa_q2_rerelease_inventory_into(frame, width, height, environment,
                                        inventory, out);
}

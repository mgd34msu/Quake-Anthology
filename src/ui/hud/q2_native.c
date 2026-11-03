/* Port of src/ui/hud/q2-native.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/q2_native.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/hud/q2_rerelease_layout.h"

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
/* JS ToInt32 for the TS bitwise operators. */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(value);
  double wrapped = fmod(fabs(truncated), 4294967296.0);
  if (truncated < 0) wrapped = -wrapped;
  if (wrapped >= 2147483648.0) wrapped -= 4294967296.0;
  if (wrapped < -2147483648.0) wrapped += 4294967296.0;
  return (int32_t)wrapped;
}
/* JS String.padStart/padEnd over bytes (UTF-16 lengths differ only
 * for non-ASCII text). Returned string is malloc'd. */
static char *pad_start(const char *text, size_t target, char fill) {
  size_t length = strlen(text);
  if (length >= target) return xstrdup(text);
  char *out = malloc(target + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memset(out, fill, target - length);
  memcpy(out + (target - length), text, length + 1);
  return out;
}
static char *pad_end(const char *text, size_t target) {
  size_t length = strlen(text);
  if (length >= target) return xstrdup(text);
  char *out = malloc(target + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length);
  memset(out + length, ' ', target - length);
  out[target] = '\0';
  return out;
}

bool qa_native_q2_is_rerelease(qa_q2_protocol_identity protocol) {
  return protocol.kind != NULL &&
         (strcmp(protocol.kind, "q2-rerelease") == 0 ||
          strcmp(protocol.kind, "q2-kex") == 0 ||
          strcmp(protocol.kind, "q2-kex-demo") == 0);
}

qa_ui_hud_q2_app_layout qa_ui_hud_q2_application_layout_for(
    qa_q2_protocol_identity protocol) {
  return qa_app_q2_application_layout(protocol);
}

static const char *config_get(const qa_native_q2_hud_frame *frame,
                              double index) {
  for (size_t i = 0; i < frame->configstring_count; i++)
    if (frame->configstrings[i].index == index)
      return frame->configstrings[i].value;
  return NULL;
}
const char *qa_native_q2_config_string_at(const qa_native_q2_hud_frame *frame,
                                       double index) {
  const char *value = config_get(frame, index);
  return value != NULL ? value : "";
}

void qa_native_q2_hud_operations_free(qa_native_q2_hud_operations *result) {
  if (result == NULL) return;
  free(result->operations);
  for (size_t i = 0; i < result->owned_count; i++) free(result->owned[i]);
  free(result->owned);
  memset(result, 0, sizeof *result);
}
bool qa_native_q2_hud_ops_push(qa_native_q2_hud_operations *out,
                               qa_native_q2_hud_operation operation) {
  if (out->count == out->capacity) {
    size_t capacity = out->capacity != 0 ? out->capacity * 2 : 16;
    qa_native_q2_hud_operation *grown =
        realloc(out->operations, capacity * sizeof *grown);
    if (grown == NULL) return false;
    out->operations = grown;
    out->capacity = capacity;
  }
  out->operations[out->count++] = operation;
  return true;
}
char *qa_native_q2_hud_ops_own(qa_native_q2_hud_operations *out, char *text) {
  if (text == NULL) qa_range_error("Out of memory");
  if (out->owned_count == out->owned_capacity) {
    size_t capacity = out->owned_capacity != 0 ? out->owned_capacity * 2 : 16;
    char **grown = realloc(out->owned, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    out->owned = grown;
    out->owned_capacity = capacity;
  }
  out->owned[out->owned_count++] = text;
  return text;
}
char *qa_native_q2_hud_ops_dup(qa_native_q2_hud_operations *out,
                               const char *text) {
  return qa_native_q2_hud_ops_own(out, xstrdup(text));
}

double qa_native_q2_hud_stat(const qa_native_q2_hud_frame *frame, double index,
                             const qa_native_q2_hud_arsenal *arsenal) {
  if (index < 0 || index >= (double)frame->stat_count) {
    char buffer[64], *text = js_number(index);
    (void)snprintf(buffer, sizeof buffer, "%s", text);
    free(text); /* the raise is caught by the HUD (NativeQ2HudLayoutError) */
        qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD stat %s is outside playerstate", buffer);
  }
  if (arsenal != NULL && arsenal->has_ammunition && index == 2)
    return arsenal->ammo_has_count ? 1 : 0;
  if (arsenal != NULL && arsenal->has_ammunition && index == 3)
    return arsenal->ammo_has_count ? arsenal->ammo_count : -1;
  return frame->stats[(size_t)index];
}

/* frame.stats[index] ?? 0 in q2NativeHudOperations (no throw). */
static double raw_stat(const qa_native_q2_hud_frame *frame, size_t index) {
  return index < frame->stat_count ? frame->stats[index] : 0;
}

/* --- classic layout (SCR_ExecuteLayoutString) --- */

static void tokenizer_release(void *ctx) { qa_tokenizer_destroy(ctx); }
static void token_release(void *ctx) {
  char **token = ctx;
  free(*token);
  *token = NULL;
}

typedef struct layout_parser {
  qa_tokenizer tokenizer;
  bool ok;
} layout_parser;

/* Next token value (malloc'd) or "" at end, as TS next(). */
static char *parser_next(layout_parser *parser) {
  qa_text_token token;
  if (!qa_tokenizer_next(&parser->tokenizer, true, &token))
    return xstrdup("");
  return token.value;
}
static bool parser_next_token(layout_parser *parser, char **out) {
  qa_text_token token;
  if (!qa_tokenizer_next(&parser->tokenizer, true, &token)) return false;
  *out = token.value;
  return true;
}
static int32_t parser_integer(layout_parser *parser) {
  char *text = parser_next(parser);
  int32_t value = qa_game_atoi(text);
  free(text);
  return value;
}

typedef struct classic_state {
  const qa_native_q2_hud_frame *frame;
  const qa_native_q2_hud_arsenal *arsenal;
  qa_ui_hud_q2_app_layout config;
  qa_native_q2_hud_operations *out;
  double width, height, x, y;
  bool ok;
} classic_state;

static double cstat(classic_state *s, double index) {
  return qa_native_q2_hud_stat(s->frame, index, s->arsenal);
}
static void push_op(classic_state *s, qa_native_q2_hud_operation op) {
  if (!qa_native_q2_hud_ops_push(s->out, op)) s->ok = false;
}
static void op_picture(classic_state *s, const char *name) {
  if (name[0] == '\0') return;
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_PICTURE;
  op.x = s->x;
  op.y = s->y;
  op.name = qa_native_q2_hud_ops_dup(s->out, name);
  push_op(s, op);
}
static void op_text(classic_state *s, const char *value, bool alternate,
                    double x, double y) {
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_TEXT;
  op.x = x;
  op.y = y;
  op.text = qa_native_q2_hud_ops_dup(s->out, value);
  op.alternate = alternate;
  push_op(s, op);
}
static void op_arsenal(classic_state *s, qa_resource_id resource,
                       double aspect) {
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_ARSENAL_PICTURE;
  op.x = s->x;
  op.y = s->y;
  op.resource = resource;
  op.aspect = aspect;
  push_op(s, op);
}
static void op_number(classic_state *s, double value, double digits,
                      bool alternate) {
  double count = digits < 5 ? digits : 5;
  if (count < 1) return;
  char *value_text = js_number(trunc(value));
  size_t text_length = strlen(value_text);
  size_t length = text_length < (size_t)count ? text_length : (size_t)count;
  double px = s->x + 2 + 16 * (count - (double)length);
  for (size_t i = 0; i < length; i++) {
    char name[32];
    if (value_text[i] == '-')
      snprintf(name, sizeof name, "%sminus", alternate ? "anum_" : "num_");
    else
      snprintf(name, sizeof name, "%s%c", alternate ? "anum_" : "num_",
               value_text[i]);
    qa_native_q2_hud_operation op;
    memset(&op, 0, sizeof op);
    op.kind = QA_Q2_HUD_OP_PICTURE;
    op.x = px;
    op.y = s->y;
    op.name = qa_native_q2_hud_ops_dup(s->out, name);
    push_op(s, op);
    px += 16;
  }
  free(value_text);
}

typedef struct client_row {
  char *name;
  char *icon;
} client_row;
static client_row client_info(classic_state *s, double index) {
  const char *max_text = config_get(s->frame, s->config.max_clients);
  double max = qa_input_js_parse_number(max_text != NULL ? max_text : "256");
  double limit = max > 1 ? max : 1;
  if (index < 0 || index >= limit) {
    char buffer[64], *text = js_number(index);
    (void)snprintf(buffer, sizeof buffer, "%s", text);
    free(text); /* the raise is caught by the HUD (NativeQ2HudLayoutError) */
        qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD client %s is outside clientinfo", buffer);
  }
  const char *info =
      qa_native_q2_config_string_at(s->frame, s->config.player_skins + index);
  const char *slash = strchr(info, '\\');
  const char *skin = slash == NULL ? "male/grunt" : slash + 1;
  client_row row;
  row.name = slash == NULL ? xstrdup(info)
                           : xdup_range(info, (size_t)(slash - info));
  const char *skin_name = skin[0] != '\0' ? skin : "male/grunt";
  size_t icon_length = strlen(skin_name) + 32;
  row.icon = malloc(icon_length);
  if (row.icon == NULL) qa_range_error("Out of memory");
  snprintf(row.icon, icon_length, "/players/%s_i.pcx", skin_name);
  return row;
}

static bool classic_layout(const char *source,
                           const qa_native_q2_hud_frame *frame, double width,
                           double height,
                           const qa_native_q2_hud_arsenal *arsenal,
                           qa_native_q2_hud_operations *out) {
  classic_state s;
  memset(&s, 0, sizeof s);
  s.frame = frame;
  s.arsenal = arsenal;
  s.config = qa_ui_hud_q2_application_layout_for(frame->protocol);
  s.out = out;
  s.width = width;
  s.height = height;
  s.ok = true;
  layout_parser parser;
  memset(&parser, 0, sizeof parser);
  if (!qa_tokenizer_init(&parser.tokenizer, source, strlen(source),
                         "Q2 HUD layout"))
    return false;
  /* A layout or parse error is caught by the HUD; the tokenizer and the
   * current token are released when it unwinds. */
  qa_cleanup tokenizer_cleanup;
  qa_cleanup_push(&tokenizer_cleanup, tokenizer_release, &parser.tokenizer);
  char *token = NULL;
  qa_cleanup token_cleanup;
  qa_cleanup_push(&token_cleanup, token_release, &token);
  while (parser_next_token(&parser, &token)) {
    if (strcmp(token, "xl") == 0) {
      s.x = parser_integer(&parser);
    } else if (strcmp(token, "xr") == 0) {
      s.x = width + parser_integer(&parser);
    } else if (strcmp(token, "xv") == 0) {
      s.x = trunc(width / 2) - 160 + parser_integer(&parser);
    } else if (strcmp(token, "yt") == 0) {
      s.y = parser_integer(&parser);
    } else if (strcmp(token, "yb") == 0) {
      s.y = height + parser_integer(&parser);
    } else if (strcmp(token, "yv") == 0) {
      s.y = trunc(height / 2) - 120 + parser_integer(&parser);
    } else if (strcmp(token, "pic") == 0) {
      int32_t index = parser_integer(&parser);
      if (index == 2 && arsenal != NULL && arsenal->has_ammunition) {
        if (arsenal->ammo_has_count && arsenal->ammo_has_icon)
          op_arsenal(&s, arsenal->ammo_icon_resource,
                     arsenal->ammo_icon_aspect);
      } else if (index == 6 && arsenal != NULL && arsenal->has_selected_item) {
        if (cstat(&s, index) != 0 && arsenal->selected_has_icon)
          op_arsenal(&s, arsenal->selected_icon_resource,
                     arsenal->selected_icon_aspect);
      } else {
        double image = cstat(&s, index);
        if (image < 0 || image >= s.config.max_images) {
          char buffer[64], *text = js_number(image);
          (void)snprintf(buffer, sizeof buffer, "%s", text);
          free(text); /* the raise is caught by the HUD (NativeQ2HudLayoutError) */
                    qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD image %s is outside configstrings", buffer);
        }
        op_picture(&s, qa_native_q2_config_string_at(frame, s.config.images + image));
      }
    } else if (strcmp(token, "picn") == 0) {
      char *name = parser_next(&parser);
      op_picture(&s, name);
      free(name);
    } else if (strcmp(token, "num") == 0) {
      int32_t digits = parser_integer(&parser);
      op_number(&s, cstat(&s, parser_integer(&parser)), digits, false);
    } else if (strcmp(token, "hnum") == 0) {
      double value = cstat(&s, 1);
      if ((to_int32(cstat(&s, 15)) & 1) != 0) op_picture(&s, "field_3");
      op_number(&s, value, 3,
                value <= 0 ||
                    (value <= 25 && ((to_int32(frame->server_frame) >> 2) & 1) != 0));
    } else if (strcmp(token, "anum") == 0) {
      double value = cstat(&s, 3);
      if (value >= 0) {
        if ((to_int32(cstat(&s, 15)) & 4) != 0) op_picture(&s, "field_3");
        op_number(&s, value, 3,
                  value <= 5 && ((to_int32(frame->server_frame) >> 2) & 1) != 0);
      }
    } else if (strcmp(token, "rnum") == 0) {
      double value = cstat(&s, 5);
      if (value >= 1) {
        if ((to_int32(cstat(&s, 15)) & 2) != 0) op_picture(&s, "field_3");
        op_number(&s, value, 3, false);
      }
    } else if (strcmp(token, "stat_string") == 0) {
      double index = cstat(&s, parser_integer(&parser));
      if (index < 0 || index >= s.config.max_config_strings)
        qa_raise("NativeQ2HudLayoutError", 0, 0, "Q2 HUD stat_string is outside configstrings");
      op_text(&s, qa_native_q2_config_string_at(frame, index), false, s.x, s.y);
    } else if (strcmp(token, "string") == 0 || strcmp(token, "string2") == 0) {
      char *value = parser_next(&parser);
      op_text(&s, value, strcmp(token, "string2") == 0, s.x, s.y);
      free(value);
    } else if (strcmp(token, "cstring") == 0 || strcmp(token, "cstring2") == 0) {
      char *value = parser_next(&parser);
      bool alternate = strcmp(token, "cstring2") == 0;
      double py = s.y;
      const char *cursor = value;
      for (;;) {
        const char *newline = strchr(cursor, '\n');
        size_t length = newline != NULL ? (size_t)(newline - cursor)
                                        : strlen(cursor);
        char *line = xdup_range(cursor, length);
        op_text(&s, line, alternate,
                s.x + trunc((320 - (double)length * 8) / 2), py);
        free(line);
        py += 8;
        if (newline == NULL) break;
        cursor = newline + 1;
      }
      free(value);
    } else if (strcmp(token, "client") == 0) {
      s.x = trunc(width / 2) - 160 + parser_integer(&parser);
      s.y = trunc(height / 2) - 120 + parser_integer(&parser);
      client_row row = client_info(&s, parser_integer(&parser));
      int32_t score = parser_integer(&parser);
      int32_t ping = parser_integer(&parser);
      int32_t time = parser_integer(&parser);
      op_text(&s, row.name, true, s.x + 32, s.y);
      op_text(&s, "Score: ", false, s.x + 32, s.y + 8);
      char *score_text = js_number(score);
      op_text(&s, score_text, true, s.x + 88, s.y + 8);
      free(score_text);
      char buffer[64];
      snprintf(buffer, sizeof buffer, "Ping:  %d", (int)ping);
      op_text(&s, buffer, false, s.x + 32, s.y + 16);
      snprintf(buffer, sizeof buffer, "Time:  %d", (int)time);
      op_text(&s, buffer, false, s.x + 32, s.y + 24);
      op_picture(&s, row.icon);
      free(row.name);
      free(row.icon);
    } else if (strcmp(token, "ctf") == 0) {
      s.x = trunc(width / 2) - 160 + parser_integer(&parser);
      s.y = trunc(height / 2) - 120 + parser_integer(&parser);
      int32_t index = parser_integer(&parser);
      client_row row = client_info(&s, index);
      int32_t score = parser_integer(&parser);
      int32_t ping_raw = parser_integer(&parser);
      int32_t ping = ping_raw < 999 ? ping_raw : 999;
      char *score_raw = js_number(score);
      char *score_text = pad_start(score_raw, 3, ' ');
      free(score_raw);
      char *ping_raw_text = js_number(ping);
      char *ping_text = pad_start(ping_raw_text, 3, ' ');
      free(ping_raw_text);
      size_t name_length = strlen(row.name);
      if (name_length > 12) name_length = 12;
      char *short_name = xdup_range(row.name, name_length);
      char *name_text = pad_end(short_name, 12);
      free(short_name);
      size_t total = strlen(score_text) + strlen(ping_text) +
                     strlen(name_text) + 3;
      char *line = malloc(total);
      if (line == NULL) qa_range_error("Out of memory");
      snprintf(line, total, "%s %s %s", score_text, ping_text, name_text);
      op_text(&s, line, (double)index == frame->player_number, s.x, s.y);
      free(line);
      free(score_text);
      free(ping_text);
      free(name_text);
      free(row.name);
      free(row.icon);
    } else if (strcmp(token, "if") == 0) {
      if (cstat(&s, parser_integer(&parser)) == 0) {
        char *skipped = NULL;
        while (parser_next_token(&parser, &skipped)) {
          bool done = strcmp(skipped, "endif") == 0;
          free(skipped);
          skipped = NULL;
          if (done) break;
        }
      }
    }
    free(token);
    token = NULL;
    if (!s.ok) break;
  }
  qa_cleanup_pop(&token_cleanup);
  qa_cleanup_pop(&tokenizer_cleanup);
  free(token);
  qa_tokenizer_destroy(&parser.tokenizer);
  return s.ok;
}

/* TS layoutOperations (both protocols). Appends to out. */
typedef struct layout_call {
  const char *source;
  const qa_native_q2_hud_frame *frame;
  double width, height;
  const qa_native_q2_hud_arsenal *arsenal;
  qa_native_q2_hud_environment *environment;
  qa_native_q2_hud_operations *out;
  bool ok;
} layout_call;
static void layout_call_run(void *ctx) {
  layout_call *c = ctx;
  if (qa_native_q2_is_rerelease(c->frame->protocol)) {
    if (c->environment == NULL)
      qa_raise("Error", 0, 0, "Rerelease HUD requires source localization and font services");
    c->ok = qa_q2_rerelease_layout_into(c->source, c->frame, c->width, c->height, c->environment, c->arsenal, c->out);
    return;
  }
  c->ok = classic_layout(c->source, c->frame, c->width, c->height, c->arsenal, c->out);
}
/* TS q2LayoutOperations: try { layoutOperations(...) } catch (error) { if (error instanceof TextParseError) throw new
 * NativeQ2HudLayoutError(error.message); throw error; }. Appends to out. */
static bool layout_into(const char *source, const qa_native_q2_hud_frame *frame, double width, double height,
                        const qa_native_q2_hud_arsenal *arsenal, qa_native_q2_hud_environment *environment,
                        qa_native_q2_hud_operations *out) {
  layout_call call = { source, frame, width, height, arsenal, environment, out, false };
  qa_caught_error caught;
  if (qa_try(layout_call_run, &call, &caught)) return call.ok;
  if (strcmp(caught.name, "TextParseError") == 0) qa_raise("NativeQ2HudLayoutError", 0, 0, "%s", caught.message);
  qa_rethrow(&caught);
}

bool qa_q2_layout_operations(const char *source,
                             const qa_native_q2_hud_frame *frame, double width,
                             double height,
                             const qa_native_q2_hud_arsenal *arsenal,
                             qa_native_q2_hud_environment *environment,
                             qa_native_q2_hud_operations *out) {
  memset(out, 0, sizeof *out);
  return layout_into(source, frame, width, height, arsenal, environment, out);
}

static void table_release(void *ctx) { qa_native_q2_hud_table_destroy(ctx); }

typedef struct inv_item {
  const char *name; /* borrowed; classic names borrow configstrings */
  double count;
  bool selected;
} inv_item;

bool qa_q2_native_hud_operations(const qa_native_q2_hud_frame *frame,
                                 double width, double height,
                                 qa_q2_hud_binding_fn binding,
                                 void *binding_ctx, const char *mode,
                                 const qa_native_q2_hud_arsenal *arsenal,
                                 qa_native_q2_hud_environment *environment,
                                 qa_native_q2_hud_operations *out) {
  memset(out, 0, sizeof *out);
  bool overlay = mode != NULL && strcmp(mode, "layout-overlay") == 0;
  bool rerelease = qa_native_q2_is_rerelease(frame->protocol);
  /* TS: a rerelease environment without a table gets a fresh empty
   * table shared by every layout call in this invocation. */
  qa_native_q2_hud_table fresh_table;
  qa_cleanup table_cleanup; /* released when a layout error unwinds */
  qa_native_q2_hud_environment fresh_env;
  qa_native_q2_hud_environment *env = environment;
  bool fresh = false;
  if (rerelease && env != NULL && env->table == NULL) {
    qa_native_q2_hud_table_init(&fresh_table);
    qa_cleanup_push(&table_cleanup, table_release, &fresh_table);
    fresh_env = *env;
    fresh_env.table = &fresh_table;
    env = &fresh_env;
    fresh = true;
  }
  bool ok = true;
  double layouts = raw_stat(frame, 13);
  if (!overlay && !(rerelease && (to_int32(layouts) & 4) != 0)) {
    const char *status = qa_native_q2_config_string_at(frame, 5);
    ok = layout_into(status, frame, width, height, arsenal, env, out);
  }
  if (ok && (to_int32(layouts) & 1) != 0)
    ok = layout_into(frame->layout, frame, width, height, arsenal, env, out);
  if (!ok || overlay || (to_int32(layouts) & 2) == 0) {
    if (fresh) { qa_cleanup_pop(&table_cleanup); qa_native_q2_hud_table_destroy(&fresh_table); }
    return ok;
  }
  if (rerelease) {
    if (env == NULL)
      qa_raise("Error", 0, 0, "Rerelease HUD requires source localization and font services");
    ok = qa_q2_rerelease_inventory_into(
        frame, width, height, env,
        arsenal != NULL && arsenal->has_inventory ? &arsenal->inventory : NULL,
        out);
    if (fresh) { qa_cleanup_pop(&table_cleanup); qa_native_q2_hud_table_destroy(&fresh_table); }
    return ok;
  }
  qa_ui_hud_q2_app_layout config =
      qa_ui_hud_q2_application_layout_for(frame->protocol);
  double selected = raw_stat(frame, 12);
  inv_item *items = NULL;
  size_t item_count = 0, item_capacity = 0;
  bool has_readout = arsenal != NULL && arsenal->has_inventory;
  if (has_readout) {
    const qa_native_q2_inventory_readout *readout = &arsenal->inventory;
    item_count = readout->item_count;
    items = malloc((item_count != 0 ? item_count : 1) * sizeof *items);
    if (items == NULL) qa_range_error("Out of memory");
    item_capacity = item_count;
    for (size_t i = 0; i < item_count; i++) {
      items[i].name = readout->items[i].label;
      items[i].count = readout->items[i].count;
      items[i].selected = readout->selected != NULL &&
                          readout->items[i].item != NULL &&
                          strcmp(readout->items[i].item, readout->selected) == 0;
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
          qa_native_q2_config_string_at(frame, config.items + (double)i);
      items[item_count].count = frame->inventory[i];
      items[item_count].selected = (double)i == selected;
      item_count++;
    }
  }
  double selected_row;
  if (!has_readout) {
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
  double top_d = (double)item_count - 17;
  if (selected_row - 8 < top_d) top_d = selected_row - 8;
  if (top_d < 0) top_d = 0;
  size_t top = (size_t)top_d;
  double x = floor((width - 256) / 2), y = floor((height - 240) / 2);
  qa_native_q2_hud_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q2_HUD_OP_PICTURE;
  op.x = x;
  op.y = y + 8;
  op.name = qa_native_q2_hud_ops_dup(out, "inventory");
  if (!qa_native_q2_hud_ops_push(out, op)) ok = false;
  const char *headers[2] = { "hotkey ### item", "------ --- ----" };
  for (size_t h = 0; h < 2; h++) {
    memset(&op, 0, sizeof op);
    op.kind = QA_Q2_HUD_OP_TEXT;
    op.x = x + 24;
    op.y = y + 24 + (double)h * 8;
    op.text = qa_native_q2_hud_ops_dup(out, headers[h]);
    op.alternate = false;
    if (!qa_native_q2_hud_ops_push(out, op)) ok = false;
  }
  size_t end = top + 17;
  if (end > item_count) end = item_count;
  for (size_t i = top; i < end; i++) {
    double row = (double)(i - top);
    double py = y + 40 + row * 8;
    size_t command_length = strlen(items[i].name) + 8;
    char *command = malloc(command_length);
    if (command == NULL) qa_range_error("Out of memory");
    snprintf(command, command_length, "use %s", items[i].name);
    const char *bound = binding != NULL ? binding(binding_ctx, command) : "";
    free(command);
    if (bound == NULL) bound = "";
    char *bound_text = pad_start(bound, 6, ' ');
    char *count_raw = js_number(items[i].count);
    char *count_text = pad_start(count_raw, 3, ' ');
    free(count_raw);
    size_t line_length = strlen(bound_text) + strlen(count_text) +
                         strlen(items[i].name) + 3;
    char *line = malloc(line_length);
    if (line == NULL) qa_range_error("Out of memory");
    snprintf(line, line_length, "%s %s %s", bound_text, count_text,
             items[i].name);
    free(bound_text);
    free(count_text);
    memset(&op, 0, sizeof op);
    op.kind = QA_Q2_HUD_OP_TEXT;
    op.x = x + 24;
    op.y = py;
    op.text = qa_native_q2_hud_ops_own(out, line);
    op.alternate = !items[i].selected;
    if (!qa_native_q2_hud_ops_push(out, op)) ok = false;
    if (items[i].selected &&
        (to_int32(trunc(frame->time_milliseconds / 100)) & 1) != 0) {
      memset(&op, 0, sizeof op);
      op.kind = QA_Q2_HUD_OP_TEXT;
      op.x = x + 16;
      op.y = py;
      op.text = qa_native_q2_hud_ops_dup(out, "\x0f");
      op.alternate = false;
      if (!qa_native_q2_hud_ops_push(out, op)) ok = false;
    }
  }
  free(items);
  if (fresh) { qa_cleanup_pop(&table_cleanup); qa_native_q2_hud_table_destroy(&fresh_table); }
  return ok;
}

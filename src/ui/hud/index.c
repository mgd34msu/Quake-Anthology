/* Port of src/ui/hud/index.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/index.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/render/scene/view.h"
#include "qa/ui/common/accessibility.h"
#include "qa/ui/common/captions.h"

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
/* UTF-8 code point count (TS Array.from(text).length). */
static size_t codepoints(const char *text) {
  size_t count = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++)
    if ((*p & 0xC0) != 0x80) count++;
  return count;
}
/* Byte length of the first `count` code points. */
static size_t codepoint_prefix_bytes(const char *text, size_t count) {
  size_t seen = 0;
  const unsigned char *p = (const unsigned char *)text;
  while (*p != '\0' && seen < count) {
    if ((*p & 0xC0) != 0x80) seen++;
    if (seen <= count) p++;
  }
  /* Include trailing continuation bytes of the last code point. */
  while (*p != '\0' && (*p & 0xC0) == 0x80) p++;
  return (size_t)(p - (const unsigned char *)text);
}
static double min_d(double a, double b) { return a < b ? a : b; }
static double max_d(double a, double b) { return a > b ? a : b; }

void qa_ui_hud_rects_free(qa_ui_hud_rects *result) {
  if (result == NULL) return;
  free(result->rects);
  memset(result, 0, sizeof *result);
}

bool qa_ui_hud_vital_rects_for(double count, double scale, double height,
                               qa_ui_hud_rects *out) {
  memset(out, 0, sizeof *out);
  double width = min_d(600 / scale / max_d(1, count), 160);
  double start = 320 - width * count / 2;
  size_t total = count > 0 ? (size_t)count : 0;
  if (total == 0) return true;
  out->rects = malloc(total * sizeof *out->rects);
  if (out->rects == NULL) return false;
  out->count = total;
  for (size_t i = 0; i < total; i++)
    out->rects[i] =
        (qa_rect){ start + (double)i * width, 476 - height, width - 4, height };
  return true;
}

typedef struct status_layout {
  qa_rect *rects;
  size_t count;
  qa_ui_transform transform;
  double minimum_text_scale;
} status_layout;

static bool compute_status_layout(qa_ui_draw_context context, double count,
                                  double hud_scale, double text_scale,
                                  double cap_height, status_layout *out) {
  memset(out, 0, sizeof *out);
  qa_rect area = context.binding.safe_area;
  qa_ui_transform fitted = qa_ui_fit(area, 1);
  double group = hud_scale * context.binding.hud_scale;
  double requested = fitted.scale * group;
  size_t total = count > 0 ? (size_t)count : 0;
  if (requested * text_scale * cap_height < 8) {
    double width = min_d(180, (area.width - 8) / max_d(1, count));
    if (total > 0) {
      out->rects = malloc(total * sizeof *out->rects);
      if (out->rects == NULL) return false;
      out->count = total;
      for (size_t i = 0; i < total; i++)
        out->rects[i] = (qa_rect){
          area.x + (area.width - width * count) / 2 + (double)i * width,
          area.y + area.height - 36, width - 4, 32
        };
    }
    out->transform = (qa_ui_transform){ 0, 0, 1 };
    out->minimum_text_scale = 8 / cap_height;
    return true;
  }
  double scale = requested;
  qa_ui_hud_rects rects;
  if (!qa_ui_hud_vital_rects_for(
          count, group, qa_ui_hud_status_rows_for(text_scale, cap_height).height,
          &rects))
    return false;
  out->rects = rects.rects;
  out->count = rects.count;
  out->minimum_text_scale = 8 / cap_height / scale;
  out->transform = (qa_ui_transform){
    fitted.x + 320 * fitted.scale * (1 - group),
    fitted.y + 480 * fitted.scale * (1 - group), scale
  };
  return true;
}

bool qa_ui_hud_vital_occupied_rects(qa_ui_draw_context context, double count,
                                    double hud_scale, double text_scale,
                                    double cap_height, qa_ui_hud_rects *out) {
  memset(out, 0, sizeof *out);
  status_layout layout;
  if (!compute_status_layout(context, count, hud_scale, text_scale, cap_height,
                             &layout))
    return false;
  if (layout.count > 0) {
    out->rects = malloc(layout.count * sizeof *out->rects);
    if (out->rects == NULL) {
      free(layout.rects);
      return false;
    }
    out->count = layout.count;
    for (size_t i = 0; i < layout.count; i++) {
      qa_rect rect = layout.rects[i];
      out->rects[i] = (qa_rect){
        layout.transform.x + rect.x * layout.transform.scale,
        layout.transform.y + rect.y * layout.transform.scale,
        rect.width * layout.transform.scale,
        rect.height * layout.transform.scale
      };
    }
  }
  free(layout.rects);
  return true;
}

qa_ui_common_hud_data qa_ui_empty_hud_data(qa_seat_id seat) {
  qa_ui_common_hud_data data;
  memset(&data, 0, sizeof data);
  data.seat = seat;
  data.visible = true;
  data.inventory_is_null = true;
  data.crosshair_visible = true;
  data.crosshair_color = (qa_vec4){ 1, 1, 1, 1 };
  return data;
}

/* --- SeatHudMessages --- */

typedef struct center_record {
  char *text; /* owned */
  qa_source_time starts, duration;
  bool instant;
  bool has_character_milliseconds;
  double character_milliseconds;
} center_record;

typedef struct notice_record {
  double sequence;
  char *text; /* owned */
  bool chat;
  qa_source_time starts, duration;
} notice_record;

struct qa_seat_hud_messages {
  qa_seat_id seat;
  double sequence;
  notice_record *notices;
  size_t notice_count, notice_capacity;
  bool has_center;
  center_record center;
  qa_event_ring queued_centers;
  qa_ui_hud_point_of_interest *points;
  size_t point_count, point_capacity;
  const qa_ui_hud_point_of_interest *source_points; /* borrowed */
  size_t source_point_count;
};

static double source_ms(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value * 1000 : time.value;
}
static bool time_alive(qa_source_time starts, qa_source_time duration,
                       double now) {
  double start = source_ms(starts);
  return now >= start && now < start + source_ms(duration);
}
static void require_seat(const qa_seat_hud_messages *messages, qa_seat_id seat) {
  if (!qa_same_seat(seat, messages->seat))
    qa_range_error("HUD message belongs to another seat");
}
static void free_center_text(const void *record, int64_t id, void *ctx) {
  (void)id;
  (void)ctx;
  free(((const center_record *)record)->text);
}
static void queued_reset(qa_seat_hud_messages *messages) {
  qa_event_ring_for_each_live(&messages->queued_centers, free_center_text,
                              NULL);
  qa_event_ring_reset(&messages->queued_centers);
}

qa_seat_hud_messages *qa_seat_hud_messages_create(qa_seat_id seat) {
  qa_seat_hud_messages *messages = calloc(1, sizeof *messages);
  if (messages == NULL) return NULL;
  messages->seat = seat;
  if (!qa_event_ring_init(&messages->queued_centers, 256,
                          sizeof(center_record))) {
    free(messages);
    return NULL;
  }
  return messages;
}
void qa_seat_hud_messages_destroy(qa_seat_hud_messages *messages) {
  if (messages == NULL) return;
  for (size_t i = 0; i < messages->notice_count; i++)
    free(messages->notices[i].text);
  free(messages->notices);
  if (messages->has_center) free(messages->center.text);
  queued_reset(messages);
  qa_event_ring_destroy(&messages->queued_centers);
  free(messages->points);
  free(messages);
}
qa_seat_id qa_seat_hud_messages_get_seat(const qa_seat_hud_messages *messages) {
  return messages->seat;
}
void qa_seat_hud_messages_set_source_points(
    qa_seat_hud_messages *messages, qa_seat_id seat,
    const qa_ui_hud_point_of_interest *points, size_t point_count) {
  require_seat(messages, seat);
  messages->source_points = points;
  messages->source_point_count = point_count;
}
void qa_seat_hud_messages_notify(qa_seat_hud_messages *messages,
                                 qa_seat_id seat, const char *text, bool chat,
                                 qa_source_time starts,
                                 qa_source_time duration) {
  require_seat(messages, seat);
  if (messages->notice_count == messages->notice_capacity) {
    size_t capacity =
        messages->notice_capacity != 0 ? messages->notice_capacity * 2 : 8;
    notice_record *grown =
        realloc(messages->notices, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    messages->notices = grown;
    messages->notice_capacity = capacity;
  }
  notice_record *notice = &messages->notices[messages->notice_count++];
  notice->sequence = messages->sequence++;
  notice->text = xstrdup(text);
  notice->chat = chat;
  notice->starts = starts;
  notice->duration = duration;
}
void qa_seat_hud_messages_center_print(qa_seat_hud_messages *messages,
                                       qa_seat_id seat, const char *text,
                                       qa_source_time starts,
                                       qa_source_time duration, bool instant,
                                       double character_milliseconds) {
  require_seat(messages, seat);
  if (instant) {
    queued_reset(messages);
    if (messages->has_center) free(messages->center.text);
    messages->center = (center_record){ xstrdup(text), starts, duration,
                                        true, false, 0 };
    messages->has_center = true;
    return;
  }
  const center_record *newest =
      qa_event_ring_newest(&messages->queued_centers);
  const center_record *prior =
      newest != NULL ? newest
                     : (messages->has_center ? &messages->center : NULL);
  double start = max_d(source_ms(starts),
                       prior == NULL ? 0
                                     : source_ms(prior->starts) +
                                           source_ms(prior->duration));
  center_record print;
  print.text = xstrdup(text);
  print.starts = (qa_source_time){ QA_SOURCE_TIME_MILLISECONDS, start };
  print.duration = (qa_source_time){
    QA_SOURCE_TIME_MILLISECONDS,
    source_ms(duration) + (double)codepoints(text) * character_milliseconds
  };
  print.instant = false;
  print.has_character_milliseconds = true;
  print.character_milliseconds = character_milliseconds;
  if (!messages->has_center) {
    messages->center = print;
    messages->has_center = true;
  } else {
    qa_event_ring_admit(&messages->queued_centers, &print);
  }
}
void qa_seat_hud_messages_clear_notify(qa_seat_hud_messages *messages) {
  for (size_t i = 0; i < messages->notice_count; i++)
    free(messages->notices[i].text);
  messages->notice_count = 0;
}
void qa_seat_hud_messages_clear_center_print(qa_seat_hud_messages *messages) {
  if (messages->has_center) free(messages->center.text);
  messages->has_center = false;
  queued_reset(messages);
}
void qa_seat_hud_messages_clear(qa_seat_hud_messages *messages) {
  qa_seat_hud_messages_clear_notify(messages);
  qa_seat_hud_messages_clear_center_print(messages);
  messages->point_count = 0;
  messages->source_points = NULL;
  messages->source_point_count = 0;
}
bool qa_seat_hud_messages_add_point(qa_seat_hud_messages *messages,
                                    qa_seat_id seat,
                                    qa_ui_hud_point_of_interest point,
                                    double now_milliseconds, double capacity) {
  require_seat(messages, seat);
  long index = -1;
  if (point.id != 0)
    for (size_t i = 0; i < messages->point_count; i++)
      if (messages->points[i].id == point.id) {
        index = (long)i;
        break;
      }
  if (index < 0)
    for (size_t i = 0; i < messages->point_count; i++)
      if (messages->points[i].expires_milliseconds <= now_milliseconds) {
        index = (long)i;
        break;
      }
  if (index < 0 && (double)messages->point_count < capacity) {
    if (messages->point_count == messages->point_capacity) {
      size_t grown_capacity =
          messages->point_capacity != 0 ? messages->point_capacity * 2 : 8;
      qa_ui_hud_point_of_interest *grown =
          realloc(messages->points, grown_capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      messages->points = grown;
      messages->point_capacity = grown_capacity;
    }
    messages->points[messages->point_count++] = point;
    return true;
  }
  if (index < 0) {
    double oldest = INFINITY;
    for (size_t i = 0; i < messages->point_count; i++)
      if (messages->points[i].id == 0 &&
          messages->points[i].expires_milliseconds < oldest) {
        oldest = messages->points[i].expires_milliseconds;
        index = (long)i;
      }
  }
  if (index < 0) return false;
  messages->points[index] = point;
  return true;
}
void qa_seat_hud_messages_remove_point(qa_seat_hud_messages *messages,
                                       double id) {
  if (id == 0) return;
  for (size_t i = 0; i < messages->point_count; i++)
    if (messages->points[i].id == id) {
      memmove(&messages->points[i], &messages->points[i + 1],
              (messages->point_count - i - 1) * sizeof *messages->points);
      messages->point_count--;
      return;
    }
}
void qa_seat_hud_active_free(qa_seat_hud_active *result) {
  if (result == NULL) return;
  free(result->notifications);
  free(result->points);
  memset(result, 0, sizeof *result);
}
bool qa_seat_hud_messages_active(qa_seat_hud_messages *messages,
                                 double now_milliseconds,
                                 qa_seat_hud_active *out) {
  memset(out, 0, sizeof *out);
  size_t kept = 0;
  for (size_t i = 0; i < messages->notice_count; i++) {
    notice_record *notice = &messages->notices[i];
    if (source_ms(notice->starts) + source_ms(notice->duration) >
        now_milliseconds) {
      messages->notices[kept++] = *notice;
    } else {
      free(notice->text);
    }
  }
  messages->notice_count = kept;
  while (messages->has_center &&
         source_ms(messages->center.starts) +
                 source_ms(messages->center.duration) <=
             now_milliseconds) {
    free(messages->center.text);
    center_record next;
    if (qa_event_ring_shift(&messages->queued_centers, &next)) {
      messages->center = next;
    } else {
      messages->has_center = false;
    }
  }
  size_t alive = 0;
  for (size_t i = 0; i < messages->notice_count; i++)
    if (time_alive(messages->notices[i].starts, messages->notices[i].duration,
                   now_milliseconds))
      alive++;
  if (alive > 0) {
    out->notifications = malloc(alive * sizeof *out->notifications);
    if (out->notifications == NULL) return false;
    for (size_t i = 0; i < messages->notice_count; i++) {
      notice_record *notice = &messages->notices[i];
      if (!time_alive(notice->starts, notice->duration, now_milliseconds))
        continue;
      out->notifications[out->notification_count++] = (qa_ui_notification){
        notice->sequence, notice->text, notice->chat, notice->starts,
        notice->duration
      };
    }
  }
  if (messages->has_center &&
      time_alive(messages->center.starts, messages->center.duration,
                 now_milliseconds)) {
    out->has_center_print = true;
    out->center_print = (qa_center_print_state){
      messages->center.text, messages->center.starts,
      messages->center.duration, messages->center.instant,
      messages->center.has_character_milliseconds,
      messages->center.character_milliseconds
    };
  }
  size_t point_total = 0;
  for (size_t i = 0; i < messages->point_count; i++)
    if (messages->points[i].expires_milliseconds > now_milliseconds)
      point_total++;
  for (size_t i = 0; i < messages->source_point_count; i++)
    if (messages->source_points[i].expires_milliseconds > now_milliseconds)
      point_total++;
  if (point_total > 0) {
    out->points = malloc(point_total * sizeof *out->points);
    if (out->points == NULL) {
      qa_seat_hud_active_free(out);
      return false;
    }
    for (size_t i = 0; i < messages->point_count; i++)
      if (messages->points[i].expires_milliseconds > now_milliseconds)
        out->points[out->point_count++] = messages->points[i];
    for (size_t i = 0; i < messages->source_point_count; i++)
      if (messages->source_points[i].expires_milliseconds > now_milliseconds)
        out->points[out->point_count++] = messages->source_points[i];
  }
  return true;
}

/* --- drawCommonHud --- */

void qa_ui_hud_commands_free(qa_ui_hud_commands *result) {
  if (result == NULL) return;
  free(result->commands);
  for (size_t i = 0; i < result->owned_count; i++) free(result->owned[i]);
  free(result->owned);
  memset(result, 0, sizeof *result);
}

typedef struct pending_command {
  qa_ui_draw_command command;
  qa_vec2 anchor;
  double scale;
  bool has_transform;
  qa_ui_transform transform;
} pending_command;

typedef struct draw_state {
  qa_ui_hud_commands *out;
  pending_command *pending;
  size_t pending_count, pending_capacity;
  qa_vec2 anchor;
  double group_scale;
  bool ok;
} draw_state;

static char *result_dup(draw_state *s, const char *text) {
  char *copy = xstrdup(text);
  if (s->out->owned_count == s->out->owned_capacity) {
    size_t capacity =
        s->out->owned_capacity != 0 ? s->out->owned_capacity * 2 : 16;
    char **grown = realloc(s->out->owned, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    s->out->owned = grown;
    s->out->owned_capacity = capacity;
  }
  s->out->owned[s->out->owned_count++] = copy;
  return copy;
}
static void result_adopt_string(qa_ui_hud_commands *out, char *text) {
  if (out->owned_count == out->owned_capacity) {
    size_t capacity = out->owned_capacity != 0 ? out->owned_capacity * 2 : 16;
    char **grown = realloc(out->owned, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    out->owned = grown;
    out->owned_capacity = capacity;
  }
  out->owned[out->owned_count++] = text;
}
static void push_pending(draw_state *s, qa_ui_draw_command command,
                         qa_vec2 anchor, double scale, bool has_transform,
                         qa_ui_transform transform) {
  if (s->pending_count == s->pending_capacity) {
    size_t capacity = s->pending_capacity != 0 ? s->pending_capacity * 2 : 32;
    pending_command *grown = realloc(s->pending, capacity * sizeof *grown);
    if (grown == NULL) {
      s->ok = false;
      return;
    }
    s->pending = grown;
    s->pending_capacity = capacity;
  }
  s->pending[s->pending_count++] =
      (pending_command){ command, anchor, scale, has_transform, transform };
}
static bool push_result(qa_ui_hud_commands *out, qa_ui_draw_command command,
                        size_t *capacity) {
  if (out->count == *capacity) {
    size_t grown_capacity = *capacity != 0 ? *capacity * 2 : 32;
    qa_ui_draw_command *grown =
        realloc(out->commands, grown_capacity * sizeof *grown);
    if (grown == NULL) return false;
    out->commands = grown;
    *capacity = grown_capacity;
  }
  out->commands[out->count++] = command;
  return true;
}
static qa_ui_draw_command fill_command(qa_rect rect, qa_vec4 color) {
  qa_ui_draw_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_UI_DRAW_FILL;
  command.rect = rect;
  command.color = color;
  return command;
}
static qa_ui_draw_command image_command(qa_resource_id resource, qa_rect rect,
                                        qa_vec4 color) {
  qa_ui_draw_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_UI_DRAW_IMAGE;
  command.resource = resource;
  command.rect = rect;
  command.tex_coords[0] = (qa_vec2){ 0, 0 };
  command.tex_coords[1] = (qa_vec2){ 1, 1 };
  command.color = color;
  return command;
}
static qa_ui_draw_command text_command(const char *text, qa_vec2 origin,
                                       qa_resource_id font, double scale,
                                       qa_vec4 color, const char *align) {
  qa_ui_draw_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_UI_DRAW_TEXT;
  command.origin = origin;
  command.text = text;
  command.font = font;
  command.scale = scale;
  command.color = color;
  command.align = align;
  command.shadow = true;
  return command;
}

typedef struct hud_draw_ctx {
  draw_state *s;
  const qa_ui_common_hud_draw_options *options;
  qa_ui_skin skin;
  qa_vec4 color, accent, background;
  double text_scale, line_height;
  double cap_height; /* skin.capInk?.height with TS defaults applied */
  double cap_top;
} hud_draw_ctx;

static const char *hud_localize(hud_draw_ctx *h, const char *text) {
  const char *value = h->options->localize(h->options->localize_ctx, text);
  return value != NULL ? value : "";
}
static double hud_measure(hud_draw_ctx *h, const char *text, double scale) {
  if (h->options->measure_text != NULL)
    return h->options->measure_text(h->options->measure_ctx, text, scale);
  return (double)codepoints(text) * 8 * scale;
}
/* TS text() helper (localizes). */
static void hud_text(hud_draw_ctx *h, const char *value, double x, double y,
                     qa_vec4 tint, const char *align) {
  qa_ui_draw_command command = text_command(
      result_dup(h->s, hud_localize(h, value)), (qa_vec2){ x, y },
      h->skin.font, h->text_scale, tint, align);
  push_pending(h->s, command, h->s->anchor, h->s->group_scale, false,
               (qa_ui_transform){ 0, 0, 1 });
}
static void hud_image(hud_draw_ctx *h, qa_resource_id resource, qa_rect rect,
                      qa_vec4 tint) {
  push_pending(h->s, image_command(resource, rect, tint), h->s->anchor,
               h->s->group_scale, false, (qa_ui_transform){ 0, 0, 1 });
}
static void hud_fill(hud_draw_ctx *h, qa_rect rect, qa_vec4 tint) {
  push_pending(h->s, fill_command(rect, tint), h->s->anchor, h->s->group_scale,
               false, (qa_ui_transform){ 0, 0, 1 });
}
static double fallback_caption_measure(void *ctx, const char *text,
                                       double scale) {
  (void)ctx;
  return (double)codepoints(text) * 8 * scale;
}

bool qa_ui_draw_common_hud(qa_ui_draw_context context,
                           const qa_ui_common_hud_data *data,
                           const qa_ui_common_hud_draw_options *options,
                           qa_ui_hud_commands *out) {
  memset(out, 0, sizeof *out);
  if (!qa_same_seat(data->seat, context.binding.seat) ||
      !qa_same_seat(data->seat, qa_seat_hud_messages_get_seat(options->messages)))
    qa_range_error("HUD frame belongs to another seat");
  if (!data->visible) return true;
  qa_ui_preference_values preferences = options->preferences;
  qa_ui_skin skin = options->skin;
  draw_state s;
  memset(&s, 0, sizeof s);
  s.out = out;
  s.anchor = (qa_vec2){ 320, 240 };
  s.group_scale = preferences.hud_scale * context.binding.hud_scale;
  s.ok = true;
  qa_ui_skin_colors palette = qa_ui_accessible_colors(
      skin.colors, preferences.high_contrast, preferences.color_mode);
  hud_draw_ctx h;
  memset(&h, 0, sizeof h);
  h.s = &s;
  h.options = options;
  h.skin = skin;
  h.color = palette.text;
  h.accent = palette.accent;
  h.background = palette.panel;
  h.text_scale = skin.font_scale * preferences.text_scale;
  h.line_height = skin.line_height * preferences.text_scale;
  h.cap_height = skin.has_cap_ink ? skin.cap_ink_height : 8;
  h.cap_top = skin.has_cap_ink ? skin.cap_ink_top : 0;
  double status_count = (double)data->vital_count +
                        (data->weapon == NULL || data->weapon->native_status
                             ? 0
                             : 1);
  status_layout status;
  if (!compute_status_layout(context, status_count, preferences.hud_scale,
                             h.text_scale, h.cap_height, &status))
    return false;
  qa_seat_hud_active state;
  if (!qa_seat_hud_messages_active(options->messages,
                                   context.time_milliseconds, &state)) {
    free(status.rects);
    return false;
  }
  /* crosshair + hit marker */
  if (preferences.crosshair && data->crosshair_visible && data->help == NULL &&
      data->inventory_is_null && data->wheel == NULL) {
    size_t first = s.pending_count;
    double size = preferences.crosshair_size;
    if (data->crosshair_image != NULL) {
      hud_image(&h, data->crosshair_image,
                (qa_rect){ 320 - size / 2, 240 - size / 2, size, size },
                data->crosshair_color);
    } else {
      hud_fill(&h, (qa_rect){ 320 - size / 2, 239, size, 2 },
               data->crosshair_color);
      hud_fill(&h, (qa_rect){ 319, 240 - size / 2, 2, size },
               data->crosshair_color);
    }
    if (data->has_hit_marker &&
        data->hit_marker_expires_milliseconds > context.time_milliseconds &&
        !preferences.reduced_flashes) {
      double opacity = max_d(
          0, min_d(1, (data->hit_marker_expires_milliseconds -
                       context.time_milliseconds) /
                          150));
      const double signs[2] = { -1, 1 };
      for (size_t xi = 0; xi < 2; xi++)
        for (size_t yi = 0; yi < 2; yi++) {
          qa_vec4 tint = h.accent;
          tint.w = opacity;
          hud_fill(&h,
                   (qa_rect){ 320 + signs[xi] * (size + 2) - 2,
                              240 + signs[yi] * (size + 2) - 2, 4, 4 },
                   tint);
        }
    }
    if (options->camera != NULL) {
      qa_rect area = options->camera->viewport;
      double scale = qa_ui_fit(context.binding.safe_area, 1).scale *
                     s.group_scale;
      qa_ui_transform transform = {
        area.x + area.width / 2 - 320 * scale,
        area.y + area.height / 2 - 240 * scale, scale
      };
      for (size_t i = first; i < s.pending_count; i++) {
        s.pending[i].has_transform = true;
        s.pending[i].transform = transform;
      }
    }
  }
  /* vitals */
  if (data->vital_count > 0) {
    s.anchor = (qa_vec2){ 320, 480 };
    for (size_t index = 0; index < data->vital_count; index++) {
      if (index >= status.count) continue;
      const qa_ui_hud_value *vital = &data->vitals[index];
      qa_rect rect = status.rects[index];
      double x = rect.x;
      bool compact = rect.height == 32;
      push_pending(&s, fill_command(rect, h.background), s.anchor,
                   s.group_scale, true, status.transform);
      if (vital->icon != NULL)
        push_pending(
            &s,
            image_command(vital->icon,
                          (qa_rect){ x + 6, rect.y + 8, 24, 24 }, h.color),
            s.anchor, s.group_scale, true, status.transform);
      double scale = compact ? status.minimum_text_scale : h.text_scale;
      char *label = xstrdup(hud_localize(&h, vital->label));
      char *value = js_number(vital->value);
      size_t full_length = strlen(label) + strlen(value) + 2;
      char *full = malloc(full_length);
      if (full == NULL) qa_range_error("Out of memory");
      snprintf(full, full_length, "%s %s", label, value);
      double left = x + (vital->icon == NULL ? 4 : 34);
      double available = rect.x + rect.width - 4 - left;
      qa_ui_hud_status_rows rows =
          qa_ui_hud_status_rows_for(h.text_scale, h.cap_height);
      const char *lines[2];
      size_t line_count;
      if (compact) {
        if (hud_measure(&h, full, scale) <= available) {
          lines[0] = full;
          line_count = 1;
        } else {
          lines[0] = label;
          lines[1] = value;
          line_count = 2;
        }
      } else {
        lines[0] = value;
        lines[1] = label;
        line_count = 2;
      }
      for (size_t row = 0; row < line_count; row++) {
        const char *line = lines[row];
        double requested_scale =
            compact ? scale : h.text_scale * (row == 0 ? 1.5 : 0.8);
        double row_scale = requested_scale;
        if (!compact && row == 0) {
          double unit = options->measure_text != NULL
                            ? options->measure_text(options->measure_ctx, line,
                                                    1)
                            : (double)codepoints(line) * 8;
          row_scale = min_d(requested_scale, available / max_d(1, unit));
        }
        size_t chars = codepoints(line);
        char *shown = xstrdup(line);
        while (chars > 0) {
          free(shown);
          shown = xdup_range(line, codepoint_prefix_bytes(line, chars));
          double measured = options->measure_text != NULL
                                ? options->measure_text(options->measure_ctx,
                                                        shown, row_scale)
                                : (double)codepoints(shown) * 8 * row_scale;
          if (measured <= available) break;
          chars--;
        }
        if (chars == 0) {
          free(shown);
          shown = xstrdup("");
        }
        double y = rect.y + (compact
                                 ? 4 + (double)row * 14 - h.cap_top * scale
                                 : (row == 0 ? 4 : rows.label_top) -
                                       h.cap_top * row_scale);
        qa_ui_draw_command command = text_command(
            result_dup(&s, shown), (qa_vec2){ left, y }, skin.font, row_scale,
            vital->warning ? h.accent : h.color, "left");
        free(shown);
        push_pending(&s, command, s.anchor, s.group_scale, true,
                     status.transform);
      }
      free(label);
      free(value);
      free(full);
    }
  }
  /* weapon */
  if (data->weapon != NULL) {
    double native_height =
        qa_ui_hud_status_rows_for(h.text_scale, h.cap_height).height;
    bool has_rect;
    qa_rect rect;
    if (data->weapon->native_status) {
      rect = (qa_rect){ 8, 476 - native_height, 152, native_height };
      has_rect = true;
    } else if (data->vital_count < status.count) {
      rect = status.rects[data->vital_count];
      has_rect = true;
    } else {
      has_rect = false;
      rect = (qa_rect){ 0, 0, 0, 0 };
    }
    if (has_rect) {
      s.anchor = (qa_vec2){ data->weapon->native_status ? 0 : 320, 480 };
      qa_ui_skin weapon_skin = skin;
      weapon_skin.colors.text = h.color;
      weapon_skin.colors.accent = h.accent;
      weapon_skin.colors.panel = h.background;
      qa_ui_weapon_commands weapon_commands;
      if (!qa_ui_draw_weapon_hud(data->weapon, rect, weapon_skin, h.text_scale,
                                 data->weapon->native_status
                                     ? 0
                                     : status.minimum_text_scale,
                                 &weapon_commands))
        s.ok = false;
      else {
        for (size_t i = 0; i < weapon_commands.count; i++) {
          if (data->weapon->native_status)
            push_pending(&s, weapon_commands.commands[i], s.anchor,
                         s.group_scale, false, (qa_ui_transform){ 0, 0, 1 });
          else
            push_pending(&s, weapon_commands.commands[i], s.anchor,
                         s.group_scale, true, status.transform);
        }
        for (size_t i = 0; i < weapon_commands.owned_count; i++)
          result_adopt_string(out, weapon_commands.owned[i]);
        weapon_commands.owned_count = 0;
        qa_ui_weapon_commands_free(&weapon_commands);
      }
    }
  }
  /* health bars */
  s.anchor = (qa_vec2){ 320, 0 };
  for (size_t index = 0; index < data->health_bar_count; index++) {
    const qa_ui_hud_health_bar *bar = &data->health_bars[index];
    double y = 24 + (double)index * (h.line_height + 16);
    hud_text(&h, bar->label, 320, y, h.color, "center");
    hud_fill(&h, (qa_rect){ 160, y + h.line_height + 2, 320, 8 }, h.background);
    double fraction =
        max_d(0, min_d(1, bar->maximum <= 0 ? 0 : bar->value / bar->maximum));
    hud_fill(&h,
             (qa_rect){ 160, y + h.line_height + 2, 320 * fraction, 8 },
             bar->color);
  }
  /* notifications */
  s.anchor = (qa_vec2){ 0, 0 };
  {
    size_t start = state.notification_count > 6 ? state.notification_count - 6
                                                : 0;
    for (size_t i = start; i < state.notification_count; i++) {
      const qa_ui_notification *notice = &state.notifications[i];
      hud_text(&h, notice->text, 12,
               12 + (double)(i - start) * h.line_height,
               notice->chat ? h.accent : h.color, "left");
    }
  }
  /* center print */
  s.anchor = (qa_vec2){ 320, 240 };
  if (state.has_center_print) {
    const qa_center_print_state *print = &state.center_print;
    double elapsed = context.time_milliseconds - source_ms(print->starts);
    char *value;
    if (print->instant) {
      value = xstrdup(print->text);
    } else {
      double per_character =
          print->has_character_milliseconds ? print->character_milliseconds
                                            : 125;
      double shown_count = floor(elapsed / per_character);
      size_t chars = shown_count > 0 ? (size_t)shown_count : 0;
      size_t total_chars = codepoints(print->text);
      if (chars > total_chars) chars = total_chars;
      value = xdup_range(print->text,
                         codepoint_prefix_bytes(print->text, chars));
    }
    size_t line_count = 1;
    for (const char *p = value; *p != '\0'; p++)
      if (*p == '\n') line_count++;
    double start_y = 160 - (double)line_count * h.line_height / 2;
    const char *cursor = value;
    size_t line_index = 0;
    for (;;) {
      const char *newline = strchr(cursor, '\n');
      size_t length = newline != NULL ? (size_t)(newline - cursor)
                                      : strlen(cursor);
      char *line = xdup_range(cursor, length);
      hud_text(&h, line, 320, start_y + (double)line_index * h.line_height,
               h.color, "center");
      free(line);
      line_index++;
      if (newline == NULL) break;
      cursor = newline + 1;
    }
    free(value);
  }
  /* damage indicators */
  if (options->camera != NULL && !preferences.reduced_flashes) {
    const qa_scene_camera *camera = options->camera;
    for (size_t i = 0; i < data->damage_indicator_count; i++) {
      const qa_ui_hud_damage_indicator *damage = &data->damage_indicators[i];
      if (damage->expires_milliseconds <= context.time_milliseconds) continue;
      qa_vec3 delta = damage->is_direction
                          ? (qa_vec3){ -damage->direction.x,
                                       -damage->direction.y,
                                       -damage->direction.z }
                          : (qa_vec3){ damage->origin.x - camera->origin.x,
                                       damage->origin.y - camera->origin.y,
                                       damage->origin.z - camera->origin.z };
      qa_vec3 axis0 = camera->axis.vectors[0];
      qa_vec3 axis1 = camera->axis.vectors[1];
      double horizontal =
          -(delta.x * axis1.x + delta.y * axis1.y + delta.z * axis1.z);
      double vertical =
          delta.x * axis0.x + delta.y * axis0.y + delta.z * axis0.z;
      double length = hypot(horizontal, vertical);
      if (length == 0) length = 1;
      qa_vec4 tint;
      tint.x = damage->is_direction ? damage->color.x : 1;
      tint.y = damage->is_direction ? damage->color.y : 0.15;
      tint.z = damage->is_direction ? damage->color.z : 0.05;
      tint.w = min_d(1, (damage->expires_milliseconds -
                         context.time_milliseconds) /
                            (damage->is_direction ? 1000 : 400));
      if (damage->is_direction && damage->has_picture) {
        double yaw = atan2(axis0.y, axis0.x) -
                     atan2(damage->direction.y, damage->direction.x) - M_PI;
        double width = min_d(damage->picture_width, 3 * damage->amount);
        double height = damage->picture_height;
        double radius =
            (preferences.crosshair ? preferences.crosshair_size : 0) +
            height / 2;
        hud_image(&h, damage->picture_image,
                  (qa_rect){ 320 + radius * sin(yaw) - width / 2,
                             240 - radius * cos(yaw) - height / 2, width,
                             height },
                  tint);
        continue;
      }
      for (int step = 0; step < 3; step++)
        hud_fill(&h,
                 (qa_rect){ 317 + horizontal / length * (55 + step * 7),
                            237 - vertical / length * (55 + step * 7), 6, 6 },
                 tint);
    }
  }
  /* pickup */
  if (data->has_pickup &&
      data->pickup_expires_milliseconds > context.time_milliseconds) {
    s.anchor = (qa_vec2){ 320, 480 };
    if (data->pickup_icon != NULL)
      hud_image(&h, data->pickup_icon, (qa_rect){ 184, 346, 28, 28 }, h.color);
    hud_text(&h, data->pickup_name, 320, 350, h.accent, "center");
  }
  /* inventory */
  if (!data->inventory_is_null) {
    hud_fill(&h, (qa_rect){ 128, 72, 384, 328 }, h.background);
    hud_text(&h, "Inventory", 320, 88, h.accent, "center");
    long selected_index = -1;
    for (size_t i = 0; i < data->inventory_count; i++)
      if (data->inventory[i].selected) {
        selected_index = (long)i;
        break;
      }
    double selected = selected_index > 0 ? (double)selected_index : 0;
    double rows_d = floor(270 / h.line_height);
    if (rows_d < 1) rows_d = 1;
    double start_d = min_d((double)data->inventory_count - rows_d,
                           selected - floor(rows_d / 2));
    if (start_d < 0) start_d = 0;
    size_t start = (size_t)start_d;
    size_t rows = (size_t)rows_d;
    size_t end = start + rows;
    if (end > data->inventory_count) end = data->inventory_count;
    for (size_t i = start; i < end; i++) {
      const qa_ui_hud_inventory_item *item = &data->inventory[i];
      double y = 114 + (double)(i - start) * h.line_height;
      if (item->selected)
        hud_fill(&h, (qa_rect){ 136, y, 368, h.line_height },
                 skin.colors.focused);
      hud_text(&h, item->binding != NULL ? item->binding : "", 144, y,
               h.accent, "left");
      if (item->icon != NULL)
        hud_image(&h, item->icon,
                  (qa_rect){ 188, y, h.line_height, h.line_height }, h.color);
      hud_text(&h, item->label, 212, y, item->selected ? h.accent : h.color,
               "left");
      char *count = js_number(item->count);
      hud_text(&h, count, 494, y, h.color, "right");
      free(count);
    }
  }
  /* prompts */
  s.anchor = (qa_vec2){ 320, 480 };
  for (size_t index = 0; index < data->prompt_count; index++) {
    const qa_ui_hud_prompt *prompt = &data->prompts[index];
    double y = 394 - (double)(data->prompt_count - index - 1) *
                         (h.line_height + 4);
    if (prompt->icon != NULL)
      hud_image(&h, prompt->icon,
                (qa_rect){ 176, y - 2, h.line_height + 4, h.line_height + 4 },
                h.color);
    const char *action = hud_localize(&h, prompt->action);
    size_t total = strlen(prompt->binding) + strlen(action) + 4;
    char *label = malloc(total);
    if (label == NULL) qa_range_error("Out of memory");
    if (prompt->binding[0] == '\0')
      snprintf(label, total, "%s", action);
    else
      snprintf(label, total, "[%s] %s", prompt->binding, action);
    hud_text(&h, label, 320, y, h.accent, "center");
    free(label);
  }
  /* help */
  if (data->help != NULL) {
    s.anchor = (qa_vec2){ 320, 240 };
    s.group_scale = 1;
    hud_fill(&h, (qa_rect){ 48, 48, 544, 360 }, h.background);
    hud_text(&h, data->help->title, 320, 68, h.accent, "center");
    double y = 104;
    for (size_t i = 0; i < data->help->line_count; i++) {
      hud_text(&h, data->help->lines[i], 68, y, h.color, "left");
      y += h.line_height;
    }
    y += h.line_height;
    for (size_t i = 0; i < data->help->objective_count; i++) {
      const qa_ui_hud_objective *objective = &data->help->objectives[i];
      const char *text = hud_localize(&h, objective->text);
      size_t total = strlen(text) + 6;
      char *label = malloc(total);
      if (label == NULL) qa_range_error("Out of memory");
      snprintf(label, total, "%s %s", objective->complete ? "[x]" : "[ ]",
               text);
      hud_text(&h, label, 68, y, objective->complete ? h.color : h.accent,
               "left");
      free(label);
      y += h.line_height;
    }
  }
  /* wheel */
  if (data->wheel != NULL) {
    s.anchor = (qa_vec2){ 320, 240 };
    s.group_scale = min_d(1, preferences.hud_scale * context.binding.hud_scale);
    const qa_ui_wheel_presentation *wheel = data->wheel;
    double opacity = preferences.reduced_flashes ? 1 : wheel->opacity;
    qa_vec4 panel = h.background;
    panel.w = h.background.w * opacity;
    hud_fill(&h, (qa_rect){ 128, 48, 384, 384 }, panel);
    for (size_t index = 0; index < wheel->item_count; index++) {
      const qa_ui_wheel_item *item = &wheel->items[index];
      double angle = (double)index * 2 * M_PI / (double)wheel->item_count;
      double x = 320 + sin(angle) * 136, y = 240 - cos(angle) * 136;
      bool selected = wheel->selected != NULL && item->id != NULL &&
                      strcmp(item->id, wheel->selected) == 0;
      qa_vec4 tint = selected ? h.accent
                              : item->owned ? h.color : skin.colors.disabled;
      tint.w = opacity;
      qa_resource_id icon = selected ? (item->selected_icon != NULL
                                            ? item->selected_icon
                                            : item->icon)
                                     : item->icon;
      if (icon != NULL)
        hud_image(&h, icon, (qa_rect){ x - 20, y - 20, 40, 40 }, tint);
      else
        hud_text(&h, item->label, x, y - 8, tint, "center");
      if (item->has_count) {
        qa_vec4 count_tint = item->count <= item->warning_count ? h.accent
                                                                 : tint;
        count_tint.w = opacity;
        char *count = js_number(item->count);
        hud_text(&h, count, x, y + 24, count_tint, "center");
        free(count);
      }
      if (selected) hud_text(&h, item->label, 320, 220, tint, "center");
    }
    hud_fill(&h,
             (qa_rect){ 318 + wheel->cursor.x * 150,
                        238 + wheel->cursor.y * 150, 4, 4 },
             h.accent);
  }
  /* carousel */
  if (data->carousel != NULL) {
    s.anchor = (qa_vec2){ 320, 480 };
    s.group_scale = preferences.hud_scale * context.binding.hud_scale;
    const qa_ui_carousel_presentation *carousel = data->carousel;
    double width = min_d(48, 600 / max_d(1, (double)carousel->item_count));
    double start = 320 - (double)carousel->item_count * width / 2;
    for (size_t index = 0; index < carousel->item_count; index++) {
      const qa_ui_wheel_item *item = &carousel->items[index];
      double x = start + (double)index * width;
      bool selected = carousel->selected != NULL && item->id != NULL &&
                      strcmp(item->id, carousel->selected) == 0;
      if (selected)
        hud_fill(&h, (qa_rect){ x, 324, width - 2, 50 }, skin.colors.focused);
      qa_resource_id icon = selected ? (item->selected_icon != NULL
                                            ? item->selected_icon
                                            : item->icon)
                                     : item->icon;
      if (icon != NULL)
        hud_image(&h, icon,
                  (qa_rect){ x + 4, 328, width - 10, width - 10 }, h.color);
      if (item->has_count) {
        char *count = js_number(item->count);
        hud_text(&h, count, x + width / 2, 358,
                 selected ? h.accent : h.color, "center");
        free(count);
      }
    }
  }
  /* finalize */
  qa_ui_transform transform = qa_ui_fit(context.binding.safe_area, 1);
  qa_rect area = context.binding.safe_area;
  size_t result_capacity = 0;
  qa_ui_draw_command clip;
  memset(&clip, 0, sizeof clip);
  clip.kind = QA_UI_DRAW_CLIP;
  clip.rect = area;
  clip.has_clip_rect = true;
  if (!push_result(out, clip, &result_capacity)) s.ok = false;
  double timer_bottom = area.y + area.height - 42;
  for (size_t i = 0; i < status.count; i++) {
    double candidate = status.transform.y +
                       status.rects[i].y * status.transform.scale - 4;
    if (candidate < timer_bottom) timer_bottom = candidate;
  }
  qa_ui_skin powerup_skin = skin;
  powerup_skin.colors.text = h.color;
  powerup_skin.colors.panel = h.background;
  qa_ui_powerup_commands powerup_commands;
  if (!qa_ui_draw_powerup_timers(
          data->powerups, data->powerup_count, area, timer_bottom,
          powerup_skin,
          h.text_scale * transform.scale * preferences.hud_scale *
              context.binding.hud_scale,
          options->measure_text, options->measure_ctx, options->localize,
          options->localize_ctx, &powerup_commands)) {
    s.ok = false;
  } else {
    for (size_t i = 0; i < powerup_commands.count; i++)
      if (!push_result(out, powerup_commands.commands[i], &result_capacity))
        s.ok = false;
    for (size_t i = 0; i < powerup_commands.owned_count; i++)
      result_adopt_string(out, powerup_commands.owned[i]);
    powerup_commands.owned_count = 0;
    qa_ui_powerup_commands_free(&powerup_commands);
  }
  for (size_t i = 0; i < s.pending_count; i++) {
    pending_command *item = &s.pending[i];
    qa_ui_transform applied =
        item->has_transform
            ? item->transform
            : (qa_ui_transform){
                  transform.x + item->anchor.x * transform.scale *
                                    (1 - item->scale),
                  transform.y + item->anchor.y * transform.scale *
                                    (1 - item->scale),
                  transform.scale * item->scale };
    if (!push_result(out, qa_ui_transform_command(item->command, applied),
                     &result_capacity))
      s.ok = false;
  }
  if (options->camera != NULL) {
    const qa_scene_camera *camera = options->camera;
    qa_scene_view_projector project = qa_scene_create_view_projector(*camera);
    qa_rect viewport = camera->viewport;
    if (data->has_help_path) {
      const double distances[3] = { 0, 24, 48 };
      for (size_t i = 0; i < 3; i++) {
        qa_vec3 origin = data->help_path_origin;
        qa_vec3 direction = data->help_path_direction;
        qa_vec4 point = qa_scene_projector_project(
            project,
            (qa_vec3){ origin.x + direction.x * distances[i],
                       origin.y + direction.y * distances[i],
                       origin.z + direction.z * distances[i] });
        if (point.w > 0) {
          qa_ui_draw_command command = fill_command(
              (qa_rect){ viewport.x + (point.x / point.w * 0.5 + 0.5) *
                                              viewport.width -
                                          3,
                         viewport.y + (-point.y / point.w * 0.5 + 0.5) *
                                              viewport.height -
                                          3,
                         6, 6 },
              h.accent);
          if (!push_result(out, command, &result_capacity)) s.ok = false;
        }
      }
    }
    for (size_t i = 0; i < state.point_count; i++) {
      const qa_ui_hud_point_of_interest *point = &state.points[i];
      qa_vec4 clip_point = qa_scene_projector_project(project, point->origin);
      double divisor = clip_point.w == 0 ? 1 : clip_point.w;
      double x = viewport.x +
                 (clip_point.x / divisor * 0.5 + 0.5) * viewport.width;
      double y = viewport.y +
                 (-clip_point.y / divisor * 0.5 + 0.5) * viewport.height;
      if (clip_point.w < 0) {
        x = viewport.x * 2 + viewport.width - x;
        y = viewport.y * 2 + viewport.height - y;
        if (y > viewport.y) {
          x = x < viewport.x + viewport.width / 2
                  ? viewport.x
                  : viewport.x + viewport.width;
          y = min_d(y, viewport.y + viewport.height * 0.75);
        }
      }
      double width = point->width * transform.scale;
      double height = point->height * transform.scale;
      double distance = hypot(x - viewport.x - viewport.width / 2,
                              y - viewport.y - viewport.height / 2);
      qa_vec4 color = point->color;
      color.w = point->color.w *
                (point->hide_on_aim
                     ? max_d(0.25, min_d(1, distance / max_d(1, width * 3)))
                     : 1);
      qa_ui_draw_command command = image_command(
          point->image,
          (qa_rect){ max_d(viewport.x,
                           min_d(viewport.x + viewport.width - width,
                                 x - width / 2)),
                     max_d(viewport.y,
                           min_d(viewport.y + viewport.height - height,
                                 y - height / 2)),
                     width, height },
          color);
      if (!push_result(out, command, &result_capacity)) s.ok = false;
    }
  }
  if (preferences.captions) {
    qa_ui_caption_commands caption_commands;
    qa_rect caption_area = { area.x + 8, area.y + area.height * 0.6,
                             area.width - 16, area.height * 0.22 };
    if (!qa_ui_get_caption_commands(
            data->captions, data->caption_count, caption_area, skin.font,
            h.text_scale * transform.scale,
            options->measure_text != NULL
                ? (qa_ui_measure_text_fn)options->measure_text
                : fallback_caption_measure,
            options->measure_text != NULL ? options->measure_ctx : NULL,
            h.cap_top, h.cap_height, &caption_commands)) {
      s.ok = false;
    } else {
      for (size_t i = 0; i < caption_commands.count; i++)
        if (!push_result(out, caption_commands.commands[i], &result_capacity))
          s.ok = false;
      for (size_t i = 0; i < caption_commands.owned_count; i++)
        result_adopt_string(out, caption_commands.owned[i]);
      caption_commands.owned_count = 0;
      qa_ui_caption_commands_free(&caption_commands);
    }
  }
  memset(&clip, 0, sizeof clip);
  clip.kind = QA_UI_DRAW_CLIP;
  clip.has_clip_rect = false;
  if (!push_result(out, clip, &result_capacity)) s.ok = false;
  free(s.pending);
  free(status.rects);
  qa_seat_hud_active_free(&state);
  if (!s.ok) {
    qa_ui_hud_commands_free(out);
    return false;
  }
  return true;
}

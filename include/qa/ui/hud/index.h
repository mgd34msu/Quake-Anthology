/*
 * Port of src/ui/hud/index.ts: hudVitalRects, hudVitalOccupiedRects,
 * the Hud* data shapes, emptyHudData, SeatHudMessages, and
 * drawCommonHud. The wheel/q1-wheel re-exports live in the
 * qa/ui/hud.h umbrella.
 * C accommodations:
 *  - SeatHudMessages is opaque (create/destroy replace the class);
 *    notify/centerPrint copy their text. setSourcePoints borrows
 *    the points array until the next set/clear/destroy (the TS
 *    retains the same reference). active() returns an owned
 *    snapshot whose strings borrow message-owned storage valid
 *    until the next messages call; free with _free.
 *  - The queued center prints use the landed qa_event_ring(256),
 *    exactly as the TS uses EventRing(256) (admit aborts full).
 *  - CommonHudData arrays are borrowed; TS null/undefined optionals
 *    are has_/NULL flags as noted per field. HudDamageIndicator is
 *    one struct with is_direction selecting the TS variant.
 *  - drawCommonHud returns an owned command result (free with
 *    _free); localize returns a borrowed string, measure_text NULL
 *    is the TS absent optional. Plain TS Errors abort via
 *    qa_range_error with the identical message.
 *  - Text truncation counts UTF-8 code points where the TS counts
 *    Array.from code points.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_HUD_INDEX_H
#define QA_UI_HUD_INDEX_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/time.h"
#include "qa/contracts/ui.h"
#include "qa/text/captions.h"
#include "qa/ui/common/layout.h"
#include "qa/ui/common/skin.h"
#include "qa/ui/hud/powerups.h"
#include "qa/ui/hud/weapon.h"
#include "qa/ui/hud/wheel.h"
#include "qa/ui/settings/index.h"

/** Owned rect array results; free with _free. */
typedef struct qa_ui_hud_rects {
  qa_rect *rects;
  size_t count;
} qa_ui_hud_rects;
void qa_ui_hud_rects_free(qa_ui_hud_rects *result);

/* TS hudVitalRects(count, scale, height = 42). False only on OOM. */
bool qa_ui_hud_vital_rects_for(double count, double scale, double height,
                               qa_ui_hud_rects *out);
/* TS hudVitalOccupiedRects(context, count, hudScale,
 * textScale = 1.5, capHeight = 8). False only on OOM. */
bool qa_ui_hud_vital_occupied_rects(qa_ui_draw_context context, double count,
                                    double hud_scale, double text_scale,
                                    double cap_height, qa_ui_hud_rects *out);

/** TS HudValue. */
typedef struct qa_ui_hud_value {
  const char *label;
  double value;
  qa_resource_id icon; /* nullable */
  bool warning;
} qa_ui_hud_value;

/** TS HudInventoryItem. */
typedef struct qa_ui_hud_inventory_item {
  const char *id;
  const char *label;
  double count;
  bool selected;
  const char *binding; /* nullable */
  qa_resource_id icon; /* nullable */
} qa_ui_hud_inventory_item;

/** TS HudPrompt. */
typedef struct qa_ui_hud_prompt {
  const char *action;
  const char *binding;
  qa_resource_id icon; /* nullable */
} qa_ui_hud_prompt;

/** TS HudHealthBar. */
typedef struct qa_ui_hud_health_bar {
  const char *id;
  const char *label;
  double value, maximum;
  qa_vec4 color;
} qa_ui_hud_health_bar;

/** TS HudHelp objective / HudHelp. */
typedef struct qa_ui_hud_objective {
  const char *text;
  bool complete;
} qa_ui_hud_objective;
typedef struct qa_ui_hud_help {
  const char *title;
  const char *const *lines;
  size_t line_count;
  const qa_ui_hud_objective *objectives;
  size_t objective_count;
} qa_ui_hud_help;

/** TS HudPointOfInterest. */
typedef struct qa_ui_hud_point_of_interest {
  double id;
  qa_vec3 origin;
  qa_resource_id image;
  double width, height;
  qa_vec4 color;
  bool hide_on_aim;
  double expires_milliseconds;
} qa_ui_hud_point_of_interest;

/** TS HudDamageIndicator. is_direction false is the origin variant;
 * has_picture marks the direction variant's optional picture. */
typedef struct qa_ui_hud_damage_indicator {
  bool is_direction;
  qa_vec3 origin;    /* origin variant */
  qa_vec3 direction; /* direction variant */
  bool has_picture;
  qa_resource_id picture_image;
  double picture_width, picture_height;
  double amount;
  qa_vec3 color;
  bool health, armor, shield;
  double expires_milliseconds;
} qa_ui_hud_damage_indicator;

/** TS CommonHudData. inventory NULL items with inventory_is_null is
 * TS null; help_path has_ covers TS undefined|null; pickup has_
 * covers TS undefined|null; hit_marker has_ covers TS null. */
typedef struct qa_ui_common_hud_data {
  const qa_active_powerup_timer *powerups;
  size_t powerup_count;
  const qa_ui_common_weapon_hud *weapon; /* optional */
  qa_seat_id seat;
  bool visible;
  const qa_ui_hud_value *vitals;
  size_t vital_count;
  bool inventory_is_null;
  const qa_ui_hud_inventory_item *inventory;
  size_t inventory_count;
  const qa_ui_hud_prompt *prompts;
  size_t prompt_count;
  const qa_ui_hud_health_bar *health_bars;
  size_t health_bar_count;
  const qa_ui_hud_help *help; /* nullable */
  const qa_active_caption *captions;
  size_t caption_count;
  const qa_ui_wheel_presentation *wheel;       /* nullable */
  const qa_ui_carousel_presentation *carousel; /* nullable */
  bool crosshair_visible;
  qa_vec4 crosshair_color;
  qa_resource_id crosshair_image; /* nullable */
  bool has_help_path;
  qa_vec3 help_path_origin, help_path_direction;
  const qa_ui_hud_damage_indicator *damage_indicators;
  size_t damage_indicator_count;
  bool has_pickup;
  const char *pickup_name;
  qa_resource_id pickup_icon; /* nullable */
  double pickup_expires_milliseconds;
  bool has_hit_marker;
  double hit_marker_damage, hit_marker_expires_milliseconds;
} qa_ui_common_hud_data;

/** TS emptyHudData(seat). */
qa_ui_common_hud_data qa_ui_empty_hud_data(qa_seat_id seat);

/** TS SeatHudMessages. */
typedef struct qa_seat_hud_messages qa_seat_hud_messages;
/* NULL only on OOM. */
qa_seat_hud_messages *qa_seat_hud_messages_create(qa_seat_id seat);
void qa_seat_hud_messages_destroy(qa_seat_hud_messages *messages);
/** TS messages.seat. Named with _get_: qa_seat_id is a type. */
qa_seat_id qa_seat_hud_messages_get_seat(const qa_seat_hud_messages *messages);
/* All seat-checked methods abort (TS Error) on another seat. */
void qa_seat_hud_messages_set_source_points(
    qa_seat_hud_messages *messages, qa_seat_id seat,
    const qa_ui_hud_point_of_interest *points, size_t point_count);
void qa_seat_hud_messages_notify(qa_seat_hud_messages *messages,
                                 qa_seat_id seat, const char *text, bool chat,
                                 qa_source_time starts, qa_source_time duration);
/* character_milliseconds is the TS argument (default 125); it is
 * only carried on non-instant prints, as in the TS. */
void qa_seat_hud_messages_center_print(qa_seat_hud_messages *messages,
                                       qa_seat_id seat, const char *text,
                                       qa_source_time starts,
                                       qa_source_time duration, bool instant,
                                       double character_milliseconds);
void qa_seat_hud_messages_clear_notify(qa_seat_hud_messages *messages);
void qa_seat_hud_messages_clear_center_print(qa_seat_hud_messages *messages);
void qa_seat_hud_messages_clear(qa_seat_hud_messages *messages);
/* TS addPoint(seat, point, now, capacity = 64). */
bool qa_seat_hud_messages_add_point(qa_seat_hud_messages *messages,
                                    qa_seat_id seat,
                                    qa_ui_hud_point_of_interest point,
                                    double now_milliseconds, double capacity);
void qa_seat_hud_messages_remove_point(qa_seat_hud_messages *messages,
                                       double id);

/** TS SeatHudMessages.active() result (owned arrays; strings borrow
 * message-owned storage). Free with _free. */
typedef struct qa_seat_hud_active {
  qa_ui_notification *notifications;
  size_t notification_count;
  bool has_center_print;
  qa_center_print_state center_print;
  qa_ui_hud_point_of_interest *points;
  size_t point_count;
} qa_seat_hud_active;
void qa_seat_hud_active_free(qa_seat_hud_active *result);
/* False only on OOM. */
bool qa_seat_hud_messages_active(qa_seat_hud_messages *messages,
                                 double now_milliseconds,
                                 qa_seat_hud_active *out);

/** TS CommonHudDrawOptions. camera NULL is TS null. */
typedef struct qa_ui_common_hud_draw_options {
  qa_ui_skin skin;
  qa_ui_hud_measure_fn measure_text; /* optional */
  void *measure_ctx;
  qa_ui_preference_values preferences;
  qa_seat_hud_messages *messages;
  const qa_scene_camera *camera; /* nullable */
  qa_ui_hud_localize_fn localize;
  void *localize_ctx;
} qa_ui_common_hud_draw_options;

/** Owned drawCommonHud result; free with _free. */
typedef struct qa_ui_hud_commands {
  qa_ui_draw_command *commands;
  size_t count;
  char **owned;
  size_t owned_count, owned_capacity;
} qa_ui_hud_commands;
void qa_ui_hud_commands_free(qa_ui_hud_commands *result);

/* TS drawCommonHud. Aborts (TS Error) on another seat's frame.
 * Returns false only on OOM. */
bool qa_ui_draw_common_hud(qa_ui_draw_context context,
                           const qa_ui_common_hud_data *data,
                           const qa_ui_common_hud_draw_options *options,
                           qa_ui_hud_commands *out);

#endif

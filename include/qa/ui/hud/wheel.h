/* Port of src/ui/hud/wheel.ts: weapon/powerup wheel and carousel
 * state adapted from q2repro client/wheel.c.
 * C accommodations: service items are borrowed until the next items
 * call; the wheel copies and sorts them. Presentations borrow
 * wheel-owned arrays valid until the next wheel call or destroy.
 * Mode is the exact TS string ("weapons"|"powerups").
 * GPL-2.0-or-later. */
#ifndef QA_UI_HUD_WHEEL_H
#define QA_UI_HUD_WHEEL_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/ui.h"

/** TS WheelItem. count NULL-flagged by has_count. */
typedef struct qa_ui_wheel_item {
  const char *id;
  double source_ordinal, sort_order;
  const char *label;
  bool owned, has_ammo;
  bool has_count;
  double count, warning_count;
  qa_resource_id icon;          /* nullable */
  qa_resource_id selected_icon; /* nullable */
} qa_ui_wheel_item;

typedef const char *qa_ui_wheel_mode; /* "weapons" | "powerups" */

/** TS WheelPresentation. */
typedef struct qa_ui_wheel_presentation {
  qa_ui_wheel_mode mode;
  const qa_ui_wheel_item *items;
  size_t item_count;
  const char *selected; /* nullable */
  double opacity;
  qa_vec2 cursor;
} qa_ui_wheel_presentation;

/** TS CarouselPresentation. */
typedef struct qa_ui_carousel_presentation {
  const qa_ui_wheel_item *items;
  size_t item_count;
  const char *selected; /* nullable */
} qa_ui_carousel_presentation;

/** TS WeaponWheelServices (one shared ctx). */
typedef struct qa_ui_weapon_wheel_services {
  void *ctx;
  qa_seat_id seat;
  const qa_ui_wheel_item *(*items)(void *ctx, qa_ui_wheel_mode mode,
                                   size_t *out_count);
  const char *(*active_item)(void *ctx); /* nullable */
  void (*select)(void *ctx, const char *id, qa_ui_wheel_mode mode,
                 qa_seat_id seat);
  double (*now)(void *ctx);
  void (*changed)(void *ctx, qa_seat_id seat);
} qa_ui_weapon_wheel_services;

/** TS WheelOptions. */
typedef struct qa_ui_wheel_options {
  double radius, selection_distance, fade_per_second;
  double carousel_timeout, carousel_lock;
  bool q2_slot_zero_deselect;
} qa_ui_wheel_options;

/** TS defaults. */
qa_ui_wheel_options qa_ui_wheel_default_options(void);

typedef struct qa_seat_weapon_wheel qa_seat_weapon_wheel;

/* NULL only on OOM. Aborts (RangeError) when radius <= 0 or the
 * selection distance is negative or not below the radius. A NULL
 * options pointer selects the TS defaults. */
qa_seat_weapon_wheel *qa_seat_weapon_wheel_create(
    const qa_ui_weapon_wheel_services *services,
    const qa_ui_wheel_options *options);
void qa_seat_weapon_wheel_destroy(qa_seat_weapon_wheel *wheel);

qa_seat_id qa_seat_weapon_wheel_get_seat(const qa_seat_weapon_wheel *wheel);
bool qa_seat_weapon_wheel_is_open(const qa_seat_weapon_wheel *wheel);
double qa_seat_weapon_wheel_time_scale(const qa_seat_weapon_wheel *wheel);
double qa_seat_weapon_wheel_weapon_lock_until(
    const qa_seat_weapon_wheel *wheel);
bool qa_seat_weapon_wheel_holster(const qa_seat_weapon_wheel *wheel);

bool qa_seat_weapon_wheel_open(qa_seat_weapon_wheel *wheel,
                               qa_ui_wheel_mode mode);
void qa_seat_weapon_wheel_close(qa_seat_weapon_wheel *wheel, bool select);
/* Aborts (TS Error) on another seat's event. */
bool qa_seat_weapon_wheel_input(qa_seat_weapon_wheel *wheel,
                                qa_seat_input_event event);
/** direction is -1 or 1, as in the TS. */
void qa_seat_weapon_wheel_cycle(qa_seat_weapon_wheel *wheel, int direction);

/** TS command() result. */
typedef struct qa_ui_wheel_command {
  bool holster, consume_attack;
} qa_ui_wheel_command;
qa_ui_wheel_command qa_seat_weapon_wheel_command(
    qa_seat_weapon_wheel *wheel, bool attack, double now_milliseconds);

void qa_seat_weapon_wheel_update(qa_seat_weapon_wheel *wheel,
                                 double now_milliseconds);
/** TS update() with the service clock. */
void qa_seat_weapon_wheel_update_now(qa_seat_weapon_wheel *wheel);

/** TS drawState(): has_wheel/has_carousel mark TS null. */
typedef struct qa_ui_wheel_draw_state {
  bool has_wheel;
  qa_ui_wheel_presentation wheel;
  bool has_carousel;
  qa_ui_carousel_presentation carousel;
} qa_ui_wheel_draw_state;
qa_ui_wheel_draw_state qa_seat_weapon_wheel_draw_state(
    qa_seat_weapon_wheel *wheel);

#endif

/* Port of src/ui/hud/wheel.ts. GPL-2.0-or-later. */
#include "qa/ui/hud/wheel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

enum wheel_state { WHEEL_CLOSED, WHEEL_OPEN };
enum carousel_state { CAROUSEL_CLOSED, CAROUSEL_OPEN, CAROUSEL_CLOSING };

struct qa_seat_weapon_wheel {
  qa_ui_weapon_wheel_services services;
  qa_ui_wheel_options options;
  enum wheel_state state;
  enum carousel_state carousel;
  double carousel_until;
  qa_ui_wheel_mode mode;
  char *selected;          /* nullable, owned */
  char *carousel_selected; /* nullable, owned */
  qa_vec2 position, analog;
  double deselect_time, opacity, last_update, lock_until;
  qa_ui_wheel_item *items; /* sorted copy, reused per call */
  size_t item_count, item_capacity;
  qa_ui_wheel_item *owned_items; /* filtered copy for carousel view */
  size_t owned_count, owned_capacity;
};

qa_ui_wheel_options qa_ui_wheel_default_options(void) {
  qa_ui_wheel_options out = { 180, 140, 3, 400, 300, true };
  return out;
}

static char *dup_or_null(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static void set_id(char **slot, const char *value) {
  char *copy = dup_or_null(value);
  if (value != NULL && copy == NULL) qa_range_error("Out of memory");
  free(*slot);
  *slot = copy;
}
static bool id_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

static int item_order(const void *a, const void *b) {
  const qa_ui_wheel_item *x = a, *y = b;
  double by_sort = x->sort_order - y->sort_order;
  if (by_sort != 0) return by_sort < 0 ? -1 : 1;
  double by_ordinal = x->source_ordinal - y->source_ordinal;
  if (by_ordinal != 0) return by_ordinal < 0 ? -1 : 1;
  return 0;
}

/* TS private items(mode): sorted copy in wheel storage. */
static const qa_ui_wheel_item *sorted_items(qa_seat_weapon_wheel *wheel,
                                            qa_ui_wheel_mode mode,
                                            size_t *out_count) {
  const qa_ui_wheel_item *source =
      wheel->services.items(wheel->services.ctx, mode, out_count);
  if (*out_count > wheel->item_capacity) {
    qa_ui_wheel_item *grown =
        realloc(wheel->items, *out_count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    wheel->items = grown;
    wheel->item_capacity = *out_count;
  }
  if (*out_count > 0)
    memcpy(wheel->items, source, *out_count * sizeof *source);
  qa_js_sort(wheel->items, *out_count, sizeof *wheel->items, item_order);
  wheel->item_count = *out_count;
  return wheel->items;
}

qa_seat_weapon_wheel *qa_seat_weapon_wheel_create(
    const qa_ui_weapon_wheel_services *services,
    const qa_ui_wheel_options *options) {
  qa_ui_wheel_options resolved =
      options != NULL ? *options : qa_ui_wheel_default_options();
  if (resolved.radius <= 0 || resolved.selection_distance < 0
      || resolved.selection_distance >= resolved.radius)
    qa_range_error("Wheel radius must exceed selection distance");
  qa_seat_weapon_wheel *wheel = calloc(1, sizeof *wheel);
  if (wheel == NULL) return NULL;
  wheel->services = *services;
  wheel->options = resolved;
  wheel->mode = "weapons";
  wheel->last_update = services->now(services->ctx);
  return wheel;
}

void qa_seat_weapon_wheel_destroy(qa_seat_weapon_wheel *wheel) {
  if (wheel == NULL) return;
  free(wheel->selected);
  free(wheel->carousel_selected);
  free(wheel->items);
  free(wheel->owned_items);
  free(wheel);
}

qa_seat_id qa_seat_weapon_wheel_get_seat(const qa_seat_weapon_wheel *wheel) {
  return wheel->services.seat;
}
bool qa_seat_weapon_wheel_is_open(const qa_seat_weapon_wheel *wheel) {
  return wheel->state == WHEEL_OPEN;
}
double qa_seat_weapon_wheel_time_scale(const qa_seat_weapon_wheel *wheel) {
  return qa_js_max(0.1, 1 - wheel->opacity);
}
double qa_seat_weapon_wheel_weapon_lock_until(
    const qa_seat_weapon_wheel *wheel) {
  return wheel->lock_until;
}
bool qa_seat_weapon_wheel_holster(const qa_seat_weapon_wheel *wheel) {
  return (wheel->state != WHEEL_CLOSED
          && strcmp(wheel->mode, "weapons") == 0)
      || wheel->carousel == CAROUSEL_OPEN;
}

bool qa_seat_weapon_wheel_open(qa_seat_weapon_wheel *wheel,
                               qa_ui_wheel_mode mode) {
  size_t count;
  sorted_items(wheel, mode, &count);
  if (count == 0) return false;
  wheel->mode = mode;
  wheel->state = WHEEL_OPEN;
  set_id(&wheel->selected, NULL);
  wheel->deselect_time = 0;
  wheel->position.x = 0; wheel->position.y = 0;
  wheel->analog.x = 0; wheel->analog.y = 0;
  wheel->services.changed(wheel->services.ctx, wheel->services.seat);
  return true;
}

void qa_seat_weapon_wheel_close(qa_seat_weapon_wheel *wheel, bool select) {
  if (wheel->state != WHEEL_OPEN) return;
  wheel->state = WHEEL_CLOSED;
  size_t count;
  const qa_ui_wheel_item *items = sorted_items(wheel, wheel->mode, &count);
  const qa_ui_wheel_item *found = NULL;
  for (size_t i = 0; i < count; i++)
    if (id_eq(items[i].id, wheel->selected)) { found = &items[i]; break; }
  if (select && found != NULL && found->owned)
    wheel->services.select(wheel->services.ctx, found->id, wheel->mode,
                           wheel->services.seat);
  wheel->services.changed(wheel->services.ctx, wheel->services.seat);
}

static void move_to(qa_seat_weapon_wheel *wheel, qa_vec2 position) {
  double distance = hypot(position.x, position.y);
  double factor = distance > wheel->options.radius
      ? wheel->options.radius / distance : 1;
  wheel->position.x = position.x * factor;
  wheel->position.y = position.y * factor;
}

bool qa_seat_weapon_wheel_input(qa_seat_weapon_wheel *wheel,
                                qa_seat_input_event event) {
  if (!qa_same_seat(event.seat, wheel->services.seat))
    qa_range_error("Weapon wheel input belongs to another seat");
  if (wheel->state == WHEEL_CLOSED) return false;
  if (strcmp(event.kind, "focus") == 0 && !event.focused) {
    qa_seat_weapon_wheel_close(wheel, false);
    return true;
  }
  if (strcmp(event.kind, "mouse-motion") == 0) {
    qa_vec2 next = { wheel->position.x + event.delta.x,
                     wheel->position.y + event.delta.y };
    move_to(wheel, next);
  } else if (strcmp(event.kind, "controller-axis") == 0
             && event.axis != NULL
             && (strcmp(event.axis, "right-x") == 0
                 || strcmp(event.axis, "right-y") == 0)) {
    if (strcmp(event.axis, "right-x") == 0) wheel->analog.x = event.value;
    else wheel->analog.y = event.value;
    qa_vec2 next = { wheel->analog.x * wheel->options.radius,
                     wheel->analog.y * wheel->options.radius };
    move_to(wheel, next);
  }
  return strcmp(event.kind, "mouse-motion") == 0
      || strcmp(event.kind, "controller-axis") == 0;
}

void qa_seat_weapon_wheel_cycle(qa_seat_weapon_wheel *wheel, int direction) {
  size_t count;
  const qa_ui_wheel_item *all = sorted_items(wheel, "weapons", &count);
  /* Filter owned into separate storage (all aliases wheel->items). */
  if (count > wheel->owned_capacity) {
    qa_ui_wheel_item *grown =
        realloc(wheel->owned_items, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    wheel->owned_items = grown;
    wheel->owned_capacity = count;
  }
  size_t owned = 0;
  for (size_t i = 0; i < count; i++)
    if (all[i].owned) wheel->owned_items[owned++] = all[i];
  wheel->owned_count = owned;
  if (owned == 0) {
    wheel->carousel = CAROUSEL_CLOSED;
    return;
  }
  if (wheel->carousel != CAROUSEL_OPEN)
    set_id(&wheel->carousel_selected,
           wheel->services.active_item(wheel->services.ctx));
  long start = -2;
  for (size_t i = 0; i < owned; i++)
    if (id_eq(wheel->owned_items[i].id, wheel->carousel_selected)) {
      start = (long)i;
      break;
    }
  if (start == -2) start = direction > 0 ? -1 : 0;
  for (size_t offset = 1; offset <= owned; offset++) {
    long idx = (start + (long)offset * direction + (long)owned)
        % (long)owned;
    const qa_ui_wheel_item *candidate = &wheel->owned_items[idx];
    if (candidate->has_ammo) {
      set_id(&wheel->carousel_selected, candidate->id);
      break;
    }
  }
  wheel->carousel = CAROUSEL_OPEN;
  wheel->carousel_until = wheel->services.now(wheel->services.ctx)
      + wheel->options.carousel_timeout;
  wheel->services.changed(wheel->services.ctx, wheel->services.seat);
}

qa_ui_wheel_command qa_seat_weapon_wheel_command(
    qa_seat_weapon_wheel *wheel, bool attack, double now_milliseconds) {
  if (wheel->carousel == CAROUSEL_CLOSING
      && now_milliseconds >= wheel->carousel_until)
    wheel->carousel = CAROUSEL_CLOSED;
  bool selecting = wheel->carousel == CAROUSEL_OPEN;
  if (wheel->carousel == CAROUSEL_OPEN
      && (attack || now_milliseconds >= wheel->carousel_until)) {
    size_t count;
    const qa_ui_wheel_item *items = sorted_items(wheel, "weapons", &count);
    const qa_ui_wheel_item *found = NULL;
    for (size_t i = 0; i < count; i++)
      if (id_eq(items[i].id, wheel->carousel_selected) && items[i].owned
          && items[i].has_ammo) {
        found = &items[i];
        break;
      }
    if (found != NULL
        && !id_eq(found->id,
                  wheel->services.active_item(wheel->services.ctx))) {
      wheel->services.select(wheel->services.ctx, found->id, "weapons",
                             wheel->services.seat);
      wheel->lock_until = now_milliseconds + wheel->options.carousel_lock;
      wheel->carousel = CAROUSEL_CLOSING;
      wheel->carousel_until = wheel->lock_until;
    } else {
      wheel->carousel = CAROUSEL_CLOSED;
    }
  }
  qa_ui_wheel_command out = {
    qa_seat_weapon_wheel_holster(wheel) || selecting,
    attack && selecting
  };
  return out;
}

void qa_seat_weapon_wheel_update(qa_seat_weapon_wheel *wheel,
                                 double now_milliseconds) {
  double elapsed = qa_js_max(0, now_milliseconds - wheel->last_update) / 1000;
  wheel->last_update = now_milliseconds;
  wheel->opacity = qa_js_max(0, qa_js_min(1, wheel->opacity + elapsed
      * wheel->options.fade_per_second
      * (qa_seat_weapon_wheel_is_open(wheel) ? 1 : -1)));
  if (!qa_seat_weapon_wheel_is_open(wheel)) return;
  size_t count;
  const qa_ui_wheel_item *items = sorted_items(wheel, wheel->mode, &count);
  if (count == 0) {
    qa_seat_weapon_wheel_close(wheel, false);
    return;
  }
  double distance = hypot(wheel->position.x, wheel->position.y);
  double slice = 3.141592653589793 * 2 / (double)count;
  char *prior = dup_or_null(wheel->selected);
  if (wheel->selected != NULL && prior == NULL)
    qa_range_error("Out of memory");
  if (distance > wheel->options.selection_distance) {
    for (size_t i = 0; i < count; i++) {
      if (!items[i].owned) continue;
      double angle = slice * (double)i;
      double dot = wheel->position.x / distance * sin(angle)
          - wheel->position.y / distance * cos(angle);
      if (dot > cos(slice / 2)) {
        set_id(&wheel->selected, items[i].id);
        wheel->deselect_time = 0;
      }
    }
  } else {
    long selected_index = -1;
    for (size_t i = 0; i < count; i++)
      if (id_eq(items[i].id, wheel->selected)) {
        selected_index = (long)i;
        break;
      }
    if ((wheel->options.q2_slot_zero_deselect ? selected_index != 0
                                              : selected_index >= 0)
        && wheel->deselect_time == 0)
      wheel->deselect_time = now_milliseconds + 200;
  }
  if (wheel->deselect_time != 0 && wheel->deselect_time < now_milliseconds) {
    set_id(&wheel->selected, NULL);
    wheel->deselect_time = 0;
  }
  if (!id_eq(prior, wheel->selected))
    wheel->services.changed(wheel->services.ctx, wheel->services.seat);
  free(prior);
}

void qa_seat_weapon_wheel_update_now(qa_seat_weapon_wheel *wheel) {
  qa_seat_weapon_wheel_update(wheel,
                              wheel->services.now(wheel->services.ctx));
}

qa_ui_wheel_draw_state qa_seat_weapon_wheel_draw_state(
    qa_seat_weapon_wheel *wheel) {
  qa_ui_wheel_draw_state out;
  memset(&out, 0, sizeof out);
  if (wheel->opacity > 0 || qa_seat_weapon_wheel_is_open(wheel)) {
    size_t count;
    const qa_ui_wheel_item *items = sorted_items(wheel, wheel->mode, &count);
    out.has_wheel = true;
    out.wheel.mode = wheel->mode;
    out.wheel.items = items;
    out.wheel.item_count = count;
    out.wheel.selected = wheel->selected;
    out.wheel.opacity = wheel->opacity;
    out.wheel.cursor.x = wheel->position.x / wheel->options.radius;
    out.wheel.cursor.y = wheel->position.y / wheel->options.radius;
  }
  if (wheel->carousel == CAROUSEL_OPEN) {
    size_t count;
    const qa_ui_wheel_item *all = sorted_items(wheel, "weapons", &count);
    if (count > wheel->owned_capacity) {
      qa_ui_wheel_item *grown =
          realloc(wheel->owned_items, count * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      wheel->owned_items = grown;
      wheel->owned_capacity = count;
    }
    size_t owned = 0;
    for (size_t i = 0; i < count; i++)
      if (all[i].owned) wheel->owned_items[owned++] = all[i];
    wheel->owned_count = owned;
    out.has_carousel = true;
    out.carousel.items = wheel->owned_items;
    out.carousel.item_count = owned;
    out.carousel.selected = wheel->carousel_selected;
    /* The wheel view above borrows wheel->items, still valid. */
  }
  return out;
}

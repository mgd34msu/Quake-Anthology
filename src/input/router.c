/* Port of src/input/router.ts. GPL-2.0-or-later. */
#include "qa/input/router.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/input/sdl_keys.h"
#include "qa/input/support.h"

typedef struct device_seat {
  double instance;
  qa_seat_input *input;
} device_seat;

typedef struct keyboard_key {
  int32_t scancode;
  double code;
} keyboard_key;

struct qa_input_router {
  qa_input_router_options options;
  qa_input_controllers controllers; /* copy; valid when has_controllers */
  bool has_controllers;
  qa_input_seat_route *routes;
  size_t route_count;
  size_t route_capacity;
  qa_seat_input **inputs; /* mirror of routes inputs */
  qa_seat_input **controller_seats; /* NULL slots allowed */
  size_t controller_seat_count;
  qa_seat_input *keyboard;
  bool has_source_joystick;
  double source_joystick;
  bool has_source_joystick_seat;
  qa_seat_id source_joystick_seat;
  keyboard_key *keyboard_keys;
  size_t keyboard_key_count;
  size_t keyboard_key_capacity;
  device_seat *device_seats;
  size_t device_seat_count;
  size_t device_seat_capacity;
  double *calibration_sensors;
  size_t calibration_count;
  size_t calibration_capacity;
  qa_sdl_input_lease *lease;
  bool has_window;
  qa_input_window window;
  bool closed;
  bool platform_active;
};

static qa_sdl_input_lease *sdl_begin_input(void *ctx) {
  return qa_sdl_window_begin_input((qa_sdl_window *)ctx);
}
static qa_sdl_size sdl_logical_size(void *ctx) {
  return qa_sdl_window_logical_size((qa_sdl_window *)ctx);
}
static qa_sdl_size sdl_drawable_size(void *ctx) {
  return qa_sdl_window_drawable_size((qa_sdl_window *)ctx);
}
static qa_sdl_event *sdl_poll_events(void *ctx, size_t *out_count) {
  return qa_sdl_window_poll_events((qa_sdl_window *)ctx, out_count);
}

qa_input_window qa_input_window_from_sdl(qa_sdl_window *window) {
  qa_input_window view = {window, sdl_begin_input, sdl_logical_size,
                          sdl_drawable_size, sdl_poll_events};
  return view;
}

static void sdl_set_assignments(void *ctx,
                                const qa_controller_selection *selections,
                                size_t count) {
  qa_sdl_controllers_set_assignments((qa_sdl_controllers *)ctx, selections, count);
}
static qa_controller_operation_result sdl_set_sensor(
    void *ctx, int32_t instance, qa_controller_sensor sensor, bool enabled) {
  return qa_sdl_controllers_set_sensor_enabled((qa_sdl_controllers *)ctx, instance,
                                               sensor, enabled);
}
static qa_controller_event *sdl_poll_controller(void *ctx, size_t *out_count) {
  return qa_sdl_controllers_poll_events((qa_sdl_controllers *)ctx, out_count);
}
static bool sdl_snapshot(void *ctx, int32_t instance, qa_controller_state *out) {
  return qa_sdl_controllers_snapshot((qa_sdl_controllers *)ctx, instance, out);
}
static int64_t *sdl_assignments(void *ctx, size_t *out_count) {
  return qa_sdl_controllers_assignments((qa_sdl_controllers *)ctx, out_count);
}

qa_input_controllers qa_input_controllers_from_sdl(
    qa_sdl_controllers *controllers) {
  qa_input_controllers view = {controllers, sdl_set_assignments, sdl_set_sensor,
                               sdl_poll_controller, sdl_snapshot, sdl_assignments};
  return view;
}

static qa_seat_input *find_seat(const qa_input_router *router, qa_seat_id seat) {
  for (size_t i = 0; i < router->route_count; i++) {
    if (qa_same_seat(qa_seat_input_seat(router->routes[i].input), seat)) {
      return router->routes[i].input;
    }
  }
  return NULL;
}

static void check_duplicates(const qa_input_seat_route *routes, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < i; j++) {
      if (qa_same_seat(qa_seat_input_seat(routes[i].input),
                       qa_seat_input_seat(routes[j].input))) {
        qa_input_error("Duplicate input seat");
      }
    }
  }
}

static bool set_routes(qa_input_router *router, const qa_input_seat_route *routes,
                       size_t count) {
  qa_input_seat_route *copy = NULL;
  qa_seat_input **inputs = NULL;
  if (count != 0) {
    copy = (qa_input_seat_route *)malloc(count * sizeof *copy);
    inputs = (qa_seat_input **)malloc(count * sizeof *inputs);
    if (copy == NULL || inputs == NULL) {
      free(copy);
      free(inputs);
      return false;
    }
    memcpy(copy, routes, count * sizeof *copy);
    for (size_t i = 0; i < count; i++) inputs[i] = routes[i].input;
  }
  free(router->routes);
  free(router->inputs);
  router->routes = copy;
  router->inputs = inputs;
  router->route_count = count;
  router->route_capacity = count;
  return true;
}

static bool refresh_controller_seats(qa_input_router *router) {
  qa_seat_input **seats = NULL;
  if (router->route_count != 0) {
    seats = (qa_seat_input **)malloc(router->route_count * sizeof *seats);
    if (seats == NULL) return false;
    for (size_t i = 0; i < router->route_count; i++) {
      seats[i] = router->routes[i].input;
    }
  }
  free(router->controller_seats);
  router->controller_seats = seats;
  router->controller_seat_count = router->route_count;
  return true;
}

static qa_seat_input *device_seat_of(const qa_input_router *router,
                                     double instance) {
  for (size_t i = 0; i < router->device_seat_count; i++) {
    if (router->device_seats[i].instance == instance) {
      return router->device_seats[i].input;
    }
  }
  return NULL;
}

static void device_seat_set(qa_input_router *router, double instance,
                            qa_seat_input *input) {
  for (size_t i = 0; i < router->device_seat_count; i++) {
    if (router->device_seats[i].instance == instance) {
      router->device_seats[i].input = input;
      return;
    }
  }
  if (router->device_seat_count == router->device_seat_capacity) {
    size_t grown =
        router->device_seat_capacity == 0 ? 4 : router->device_seat_capacity * 2;
    device_seat *seats =
        (device_seat *)realloc(router->device_seats, grown * sizeof *seats);
    if (seats == NULL) return;
    router->device_seats = seats;
    router->device_seat_capacity = grown;
  }
  router->device_seats[router->device_seat_count].instance = instance;
  router->device_seats[router->device_seat_count].input = input;
  router->device_seat_count++;
}

static void device_seat_delete(qa_input_router *router, double instance) {
  for (size_t i = 0; i < router->device_seat_count; i++) {
    if (router->device_seats[i].instance == instance) {
      memmove(&router->device_seats[i], &router->device_seats[i + 1],
              (router->device_seat_count - i - 1) * sizeof *router->device_seats);
      router->device_seat_count--;
      return;
    }
  }
}

static void calibration_add(qa_input_router *router, double instance) {
  for (size_t i = 0; i < router->calibration_count; i++) {
    if (router->calibration_sensors[i] == instance) return;
  }
  if (router->calibration_count == router->calibration_capacity) {
    size_t grown =
        router->calibration_capacity == 0 ? 4 : router->calibration_capacity * 2;
    double *sensors =
        (double *)realloc(router->calibration_sensors, grown * sizeof *sensors);
    if (sensors == NULL) return;
    router->calibration_sensors = sensors;
    router->calibration_capacity = grown;
  }
  router->calibration_sensors[router->calibration_count++] = instance;
}

static void calibration_delete(qa_input_router *router, double instance) {
  for (size_t i = 0; i < router->calibration_count; i++) {
    if (router->calibration_sensors[i] == instance) {
      memmove(&router->calibration_sensors[i], &router->calibration_sensors[i + 1],
              (router->calibration_count - i - 1) *
                  sizeof *router->calibration_sensors);
      router->calibration_count--;
      return;
    }
  }
}

static double router_now(const qa_input_router *router) {
  return router->options.now(router->options.now_ctx);
}

static void finish_gyro_calibration(qa_input_router *router) {
  if (!router->platform_active) return;
  size_t i = 0;
  while (i < router->calibration_count) {
    double instance = router->calibration_sensors[i];
    qa_seat_input *seat = device_seat_of(router, instance);
    if (seat != NULL &&
        qa_gamepad_input_gyro_calibration(qa_seat_input_gamepad(seat)).kind ==
            QA_GYRO_CALIBRATION_CALIBRATING) {
      i++;
      continue;
    }
    calibration_delete(router, instance);
    if (seat == NULL || qa_seat_input_gamepad(seat)->tuning.gyro.enabled) continue;
    if (router->has_controllers) {
      qa_controller_operation_result result =
          router->controllers.set_sensor_enabled(router->controllers.ctx,
                                                 (int32_t)instance,
                                                 QA_CONTROLLER_SENSOR_GYRO, false);
      if (router->options.controller_operation != NULL) {
        router->options.controller_operation(
            router->options.controller_operation_ctx, qa_seat_input_seat(seat),
            result);
      }
    }
  }
}

qa_input_router *qa_input_router_create(const qa_input_router_options *options) {
  qa_input_router *router = (qa_input_router *)calloc(1, sizeof *router);
  if (router == NULL) return NULL;
  router->options = *options;
  router->platform_active = !options->defer_platform;
  router->has_controllers = options->controllers != NULL;
  if (router->has_controllers) router->controllers = *options->controllers;
  check_duplicates(options->seats, options->seat_count);
  if (!set_routes(router, options->seats, options->seat_count) ||
      !refresh_controller_seats(router)) {
    qa_input_router_destroy(router);
    return NULL;
  }
  qa_input_router_set_keyboard_seat(router, options->has_keyboard_seat,
                                    options->keyboard_seat);
  if (router->platform_active && router->has_controllers) {
    qa_controller_selection *selections = NULL;
    if (router->route_count != 0) {
      selections = (qa_controller_selection *)malloc(router->route_count *
                                                     sizeof *selections);
      if (selections != NULL) {
        for (size_t i = 0; i < router->route_count; i++) {
          selections[i] = router->routes[i].controller;
        }
        router->controllers.set_assignments(router->controllers.ctx, selections,
                                            router->route_count);
      }
      free(selections);
    } else {
      router->controllers.set_assignments(router->controllers.ctx, NULL, 0);
    }
  }
  return router;
}

void qa_input_router_destroy(qa_input_router *router) {
  if (router == NULL) return;
  qa_input_router_close(router);
  free(router->routes);
  free(router->inputs);
  free(router->controller_seats);
  free(router->keyboard_keys);
  free(router->device_seats);
  free(router->calibration_sensors);
  free(router);
}

qa_seat_input **qa_input_router_inputs(qa_input_router *router, size_t *out_count) {
  *out_count = router->route_count;
  return router->inputs;
}

void qa_input_router_set_source_joystick(qa_input_router *router, double instance,
                                         bool has_instance, qa_seat_id seat,
                                         bool has_seat) {
  if (has_seat && find_seat(router, seat) == NULL) {
    qa_input_error("Source joystick seat is not active");
  }
  bool same_instance = has_instance == router->has_source_joystick &&
                       (!has_instance || instance == router->source_joystick);
  bool same_seat = has_seat == router->has_source_joystick_seat &&
                   (!has_seat || (router->has_source_joystick_seat &&
                                  qa_same_seat(seat, router->source_joystick_seat)));
  if (same_instance && same_seat) return;
  if (router->has_source_joystick) {
    qa_seat_input *previous = device_seat_of(router, router->source_joystick);
    if (previous != NULL) {
      qa_seat_input_release_device(previous, router->source_joystick,
                                   router_now(router));
    }
  }
  if (has_instance) {
    qa_seat_input *next = device_seat_of(router, instance);
    if (next != NULL) {
      qa_seat_input_release_device(next, instance, router_now(router));
    }
  }
  router->has_source_joystick = has_instance;
  router->source_joystick = instance;
  router->has_source_joystick_seat = has_instance && has_seat;
  if (router->has_source_joystick_seat) router->source_joystick_seat = seat;
}

void qa_input_router_publish_seats(qa_input_router *router,
                                   const qa_input_seat_route *routes,
                                   size_t route_count, bool has_keyboard,
                                   qa_seat_id keyboard) {
  check_duplicates(routes, route_count);
  if (has_keyboard) {
    bool published = false;
    for (size_t i = 0; i < route_count; i++) {
      if (qa_same_seat(qa_seat_input_seat(routes[i].input), keyboard)) {
        published = true;
      }
    }
    if (!published) qa_input_error("Keyboard seat is not published");
  }
  for (size_t i = 0; i < router->route_count; i++) {
    qa_seat_input_release(router->routes[i].input, router_now(router), NULL);
  }
  (void)set_routes(router, routes, route_count);
  router->has_source_joystick = false;
  router->has_source_joystick_seat = false;
  qa_input_router_set_keyboard_seat(router, has_keyboard, keyboard);
  qa_input_router_restart(router);
  qa_input_router_update_capture(router);
}

void qa_input_router_retain_seats(qa_input_router *router, const qa_seat_id *seats,
                                  size_t seat_count, bool has_keyboard,
                                  qa_seat_id keyboard) {
  for (size_t i = 0; i < seat_count; i++) {
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) duplicate = duplicate || qa_same_seat(seats[i], seats[j]);
    if (duplicate || find_seat(router, seats[i]) == NULL) {
      qa_input_error("Retained input seats must be distinct current owners");
    }
  }
  if (has_keyboard) {
    bool retained_keyboard = false;
    for (size_t i = 0; i < seat_count; i++) {
      retained_keyboard = retained_keyboard || qa_same_seat(seats[i], keyboard);
    }
    if (!retained_keyboard) qa_input_error("Keyboard seat is not retained");
  }
  for (size_t i = 0; i < router->route_count; i++) {
    bool retained = false;
    for (size_t j = 0; j < seat_count; j++) {
      retained = retained ||
                 qa_same_seat(qa_seat_input_seat(router->routes[i].input), seats[j]);
    }
    if (!retained) {
      qa_seat_input_release(router->routes[i].input, router_now(router), NULL);
      qa_gamepad_input_cancel_gyro_calibration(
          qa_seat_input_gamepad(router->routes[i].input));
    }
  }
  finish_gyro_calibration(router);
  qa_input_seat_route *kept = (qa_input_seat_route *)calloc(
      router->route_count != 0 ? router->route_count : 1, sizeof *kept);
  size_t kept_count = 0;
  if (kept != NULL) {
    for (size_t i = 0; i < router->route_count; i++) {
      bool retained = false;
      for (size_t j = 0; j < seat_count; j++) {
        retained = retained ||
                   qa_same_seat(qa_seat_input_seat(router->routes[i].input), seats[j]);
      }
      if (retained) kept[kept_count++] = router->routes[i];
    }
    (void)set_routes(router, kept, kept_count);
    free(kept);
  }
  /* SDL assignment slots stay stable until an explicit routing
   * change or restart. */
  for (size_t i = 0; i < router->controller_seat_count; i++) {
    qa_seat_input *input = router->controller_seats[i];
    bool retained = false;
    if (input != NULL) {
      for (size_t j = 0; j < seat_count; j++) {
        retained = retained || qa_same_seat(qa_seat_input_seat(input), seats[j]);
      }
    }
    router->controller_seats[i] = retained ? input : NULL;
  }
  size_t d = 0;
  while (d < router->device_seat_count) {
    bool retained = false;
    for (size_t j = 0; j < seat_count; j++) {
      retained = retained || qa_same_seat(
                                 qa_seat_input_seat(router->device_seats[d].input),
                                 seats[j]);
    }
    if (!retained) {
      double instance = router->device_seats[d].instance;
      device_seat_delete(router, instance);
      if (router->has_source_joystick && router->source_joystick == instance) {
        router->has_source_joystick = false;
        router->has_source_joystick_seat = false;
      }
    } else {
      d++;
    }
  }
  if (router->has_source_joystick_seat) {
    bool retained = false;
    for (size_t j = 0; j < seat_count; j++) {
      retained = retained || qa_same_seat(router->source_joystick_seat, seats[j]);
    }
    if (!retained) {
      router->has_source_joystick = false;
      router->has_source_joystick_seat = false;
    }
  }
  if (router->keyboard != NULL) {
    bool retained = false;
    for (size_t j = 0; j < seat_count; j++) {
      retained = retained || qa_same_seat(qa_seat_input_seat(router->keyboard), seats[j]);
    }
    if (!retained) {
      router->keyboard = NULL;
      router->keyboard_key_count = 0;
    }
  }
  qa_seat_input *wanted = has_keyboard ? find_seat(router, keyboard) : NULL;
  if (router->keyboard != wanted) {
    qa_input_router_set_keyboard_seat(router, has_keyboard, keyboard);
  }
  qa_input_router_update_capture(router);
}

bool qa_input_router_keyboard_seat(const qa_input_router *router,
                                   qa_seat_id *out) {
  if (router->keyboard == NULL) return false;
  *out = qa_seat_input_seat(router->keyboard);
  return true;
}

qa_controller_selection qa_input_router_controller_selection(
    const qa_input_router *router, qa_seat_id seat) {
  for (size_t i = 0; i < router->route_count; i++) {
    if (qa_same_seat(qa_seat_input_seat(router->routes[i].input), seat)) {
      return router->routes[i].controller;
    }
  }
  qa_input_error("Unknown input seat");
}

void qa_input_router_set_controller_selection(qa_input_router *router,
                                              qa_seat_id seat,
                                              qa_controller_selection selection) {
  for (size_t i = 0; i < router->route_count; i++) {
    if (qa_same_seat(qa_seat_input_seat(router->routes[i].input), seat)) {
      router->routes[i].controller = selection;
      qa_input_router_restart(router);
      return;
    }
  }
  qa_input_error("Unknown input seat");
}

qa_seat_input *qa_input_router_seat(const qa_input_router *router, qa_seat_id seat) {
  return find_seat(router, seat);
}

bool qa_input_router_controller_for(const qa_input_router *router, qa_seat_id seat,
                                    double *out_instance) {
  if (router->has_source_joystick && router->has_source_joystick_seat &&
      qa_same_seat(router->source_joystick_seat, seat)) {
    *out_instance = router->source_joystick;
    return true;
  }
  for (size_t i = 0; i < router->device_seat_count; i++) {
    if (router->has_source_joystick &&
        router->device_seats[i].instance == router->source_joystick) {
      continue;
    }
    if (qa_same_seat(qa_seat_input_seat(router->device_seats[i].input), seat)) {
      *out_instance = router->device_seats[i].instance;
      return true;
    }
  }
  return false;
}

static qa_controller_operation_result disconnected_result(void) {
  qa_controller_operation_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_CONTROLLER_OPERATION_DISCONNECTED;
  snprintf(result.reason, sizeof result.reason, "Seat has no assigned controller");
  return result;
}

qa_controller_operation_result qa_input_router_set_gyro_enabled(
    qa_input_router *router, qa_seat_id seat, bool enabled) {
  qa_seat_input *input = find_seat(router, seat);
  if (input == NULL) qa_input_error("Gyro route refers to an unregistered seat");
  double instance = 0;
  bool has = qa_input_router_controller_for(router, seat, &instance);
  if (!router->platform_active || !has || !router->has_controllers) {
    return disconnected_result();
  }
  qa_controller_operation_result result = router->controllers.set_sensor_enabled(
      router->controllers.ctx, (int32_t)instance, QA_CONTROLLER_SENSOR_GYRO,
      enabled);
  if (result.kind == QA_CONTROLLER_OPERATION_ACCEPTED) {
    qa_gamepad_input *gamepad = qa_seat_input_gamepad(input);
    qa_gamepad_input_cancel_gyro_calibration(gamepad);
    calibration_delete(router, instance);
    gamepad->tuning.gyro.enabled = enabled;
  }
  return result;
}

static qa_seat_input *gyro_seat(qa_input_router *router, qa_seat_id seat) {
  qa_seat_input *input = find_seat(router, seat);
  if (input == NULL) qa_input_error("Gyro route refers to an unregistered seat");
  return input;
}

qa_gyro_calibration_state qa_input_router_gyro_calibration(
    qa_input_router *router, qa_seat_id seat) {
  return qa_gamepad_input_gyro_calibration(
      qa_seat_input_gamepad(gyro_seat(router, seat)));
}

qa_controller_operation_result qa_input_router_begin_gyro_calibration(
    qa_input_router *router, qa_seat_id seat) {
  qa_seat_input *input = gyro_seat(router, seat);
  double instance = 0;
  bool has = qa_input_router_controller_for(router, seat, &instance);
  if (!router->platform_active || !has || !router->has_controllers) {
    return disconnected_result();
  }
  if (!qa_seat_input_focused(input)) {
    qa_controller_operation_result result;
    memset(&result, 0, sizeof result);
    result.kind = QA_CONTROLLER_OPERATION_FAILED;
    snprintf(result.reason, sizeof result.reason,
             "Focus the game window before calibrating");
    return result;
  }
  qa_controller_operation_result result = router->controllers.set_sensor_enabled(
      router->controllers.ctx, (int32_t)instance, QA_CONTROLLER_SENSOR_GYRO, true);
  if (result.kind == QA_CONTROLLER_OPERATION_ACCEPTED) {
    qa_gamepad_input *gamepad = qa_seat_input_gamepad(input);
    if (!gamepad->tuning.gyro.enabled) calibration_add(router, instance);
    qa_gamepad_input_begin_gyro_calibration(gamepad);
  }
  return result;
}

void qa_input_router_cancel_gyro_calibration(qa_input_router *router,
                                             qa_seat_id seat) {
  qa_gamepad_input_cancel_gyro_calibration(
      qa_seat_input_gamepad(gyro_seat(router, seat)));
  finish_gyro_calibration(router);
}

void qa_input_router_reset_gyro_calibration(qa_input_router *router,
                                            qa_seat_id seat) {
  qa_gamepad_input_reset_gyro_calibration(
      qa_seat_input_gamepad(gyro_seat(router, seat)));
  finish_gyro_calibration(router);
}

void qa_input_router_set_keyboard_seat(qa_input_router *router, bool has_seat,
                                       qa_seat_id seat) {
  if (router->platform_active && router->keyboard != NULL) {
    qa_seat_input_release(router->keyboard, router_now(router), NULL);
  }
  router->keyboard_key_count = 0;
  router->keyboard = has_seat ? find_seat(router, seat) : NULL;
  if (has_seat && router->keyboard == NULL) {
    qa_input_error("Keyboard route refers to an unregistered seat");
  }
}

void qa_input_router_attach_window(qa_input_router *router,
                                   qa_input_window window) {
  qa_input_router_detach_window(router);
  if (!router->platform_active) {
    router->platform_active = true;
    qa_input_router_restart(router);
  }
  router->window = window;
  router->has_window = true;
  router->lease = window.begin_input(window.ctx);
  qa_input_router_update_capture(router);
}

void qa_input_router_transfer_window_to(qa_input_router *router,
                                        qa_input_router *next) {
  next->platform_active = router->platform_active;
  router->platform_active = false;
  for (size_t i = 0; i < router->calibration_count; i++) {
    calibration_add(next, router->calibration_sensors[i]);
  }
  router->calibration_count = 0;
  next->window = router->window;
  next->has_window = router->has_window;
  next->lease = router->lease;
  router->has_window = false;
  router->lease = NULL;
}

void qa_input_router_detach_window(qa_input_router *router) {
  if (router->platform_active) {
    for (size_t i = 0; i < router->route_count; i++) {
      qa_seat_input_release(router->routes[i].input, router_now(router), NULL);
    }
  }
  finish_gyro_calibration(router);
  router->keyboard_key_count = 0;
  if (router->lease != NULL) {
    qa_sdl_input_lease_close(router->lease);
    qa_sdl_input_lease_destroy(router->lease);
  }
  router->lease = NULL;
  router->has_window = false;
}

void qa_input_router_update_capture(qa_input_router *router) {
  finish_gyro_calibration(router);
  bool playing = router->keyboard != NULL &&
                 qa_seat_input_focused(router->keyboard) &&
                 strcmp(qa_seat_input_get_focus(router->keyboard).kind, "game") == 0;
  if (router->lease != NULL) {
    qa_sdl_input_lease_set_relative_mouse(router->lease, playing);
  }
}

static qa_vec2 pointer_position(const qa_input_router *router, double x, double y) {
  qa_vec2 position = {x, y};
  if (!router->has_window) return position;
  qa_sdl_size logical = router->window.logical_size(router->window.ctx);
  qa_sdl_size drawable = router->window.drawable_size(router->window.ctx);
  position.x = x * drawable.width / logical.width;
  position.y = y * drawable.height / logical.height;
  return position;
}

static double event_time(const qa_input_router *router, double timestamp) {
  double ticks2 = router->options.ticks(router->options.ticks_ctx);
  double now = router_now(router);
  return qa_sdl_event_time(timestamp, ticks2,
                           now, router->options.subframe);
}

static bool keyboard_code(const qa_input_router *router, int32_t scancode,
                          double *out) {
  for (size_t i = 0; i < router->keyboard_key_count; i++) {
    if (router->keyboard_keys[i].scancode == scancode) {
      *out = router->keyboard_keys[i].code;
      return true;
    }
  }
  return false;
}

static void keyboard_code_set(qa_input_router *router, int32_t scancode,
                              double code) {
  for (size_t i = 0; i < router->keyboard_key_count; i++) {
    if (router->keyboard_keys[i].scancode == scancode) {
      router->keyboard_keys[i].code = code;
      return;
    }
  }
  if (router->keyboard_key_count == router->keyboard_key_capacity) {
    size_t grown =
        router->keyboard_key_capacity == 0 ? 8 : router->keyboard_key_capacity * 2;
    keyboard_key *keys =
        (keyboard_key *)realloc(router->keyboard_keys, grown * sizeof *keys);
    if (keys == NULL) return;
    router->keyboard_keys = keys;
    router->keyboard_key_capacity = grown;
  }
  router->keyboard_keys[router->keyboard_key_count].scancode = scancode;
  router->keyboard_keys[router->keyboard_key_count].code = code;
  router->keyboard_key_count++;
}

static void keyboard_code_delete(qa_input_router *router, int32_t scancode) {
  for (size_t i = 0; i < router->keyboard_key_count; i++) {
    if (router->keyboard_keys[i].scancode == scancode) {
      memmove(&router->keyboard_keys[i], &router->keyboard_keys[i + 1],
              (router->keyboard_key_count - i - 1) * sizeof *router->keyboard_keys);
      router->keyboard_key_count--;
      return;
    }
  }
}

void qa_input_router_handle_platform(qa_input_router *router,
                                     const qa_sdl_event *event) {
  if (router->closed) qa_input_error("Input router is closed");
  qa_seat_input *seat = router->keyboard;
  double time = event_time(router, event->timestamp);
  if (event->kind == QA_SDL_EVENT_WINDOW &&
      (event->as.window.event == 12 || event->as.window.event == 13)) {
    for (size_t i = 0; i < router->route_count; i++) {
      qa_seat_input_event focus;
      memset(&focus, 0, sizeof focus);
      focus.seat = qa_seat_input_seat(router->routes[i].input);
      focus.time_milliseconds = time;
      focus.kind = "focus";
      focus.focused = event->as.window.event == 12;
      (void)qa_seat_input_input(router->routes[i].input, &focus);
    }
    if (event->as.window.event == 13) router->keyboard_key_count = 0;
    qa_input_router_update_capture(router);
    return;
  }
  if (seat == NULL) {
    qa_input_unhandled_event unhandled = {false, event, NULL};
    router->options.unhandled(router->options.unhandled_ctx, &unhandled);
    return;
  }
  qa_seat_input_event translated;
  memset(&translated, 0, sizeof translated);
  translated.seat = qa_seat_input_seat(seat);
  translated.time_milliseconds = time;
  switch (event->kind) {
    case QA_SDL_EVENT_KEY: {
      double code = 0;
      if (!keyboard_code(router, event->as.key.scancode, &code)) {
        code = qa_sdl_game_key(event->as.key.keycode, event->as.key.modifiers);
      }
      if (code == 0) return;
      if (event->as.key.down) {
        keyboard_code_set(router, event->as.key.scancode, code);
      } else {
        keyboard_code_delete(router, event->as.key.scancode);
      }
      translated.kind = "key";
      translated.code = code;
      translated.down = event->as.key.down;
      translated.repeat = event->as.key.repeat;
      break;
    }
    case QA_SDL_EVENT_TEXT:
      translated.kind = "text";
      translated.text = event->as.text.text;
      break;
    case QA_SDL_EVENT_MOUSE_MOTION:
      /* Relative game motion ignores the position, and motion never changes
         focus or capture, so the native size queries are skipped. */
      translated.kind = "mouse-motion";
      if (strcmp(qa_seat_input_get_focus(seat).kind, "game") == 0) {
        translated.position.x = event->as.mouse_motion.x;
        translated.position.y = event->as.mouse_motion.y;
      } else {
        translated.position = pointer_position(router, event->as.mouse_motion.x,
                                               event->as.mouse_motion.y);
      }
      translated.delta.x = event->as.mouse_motion.dx;
      translated.delta.y = event->as.mouse_motion.dy;
      (void)qa_seat_input_input(seat, &translated);
      return;
    case QA_SDL_EVENT_MOUSE_BUTTON: {
      if (strcmp(qa_seat_input_get_focus(seat).kind, "game") != 0) {
        qa_seat_input_event motion;
        memset(&motion, 0, sizeof motion);
        motion.seat = translated.seat;
        motion.time_milliseconds = time;
        motion.kind = "mouse-motion";
        motion.position = pointer_position(router, event->as.mouse_button.x,
                                           event->as.mouse_button.y);
        (void)qa_seat_input_input(seat, &motion);
      }
      translated.kind = "mouse-button";
      translated.button = event->as.mouse_button.button;
      translated.down = event->as.mouse_button.down;
      break;
    }
    case QA_SDL_EVENT_MOUSE_WHEEL:
      translated.kind = "mouse-wheel";
      translated.delta.x =
          event->as.mouse_wheel.precise_x * (event->as.mouse_wheel.flipped ? -1 : 1);
      translated.delta.y =
          event->as.mouse_wheel.precise_y * (event->as.mouse_wheel.flipped ? -1 : 1);
      break;
    default: {
      qa_input_unhandled_event unhandled = {false, event, NULL};
      router->options.unhandled(router->options.unhandled_ctx, &unhandled);
      return;
    }
  }
  (void)qa_seat_input_input(seat, &translated);
  qa_input_router_update_capture(router);
}

static void unhandled_controller(qa_input_router *router,
                                 const qa_controller_event *event) {
  qa_input_unhandled_event unhandled = {true, NULL, event};
  router->options.unhandled(router->options.unhandled_ctx, &unhandled);
}

void qa_input_router_handle_controller(qa_input_router *router,
                                       const qa_controller_event *event) {
  if (router->closed) qa_input_error("Input router is closed");
  double time = event_time(router, event->timestamp);
  if (event->kind == QA_CONTROLLER_EVENT_ASSIGNMENT) {
    if (event->assignment_has_previous) {
      qa_seat_input *previous =
          device_seat_of(router, (double)event->assignment_previous);
      if (previous != NULL) {
        qa_seat_input_release_device(previous, (double)event->assignment_previous,
                                     time);
      }
      finish_gyro_calibration(router);
      device_seat_delete(router, (double)event->assignment_previous);
    }
    qa_seat_input *seat = NULL;
    if (event->assignment_slot >= 0 &&
        (size_t)event->assignment_slot < router->controller_seat_count) {
      seat = router->controller_seats[event->assignment_slot];
    }
    if (event->assignment_has_instance && seat != NULL) {
      qa_gamepad_input_reset_gyro_calibration(qa_seat_input_gamepad(seat));
      qa_seat_input_remap_controller_bindings(seat,
                                              (double)event->assignment_instance);
      device_seat_set(router, (double)event->assignment_instance, seat);
      if (qa_seat_input_gamepad(seat)->tuning.gyro.enabled) {
        qa_controller_operation_result result =
            qa_input_router_set_gyro_enabled(router, qa_seat_input_seat(seat), true);
        if (router->options.controller_operation != NULL) {
          router->options.controller_operation(
              router->options.controller_operation_ctx, qa_seat_input_seat(seat),
              result);
        }
      }
    }
    unhandled_controller(router, event);
    return;
  }
  if (event->kind == QA_CONTROLLER_EVENT_CONNECTED ||
      event->kind == QA_CONTROLLER_EVENT_REMAPPED) {
    unhandled_controller(router, event);
    return;
  }
  qa_seat_input *seat = device_seat_of(router, event->instance);
  if (seat == NULL) {
    unhandled_controller(router, event);
    return;
  }
  if (router->has_source_joystick && (double)event->instance == router->source_joystick &&
      event->kind != QA_CONTROLLER_EVENT_DISCONNECTED) {
    return;
  }
  qa_seat_input_event translated;
  memset(&translated, 0, sizeof translated);
  translated.seat = qa_seat_input_seat(seat);
  translated.time_milliseconds = time;
  translated.device = event->instance;
  switch (event->kind) {
    case QA_CONTROLLER_EVENT_DISCONNECTED:
      qa_seat_input_release_device(seat, event->instance, time);
      calibration_delete(router, event->instance);
      device_seat_delete(router, event->instance);
      break;
    case QA_CONTROLLER_EVENT_BUTTON:
      translated.kind = "controller-button";
      translated.button = event->button;
      translated.down = event->down;
      (void)qa_seat_input_input(seat, &translated);
      break;
    case QA_CONTROLLER_EVENT_AXIS: {
      qa_controller_axis axis = qa_controller_axis_name(event->axis);
      if (axis != NULL) {
        translated.kind = "controller-axis";
        translated.axis = axis;
        translated.value = qa_normalized_controller_axis(axis, event->value);
        (void)qa_seat_input_input(seat, &translated);
      }
      break;
    }
    case QA_CONTROLLER_EVENT_SENSOR:
      if (event->sensor == QA_CONTROLLER_SENSOR_GYRO) {
        qa_vec3 sample = {event->x, event->y, event->z};
        double stamp = event->timestamp_us == 0
                           ? (double)event->timestamp
                           : (double)event->timestamp_us / 1000;
        qa_seat_input_gyro(seat, sample, stamp);
        finish_gyro_calibration(router);
      } else {
        unhandled_controller(router, event);
      }
      break;
    default:
      unhandled_controller(router, event);
      break;
  }
}

void qa_input_router_pump(qa_input_router *router) {
  size_t count = 0;
  qa_sdl_event *platform =
      router->has_window ? router->window.poll_events(router->window.ctx, &count)
                         : NULL;
  for (size_t i = 0; i < count; i++) {
    qa_input_router_handle_platform(router, &platform[i]);
  }
  free(platform);
  count = 0;
  qa_controller_event *controller =
      router->has_controllers
          ? router->controllers.poll_events(router->controllers.ctx, &count)
          : NULL;
  for (size_t i = 0; i < count; i++) {
    qa_input_router_handle_controller(router, &controller[i]);
  }
  qa_controller_events_free(controller, count);
  for (size_t i = 0; i < router->device_seat_count; i++) {
    double instance = router->device_seats[i].instance;
    qa_seat_input *seat = router->device_seats[i].input;
    if (router->has_source_joystick && instance == router->source_joystick) continue;
    if (!qa_seat_input_focused(seat) ||
        strcmp(qa_seat_input_get_focus(seat).kind, "game") != 0) {
      continue;
    }
    if (!router->has_controllers) continue;
    qa_controller_state snapshot;
    if (!router->controllers.snapshot(router->controllers.ctx, (int32_t)instance,
                                      &snapshot)) {
      continue;
    }
    for (int axis_number = 0; axis_number < QA_CONTROLLER_AXIS_COUNT; axis_number++) {
      qa_controller_axis axis = qa_controller_axis_name(axis_number);
      if (axis != NULL) {
        qa_gamepad_input_axis(
            qa_seat_input_gamepad(seat), axis,
            qa_normalized_controller_axis(axis, snapshot.axes[axis_number]));
      }
    }
  }
  qa_input_router_update_capture(router);
}

void qa_input_router_restart(qa_input_router *router) {
  if (router->platform_active) {
    for (size_t i = 0; i < router->route_count; i++) {
      qa_seat_input_release(router->routes[i].input, router_now(router), NULL);
      qa_gamepad_input_reset_gyro_calibration(
          qa_seat_input_gamepad(router->routes[i].input));
    }
  }
  finish_gyro_calibration(router);
  router->device_seat_count = 0;
  router->keyboard_key_count = 0;
  (void)refresh_controller_seats(router);
  if (router->platform_active && router->has_controllers) {
    qa_controller_selection *selections = NULL;
    if (router->route_count != 0) {
      selections = (qa_controller_selection *)malloc(router->route_count *
                                                     sizeof *selections);
      if (selections != NULL) {
        for (size_t i = 0; i < router->route_count; i++) {
          selections[i] = router->routes[i].controller;
        }
        router->controllers.set_assignments(router->controllers.ctx, selections,
                                            router->route_count);
      }
      free(selections);
    } else {
      router->controllers.set_assignments(router->controllers.ctx, NULL, 0);
    }
  }
  if (router->has_controllers) {
    size_t count = 0;
    int64_t *assignments =
        router->controllers.assignments(router->controllers.ctx, &count);
    for (size_t slot = 0; slot < count; slot++) {
      if (slot >= router->route_count) break;
      qa_seat_input *input = router->routes[slot].input;
      if (assignments[slot] >= 0 && input != NULL) {
        if (router->platform_active) {
          qa_seat_input_remap_controller_bindings(input, (double)assignments[slot]);
        }
        device_seat_set(router, (double)assignments[slot], input);
      }
    }
    free(assignments);
  }
  qa_input_router_update_capture(router);
}

void qa_input_router_close(qa_input_router *router) {
  if (router->closed) return;
  qa_input_router_detach_window(router);
  if (router->platform_active) {
    for (size_t i = 0; i < router->route_count; i++) {
      qa_gamepad_input_reset_gyro_calibration(
          qa_seat_input_gamepad(router->routes[i].input));
    }
  }
  router->device_seat_count = 0;
  router->closed = true;
}

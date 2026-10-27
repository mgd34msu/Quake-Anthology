/*
 * Port of src/input/router.ts: borrows platform event ownership
 * and routes SDL/controller events to per-seat inputs. The TS
 * Pick-typed window and controllers become vtables with SDL
 * adapters (test doubles implement the same shapes, as the TS
 * tests do). Controller events the vtable returns are freed
 * with qa_controller_events_free. SDL key constants come from
 * <SDL2/SDL.h> through the platform headers.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_ROUTER_H
#define QA_INPUT_ROUTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/input/gamepad.h"
#include "qa/input/seat.h"
#include "qa/platform/controller.h"
#include "qa/platform/sdl.h"

/** TS InputWindow (Pick of SdlWindow). */
typedef struct qa_input_window {
  void *ctx;
  qa_sdl_input_lease *(*begin_input)(void *ctx);
  qa_sdl_size (*logical_size)(void *ctx);
  qa_sdl_size (*drawable_size)(void *ctx);
  qa_sdl_event *(*poll_events)(void *ctx, size_t *out_count);
} qa_input_window;

qa_input_window qa_input_window_from_sdl(qa_sdl_window *window);

/** TS controllers Pick of SdlControllers. assignments returns a
 * malloc'd instance per slot (-1 is TS null). */
typedef struct qa_input_controllers {
  void *ctx;
  void (*set_assignments)(void *ctx, const qa_controller_selection *selections,
                          size_t count);
  qa_controller_operation_result (*set_sensor_enabled)(
      void *ctx, int32_t instance, qa_controller_sensor sensor, bool enabled);
  qa_controller_event *(*poll_events)(void *ctx, size_t *out_count);
  bool (*snapshot)(void *ctx, int32_t instance, qa_controller_state *out);
  int64_t *(*assignments)(void *ctx, size_t *out_count);
} qa_input_controllers;

qa_input_controllers qa_input_controllers_from_sdl(qa_sdl_controllers *controllers);

/** TS InputSeatRoute. */
typedef struct qa_input_seat_route {
  qa_seat_input *input;
  qa_controller_selection controller;
} qa_input_seat_route;

/** TS unhandled (event: SdlEvent | ControllerEvent). */
typedef struct qa_input_unhandled_event {
  bool is_controller;
  const qa_sdl_event *sdl;
  const qa_controller_event *controller;
} qa_input_unhandled_event;
typedef void (*qa_input_unhandled_fn)(void *ctx,
                                      const qa_input_unhandled_event *event);
typedef void (*qa_controller_operation_fn)(
    void *ctx, qa_seat_id seat, qa_controller_operation_result result);
typedef double (*qa_input_clock_fn)(void *ctx);

/** TS InputRouterOptions. controllers NULL is the TS null;
 * keyboard_seat is used when has_keyboard_seat is true. */
typedef struct qa_input_router_options {
  const qa_input_seat_route *seats;
  size_t seat_count;
  bool has_keyboard_seat;
  qa_seat_id keyboard_seat;
  const qa_input_controllers *controllers;
  qa_input_clock_fn now;
  void *now_ctx;
  qa_input_clock_fn ticks;
  void *ticks_ctx;
  bool subframe;
  qa_input_unhandled_fn unhandled;
  void *unhandled_ctx;
  bool defer_platform;
  qa_controller_operation_fn controller_operation;
  void *controller_operation_ctx;
} qa_input_router_options;

typedef struct qa_input_router qa_input_router;

/* NULL only on allocation failure; duplicate seats abort with
 * the TS Error message. */
qa_input_router *qa_input_router_create(const qa_input_router_options *options);
void qa_input_router_destroy(qa_input_router *router);
/* Borrowed router-owned array of seat inputs. */
qa_seat_input **qa_input_router_inputs(qa_input_router *router, size_t *out_count);
/* instance < 0 clears (TS null); seat used when has_seat. */
void qa_input_router_set_source_joystick(qa_input_router *router, double instance,
                                         bool has_instance, qa_seat_id seat,
                                         bool has_seat);
void qa_input_router_publish_seats(qa_input_router *router,
                                   const qa_input_seat_route *routes,
                                   size_t route_count, bool has_keyboard,
                                   qa_seat_id keyboard);
void qa_input_router_retain_seats(qa_input_router *router, const qa_seat_id *seats,
                                  size_t seat_count, bool has_keyboard,
                                  qa_seat_id keyboard);
bool qa_input_router_keyboard_seat(const qa_input_router *router,
                                   qa_seat_id *out);
qa_controller_selection qa_input_router_controller_selection(
    const qa_input_router *router, qa_seat_id seat);
void qa_input_router_set_controller_selection(qa_input_router *router,
                                              qa_seat_id seat,
                                              qa_controller_selection selection);
qa_seat_input *qa_input_router_seat(const qa_input_router *router, qa_seat_id seat);
bool qa_input_router_controller_for(const qa_input_router *router, qa_seat_id seat,
                                    double *out_instance);
qa_controller_operation_result qa_input_router_set_gyro_enabled(
    qa_input_router *router, qa_seat_id seat, bool enabled);
qa_gyro_calibration_state qa_input_router_gyro_calibration(
    qa_input_router *router, qa_seat_id seat);
qa_controller_operation_result qa_input_router_begin_gyro_calibration(
    qa_input_router *router, qa_seat_id seat);
void qa_input_router_cancel_gyro_calibration(qa_input_router *router,
                                             qa_seat_id seat);
void qa_input_router_reset_gyro_calibration(qa_input_router *router,
                                            qa_seat_id seat);
void qa_input_router_set_keyboard_seat(qa_input_router *router, bool has_seat,
                                       qa_seat_id seat);
void qa_input_router_attach_window(qa_input_router *router,
                                   qa_input_window window);
void qa_input_router_transfer_window_to(qa_input_router *router,
                                        qa_input_router *next);
void qa_input_router_detach_window(qa_input_router *router);
void qa_input_router_update_capture(qa_input_router *router);
void qa_input_router_handle_platform(qa_input_router *router,
                                     const qa_sdl_event *event);
void qa_input_router_handle_controller(qa_input_router *router,
                                       const qa_controller_event *event);
void qa_input_router_pump(qa_input_router *router);
void qa_input_router_restart(qa_input_router *router);
void qa_input_router_close(qa_input_router *router);

#endif

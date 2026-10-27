/*
 * Port of src/ui/common/controller.ts: NativeUiController.
 * Navigation, wrapping, menu stack and fields follow Q3
 * ui_qmenu.c/ui_field.c; every mutable cursor, field, list and held
 * controller direction belongs to a seat.
 * C accommodations:
 *  - TS closures in NativeUiOptions become fn ptr + one shared ctx.
 *    localize returns a borrowed string valid until its next call;
 *    the controller copies what it keeps. clipboard NULL return is
 *    TS null; a NULL localize/clipboard/measure_text fn is the TS
 *    absent optional.
 *  - UiMenuFactory returns a qa_ui_menu by value. The factory owner
 *    keeps the referenced controls/strings alive until its next
 *    factory call (the TS re-reads the factory the same way).
 *  - register() returned a dispose closure; here it returns a handle
 *    passed to qa_native_ui_controller_unregister.
 *  - draw() returns a controller-owned command array, valid until
 *    the next draw/input that redraws or destroy.
 *  - Plain TS Errors abort with the identical message
 *    (qa_range_error), as in the landed cvar port.
 *  - Text editing counts UTF-8 code points where the TS counts
 *    Array.from code points; byte counts differ only in storage.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_CONTROLLER_H
#define QA_UI_COMMON_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
#include "qa/ui/common/skin.h"

/** TS UiSound: "open"|"close"|"move"|"change"|"reject" (exact). */
typedef const char *qa_ui_sound;

/** TS UiMenuFactory. */
typedef qa_ui_menu (*qa_ui_menu_factory_fn)(void *ctx);

/** TS NativeUiOptions.appearance() result. */
typedef struct qa_native_ui_appearance {
  double menu_scale, text_scale;
  bool high_contrast;
  const char *color_mode; /* nullable TS colorMode */
} qa_native_ui_appearance;

/** TS NativeUiOptions. */
typedef struct qa_native_ui_options {
  void *ctx;
  qa_seat_id seat;
  qa_ui_skin (*skin)(void *ctx);
  double (*now)(void *ctx);
  const qa_input_binding *(*bindings)(void *ctx, size_t *out_count);
  void (*focus)(void *ctx, qa_seat_input_focus focus, double now_ms);
  void (*sound)(void *ctx, qa_ui_sound sound, qa_seat_id seat);
  void (*execute_script)(void *ctx, qa_legacy_ui_script script,
                         qa_seat_id seat);
  const char *(*localize)(void *ctx, const char *text);   /* optional */
  qa_native_ui_appearance (*appearance)(void *ctx);      /* optional */
  const char *(*clipboard)(void *ctx);                    /* optional */
  double (*measure_text)(void *ctx, const char *text, double scale);
} qa_native_ui_options;

typedef struct qa_native_ui_controller qa_native_ui_controller;

/* NULL only on OOM. */
qa_native_ui_controller *qa_native_ui_controller_create(
    const qa_native_ui_options *options);
void qa_native_ui_controller_destroy(qa_native_ui_controller *controller);

/** TS controller.seat. Named with _get_: qa_seat_id is a type. */
qa_seat_id qa_native_ui_controller_get_seat(
    const qa_native_ui_controller *controller);

/* TS register(id, factory): returns a handle >= 0 for unregister,
 * -1 on OOM. Aborts (TS Error) if id is already registered. */
int qa_native_ui_controller_register(qa_native_ui_controller *controller,
                                     qa_ui_menu_id id,
                                     qa_ui_menu_factory_fn factory,
                                     void *factory_ctx);
/** The dispose closure returned by TS register(). */
void qa_native_ui_controller_unregister(qa_native_ui_controller *controller,
                                        int handle);

/** TS activeMenu getter: NULL is TS null. */
qa_ui_menu_id qa_native_ui_controller_active_menu(
    const qa_native_ui_controller *controller);
/** TS bindingCapture getter. */
bool qa_native_ui_controller_binding_capture(
    const qa_native_ui_controller *controller);
/** TS captureBinding(accept, cancel). */
void qa_native_ui_controller_capture_binding(
    qa_native_ui_controller *controller,
    void (*accept)(void *ctx, qa_physical_input input),
    void (*cancel)(void *ctx), void *ctx);

/** TS state(). */
qa_seat_ui_state qa_native_ui_controller_state(
    qa_native_ui_controller *controller);
/** TS presentation(state). Arrays borrow from the caller. */
void qa_native_ui_controller_presentation(
    qa_native_ui_controller *controller,
    const qa_ui_notification *notifications, size_t notification_count,
    const qa_center_print_state *center_print, bool show_scores);
/** TS openMenu(id). Aborts (TS Error) on an unknown menu. */
void qa_native_ui_controller_open_menu(qa_native_ui_controller *controller,
                                       qa_ui_menu_id id);
/** TS closeMenu(). */
void qa_native_ui_controller_close_menu(qa_native_ui_controller *controller);
/** TS closeAll(). */
void qa_native_ui_controller_close_all(qa_native_ui_controller *controller);
/** TS executeScript(script). */
void qa_native_ui_controller_execute_script(
    qa_native_ui_controller *controller, qa_legacy_ui_script script);
/** TS input(event). Aborts (TS Error) on another seat's event. */
bool qa_native_ui_controller_input(qa_native_ui_controller *controller,
                                   qa_seat_input_event event);
/** TS draw(context). Result is controller-owned (see header note). */
const qa_ui_draw_command *qa_native_ui_controller_draw(
    qa_native_ui_controller *controller, qa_ui_draw_context context,
    size_t *out_count);

#endif

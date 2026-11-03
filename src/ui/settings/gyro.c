/* Port of src/ui/settings/gyro.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/gyro.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/input/support.h"
#include "qa/ui/common/layout.h"

typedef struct gyro_state gyro_state;
typedef struct gctx { gyro_state *state; int action; } gctx;
struct gyro_state {
  qa_native_ui_controller *controller;
  qa_ui_gyro_settings settings;
  const char *title;
  char *error;
  int handle;
  qa_ui_control controls[12];
  gctx ctxs[12];
  char label[256];
  char status[256];
};

enum { G_NONE, G_ENABLED, G_CALIBRATE, G_CANCEL, G_RESET, G_YAW, G_PITCH,
       G_BACK };

static void set_error(gyro_state *s, const char *value) {
  free(s->error);
  s->error = qa_input_dup(value);
  if (s->error == NULL) qa_range_error("Out of memory");
}
static void save(gyro_state *s) {
  const char *error = s->settings.save(s->settings.ctx);
  if (error != NULL) set_error(s, error);
}
static qa_seat_id seat_of(gyro_state *s) {
  return qa_native_ui_controller_get_seat(s->controller);
}
static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  gctx *c = v;
  gyro_state *s = c->state;
  qa_input_router *router = s->settings.router;
  switch (c->action) {
    case G_CALIBRATE: {
      qa_controller_operation_result result =
          qa_input_router_begin_gyro_calibration(router, seat_of(s));
      set_error(s, result.kind == QA_CONTROLLER_OPERATION_ACCEPTED
                       ? ""
                       : result.reason);
      break;
    }
    case G_CANCEL:
      qa_input_router_cancel_gyro_calibration(router, seat_of(s));
      break;
    case G_RESET:
      qa_input_router_reset_gyro_calibration(router, seat_of(s));
      set_error(s, "");
      break;
    case G_BACK:
      qa_native_ui_controller_close_menu(s->controller);
      break;
    default: break;
  }
}
static void on_change_checked(void *v, qa_seat_id seat, bool value) {
  (void)seat;
  gctx *c = v;
  gyro_state *s = c->state;
  qa_controller_operation_result result =
      qa_input_router_set_gyro_enabled(s->settings.router, seat_of(s), value);
  set_error(s, result.kind == QA_CONTROLLER_OPERATION_ACCEPTED
                   ? ""
                   : result.reason);
  if (result.kind == QA_CONTROLLER_OPERATION_ACCEPTED) save(s);
}
static void on_change_value(void *v, qa_seat_id seat, double value) {
  (void)seat;
  gctx *c = v;
  gyro_state *s = c->state;
  qa_seat_input *input = qa_input_router_seat(s->settings.router, seat_of(s));
  if (input != NULL) {
    qa_gamepad_input *pad = qa_seat_input_gamepad(input);
    if (c->action == G_YAW) pad->tuning.gyro.yaw_sensitivity = value;
    else pad->tuning.gyro.pitch_sensitivity = value;
    save(s);
  }
}
static qa_ui_control button(gyro_state *s, size_t slot, const char *id,
                            const char *label, double row, int action,
                            bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.enabled = enabled;
  control.visible = true;
  s->ctxs[slot].state = s;
  s->ctxs[slot].action = action;
  control.ctx = &s->ctxs[slot];
  control.activate_seat = on_activate;
  return control;
}
static void menu_close(void *v, qa_seat_id seat) {
  gyro_state *s = v;
  qa_input_router_cancel_gyro_calibration(s->settings.router, seat);
}
static qa_ui_menu factory(void *v) {
  gyro_state *s = v;
  qa_seat_id seat = seat_of(s);
  qa_controller_device device;
  memset(&device, 0, sizeof device);
  bool have_device = s->settings.device(s->settings.ctx, &device);
  qa_seat_input *input = qa_input_router_seat(s->settings.router, seat);
  bool capable = false;
  if (have_device) {
    /* TS device.capabilities.sensors — the C device carries its
     * capabilities inline in newer headers; sensor presence is
     * tested through the capabilities member when present. */
    for (size_t i = 0; i < device.capabilities.sensor_count; i++)
      if (device.capabilities.sensors[i].kind == QA_CONTROLLER_SENSOR_GYRO)
        capable = true;
  }
  bool enabled = capable && !s->settings.busy(s->settings.ctx);
  qa_gyro_calibration_state calibration =
      qa_input_router_gyro_calibration(s->settings.router, seat);
  const char *status;
  if (!have_device) status = "No controller connected.";
  else if (!capable) status = "This controller has no gyroscope.";
  else if (calibration.kind == QA_GYRO_CALIBRATION_CALIBRATING) {
    snprintf(s->status, sizeof s->status, "Keep still: %s%%",
             QA_JS_NUMBER(qa_js_round(calibration.progress * 100)));
    status = s->status;
  } else if (calibration.kind == QA_GYRO_CALIBRATION_READY)
    status = "Calibration complete.";
  else
    status = "Place the controller on a steady surface.";
  const char *name = have_device && device.name != NULL ? device.name
                                                        : "Controller";
  snprintf(s->label, 51, "%s", name); /* TS slice(0, 50) */
  qa_ui_control *controls = s->controls;
  size_t at = 0;
  controls[at] = button(s, at, "ui:gyro:device", s->label, 0, G_NONE, false);
  at++;
  qa_ui_control toggle;
  memset(&toggle, 0, sizeof toggle);
  toggle.id = "ui:gyro:enabled";
  toggle.kind = "toggle";
  toggle.label = "Gyro aiming";
  toggle.rect = qa_ui_menu_row_default(1);
  toggle.visible = true;
  toggle.enabled = enabled;
  toggle.checked = input != NULL
                   && qa_seat_input_gamepad(input)->tuning.gyro.enabled;
  s->ctxs[at].state = s;
  s->ctxs[at].action = G_ENABLED;
  toggle.ctx = &s->ctxs[at];
  toggle.change_checked = on_change_checked;
  controls[at++] = toggle;
  controls[at] = button(
      s, at, "ui:gyro:calibrate",
      calibration.kind == QA_GYRO_CALIBRATION_READY ? "Recalibrate"
                                                    : "Calibrate",
      2, G_CALIBRATE,
      enabled && calibration.kind != QA_GYRO_CALIBRATION_CALIBRATING);
  at++;
  controls[at] = button(s, at, "ui:gyro:cancel", "Cancel calibration", 3,
                        G_CANCEL,
                        calibration.kind == QA_GYRO_CALIBRATION_CALIBRATING);
  at++;
  controls[at] = button(s, at, "ui:gyro:status",
                        s->error[0] != '\0' ? s->error : status, 4, G_NONE,
                        false);
  at++;
  controls[at] = button(s, at, "ui:gyro:reset", "Reset calibration", 5,
                        G_RESET, enabled);
  at++;
  controls[at] = button(s, at, "ui:gyro:reconnect",
                        "Recalibrate after reconnecting.", 6, G_NONE,
                        false);
  at++;
  controls[at] = button(s, at, "ui:gyro:saved",
                        s->settings.message(s->settings.ctx), 7, G_NONE,
                        false);
  at++;
  for (int axis = 0; axis < 2; axis++) {
    qa_ui_control slider;
    memset(&slider, 0, sizeof slider);
    slider.id = axis == 0 ? "ui:gyro:yawSensitivity"
                          : "ui:gyro:pitchSensitivity";
    slider.kind = "slider";
    slider.label = axis == 0 ? "Gyro yaw sensitivity"
                             : "Gyro pitch sensitivity";
    slider.rect = qa_ui_menu_row_default(8 + axis);
    slider.visible = true;
    slider.enabled = enabled;
    slider.minimum = 0;
    slider.maximum = 10;
    slider.step = 0.1;
    slider.value = input != NULL
                       ? (axis == 0
                              ? qa_seat_input_gamepad(input)
                                    ->tuning.gyro.yaw_sensitivity
                              : qa_seat_input_gamepad(input)
                                    ->tuning.gyro.pitch_sensitivity)
                       : 1;
    s->ctxs[at].state = s;
    s->ctxs[at].action = axis == 0 ? G_YAW : G_PITCH;
    slider.ctx = &s->ctxs[at];
    slider.change_value = on_change_value;
    controls[at++] = slider;
  }
  controls[at] = button(s, at, "ui:gyro:back", "Back", 11, G_BACK, true);
  at++;
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:settings:gyro";
  menu.title = s->title;
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.close = menu_close;
  return menu;
}
static void gyro_dispose(qa_ui_settings_menus *menus) {
  gyro_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  free(s->error);
  free(s);
  menus->state = NULL;
}
qa_ui_settings_menus qa_ui_register_gyro_settings_menu(
    qa_native_ui_controller *controller,
    const qa_ui_gyro_settings *settings, const char *title) {
  gyro_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->settings = *settings;
  s->title = title != NULL ? title : "Gyro controls";
  s->handle = -1;
  set_error(s, "");
  int handle = qa_native_ui_controller_register(controller,
                                                "menu:settings:gyro",
                                                factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:settings:gyro", s, gyro_dispose };
  return menus;
}

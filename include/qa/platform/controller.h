/*
 * Port of src/platform/controller.ts: one SDL2 game-controller event
 * owner with independently routed native handles, explicit-before-
 * automatic assignment (Q1/Q2 gamepad_assign intent), and a real
 * SDL virtual joystick for diagnostics. No gameplay or key
 * bindings. C accommodations: linked directly instead of Bun FFI;
 * the main-thread ownership check has no C counterpart (callers own
 * thread affinity, as in sdl_render_context.h); device/event lists
 * are malloc'd arrays the caller disposes. `number` arguments stay
 * double where validation is observable. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_CONTROLLER_H
#define QA_PLATFORM_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "qa/foundation/bounded_queue.h"

#define QA_CONTROLLER_AXIS_COUNT 6
#define QA_CONTROLLER_BUTTON_COUNT 21
#define QA_CONTROLLER_SENSOR_COUNT 6

typedef enum qa_controller_sensor {
  QA_CONTROLLER_SENSOR_ACCELEROMETER,
  QA_CONTROLLER_SENSOR_GYRO,
  QA_CONTROLLER_SENSOR_ACCELEROMETER_LEFT,
  QA_CONTROLLER_SENSOR_GYRO_LEFT,
  QA_CONTROLLER_SENSOR_ACCELEROMETER_RIGHT,
  QA_CONTROLLER_SENSOR_GYRO_RIGHT,
} qa_controller_sensor;

/* TS ControllerSensor spellings, in enum order. */
extern const char *const qa_controller_sensor_names[QA_CONTROLLER_SENSOR_COUNT];

typedef struct qa_controller_sensor_info {
  qa_controller_sensor kind;
  bool enabled;
  float rate_hz;
} qa_controller_sensor_info;

typedef struct qa_controller_capabilities {
  bool axes[QA_CONTROLLER_AXIS_COUNT];
  bool buttons[QA_CONTROLLER_BUTTON_COUNT];
  bool rumble;
  bool trigger_rumble;
  bool led;
  int touchpads;
  qa_controller_sensor_info sensors[QA_CONTROLLER_SENSOR_COUNT];
  size_t sensor_count;
} qa_controller_capabilities;

typedef struct qa_controller_device {
  int32_t instance;
  char *name;   /* owned */
  char *guid;   /* owned; NULL is TS null */
  char *serial; /* owned; NULL is TS null */
  /* Stable while this provider is open, even when an earlier identical pad disconnects. */
  int64_t ordinal;
  bool is_virtual;
  qa_controller_capabilities capabilities;
} qa_controller_device;

typedef struct qa_controller_state {
  int16_t axes[QA_CONTROLLER_AXIS_COUNT];
  bool buttons[QA_CONTROLLER_BUTTON_COUNT];
} qa_controller_state;

typedef enum qa_controller_selection_kind {
  QA_CONTROLLER_SELECTION_AUTOMATIC,
  QA_CONTROLLER_SELECTION_NONE,
  QA_CONTROLLER_SELECTION_DEVICE,
  QA_CONTROLLER_SELECTION_SERIAL,
} qa_controller_selection_kind;

typedef struct qa_controller_selection {
  qa_controller_selection_kind kind;
  char guid[33];   /* device/serial */
  double ordinal;  /* device */
  const char *serial; /* serial; borrowed at the call */
} qa_controller_selection;

typedef enum qa_controller_operation_kind {
  QA_CONTROLLER_OPERATION_ACCEPTED,
  QA_CONTROLLER_OPERATION_UNSUPPORTED,
  QA_CONTROLLER_OPERATION_DISCONNECTED,
  QA_CONTROLLER_OPERATION_FAILED,
} qa_controller_operation_kind;

typedef struct qa_controller_operation_result {
  qa_controller_operation_kind kind;
  char reason[256]; /* empty when accepted */
} qa_controller_operation_result;

typedef enum qa_controller_event_kind {
  QA_CONTROLLER_EVENT_CONNECTED,
  QA_CONTROLLER_EVENT_REMAPPED,
  QA_CONTROLLER_EVENT_ASSIGNMENT,
  QA_CONTROLLER_EVENT_DISCONNECTED,
  QA_CONTROLLER_EVENT_AXIS,
  QA_CONTROLLER_EVENT_BUTTON,
  QA_CONTROLLER_EVENT_TOUCHPAD,
  QA_CONTROLLER_EVENT_SENSOR,
  QA_CONTROLLER_EVENT_UNRECOGNIZED,
} qa_controller_event_kind;

typedef enum qa_controller_touchpad_phase {
  QA_CONTROLLER_TOUCHPAD_DOWN,
  QA_CONTROLLER_TOUCHPAD_MOTION,
  QA_CONTROLLER_TOUCHPAD_UP,
} qa_controller_touchpad_phase;

typedef struct qa_controller_event {
  qa_controller_event_kind kind;
  uint32_t timestamp;
  /* Device events (disconnected/axis/button/touchpad/sensor/unrecognized). */
  int32_t instance;
  int64_t slot; /* -1 is TS null */
  /* connected/remapped */
  qa_controller_device device;
  /* assignment */
  int64_t assignment_slot;
  int64_t assignment_previous; /* -1 (with the flag) is TS null */
  bool assignment_has_previous;
  int64_t assignment_instance;
  bool assignment_has_instance;
  /* axis/button */
  int axis;
  int value;
  int button;
  bool down;
  /* touchpad */
  qa_controller_touchpad_phase phase;
  int32_t touchpad;
  int32_t finger;
  float x;
  float y;
  float pressure;
  /* sensor */
  qa_controller_sensor sensor;
  float z;
  uint64_t timestamp_us;
  /* unrecognized */
  uint32_t type;
  uint8_t bytes[56];
} qa_controller_event;

typedef struct qa_sdl_controllers qa_sdl_controllers;

typedef struct qa_sdl_runtime {
  char version[16];
  const char *revision; /* SDL-owned */
} qa_sdl_runtime;

qa_sdl_runtime qa_sdl_controllers_runtime(void);

/* False only on OOM. Heap owner; close then free. */
qa_sdl_controllers *qa_sdl_controllers_open(void);
bool qa_sdl_controllers_closed(const qa_sdl_controllers *controllers);
/* Malloc'd; dispose with qa_controller_devices_free. NULL only on OOM. */
qa_controller_device *qa_sdl_controllers_devices(qa_sdl_controllers *controllers,
                                                 size_t *out_count);
void qa_controller_devices_free(qa_controller_device *devices, size_t count);
/* Malloc'd instance per slot (-1 unrouted), count == selections; caller frees. */
int64_t *qa_sdl_controllers_assignments(qa_sdl_controllers *controllers,
                                        size_t *out_count);
void qa_sdl_controllers_version(qa_sdl_controllers *controllers, char *out,
                                size_t out_size);
/* False when the instance is unknown or detached (TS null). */
bool qa_sdl_controllers_snapshot(qa_sdl_controllers *controllers, int32_t instance,
                                 qa_controller_state *out);
void qa_sdl_controllers_set_assignments(qa_sdl_controllers *controllers,
                                        const qa_controller_selection *selections,
                                        size_t count);
/* Malloc'd; dispose with qa_controller_events_free. NULL with count 0, or OOM. */
qa_controller_event *qa_sdl_controllers_poll_events(qa_sdl_controllers *controllers,
                                                    size_t *out_count);
void qa_controller_events_free(qa_controller_event *events, size_t count);
void qa_controller_device_dispose(qa_controller_device *device);
typedef enum qa_controller_mapping_result {
  QA_CONTROLLER_MAPPING_ADDED,
  QA_CONTROLLER_MAPPING_UPDATED,
} qa_controller_mapping_result;
qa_controller_mapping_result qa_sdl_controllers_add_mapping(
    qa_sdl_controllers *controllers, const char *mapping);
qa_controller_operation_result qa_sdl_controllers_rumble(
    qa_sdl_controllers *controllers, int32_t instance, double low, double high,
    double duration_ms);
qa_controller_operation_result qa_sdl_controllers_rumble_triggers(
    qa_sdl_controllers *controllers, int32_t instance, double left, double right,
    double duration_ms);
qa_controller_operation_result qa_sdl_controllers_set_led(
    qa_sdl_controllers *controllers, int32_t instance, double red, double green,
    double blue);
qa_controller_operation_result qa_sdl_controllers_set_sensor_enabled(
    qa_sdl_controllers *controllers, int32_t instance, qa_controller_sensor sensor,
    bool enabled);
/* accepted fills the sample (TS {kind:"sample"}); other results pass through. */
qa_controller_operation_result qa_sdl_controllers_read_sensor(
    qa_sdl_controllers *controllers, int32_t instance,
    qa_controller_sensor sensor, float *out_x, float *out_y, float *out_z,
    bool *out_is_sample);
void qa_sdl_controllers_close(qa_sdl_controllers *controllers);

typedef enum qa_virtual_controller_rumble_kind {
  QA_VIRTUAL_RUMBLE,
  QA_VIRTUAL_TRIGGER_RUMBLE,
} qa_virtual_controller_rumble_kind;

typedef struct qa_virtual_controller_rumble {
  qa_virtual_controller_rumble_kind kind;
  uint16_t low;
  uint16_t high;
} qa_virtual_controller_rumble;

typedef struct qa_virtual_sdl_controller qa_virtual_sdl_controller;

/* name NULL selects the TypeScript default. NULL only on OOM.
 * Heap controller; close then free. */
qa_virtual_sdl_controller *qa_virtual_sdl_controller_attach(const char *name,
                                                            bool rumble);
int32_t qa_virtual_sdl_controller_instance(const qa_virtual_sdl_controller *controller);
bool qa_virtual_sdl_controller_closed(const qa_virtual_sdl_controller *controller);
void qa_virtual_sdl_controller_set_axis(qa_virtual_sdl_controller *controller,
                                        double axis, double value);
void qa_virtual_sdl_controller_set_button(qa_virtual_sdl_controller *controller,
                                          double button, bool down);
/* Malloc'd effects in arrival order; caller frees. NULL with count 0. */
qa_virtual_controller_rumble *qa_virtual_sdl_controller_drain_rumble(
    qa_virtual_sdl_controller *controller, size_t *out_count);
void qa_virtual_sdl_controller_close(qa_virtual_sdl_controller *controller);

#endif

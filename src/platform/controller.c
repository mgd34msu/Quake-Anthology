/* Port of src/platform/controller.ts. GPL-2.0-or-later. */
#include "qa/platform/controller.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

/* TS sensorNames order; SDL sensor ids are index + 1 in this order. */
const char *const qa_controller_sensor_names[QA_CONTROLLER_SENSOR_COUNT] = {
    "accelerometer", "gyro", "accelerometer-left",
    "gyro-left", "accelerometer-right", "gyro-right",
};
static const int sensor_ids[QA_CONTROLLER_SENSOR_COUNT] = {1, 2, 3, 4, 5, 6};

static int checked(int value, const char *operation) {
  if (value < 0) qa_error("%s: %s", operation, SDL_GetError());
  return value;
}

static void *required_ptr(void *value, const char *operation) {
  if (value == NULL) qa_error("%s: %s", operation, SDL_GetError());
  return value;
}

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

static void integer(double value, double minimum, double maximum, const char *name) {
  if (!isfinite(value) || floor(value) != value || value < minimum ||
      value > maximum)
    qa_range_error("%s must be an integer in %.0f..%.0f", name, minimum, maximum);
}

static uint16_t amplitude(double value) {
  if (!isfinite(value) || value < 0 || value > 1)
    qa_range_error("Rumble amplitude must be finite and in 0..1");
  return (uint16_t)floor(value * 65535.0 + 0.5); /* Math.round */
}

static void initialize(void) {
  SDL_SetMainReady();
  if (SDL_SetHint("SDL_NO_SIGNAL_HANDLERS", "1") != SDL_TRUE)
    qa_error("SDL must leave signal handling to the Unix signal owner");
  checked(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER), "SDL_InitSubSystem controller");
}

typedef struct owned_controller {
  SDL_GameController *pointer;
  int32_t instance;
  int64_t ordinal;
  bool is_virtual;
  int16_t axes[QA_CONTROLLER_AXIS_COUNT];
  bool buttons[QA_CONTROLLER_BUTTON_COUNT];
  char *guid;
  char *name;
  char *serial;
} owned_controller;

struct qa_sdl_controllers {
  owned_controller *opened;
  size_t opened_count;
  size_t opened_capacity;
  qa_controller_event *pending;
  size_t pending_count;
  size_t pending_capacity;
  qa_controller_selection *selections;
  char **selection_serials; /* owned copies parallel to selections */
  size_t selection_count;
  int64_t *routes; /* instance or -1 */
  size_t route_count;
  int previous_event_state;
  bool closed;
};

static qa_sdl_controllers *owner_singleton = NULL;

static bool guid_valid(const char *text, size_t length) {
  if (length != 32) return false;
  for (size_t i = 0; i < 32; i++) {
    if (!isxdigit((unsigned char)text[i])) return false;
  }
  return true;
}

static void identity(SDL_GameController *pointer, char **out_guid, char **out_name,
                     char **out_serial) {
  char *guid = NULL;
  char *mapping = SDL_GameControllerMapping(pointer);
  if (mapping != NULL) {
    const char *comma = strchr(mapping, ',');
    size_t head = comma == NULL ? strlen(mapping) : (size_t)(comma - mapping);
    if (guid_valid(mapping, head)) {
      guid = (char *)malloc(33);
      if (guid != NULL) {
        for (size_t i = 0; i < 32; i++)
          guid[i] = (char)tolower((unsigned char)mapping[i]);
        guid[32] = '\0';
      }
    }
    SDL_free(mapping);
  }
  const char *name = SDL_GameControllerName(pointer);
  *out_guid = guid;
  *out_name = dup_text(name == NULL ? "Unnamed SDL controller" : name);
  *out_serial = dup_text(SDL_GameControllerGetSerial(pointer));
}

static qa_controller_capabilities capabilities(SDL_GameController *pointer) {
  qa_controller_capabilities result;
  memset(&result, 0, sizeof result);
  for (int axis = 0; axis < QA_CONTROLLER_AXIS_COUNT; axis++)
    result.axes[axis] = SDL_GameControllerHasAxis(pointer, axis) != SDL_FALSE;
  for (int button = 0; button < QA_CONTROLLER_BUTTON_COUNT; button++)
    result.buttons[button] =
        SDL_GameControllerHasButton(pointer, button) != SDL_FALSE;
  result.rumble = SDL_GameControllerHasRumble(pointer) != SDL_FALSE;
  result.trigger_rumble =
      SDL_GameControllerHasRumbleTriggers(pointer) != SDL_FALSE;
  result.led = SDL_GameControllerHasLED(pointer) != SDL_FALSE;
  result.touchpads =
      checked(SDL_GameControllerGetNumTouchpads(pointer),
              "SDL_GameControllerGetNumTouchpads");
  for (int i = 0; i < QA_CONTROLLER_SENSOR_COUNT; i++) {
    if (SDL_GameControllerHasSensor(pointer, (SDL_SensorType)sensor_ids[i]) ==
        SDL_FALSE)
      continue;
    qa_controller_sensor_info *info = &result.sensors[result.sensor_count++];
    info->kind = (qa_controller_sensor)i;
    info->enabled = SDL_GameControllerIsSensorEnabled(
                        pointer, (SDL_SensorType)sensor_ids[i]) != SDL_FALSE;
    info->rate_hz = SDL_GameControllerGetSensorDataRate(
        pointer, (SDL_SensorType)sensor_ids[i]);
  }
  return result;
}

void qa_controller_device_dispose(qa_controller_device *device) {
  free(device->name);
  free(device->guid);
  free(device->serial);
  device->name = NULL;
  device->guid = NULL;
  device->serial = NULL;
}

static qa_controller_device describe(const owned_controller *device) {
  qa_controller_device result;
  memset(&result, 0, sizeof result);
  result.instance = device->instance;
  result.name = dup_text(device->name);
  result.guid = dup_text(device->guid);
  result.serial = dup_text(device->serial);
  result.ordinal = device->ordinal;
  result.is_virtual = device->is_virtual;
  result.capabilities = capabilities(device->pointer);
  return result;
}

static void require_open(qa_sdl_controllers *controllers) {
  if (controllers->closed || owner_singleton != controllers)
    qa_error("SDL controller provider is closed");
}

static owned_controller *find_device(qa_sdl_controllers *controllers,
                                     int32_t instance) {
  for (size_t i = 0; i < controllers->opened_count; i++) {
    if (controllers->opened[i].instance == instance) return &controllers->opened[i];
  }
  return NULL;
}

static int64_t slot_of(const qa_sdl_controllers *controllers, int32_t instance) {
  for (size_t i = 0; i < controllers->route_count; i++) {
    if (controllers->routes[i] == (int64_t)instance) return (int64_t)i;
  }
  return -1;
}

static void push_event(qa_sdl_controllers *controllers,
                       const qa_controller_event *event) {
  if (controllers->pending_count == controllers->pending_capacity) {
    size_t capacity = controllers->pending_capacity == 0
                          ? 16
                          : controllers->pending_capacity * 2;
    qa_controller_event *next = (qa_controller_event *)realloc(
        controllers->pending, capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    controllers->pending = next;
    controllers->pending_capacity = capacity;
  }
  controllers->pending[controllers->pending_count++] = *event;
}

static void stop_device(qa_sdl_controllers *controllers, int32_t instance) {
  owned_controller *device = find_device(controllers, instance);
  if (device == NULL || SDL_GameControllerGetAttached(device->pointer) == SDL_FALSE)
    return;
  if (SDL_GameControllerHasRumble(device->pointer) != SDL_FALSE)
    SDL_GameControllerRumble(device->pointer, 0, 0, 0);
  if (SDL_GameControllerHasRumbleTriggers(device->pointer) != SDL_FALSE)
    SDL_GameControllerRumbleTriggers(device->pointer, 0, 0, 0);
}

static void resolve(qa_sdl_controllers *controllers, uint32_t timestamp) {
  size_t count = controllers->selection_count;
  int64_t *routes = NULL;
  if (count != 0) {
    routes = (int64_t *)malloc(count * sizeof *routes);
    if (routes == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < count; i++) routes[i] = -1;
  }
  /* Explicit selections first, then automatic; a device is claimed once. */
  for (int pass = 0; pass < 2; pass++) {
    for (size_t slot = 0; slot < count; slot++) {
      const qa_controller_selection *selection = &controllers->selections[slot];
      if (pass == 0 && selection->kind != QA_CONTROLLER_SELECTION_DEVICE &&
          selection->kind != QA_CONTROLLER_SELECTION_SERIAL)
        continue;
      if (pass == 1 && selection->kind != QA_CONTROLLER_SELECTION_AUTOMATIC)
        continue;
      owned_controller *match = NULL;
      size_t matches = 0;
      for (size_t d = 0; d < controllers->opened_count; d++) {
        owned_controller *device = &controllers->opened[d];
        bool claimed = false;
        for (size_t r = 0; r < count; r++) {
          if (routes[r] == (int64_t)device->instance) claimed = true;
        }
        if (claimed) continue;
        if (pass == 1) {
          match = device;
          matches = 1;
          break;
        }
        if (device->guid == NULL || strcmp(device->guid, selection->guid) != 0)
          continue;
        bool key_matches =
            selection->kind == QA_CONTROLLER_SELECTION_DEVICE
                ? device->ordinal == (int64_t)selection->ordinal
                : device->serial != NULL &&
                      strcmp(device->serial, controllers->selection_serials[slot]) == 0;
        if (key_matches) {
          match = device;
          matches++;
        }
      }
      if (match != NULL && matches == 1) routes[slot] = (int64_t)match->instance;
    }
  }
  size_t span = count > controllers->route_count ? count : controllers->route_count;
  for (size_t slot = 0; slot < span; slot++) {
    int64_t previous =
        slot < controllers->route_count ? controllers->routes[slot] : -1;
    int64_t instance = slot < count ? routes[slot] : -1;
    if (previous == instance) continue;
    if (previous != -1) stop_device(controllers, (int32_t)previous);
    qa_controller_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_CONTROLLER_EVENT_ASSIGNMENT;
    event.timestamp = timestamp;
    event.assignment_slot = (int64_t)slot;
    event.assignment_previous = previous;
    event.assignment_has_previous = previous != -1;
    event.assignment_instance = instance;
    event.assignment_has_instance = instance != -1;
    event.slot = -1;
    push_event(controllers, &event);
  }
  free(controllers->routes);
  controllers->routes = routes;
  controllers->route_count = count;
}

static void discover(qa_sdl_controllers *controllers, uint32_t timestamp,
                     bool single, int single_index) {
  int count = checked(SDL_NumJoysticks(), "SDL_NumJoysticks");
  int begin = single ? single_index : 0;
  int end = single ? (count < single_index + 1 ? count : single_index + 1) : count;
  for (int index = begin; index >= 0 && index < end; index++) {
    SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
    if (instance < 0 || find_device(controllers, instance) != NULL ||
        SDL_IsGameController(index) == SDL_FALSE)
      continue;
    SDL_GameController *pointer = (SDL_GameController *)required_ptr(
        SDL_GameControllerOpen(index), "SDL_GameControllerOpen");
    SDL_Joystick *joystick = (SDL_Joystick *)required_ptr(
        SDL_GameControllerGetJoystick(pointer), "SDL_GameControllerGetJoystick");
    if (checked(SDL_JoystickInstanceID(joystick), "SDL_JoystickInstanceID") !=
        instance) {
      SDL_GameControllerClose(pointer);
      qa_error("SDL controller instance changed while opening");
    }
    char *guid = NULL;
    char *name = NULL;
    char *serial = NULL;
    identity(pointer, &guid, &name, &serial);
    int64_t ordinal = 0;
    for (;;) {
      bool taken = false;
      for (size_t d = 0; d < controllers->opened_count; d++) {
        owned_controller *other = &controllers->opened[d];
        bool same_guid = (guid == NULL && other->guid == NULL) ||
                         (guid != NULL && other->guid != NULL &&
                          strcmp(guid, other->guid) == 0);
        if (same_guid && other->ordinal == ordinal) taken = true;
      }
      if (!taken) break;
      ordinal++;
    }
    if (controllers->opened_count == controllers->opened_capacity) {
      size_t capacity = controllers->opened_capacity == 0
                            ? 8
                            : controllers->opened_capacity * 2;
      owned_controller *next = (owned_controller *)realloc(controllers->opened,
                                                           capacity * sizeof *next);
      if (next == NULL) {
        SDL_GameControllerClose(pointer);
        free(guid);
        free(name);
        free(serial);
        qa_range_error("out of memory");
      }
      controllers->opened = next;
      controllers->opened_capacity = capacity;
    }
    owned_controller *device = &controllers->opened[controllers->opened_count];
    memset(device, 0, sizeof *device);
    device->pointer = pointer;
    device->instance = instance;
    device->ordinal = ordinal;
    device->is_virtual = SDL_JoystickIsVirtual(index) != SDL_FALSE;
    device->guid = guid;
    device->name = name;
    device->serial = serial;
    for (int axis = 0; axis < QA_CONTROLLER_AXIS_COUNT; axis++)
      device->axes[axis] = SDL_GameControllerGetAxis(pointer, axis);
    for (int button = 0; button < QA_CONTROLLER_BUTTON_COUNT; button++)
      device->buttons[button] =
          SDL_GameControllerGetButton(pointer, button) != 0;
    qa_controller_device description = describe(device);
    controllers->opened_count++;
    qa_controller_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_CONTROLLER_EVENT_CONNECTED;
    event.timestamp = timestamp;
    event.device = description;
    event.slot = -1;
    push_event(controllers, &event);
  }
  resolve(controllers, timestamp);
}

static void remove_device(qa_sdl_controllers *controllers, size_t index,
                          uint32_t timestamp) {
  owned_controller *device = &controllers->opened[index];
  int32_t instance = device->instance;
  int64_t slot = slot_of(controllers, instance);
  stop_device(controllers, instance);
  SDL_GameControllerClose(device->pointer);
  free(device->guid);
  free(device->name);
  free(device->serial);
  memmove(&controllers->opened[index], &controllers->opened[index + 1],
          (controllers->opened_count - index - 1) * sizeof *device);
  controllers->opened_count--;
  qa_controller_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_CONTROLLER_EVENT_DISCONNECTED;
  event.timestamp = timestamp;
  event.instance = instance;
  event.slot = slot;
  push_event(controllers, &event);
  resolve(controllers, timestamp);
}

static void collect(qa_sdl_controllers *controllers) {
  SDL_GameControllerUpdate();
  for (;;) {
    SDL_Event raw;
    memset(&raw, 0, sizeof raw);
    int count = checked(SDL_PeepEvents(&raw, 1, SDL_GETEVENT, 0x650, 0x66f),
                        "SDL_PeepEvents controller");
    if (count == 0) break;
    uint32_t type = raw.type;
    uint32_t timestamp = raw.common.timestamp;
    /* ADDED carries a device index in `which`. */
    if (type == SDL_CONTROLLERDEVICEADDED) {
      discover(controllers, timestamp, true, raw.cdevice.which);
      continue;
    }
    int32_t instance = raw.cdevice.which;
    size_t index = controllers->opened_count;
    for (size_t i = 0; i < controllers->opened_count; i++) {
      if (controllers->opened[i].instance == instance) index = i;
    }
    if (index == controllers->opened_count) continue;
    owned_controller *device = &controllers->opened[index];
    qa_controller_event event;
    memset(&event, 0, sizeof event);
    event.timestamp = timestamp;
    event.instance = instance;
    event.slot = slot_of(controllers, instance);
    switch (type) {
      case SDL_CONTROLLERAXISMOTION: {
        int axis = raw.caxis.axis;
        if (axis < QA_CONTROLLER_AXIS_COUNT) {
          device->axes[axis] = raw.caxis.value;
          event.kind = QA_CONTROLLER_EVENT_AXIS;
          event.axis = axis;
          event.value = raw.caxis.value;
          push_event(controllers, &event);
        }
        break;
      }
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int button = raw.cbutton.button;
        if (button < QA_CONTROLLER_BUTTON_COUNT) {
          bool down = type == SDL_CONTROLLERBUTTONDOWN;
          device->buttons[button] = down;
          event.kind = QA_CONTROLLER_EVENT_BUTTON;
          event.button = button;
          event.down = down;
          push_event(controllers, &event);
        }
        break;
      }
      case SDL_CONTROLLERDEVICEREMOVED:
        remove_device(controllers, index, timestamp);
        break;
      case SDL_CONTROLLERDEVICEREMAPPED: {
        char *guid = NULL;
        char *name = NULL;
        char *serial = NULL;
        identity(device->pointer, &guid, &name, &serial);
        free(device->guid);
        free(device->name);
        free(device->serial);
        device->guid = guid;
        device->name = name;
        device->serial = serial;
        for (int axis = 0; axis < QA_CONTROLLER_AXIS_COUNT; axis++)
          device->axes[axis] = SDL_GameControllerGetAxis(device->pointer, axis);
        for (int button = 0; button < QA_CONTROLLER_BUTTON_COUNT; button++)
          device->buttons[button] =
              SDL_GameControllerGetButton(device->pointer, button) != 0;
        event.kind = QA_CONTROLLER_EVENT_REMAPPED;
        event.device = describe(device);
        push_event(controllers, &event);
        resolve(controllers, timestamp);
        break;
      }
      case SDL_CONTROLLERTOUCHPADDOWN:
      case SDL_CONTROLLERTOUCHPADMOTION:
      case SDL_CONTROLLERTOUCHPADUP:
        event.kind = QA_CONTROLLER_EVENT_TOUCHPAD;
        event.phase = type == SDL_CONTROLLERTOUCHPADDOWN
                          ? QA_CONTROLLER_TOUCHPAD_DOWN
                          : type == SDL_CONTROLLERTOUCHPADMOTION
                                ? QA_CONTROLLER_TOUCHPAD_MOTION
                                : QA_CONTROLLER_TOUCHPAD_UP;
        event.touchpad = raw.ctouchpad.touchpad;
        event.finger = raw.ctouchpad.finger;
        event.x = raw.ctouchpad.x;
        event.y = raw.ctouchpad.y;
        event.pressure = raw.ctouchpad.pressure;
        push_event(controllers, &event);
        break;
      case SDL_CONTROLLERSENSORUPDATE: {
        int found = -1;
        for (int i = 0; i < QA_CONTROLLER_SENSOR_COUNT; i++) {
          if (sensor_ids[i] == raw.csensor.sensor) found = i;
        }
        if (found >= 0) {
          event.kind = QA_CONTROLLER_EVENT_SENSOR;
          event.sensor = (qa_controller_sensor)found;
          event.x = raw.csensor.data[0];
          event.y = raw.csensor.data[1];
          event.z = raw.csensor.data[2];
          event.timestamp_us = raw.csensor.timestamp_us;
        } else {
          event.kind = QA_CONTROLLER_EVENT_UNRECOGNIZED;
          event.type = type;
          memcpy(event.bytes, &raw, 56);
        }
        push_event(controllers, &event);
        break;
      }
      default:
        event.kind = QA_CONTROLLER_EVENT_UNRECOGNIZED;
        event.type = type;
        memcpy(event.bytes, &raw, 56);
        push_event(controllers, &event);
        break;
    }
  }
  /* A device can disappear while SDL events are disabled elsewhere. */
  for (size_t i = 0; i < controllers->opened_count;) {
    if (SDL_GameControllerGetAttached(controllers->opened[i].pointer) == SDL_FALSE)
      remove_device(controllers, i, SDL_GetTicks());
    else
      i++;
  }
}

qa_sdl_runtime qa_sdl_controllers_runtime(void) {
  SDL_version version;
  SDL_GetVersion(&version);
  qa_sdl_runtime result;
  snprintf(result.version, sizeof result.version, "%u.%u.%u", version.major,
           version.minor, version.patch);
  result.revision = SDL_GetRevision();
  return result;
}

qa_sdl_controllers *qa_sdl_controllers_open(void) {
  if (owner_singleton != NULL)
    qa_error("SDL controller events already have an owner");
  initialize();
  int previous = SDL_GameControllerEventState(SDL_QUERY);
  qa_sdl_controllers *controllers =
      (qa_sdl_controllers *)calloc(1, sizeof *controllers);
  if (controllers == NULL) {
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    return NULL;
  }
  controllers->previous_event_state = previous;
  owner_singleton = controllers;
  SDL_GameControllerEventState(SDL_ENABLE);
  discover(controllers, SDL_GetTicks(), false, 0);
  return controllers;
}

bool qa_sdl_controllers_closed(const qa_sdl_controllers *controllers) {
  return owner_singleton != controllers;
}

qa_controller_device *qa_sdl_controllers_devices(qa_sdl_controllers *controllers,
                                                 size_t *out_count) {
  require_open(controllers);
  *out_count = controllers->opened_count;
  if (controllers->opened_count == 0) return NULL;
  qa_controller_device *devices = (qa_controller_device *)malloc(
      controllers->opened_count * sizeof *devices);
  if (devices == NULL) return NULL;
  for (size_t i = 0; i < controllers->opened_count; i++)
    devices[i] = describe(&controllers->opened[i]);
  return devices;
}

void qa_controller_devices_free(qa_controller_device *devices, size_t count) {
  if (devices == NULL) return;
  for (size_t i = 0; i < count; i++) qa_controller_device_dispose(&devices[i]);
  free(devices);
}

int64_t *qa_sdl_controllers_assignments(qa_sdl_controllers *controllers,
                                        size_t *out_count) {
  require_open(controllers);
  *out_count = controllers->route_count;
  if (controllers->route_count == 0) return NULL;
  int64_t *routes =
      (int64_t *)malloc(controllers->route_count * sizeof *routes);
  if (routes == NULL) return NULL;
  memcpy(routes, controllers->routes, controllers->route_count * sizeof *routes);
  return routes;
}

void qa_sdl_controllers_version(qa_sdl_controllers *controllers, char *out,
                                size_t out_size) {
  require_open(controllers);
  qa_sdl_runtime runtime = qa_sdl_controllers_runtime();
  snprintf(out, out_size, "%s", runtime.version);
}

bool qa_sdl_controllers_snapshot(qa_sdl_controllers *controllers, int32_t instance,
                                 qa_controller_state *out) {
  require_open(controllers);
  owned_controller *device = find_device(controllers, instance);
  if (device == NULL ||
      SDL_GameControllerGetAttached(device->pointer) == SDL_FALSE)
    return false;
  memcpy(out->axes, device->axes, sizeof out->axes);
  memcpy(out->buttons, device->buttons, sizeof out->buttons);
  return true;
}

void qa_sdl_controllers_set_assignments(qa_sdl_controllers *controllers,
                                        const qa_controller_selection *selections,
                                        size_t count) {
  require_open(controllers);
  for (size_t i = 0; i < count; i++) {
    const qa_controller_selection *selection = &selections[i];
    if (selection->kind == QA_CONTROLLER_SELECTION_DEVICE ||
        selection->kind == QA_CONTROLLER_SELECTION_SERIAL) {
      if (!guid_valid(selection->guid, strlen(selection->guid)))
        qa_error("Controller assignment GUID requires 32 hexadecimal digits");
      if (selection->kind == QA_CONTROLLER_SELECTION_DEVICE)
        integer(selection->ordinal, 0, 0x7fffffff, "Controller ordinal");
      else if (selection->serial == NULL || selection->serial[0] == '\0')
        qa_error("Controller serial must be nonempty and contain no NUL");
    }
  }
  collect(controllers);
  for (size_t i = 0; i < controllers->selection_count; i++)
    free(controllers->selection_serials[i]);
  free(controllers->selections);
  free(controllers->selection_serials);
  controllers->selections = NULL;
  controllers->selection_serials = NULL;
  controllers->selection_count = 0;
  if (count != 0) {
    controllers->selections =
        (qa_controller_selection *)malloc(count * sizeof *selections);
    controllers->selection_serials = (char **)calloc(count, sizeof(char *));
    if (controllers->selections == NULL || controllers->selection_serials == NULL)
      qa_range_error("out of memory");
    memcpy(controllers->selections, selections, count * sizeof *selections);
    for (size_t i = 0; i < count; i++) {
      qa_controller_selection *copy = &controllers->selections[i];
      if (copy->kind == QA_CONTROLLER_SELECTION_DEVICE ||
          copy->kind == QA_CONTROLLER_SELECTION_SERIAL) {
        for (size_t c = 0; copy->guid[c] != '\0'; c++)
          copy->guid[c] = (char)tolower((unsigned char)copy->guid[c]);
      }
      if (copy->kind == QA_CONTROLLER_SELECTION_SERIAL) {
        controllers->selection_serials[i] = dup_text(copy->serial);
        if (controllers->selection_serials[i] == NULL)
          qa_range_error("out of memory");
      }
      copy->serial = controllers->selection_serials[i];
    }
    controllers->selection_count = count;
  }
  resolve(controllers, SDL_GetTicks());
}

qa_controller_event *qa_sdl_controllers_poll_events(qa_sdl_controllers *controllers,
                                                    size_t *out_count) {
  require_open(controllers);
  collect(controllers);
  qa_controller_event *events = controllers->pending;
  *out_count = controllers->pending_count;
  controllers->pending = NULL;
  controllers->pending_count = 0;
  controllers->pending_capacity = 0;
  return events;
}

void qa_controller_events_free(qa_controller_event *events, size_t count) {
  if (events == NULL) return;
  for (size_t i = 0; i < count; i++) {
    if (events[i].kind == QA_CONTROLLER_EVENT_CONNECTED ||
        events[i].kind == QA_CONTROLLER_EVENT_REMAPPED)
      qa_controller_device_dispose(&events[i].device);
  }
  free(events);
}

qa_controller_mapping_result qa_sdl_controllers_add_mapping(
    qa_sdl_controllers *controllers, const char *mapping) {
  require_open(controllers);
  /* /^[0-9a-f]{32},[^,]+,/i */
  bool valid = strlen(mapping) > 34 && guid_valid(mapping, 32) &&
               mapping[32] == ',';
  if (valid) {
    const char *comma = strchr(mapping + 33, ',');
    valid = comma != NULL && comma != mapping + 33;
  }
  if (!valid)
    qa_error("SDL controller mapping requires a GUID, name and bindings");
  int result = checked(SDL_GameControllerAddMapping(mapping),
                       "SDL_GameControllerAddMapping");
  discover(controllers, SDL_GetTicks(), false, 0);
  return result == 0 ? QA_CONTROLLER_MAPPING_UPDATED : QA_CONTROLLER_MAPPING_ADDED;
}

typedef enum operate_capability {
  CAP_RUMBLE,
  CAP_TRIGGER_RUMBLE,
  CAP_LED,
  CAP_SENSOR,
} operate_capability;

static qa_controller_operation_result operate(
    qa_sdl_controllers *controllers, int32_t instance, operate_capability cap,
    qa_controller_sensor sensor,
    int (*operation)(SDL_GameController *pointer, void *ctx), void *ctx) {
  require_open(controllers);
  qa_controller_operation_result result;
  memset(&result, 0, sizeof result);
  owned_controller *device = find_device(controllers, instance);
  if (device == NULL ||
      SDL_GameControllerGetAttached(device->pointer) == SDL_FALSE) {
    result.kind = QA_CONTROLLER_OPERATION_DISCONNECTED;
    snprintf(result.reason, sizeof result.reason,
             "SDL controller %d is not connected", (int)instance);
    return result;
  }
  qa_controller_capabilities support = capabilities(device->pointer);
  const char *capability_name = qa_controller_sensor_names[sensor];
  bool supported = false;
  switch (cap) {
    case CAP_RUMBLE: supported = support.rumble; capability_name = "rumble"; break;
    case CAP_TRIGGER_RUMBLE:
      supported = support.trigger_rumble;
      capability_name = "triggerRumble";
      break;
    case CAP_LED: supported = support.led; capability_name = "led"; break;
    case CAP_SENSOR:
      for (size_t i = 0; i < support.sensor_count; i++) {
        if (support.sensors[i].kind == sensor) supported = true;
      }
      break;
  }
  if (!supported) {
    result.kind = QA_CONTROLLER_OPERATION_UNSUPPORTED;
    snprintf(result.reason, sizeof result.reason, "%s does not support %s",
             device->name, capability_name);
    return result;
  }
  if (operation(device->pointer, ctx) >= 0) {
    result.kind = QA_CONTROLLER_OPERATION_ACCEPTED;
    return result;
  }
  const char *message = SDL_GetError();
  result.kind = QA_CONTROLLER_OPERATION_FAILED;
  if (message != NULL && message[0] != '\0')
    snprintf(result.reason, sizeof result.reason, "%s", message);
  else
    snprintf(result.reason, sizeof result.reason,
             "SDL controller %s failed without an SDL error message",
             capability_name);
  return result;
}

typedef struct rumble_args {
  uint16_t low;
  uint16_t high;
  uint32_t duration;
} rumble_args;
static int do_rumble(SDL_GameController *pointer, void *ctx) {
  const rumble_args *args = (const rumble_args *)ctx;
  return SDL_GameControllerRumble(pointer, args->low, args->high, args->duration);
}
static int do_rumble_triggers(SDL_GameController *pointer, void *ctx) {
  const rumble_args *args = (const rumble_args *)ctx;
  return SDL_GameControllerRumbleTriggers(pointer, args->low, args->high,
                                          args->duration);
}
typedef struct led_args {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} led_args;
static int do_led(SDL_GameController *pointer, void *ctx) {
  const led_args *args = (const led_args *)ctx;
  return SDL_GameControllerSetLED(pointer, args->red, args->green, args->blue);
}
typedef struct sensor_args {
  int id;
  int enabled;
  float *values;
} sensor_args;
static int do_sensor_enabled(SDL_GameController *pointer, void *ctx) {
  const sensor_args *args = (const sensor_args *)ctx;
  return SDL_GameControllerSetSensorEnabled(pointer, (SDL_SensorType)args->id,
                                            args->enabled ? SDL_TRUE : SDL_FALSE);
}
static int do_sensor_read(SDL_GameController *pointer, void *ctx) {
  const sensor_args *args = (const sensor_args *)ctx;
  return SDL_GameControllerGetSensorData(pointer, (SDL_SensorType)args->id,
                                         args->values, 3);
}

qa_controller_operation_result qa_sdl_controllers_rumble(
    qa_sdl_controllers *controllers, int32_t instance, double low, double high,
    double duration_ms) {
  rumble_args args;
  args.low = amplitude(low);
  args.high = amplitude(high);
  integer(duration_ms, 0, 4294967295.0, "Rumble duration");
  args.duration = (uint32_t)duration_ms;
  return operate(controllers, instance, CAP_RUMBLE, QA_CONTROLLER_SENSOR_GYRO,
                 do_rumble, &args);
}

qa_controller_operation_result qa_sdl_controllers_rumble_triggers(
    qa_sdl_controllers *controllers, int32_t instance, double left, double right,
    double duration_ms) {
  rumble_args args;
  args.low = amplitude(left);
  args.high = amplitude(right);
  integer(duration_ms, 0, 4294967295.0, "Rumble duration");
  args.duration = (uint32_t)duration_ms;
  return operate(controllers, instance, CAP_TRIGGER_RUMBLE,
                 QA_CONTROLLER_SENSOR_GYRO, do_rumble_triggers, &args);
}

qa_controller_operation_result qa_sdl_controllers_set_led(
    qa_sdl_controllers *controllers, int32_t instance, double red, double green,
    double blue) {
  integer(red, 0, 255, "LED component");
  integer(green, 0, 255, "LED component");
  integer(blue, 0, 255, "LED component");
  led_args args = {(uint8_t)red, (uint8_t)green, (uint8_t)blue};
  return operate(controllers, instance, CAP_LED, QA_CONTROLLER_SENSOR_GYRO, do_led,
                 &args);
}

qa_controller_operation_result qa_sdl_controllers_set_sensor_enabled(
    qa_sdl_controllers *controllers, int32_t instance, qa_controller_sensor sensor,
    bool enabled) {
  sensor_args args = {sensor_ids[sensor], enabled ? 1 : 0, NULL};
  return operate(controllers, instance, CAP_SENSOR, sensor, do_sensor_enabled,
                 &args);
}

qa_controller_operation_result qa_sdl_controllers_read_sensor(
    qa_sdl_controllers *controllers, int32_t instance,
    qa_controller_sensor sensor, float *out_x, float *out_y, float *out_z,
    bool *out_is_sample) {
  float values[3] = {0, 0, 0};
  sensor_args args = {sensor_ids[sensor], 0, values};
  qa_controller_operation_result result =
      operate(controllers, instance, CAP_SENSOR, sensor, do_sensor_read, &args);
  *out_is_sample = result.kind == QA_CONTROLLER_OPERATION_ACCEPTED;
  if (*out_is_sample) {
    *out_x = values[0];
    *out_y = values[1];
    *out_z = values[2];
  }
  return result;
}

void qa_sdl_controllers_close(qa_sdl_controllers *controllers) {
  if (controllers == NULL || owner_singleton != controllers) return;
  for (size_t i = 0; i < controllers->opened_count; i++) {
    stop_device(controllers, controllers->opened[i].instance);
    SDL_GameControllerClose(controllers->opened[i].pointer);
    free(controllers->opened[i].guid);
    free(controllers->opened[i].name);
    free(controllers->opened[i].serial);
  }
  free(controllers->opened);
  qa_controller_events_free(controllers->pending, controllers->pending_count);
  for (size_t i = 0; i < controllers->selection_count; i++)
    free(controllers->selection_serials[i]);
  free(controllers->selection_serials);
  free(controllers->selections);
  free(controllers->routes);
  controllers->closed = true;
  SDL_GameControllerEventState(controllers->previous_event_state);
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
  owner_singleton = NULL;
}

struct qa_virtual_sdl_controller {
  SDL_Joystick *pointer;
  int32_t instance;
  qa_bounded_queue effects;
  char *name;
  void *sink;
};

typedef struct rumble_sink {
  struct qa_virtual_sdl_controller *controller;
} rumble_sink;

static int virtual_rumble(void *userdata, Uint16 low, Uint16 high) {
  rumble_sink *sink = (rumble_sink *)userdata;
  qa_virtual_controller_rumble effect = {QA_VIRTUAL_RUMBLE, low, high};
  qa_bounded_queue_push(&sink->controller->effects, &effect);
  return 0;
}

static int virtual_rumble_triggers(void *userdata, Uint16 low, Uint16 high) {
  rumble_sink *sink = (rumble_sink *)userdata;
  qa_virtual_controller_rumble effect = {QA_VIRTUAL_TRIGGER_RUMBLE, low, high};
  qa_bounded_queue_push(&sink->controller->effects, &effect);
  return 0;
}

qa_virtual_sdl_controller *qa_virtual_sdl_controller_attach(const char *name,
                                                            bool rumble) {
  initialize();
  qa_virtual_sdl_controller *controller =
      (qa_virtual_sdl_controller *)calloc(1, sizeof *controller);
  rumble_sink *sink = (rumble_sink *)malloc(sizeof *sink);
  if (controller == NULL || sink == NULL ||
      !qa_bounded_queue_init(&controller->effects, 4096,
                             sizeof(qa_virtual_controller_rumble))) {
    free(controller);
    free(sink);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    return NULL;
  }
  sink->controller = controller;
  controller->sink = sink;
  controller->name = dup_text(name == NULL ? "Quake SDL virtual controller" : name);
  if (controller->name == NULL) {
    qa_bounded_queue_destroy(&controller->effects);
    free(controller);
    free(sink);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    return NULL;
  }
  SDL_VirtualJoystickDesc descriptor;
  memset(&descriptor, 0, sizeof descriptor);
  descriptor.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  descriptor.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
  descriptor.naxes = QA_CONTROLLER_AXIS_COUNT;
  descriptor.nbuttons = QA_CONTROLLER_BUTTON_COUNT;
  descriptor.button_mask = (Uint32)((1u << QA_CONTROLLER_BUTTON_COUNT) - 1u);
  descriptor.axis_mask = (Uint32)((1u << QA_CONTROLLER_AXIS_COUNT) - 1u);
  descriptor.name = controller->name;
  descriptor.userdata = sink;
  if (rumble) {
    descriptor.Rumble = virtual_rumble;
    descriptor.RumbleTriggers = virtual_rumble_triggers;
  }
  int index = -1;
  SDL_Joystick *pointer = NULL;
  index = SDL_JoystickAttachVirtualEx(&descriptor);
  if (index < 0) {
    const char *message = SDL_GetError();
    qa_bounded_queue_destroy(&controller->effects);
    free(controller->name);
    free(controller);
    free(sink);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    qa_error("SDL_JoystickAttachVirtualEx: %s", message);
  }
  pointer = SDL_JoystickOpen(index);
  if (pointer == NULL) {
    const char *message = SDL_GetError();
    SDL_JoystickDetachVirtual(index);
    qa_bounded_queue_destroy(&controller->effects);
    free(controller->name);
    free(controller);
    free(sink);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    qa_error("SDL_JoystickOpen virtual: %s", message);
  }
  int32_t instance =
      checked(SDL_JoystickInstanceID(pointer), "SDL_JoystickInstanceID virtual");
  /* Virtual trigger joysticks use -32768 for the unpressed end of their mapping. */
  const int trigger_axes[2] = {4, 5};
  for (int i = 0; i < 2; i++)
    checked(SDL_JoystickSetVirtualAxis(pointer, trigger_axes[i], -32768),
            "SDL_JoystickSetVirtualAxis trigger");
  SDL_JoystickUpdate();
  controller->pointer = pointer;
  controller->instance = instance;
  return controller;
}

int32_t qa_virtual_sdl_controller_instance(
    const qa_virtual_sdl_controller *controller) {
  return controller->instance;
}

bool qa_virtual_sdl_controller_closed(const qa_virtual_sdl_controller *controller) {
  return controller->pointer == NULL;
}

static SDL_Joystick *virtual_opened(qa_virtual_sdl_controller *controller) {
  if (controller->pointer == NULL) qa_error("SDL virtual controller is closed");
  return controller->pointer;
}

void qa_virtual_sdl_controller_set_axis(qa_virtual_sdl_controller *controller,
                                        double axis, double value) {
  SDL_Joystick *pointer = virtual_opened(controller);
  integer(axis, 0, QA_CONTROLLER_AXIS_COUNT - 1, "Controller axis");
  integer(value, -32768, 32767, "Controller axis value");
  checked(SDL_JoystickSetVirtualAxis(pointer, (int)axis, (Sint16)value),
          "SDL_JoystickSetVirtualAxis");
  SDL_JoystickUpdate();
}

void qa_virtual_sdl_controller_set_button(qa_virtual_sdl_controller *controller,
                                          double button, bool down) {
  SDL_Joystick *pointer = virtual_opened(controller);
  integer(button, 0, QA_CONTROLLER_BUTTON_COUNT - 1, "Controller button");
  checked(SDL_JoystickSetVirtualButton(pointer, (int)button, down ? 1 : 0),
          "SDL_JoystickSetVirtualButton");
  SDL_JoystickUpdate();
}

qa_virtual_controller_rumble *qa_virtual_sdl_controller_drain_rumble(
    qa_virtual_sdl_controller *controller, size_t *out_count) {
  (void)virtual_opened(controller);
  return (qa_virtual_controller_rumble *)qa_bounded_queue_take_all(
      &controller->effects, out_count);
}

void qa_virtual_sdl_controller_close(qa_virtual_sdl_controller *controller) {
  if (controller == NULL || controller->pointer == NULL) return;
  SDL_Joystick *pointer = controller->pointer;
  int count = checked(SDL_NumJoysticks(), "SDL_NumJoysticks virtual detach");
  for (int index = 0; index < count; index++) {
    if (SDL_JoystickGetDeviceInstanceID(index) == controller->instance) {
      checked(SDL_JoystickDetachVirtual(index), "SDL_JoystickDetachVirtual");
      break;
    }
  }
  controller->pointer = NULL;
  SDL_JoystickClose(pointer);
  free(controller->sink);
  controller->sink = NULL;
  free(controller->name);
  controller->name = NULL;
  qa_bounded_queue_destroy(&controller->effects);
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}



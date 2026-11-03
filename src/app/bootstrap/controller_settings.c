/* Port of src/app/bootstrap/controller-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/controller_settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/gamepad.h"
#include "qa/llm/errors.h"

/* TS Profile. identity_device false is {kind:"seat"}. */
typedef struct profile {
  double instance;
  bool identity_device;
  char guid[33];
  char *serial;
  char *path;
  qa_gyro_tuning fallback;
  bool busy;
  char *message;
} profile;
typedef struct profile_entry { qa_seat_id seat; profile *profile; } profile_entry;
typedef struct fallback_entry { qa_seat_id seat; qa_gyro_tuning gyro; } fallback_entry;
typedef struct ui_view {
  qa_app_controller_settings *settings;
  qa_seat_id seat;
  bool has_device;
  qa_controller_device device; /* last device() answer, owned */
} ui_view;

struct qa_app_controller_settings {
  qa_input_router *router;
  qa_seat_id *seats; size_t seat_count;
  qa_app_controller_devices_fn devices; void *devices_ctx;
  qa_config_store owned_store; bool owns_store;
  const qa_config_store *store;
  qa_app_controller_report_fn report; void *report_ctx;
  profile_entry *profiles; size_t profile_count, profile_capacity;
  fallback_entry *fallbacks; size_t fallback_count, fallback_capacity;
  ui_view **views; size_t view_count, view_capacity;
  bool closed;
};

static void *grow(void *items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return items;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}

static void profile_free(profile *value) {
  if (value == NULL) return;
  free(value->serial); free(value->path); free(value->message); free(value);
}

static void set_message(profile *value, const char *text) {
  char *copy = dup_text(text);
  free(value->message);
  value->message = copy;
}

static profile_entry *find_profile(const qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->profile_count; i++)
    if (qa_same_seat(settings->profiles[i].seat, seat)) return &settings->profiles[i];
  return NULL;
}

static void delete_profile_at(qa_app_controller_settings *settings, size_t index) {
  profile_free(settings->profiles[index].profile);
  memmove(&settings->profiles[index], &settings->profiles[index + 1],
          (settings->profile_count - index - 1) * sizeof *settings->profiles);
  settings->profile_count--;
}

static void delete_profile(qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->profile_count; i++)
    if (qa_same_seat(settings->profiles[i].seat, seat)) { delete_profile_at(settings, i); return; }
}

/* TS profiles.set(seat, profile): replaces in place, keeping Map order. */
static void set_profile(qa_app_controller_settings *settings, qa_seat_id seat, profile *value) {
  profile_entry *existing = find_profile(settings, seat);
  if (existing != NULL) {
    if (existing->profile != value) profile_free(existing->profile);
    existing->profile = value;
    return;
  }
  settings->profiles = grow(settings->profiles, &settings->profile_capacity, settings->profile_count,
                            sizeof *settings->profiles);
  settings->profiles[settings->profile_count].seat = seat;
  settings->profiles[settings->profile_count].profile = value;
  settings->profile_count++;
}

static fallback_entry *find_fallback(const qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->fallback_count; i++)
    if (qa_same_seat(settings->fallbacks[i].seat, seat)) return &settings->fallbacks[i];
  return NULL;
}

/* { ...(router.seat(seat)?.gamepad.tuning.gyro ?? defaultGamepadTuning.gyro) } */
static qa_gyro_tuning seat_gyro(const qa_app_controller_settings *settings, qa_seat_id seat) {
  qa_seat_input *input = qa_input_router_seat(settings->router, seat);
  return input != NULL ? qa_seat_input_gamepad(input)->tuning.gyro : qa_default_gamepad_tuning.gyro;
}

static void add_fallback(qa_app_controller_settings *settings, qa_seat_id seat) {
  settings->fallbacks = grow(settings->fallbacks, &settings->fallback_capacity, settings->fallback_count,
                             sizeof *settings->fallbacks);
  settings->fallbacks[settings->fallback_count].seat = seat;
  settings->fallbacks[settings->fallback_count].gyro = seat_gyro(settings, seat);
  settings->fallback_count++;
}

static void set_seats(qa_app_controller_settings *settings, const qa_seat_id *seats, size_t count) {
  qa_seat_id *copy = count == 0 ? NULL : malloc(count * sizeof *copy);
  if (count != 0 && copy == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(copy, seats, count * sizeof *copy);
  free(settings->seats);
  settings->seats = copy; settings->seat_count = count;
}

qa_app_controller_settings *qa_app_controller_settings_create(qa_input_router *router,
    const qa_seat_id *seats, size_t seat_count, qa_app_controller_devices_fn devices, void *devices_ctx,
    const qa_config_store *store, qa_app_controller_report_fn report, void *report_ctx) {
  qa_app_controller_settings *settings = calloc(1, sizeof *settings);
  if (settings == NULL) return NULL;
  settings->router = router;
  settings->devices = devices; settings->devices_ctx = devices_ctx;
  settings->report = report; settings->report_ctx = report_ctx;
  if (store == NULL) {
    /* TS default: join(homedir(), ".local", "share", "quake-typescript", "settings"). */
    const char *home = getenv("HOME");
    char root[4096];
    snprintf(root, sizeof root, "%s/.local/share/quake-typescript/settings", home != NULL ? home : "");
    if (!qa_config_store_init(&settings->owned_store, root)) { free(settings); return NULL; }
    settings->owns_store = true;
    settings->store = &settings->owned_store;
  } else settings->store = store;
  set_seats(settings, seats, seat_count);
  for (size_t i = 0; i < seat_count; i++) {
    fallback_entry *existing = find_fallback(settings, seats[i]);
    if (existing != NULL) existing->gyro = seat_gyro(settings, seats[i]);
    else add_fallback(settings, seats[i]);
  }
  return settings;
}

void qa_app_controller_settings_destroy(qa_app_controller_settings *settings) {
  if (settings == NULL) return;
  for (size_t i = 0; i < settings->profile_count; i++) profile_free(settings->profiles[i].profile);
  for (size_t i = 0; i < settings->view_count; i++) {
    if (settings->views[i]->has_device) qa_controller_device_dispose(&settings->views[i]->device);
    free(settings->views[i]);
  }
  free(settings->views); free(settings->profiles); free(settings->fallbacks); free(settings->seats);
  if (settings->owns_store) qa_config_store_destroy(&settings->owned_store);
  free(settings);
}

qa_input_router *qa_app_controller_settings_router(const qa_app_controller_settings *settings) {
  return settings->router;
}

void qa_app_controller_settings_settle(qa_app_controller_settings *settings) { (void)settings; }

static bool listed(const qa_seat_id *seats, size_t count, qa_seat_id seat) {
  for (size_t i = 0; i < count; i++) if (qa_same_seat(seats[i], seat)) return true;
  return false;
}

void qa_app_controller_settings_publish_seats(qa_app_controller_settings *settings, const qa_seat_id *seats, size_t seat_count) {
  /* TS "Controller profiles are still loading": the synchronous port has
   * no pending loads between calls. */
  set_seats(settings, seats, seat_count);
  for (size_t i = settings->profile_count; i-- > 0;)
    if (!listed(seats, seat_count, settings->profiles[i].seat)) delete_profile_at(settings, i);
  for (size_t i = settings->fallback_count; i-- > 0;)
    if (!listed(seats, seat_count, settings->fallbacks[i].seat)) {
      memmove(&settings->fallbacks[i], &settings->fallbacks[i + 1],
              (settings->fallback_count - i - 1) * sizeof *settings->fallbacks);
      settings->fallback_count--;
    }
  for (size_t i = 0; i < seat_count; i++)
    if (find_fallback(settings, seats[i]) == NULL) add_fallback(settings, seats[i]);
}

static profile *copy_profile(const profile *source) {
  profile *copy = calloc(1, sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  *copy = *source;
  copy->serial = source->serial != NULL ? dup_text(source->serial) : NULL;
  copy->path = dup_text(source->path);
  copy->message = dup_text(source->message);
  return copy;
}

void qa_app_controller_settings_copy_settled_profiles_from(qa_app_controller_settings *settings,
    const qa_app_controller_settings *previous) {
  for (size_t i = 0; i < settings->seat_count; i++) {
    profile_entry *prior = find_profile(previous, settings->seats[i]);
    if (prior != NULL) set_profile(settings, settings->seats[i], copy_profile(prior->profile));
  }
}

static bool controller_for(const qa_app_controller_settings *settings, qa_seat_id seat, double *out) {
  return qa_input_router_controller_for(settings->router, seat, out);
}

/* TS current(seat, profile). */
static bool current(const qa_app_controller_settings *settings, qa_seat_id seat, const profile *value) {
  if (settings->closed) return false;
  profile_entry *entry = find_profile(settings, seat);
  if (entry == NULL || entry->profile != value) return false;
  double instance;
  return controller_for(settings, seat, &instance) && instance == value->instance;
}

static void failed(qa_app_controller_settings *settings, profile *value, const char *message) {
  set_message(value, message);
  if (settings->report != NULL) {
    size_t length = strlen(value->message) + 2;
    char *line = malloc(length);
    if (line == NULL) qa_range_error("Out of memory");
    snprintf(line, length, "%s\n", value->message);
    settings->report(settings->report_ctx, line);
    free(line);
  }
}

/* TS load(seat, profile): the try body returns an error message where the
 * TS throws; the catch and finally run below. */
static const char *load_body(qa_app_controller_settings *settings, qa_seat_id seat, profile *value,
                             bool *returned, qa_controller_operation_result *result) {
  qa_gyro_profile saved;
  memset(&saved, 0, sizeof saved);
  bool has_saved = qa_config_store_load_gyro(settings->store, value->path, &saved);
  if (!current(settings, seat, value)) {
    if (has_saved) qa_gyro_profile_free(&saved);
    *returned = true;
    return NULL;
  }
  qa_seat_input *input = qa_input_router_seat(settings->router, seat);
  if (input != NULL) {
    if (has_saved && (saved.identity_is_device != value->identity_device
        || (saved.identity_is_device && (!value->identity_device || strcmp(saved.guid, value->guid) != 0
            || strcmp(saved.serial != NULL ? saved.serial : "", value->serial != NULL ? value->serial : "") != 0)))) {
      qa_gyro_profile_free(&saved);
      return "Gyro settings belong to another controller";
    }
    qa_gyro_tuning tuning = has_saved ? saved.tuning : value->fallback;
    if (has_saved) qa_gyro_profile_free(&saved);
    qa_gamepad_input *gamepad = qa_seat_input_gamepad(input);
    gamepad->tuning.gyro = tuning;
    gamepad->tuning.gyro.enabled = false;
    *result = qa_input_router_set_gyro_enabled(settings->router, seat, tuning.enabled);
    if (result->kind != QA_CONTROLLER_OPERATION_ACCEPTED) return result->reason;
  } else if (has_saved) qa_gyro_profile_free(&saved);
  set_message(value, value->identity_device ? "Saved for this controller and seat." : "Saved for this seat.");
  return NULL;
}

static void load(qa_app_controller_settings *settings, qa_seat_id seat, profile *value) {
  bool returned = false;
  qa_controller_operation_result result;
  memset(&result, 0, sizeof result);
  const char *error = load_body(settings, seat, value, &returned, &result);
  if (error != NULL && current(settings, seat, value)) failed(settings, value, error);
  if (current(settings, seat, value)) value->busy = false;
}

static bool find_device(qa_app_controller_settings *settings, double instance, qa_controller_device *out) {
  size_t count = 0;
  qa_controller_device *devices = settings->devices(settings->devices_ctx, &count);
  bool found = false;
  for (size_t i = 0; i < count && !found; i++) {
    if ((double)devices[i].instance != instance) continue;
    *out = devices[i];
    memset(&devices[i], 0, sizeof devices[i]); /* ownership moves to out */
    found = true;
  }
  qa_controller_devices_free(devices, count);
  return found;
}

void qa_app_controller_settings_update(qa_app_controller_settings *settings) {
  if (settings->closed) return;
  for (size_t i = 0; i < settings->seat_count; i++) {
    qa_seat_id seat = settings->seats[i];
    double instance;
    bool routed = controller_for(settings, seat, &instance);
    profile_entry *entry = find_profile(settings, seat);
    if (!routed) { delete_profile(settings, seat); continue; }
    if (entry != NULL && entry->profile->instance == instance) continue;
    qa_controller_device device;
    if (!find_device(settings, instance, &device)) continue;
    profile *value = calloc(1, sizeof *value);
    if (value == NULL) qa_range_error("Out of memory");
    value->instance = instance;
    value->identity_device = device.guid != NULL && device.serial != NULL && device.serial[0] != '\0';
    char name[33 + 1 + 65];
    if (value->identity_device) {
      snprintf(value->guid, sizeof value->guid, "%s", device.guid);
      value->serial = dup_text(device.serial);
      char digest[65];
      qa_llm_sha256_hex(value->serial, strlen(value->serial), digest);
      snprintf(name, sizeof name, "%s-%s", value->guid, digest);
    } else snprintf(name, sizeof name, "seat");
    qa_controller_device_dispose(&device);
    char path[256];
    snprintf(path, sizeof path, "controllers/seat-%lld/%s.json", (long long)(seat.index + 1), name);
    value->path = dup_text(path);
    fallback_entry *fallback = find_fallback(settings, seat);
    value->fallback = fallback != NULL ? fallback->gyro : qa_default_gamepad_tuning.gyro;
    value->busy = true;
    value->message = dup_text("Loading settings...");
    set_profile(settings, seat, value);
    qa_seat_input *input = qa_input_router_seat(settings->router, seat);
    if (input != NULL) qa_seat_input_gamepad(input)->tuning.gyro = qa_default_gamepad_tuning.gyro;
    (void)qa_input_router_set_gyro_enabled(settings->router, seat, false); /* TS discards the result */
    load(settings, seat, value);
  }
}

bool qa_app_controller_settings_busy(const qa_app_controller_settings *settings, qa_seat_id seat) {
  profile_entry *entry = find_profile(settings, seat);
  return entry != NULL && entry->profile->busy;
}

const char *qa_app_controller_settings_message(const qa_app_controller_settings *settings, qa_seat_id seat) {
  profile_entry *entry = find_profile(settings, seat);
  return entry != NULL ? entry->profile->message : "Connect a controller with a gyroscope.";
}

void qa_app_controller_settings_save(qa_app_controller_settings *settings, qa_seat_id seat) {
  profile_entry *entry = find_profile(settings, seat);
  qa_seat_input *input = qa_input_router_seat(settings->router, seat);
  if (entry == NULL || input == NULL) return;
  profile *value = entry->profile;
  if (!current(settings, seat, value) || value->busy) return;
  qa_gyro_tuning tuning = qa_seat_input_gamepad(input)->tuning.gyro;
  value->busy = true;
  qa_gyro_profile saved;
  memset(&saved, 0, sizeof saved);
  saved.version = 1;
  saved.identity_is_device = value->identity_device;
  memcpy(saved.guid, value->guid, sizeof saved.guid);
  saved.serial = value->serial;
  saved.tuning = tuning;
  errno = 0;
  if (!qa_config_store_save_gyro(settings->store, value->path, &saved) && current(settings, seat, value))
    failed(settings, value, errno != 0 ? strerror(errno) : "Gyro settings could not be saved");
  if (current(settings, seat, value)) value->busy = false;
}

static bool view_device(void *ctx, qa_controller_device *out) {
  ui_view *view = ctx;
  qa_app_controller_settings *settings = view->settings;
  if (view->has_device) { qa_controller_device_dispose(&view->device); view->has_device = false; }
  if (settings->closed) return false;
  double instance;
  if (!controller_for(settings, view->seat, &instance)) return false; /* find(... === null) */
  if (!find_device(settings, instance, &view->device)) return false;
  view->has_device = true;
  *out = view->device; /* borrowed until the next device() call */
  return true;
}
static bool view_busy(void *ctx) { ui_view *view = ctx; return qa_app_controller_settings_busy(view->settings, view->seat); }
static const char *view_message(void *ctx) { ui_view *view = ctx; return qa_app_controller_settings_message(view->settings, view->seat); }
static const char *view_save(void *ctx) { ui_view *view = ctx; qa_app_controller_settings_save(view->settings, view->seat); return NULL; }

qa_ui_gyro_settings qa_app_controller_settings_ui(qa_app_controller_settings *settings, qa_seat_id seat) {
  ui_view *view = calloc(1, sizeof *view);
  if (view == NULL) qa_range_error("Out of memory");
  view->settings = settings; view->seat = seat;
  settings->views = grow(settings->views, &settings->view_capacity, settings->view_count, sizeof *settings->views);
  settings->views[settings->view_count++] = view;
  qa_ui_gyro_settings ui = {view, settings->router, view_device, view_busy, view_message, view_save};
  return ui;
}

void qa_app_controller_settings_close(qa_app_controller_settings *settings) {
  settings->closed = true;
  for (size_t i = 0; i < settings->profile_count; i++) profile_free(settings->profiles[i].profile);
  settings->profile_count = 0;
}

/* Port of src/app/bootstrap/controller-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/controller_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct {
  qa_seat_id seat;
  double instance;
  bool device_identity;
  char *guid, *serial, *path;
  bool busy;
  char message[512];
} profile_t;
struct qa_app_controller_settings {
  qa_app_controller_settings_host host;
  qa_seat_id *seats; size_t seat_count;
  profile_t *profiles; size_t profile_count, profile_cap;
  long pending;
  bool closed;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static profile_t *find_profile(qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->profile_count; i++)
    if (qa_same_seat(settings->profiles[i].seat, seat)) return &settings->profiles[i];
  return NULL;
}
static void remove_profile(qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->profile_count; i++)
    if (qa_same_seat(settings->profiles[i].seat, seat)) {
      free(settings->profiles[i].guid); free(settings->profiles[i].serial); free(settings->profiles[i].path);
      memmove(&settings->profiles[i], &settings->profiles[i + 1], (settings->profile_count - i - 1) * sizeof *settings->profiles);
      settings->profile_count--;
      return;
    }
}
qa_app_controller_settings *qa_app_controller_settings_create(qa_app_controller_settings_host host, const qa_seat_id *seats, size_t seat_count) {
  qa_app_controller_settings *settings = calloc(1, sizeof *settings);
  if (!settings) return NULL;
  settings->host = host;
  settings->seats = malloc((seat_count ? seat_count : 1) * sizeof *settings->seats);
  memcpy(settings->seats, seats, seat_count * sizeof *seats);
  settings->seat_count = seat_count;
  return settings;
}
void qa_app_controller_settings_destroy(qa_app_controller_settings *settings) {
  if (!settings) return;
  for (size_t i = 0; i < settings->profile_count; i++) {
    free(settings->profiles[i].guid); free(settings->profiles[i].serial); free(settings->profiles[i].path);
  }
  free(settings->profiles);
  free(settings->seats);
  free(settings);
}
void qa_app_controller_settings_publish_seats(qa_app_controller_settings *settings, const qa_seat_id *seats, size_t seat_count) {
  if (settings->pending != 0) qa_range_error("Controller profiles are still loading");
  for (size_t i = 0; i < settings->profile_count;) {
    bool kept = false;
    for (size_t j = 0; j < seat_count; j++) if (qa_same_seat(settings->profiles[i].seat, seats[j])) kept = true;
    if (kept) i++; else remove_profile(settings, settings->profiles[i].seat);
  }
  free(settings->seats);
  settings->seats = malloc((seat_count ? seat_count : 1) * sizeof *settings->seats);
  memcpy(settings->seats, seats, seat_count * sizeof *seats);
  settings->seat_count = seat_count;
}
void qa_app_controller_settings_copy_settled_from(qa_app_controller_settings *settings, const qa_app_controller_settings *previous) {
  if (previous->pending != 0) qa_range_error("Controller profiles are still loading");
  for (size_t i = 0; i < settings->seat_count; i++) {
    for (size_t j = 0; j < previous->profile_count; j++) {
      if (!qa_same_seat(previous->profiles[j].seat, settings->seats[i])) continue;
      remove_profile(settings, settings->seats[i]);
      if (settings->profile_count == settings->profile_cap) {
        settings->profile_cap = settings->profile_cap ? settings->profile_cap * 2 : 8;
        settings->profiles = realloc(settings->profiles, settings->profile_cap * sizeof *settings->profiles);
      }
      profile_t copy = previous->profiles[j];
      copy.guid = dup(copy.guid); copy.serial = dup(copy.serial); copy.path = dup(copy.path);
      settings->profiles[settings->profile_count++] = copy;
    }
  }
}
void qa_app_controller_settings_update(qa_app_controller_settings *settings) {
  if (settings->closed) return;
  for (size_t s = 0; s < settings->seat_count; s++) {
    qa_seat_id seat = settings->seats[s];
    double instance = settings->host.controller_for(settings->host.ctx, seat);
    profile_t *current = find_profile(settings, seat);
    if (instance < 0) { remove_profile(settings, seat); continue; }
    if (current && current->instance == instance) continue;
    qa_app_controller_device_view device;
    if (!settings->host.device(settings->host.ctx, instance, &device)) continue;
    bool device_identity = device.guid && device.serial && device.serial[0] != '\0';
    char name[512];
    if (!device_identity) snprintf(name, sizeof name, "seat");
    else {
      char hex[65];
      settings->host.sha256_hex(settings->host.ctx, device.serial, hex);
      snprintf(name, sizeof name, "%s-%s", device.guid, hex);
    }
    remove_profile(settings, seat);
    if (settings->profile_count == settings->profile_cap) {
      settings->profile_cap = settings->profile_cap ? settings->profile_cap * 2 : 8;
      settings->profiles = realloc(settings->profiles, settings->profile_cap * sizeof *settings->profiles);
    }
    profile_t *profile = &settings->profiles[settings->profile_count++];
    memset(profile, 0, sizeof *profile);
    profile->seat = seat;
    profile->instance = instance;
    profile->device_identity = device_identity;
    profile->guid = dup(device_identity ? device.guid : NULL);
    profile->serial = dup(device_identity ? device.serial : NULL);
    char path[1024];
    snprintf(path, sizeof path, "controllers/seat-%lld/%s.json", (long long)(seat.index + 1), name);
    profile->path = dup(path);
    profile->busy = true;
    snprintf(profile->message, sizeof profile->message, "Loading settings...");
    settings->host.reset_gyro(settings->host.ctx, seat);
    settings->pending++;
    const char *error = settings->host.load_profile(settings->host.ctx, seat, profile->path, device_identity, profile->guid, profile->serial);
    settings->pending--;
    /* Synchronous port: current() is re-checked by instance identity. */
    profile_t *after = find_profile(settings, seat);
    if (after == profile && !settings->closed && settings->host.controller_for(settings->host.ctx, seat) == instance) {
      if (error) {
        snprintf(profile->message, sizeof profile->message, "%s", error);
        char report[600];
        snprintf(report, sizeof report, "%s\n", error);
        if (settings->host.report) settings->host.report(settings->host.ctx, report);
      } else {
        snprintf(profile->message, sizeof profile->message, "%s",
            device_identity ? "Saved for this controller and seat." : "Saved for this seat.");
      }
      profile->busy = false;
    }
  }
}
bool qa_app_controller_settings_busy(const qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->profile_count; i++)
    if (qa_same_seat(settings->profiles[i].seat, seat)) return settings->profiles[i].busy;
  return false;
}
const char *qa_app_controller_settings_message(const qa_app_controller_settings *settings, qa_seat_id seat) {
  for (size_t i = 0; i < settings->profile_count; i++)
    if (qa_same_seat(settings->profiles[i].seat, seat)) return settings->profiles[i].message;
  return "Connect a controller with a gyroscope.";
}
const char *qa_app_controller_settings_save(qa_app_controller_settings *settings, qa_seat_id seat) {
  profile_t *profile = find_profile(settings, seat);
  if (!profile || settings->closed || settings->host.controller_for(settings->host.ctx, seat) != profile->instance || profile->busy) return NULL;
  profile->busy = true;
  const char *error = settings->host.save_profile(settings->host.ctx, seat, profile->path, profile->device_identity, profile->guid, profile->serial);
  if (error && find_profile(settings, seat) == profile) {
    snprintf(profile->message, sizeof profile->message, "%s", error);
    char report[600];
    snprintf(report, sizeof report, "%s\n", error);
    if (settings->host.report) settings->host.report(settings->host.ctx, report);
  }
  if (find_profile(settings, seat) == profile) profile->busy = false;
  return error;
}
void qa_app_controller_settings_close(qa_app_controller_settings *settings) {
  settings->closed = true;
  for (size_t i = 0; i < settings->profile_count; i++) {
    free(settings->profiles[i].guid); free(settings->profiles[i].serial); free(settings->profiles[i].path);
  }
  settings->profile_count = 0;
}

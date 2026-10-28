/* Port of src/app/bootstrap/input-devices.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/input_devices.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/foundation/support.h"
bool qa_app_input_device_store(const char *user_content_root, qa_config_store *out) {
  char *root = qa_app_console_config_root(user_content_root);
  bool ok = qa_config_store_init(out, root);
  free(root);
  return ok;
}
qa_cvar_archive_entry *qa_app_load_input_device_settings(const qa_config_store *store, size_t *out_count) {
  const char *owner[] = {"input", "devices"};
  return qa_app_load_cvar_archive(store, owner, 2, QA_COMMAND_DIALECT_Q3, out_count);
}
struct qa_app_input_devices {
  qa_cvar_registry *cvars;
  const qa_config_store *store;
  qa_app_input_devices_host host;
  bool has_router;
  char *signature;
  double retry_at;
  bool closed;
};
static char *selection(qa_app_input_devices *devices) {
  static const char *const names[] = {"in_midi", "in_mididevice", "in_midichannel", "in_midiseat", "in_joystick", "in_joystickProfile", "in_joystickSeat"};
  char *parts[7];
  size_t total = 1;
  for (size_t i = 0; i < 7; i++) { parts[i] = qa_cvar_registry_variable_string(devices->cvars, names[i]); total += strlen(parts[i]) + 1; }
  char *joined = malloc(total);
  size_t len = 0;
  for (size_t i = 0; i < 7; i++) { len += (size_t)snprintf(joined + len, total - len, "%s%s", i ? "/" : "", parts[i]); free(parts[i]); }
  return joined;
}
qa_app_input_devices *qa_app_input_devices_create(qa_cvar_registry *cvars, const qa_config_store *store, qa_app_input_devices_host host) {
  qa_app_input_devices *devices = calloc(1, sizeof *devices);
  if (!devices) return NULL;
  devices->cvars = cvars;
  devices->store = store;
  devices->host = host;
  devices->signature = calloc(1, 1);
  /* registerInputDeviceCvars is host/device-settings owned. */
  return devices;
}
void qa_app_input_devices_destroy(qa_app_input_devices *devices) {
  if (!devices) return;
  qa_app_input_devices_close(devices);
  free(devices->signature);
  free(devices);
}
void qa_app_input_devices_release(qa_app_input_devices *devices, double time, bool has_commands) {
  if (has_commands) {
    devices->host.seat_release(devices->host.ctx, true, time, true);
    devices->host.seat_release(devices->host.ctx, false, time, true);
  }
  devices->host.midi_release(devices->host.ctx, time, has_commands);
  devices->host.joystick_release(devices->host.ctx, time, has_commands);
}
void qa_app_input_devices_activate(qa_app_input_devices *devices, const void *router) {
  (void)router;
  if (devices->closed) qa_range_error("Input device owner is closed");
  qa_app_input_devices_release(devices, devices->host.now(devices->host.ctx), false);
  devices->host.router_set_joystick(devices->host.ctx, false, false, 0);
  devices->has_router = true;
  if (devices->signature[0] == '\0') qa_app_input_devices_restart(devices);
  devices->host.router_set_joystick(devices->host.ctx, devices->host.joystick_has_instance(devices->host.ctx), true,
                                    qa_cvar_registry_variable_value(devices->cvars, "in_joystickSeat") - 1);
}
void qa_app_input_devices_restart(qa_app_input_devices *devices) {
  if (devices->closed) qa_range_error("Input device owner is closed");
  qa_app_input_devices_release(devices, devices->host.now(devices->host.ctx), false);
  devices->host.midi_restart(devices->host.ctx);
  devices->host.joystick_restart(devices->host.ctx);
  free(devices->signature);
  devices->signature = selection(devices);
  devices->host.router_set_joystick(devices->host.ctx, devices->host.joystick_has_instance(devices->host.ctx), true,
                                    qa_cvar_registry_variable_value(devices->cvars, "in_joystickSeat") - 1);
}
void qa_app_input_devices_frame(qa_app_input_devices *devices, double now) {
  if (devices->closed || !devices->has_router) return;
  char *current = selection(devices);
  bool changed = strcmp(current, devices->signature) != 0;
  free(current);
  if (changed) qa_app_input_devices_restart(devices);
  if (now >= devices->retry_at) {
    devices->retry_at = now + 1000;
    if (qa_cvar_registry_variable_value(devices->cvars, "in_midi") != 0 && !devices->host.midi_connected(devices->host.ctx))
      devices->host.midi_restart(devices->host.ctx);
    if (qa_cvar_registry_variable_value(devices->cvars, "in_joystick") != 0 && !devices->host.joystick_has_instance(devices->host.ctx))
      devices->host.joystick_restart(devices->host.ctx);
    devices->host.router_set_joystick(devices->host.ctx, devices->host.joystick_has_instance(devices->host.ctx), true,
                                      qa_cvar_registry_variable_value(devices->cvars, "in_joystickSeat") - 1);
  }
  if (!devices->host.router_has_focused(devices->host.ctx)) qa_app_input_devices_release(devices, now, false);
  devices->host.midi_frame(devices->host.ctx, now);
  devices->host.joystick_frame(devices->host.ctx, now);
  devices->host.router_set_joystick(devices->host.ctx, devices->host.joystick_has_instance(devices->host.ctx), true,
                                    qa_cvar_registry_variable_value(devices->cvars, "in_joystickSeat") - 1);
}
void qa_app_input_devices_info(qa_app_input_devices *devices) { devices->host.midi_info(devices->host.ctx); }
void qa_app_input_devices_save(qa_app_input_devices *devices) {
  /* Filtered archive save to cvars/input/devices.json (dialect q3);
   * the inputDeviceCvarNames filter is applied by the archive writer
   * host row through saveCvarArchive entries. */
  size_t count = 0;
  qa_cvar_archive_entry *entries = qa_cvar_registry_archive_entries(devices->cvars, NULL, NULL, &count);
  const char *owner[] = {"input", "devices"};
  qa_app_save_cvar_archive(devices->store, owner, 2, devices->cvars, entries, count);
  qa_app_cvar_archive_entries_free(entries, count);
}
void qa_app_input_devices_close(qa_app_input_devices *devices) {
  if (!devices || devices->closed) return;
  qa_app_input_devices_release(devices, devices->host.now(devices->host.ctx), false);
  devices->host.router_set_joystick(devices->host.ctx, false, false, 0);
  devices->closed = true;
  devices->has_router = false;
  devices->host.midi_close(devices->host.ctx);
  devices->host.joystick_close(devices->host.ctx);
}

/* Port of src/app/bootstrap/input-devices.ts. MIDI/joystick device
 * owners (input/midi, input/source-input) and the router are host
 * services; selection signature, retry cadence and closed guards are
 * kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_INPUT_DEVICES_H
#define QA_APP_BOOTSTRAP_INPUT_DEVICES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/core/cvars/index.h"
#include "qa/settings/config.h"
/* TS inputDeviceStore: fills a store at the console config root. */
bool qa_app_input_device_store(const char *user_content_root, qa_config_store *out);
qa_cvar_archive_entry *qa_app_load_input_device_settings(const qa_config_store *store, size_t *out_count);
typedef struct qa_app_input_devices qa_app_input_devices;
typedef struct qa_app_input_devices_host {
  void *ctx;
  void (*print)(void *ctx, const char *text);
  /* Router surface. */
  bool (*router_has_focused)(void *ctx);
  void (*router_set_joystick)(void *ctx, bool has_instance, bool has_seat, double seat_index);
  bool (*target_seat)(void *ctx, double seat_index); /* resolves in_*seat */
  /* Device owners. */
  void (*midi_restart)(void *ctx);
  void (*midi_release)(void *ctx, double time, bool has_commands);
  void (*midi_frame)(void *ctx, double now);
  void (*midi_info)(void *ctx);
  void (*midi_close)(void *ctx);
  bool (*midi_connected)(void *ctx);
  void (*joystick_restart)(void *ctx);
  void (*joystick_release)(void *ctx, double time, bool has_commands);
  void (*joystick_frame)(void *ctx, double now);
  void (*joystick_close)(void *ctx);
  bool (*joystick_has_instance)(void *ctx);
  void (*seat_release)(void *ctx, bool midi_seat, double time, bool has_commands);
  double (*now)(void *ctx); /* performance.now */
} qa_app_input_devices_host;
qa_app_input_devices *qa_app_input_devices_create(qa_cvar_registry *cvars, const qa_config_store *store, qa_app_input_devices_host host);
void qa_app_input_devices_destroy(qa_app_input_devices *devices);
void qa_app_input_devices_activate(qa_app_input_devices *devices, const void *router);
void qa_app_input_devices_release(qa_app_input_devices *devices, double time, bool has_commands);
void qa_app_input_devices_restart(qa_app_input_devices *devices);
void qa_app_input_devices_frame(qa_app_input_devices *devices, double now);
void qa_app_input_devices_info(qa_app_input_devices *devices);
void qa_app_input_devices_save(qa_app_input_devices *devices);
void qa_app_input_devices_close(qa_app_input_devices *devices);
#endif

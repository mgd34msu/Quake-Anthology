/* Port of src/app/bootstrap/input-devices.ts: one MIDI and Source
 * joystick device owner that follows the published input router. The
 * async save runs synchronously (PORTING.md). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_INPUT_DEVICES_H
#define QA_APP_BOOTSTRAP_INPUT_DEVICES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/core/cvars/index.h"
#include "qa/input/midi.h"
#include "qa/input/router.h"
#include "qa/input/seat.h"
#include "qa/settings/config.h"
#include "qa/ui/settings/index.h"
/* TS inputDeviceStore: fills a store at the console config root. */
bool qa_app_input_device_store(const char *user_content_root, qa_config_store *out);
/* TS loadInputDeviceSettings. Malloc'd entries; free with
 * qa_app_cvar_archive_entries_free. */
qa_cvar_archive_entry *qa_app_load_input_device_settings(const qa_config_store *store, size_t *out_count);
typedef struct qa_app_input_devices qa_app_input_devices;
/* TS constructor(cvars, store, print, midiBoundary?). cvars and store are
 * borrowed; midi_boundary NULL is the TS undefined (the MIDI owner opens
 * the Linux boundary). NULL only on allocation failure. */
qa_app_input_devices *qa_app_input_devices_create(qa_cvar_registry *cvars, const qa_config_store *store,
    qa_command_print_fn print, void *print_ctx, const qa_midi_input_boundary *midi_boundary);
void qa_app_input_devices_destroy(qa_app_input_devices *devices);
/* TS bindPrint(print): the devices outlive each world's input, so the sink
 * follows the adopting input. */
void qa_app_input_devices_bind_print(qa_app_input_devices *devices, qa_command_print_fn print, void *print_ctx);
void qa_app_input_devices_activate(qa_app_input_devices *devices, qa_input_router *router);
/* commands NULL is the TS undefined argument. */
void qa_app_input_devices_release(qa_app_input_devices *devices, double time,
    const qa_command_append_target *commands);
void qa_app_input_devices_restart(qa_app_input_devices *devices);
void qa_app_input_devices_frame(qa_app_input_devices *devices, double now);
void qa_app_input_devices_info(qa_app_input_devices *devices);
void qa_app_input_devices_save(qa_app_input_devices *devices);
/* TS bindings(): owned array; free with qa_ui_setting_bindings_free. The
 * bindings borrow the device owner and must not outlive it. */
qa_ui_setting_bindings qa_app_input_devices_bindings(qa_app_input_devices *devices);
void qa_app_input_devices_close(qa_app_input_devices *devices);
/* performance.now() over CLOCK_MONOTONIC. */
double qa_app_performance_now(void);
#endif

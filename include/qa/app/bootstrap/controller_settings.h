/* Port of src/app/bootstrap/controller-settings.ts: per-seat gyro
 * profile disk I/O. Active gyro tuning and calibration remain in the
 * input router. The TS async load/save run synchronously (PORTING.md),
 * so settle() has nothing to wait for and the pending set is always
 * empty between calls. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONTROLLER_SETTINGS_H
#define QA_APP_BOOTSTRAP_CONTROLLER_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/input/router.h"
#include "qa/platform/controller.h"
#include "qa/settings/config.h"
#include "qa/ui/settings/gyro.h"
typedef struct qa_app_controller_settings qa_app_controller_settings;
/* TS () => readonly ControllerDevice[]: malloc'd devices the callee
 * frees with qa_controller_devices_free. */
typedef qa_controller_device *(*qa_app_controller_devices_fn)(void *ctx, size_t *out_count);
/* TS report(message). */
typedef void (*qa_app_controller_report_fn)(void *ctx, const char *message);
/* store NULL is the TS default ConfigStore(~/.local/share/quake-typescript/
 * settings), owned here; otherwise borrowed. report NULL is the TS no-op.
 * The router is borrowed. NULL only on allocation failure. */
qa_app_controller_settings *qa_app_controller_settings_create(qa_input_router *router,
    const qa_seat_id *seats, size_t seat_count, qa_app_controller_devices_fn devices, void *devices_ctx,
    const qa_config_store *store, qa_app_controller_report_fn report, void *report_ctx);
void qa_app_controller_settings_destroy(qa_app_controller_settings *settings);
qa_input_router *qa_app_controller_settings_router(const qa_app_controller_settings *settings);
/* TS settle(): synchronous loads leave nothing pending. */
void qa_app_controller_settings_settle(qa_app_controller_settings *settings);
void qa_app_controller_settings_publish_seats(qa_app_controller_settings *settings, const qa_seat_id *seats, size_t seat_count);
void qa_app_controller_settings_copy_settled_profiles_from(qa_app_controller_settings *settings,
    const qa_app_controller_settings *previous);
void qa_app_controller_settings_update(qa_app_controller_settings *settings);
bool qa_app_controller_settings_busy(const qa_app_controller_settings *settings, qa_seat_id seat);
/* Borrowed text valid until the profile changes. */
const char *qa_app_controller_settings_message(const qa_app_controller_settings *settings, qa_seat_id seat);
void qa_app_controller_settings_save(qa_app_controller_settings *settings, qa_seat_id seat);
/* TS ui(seat). The view's ctx is settings-owned and lives until destroy. */
qa_ui_gyro_settings qa_app_controller_settings_ui(qa_app_controller_settings *settings, qa_seat_id seat);
void qa_app_controller_settings_close(qa_app_controller_settings *settings);
#endif

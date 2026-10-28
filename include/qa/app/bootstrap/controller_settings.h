/* Port of src/app/bootstrap/controller-settings.ts. Router/device IO
 * is host-wired; profile bookkeeping, profile path naming (host
 * sha256 hex of the serial) and status text are kept here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONTROLLER_SETTINGS_H
#define QA_APP_BOOTSTRAP_CONTROLLER_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
typedef struct qa_app_controller_settings qa_app_controller_settings;
/* TS ControllerDevice fields read here. */
typedef struct qa_app_controller_device_view {
  double instance;
  const char *guid, *serial; /* NULL = TS null */
} qa_app_controller_device_view;
typedef struct qa_app_controller_settings_host {
  void *ctx;
  void (*report)(void *ctx, const char *message);
  /* Returns instance or -1 for TS null. */
  double (*controller_for)(void *ctx, qa_seat_id seat);
  bool (*device)(void *ctx, double instance, qa_app_controller_device_view *out);
  /* Host sha256 hex (64 chars + NUL) of the serial text. */
  void (*sha256_hex)(void *ctx, const char *text, char out[65]);
  /* Gyro load/save + router application; error or NULL. */
  const char *(*load_profile)(void *ctx, qa_seat_id seat, const char *path, bool device_identity, const char *guid, const char *serial);
  const char *(*save_profile)(void *ctx, qa_seat_id seat, const char *path, bool device_identity, const char *guid, const char *serial);
  void (*reset_gyro)(void *ctx, qa_seat_id seat);
} qa_app_controller_settings_host;
qa_app_controller_settings *qa_app_controller_settings_create(qa_app_controller_settings_host host, const qa_seat_id *seats, size_t seat_count);
void qa_app_controller_settings_destroy(qa_app_controller_settings *settings);
void qa_app_controller_settings_publish_seats(qa_app_controller_settings *settings, const qa_seat_id *seats, size_t seat_count);
void qa_app_controller_settings_copy_settled_from(qa_app_controller_settings *settings, const qa_app_controller_settings *previous);
void qa_app_controller_settings_update(qa_app_controller_settings *settings);
bool qa_app_controller_settings_busy(const qa_app_controller_settings *settings, qa_seat_id seat);
const char *qa_app_controller_settings_message(const qa_app_controller_settings *settings, qa_seat_id seat);
const char *qa_app_controller_settings_save(qa_app_controller_settings *settings, qa_seat_id seat);
void qa_app_controller_settings_close(qa_app_controller_settings *settings);
#endif

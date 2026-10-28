/* Port of src/app/bootstrap/prepared-startup.ts. TS restarts are
 * synchronous here (PORTING.md): the promise tail is serialized by
 * construction. allowSeatConfigurationCommand and the seat
 * configuration gating are ported; registry/config-script work is
 * in-file over landed core services; world-lifecycle hooks ride
 * the host below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PREPARED_STARTUP_H
#define QA_APP_BOOTSTRAP_PREPARED_STARTUP_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/seat.h"
/* TS allowSeatConfigurationCommand. */
bool qa_app_allow_seat_configuration_command(qa_command_context source);
typedef struct qa_app_prepared_startup_host {
  void *ctx;
  /* World-lifecycle hooks mirrored on the host; borrowed errors. */
  const char *(*prepare_lifecycle)(void *ctx, const void *options, const void *content, bool refine, bool resumed);
  const char *(*set_local_seats)(void *ctx, const void *identities, size_t count);
  const char *(*reset_to_menu)(void *ctx, const char *status);
  double (*local_player_capacity)(void *ctx);   /* NULL -> 4 */
  void (*host_reset_settings)(void *ctx, const char *status); /* may be NULL */
} qa_app_prepared_startup_host;
typedef struct qa_app_prepared_startup qa_app_prepared_startup;
typedef struct qa_app_prepared_seat {
  qa_command_context context;
  qa_seat_id id;
  qa_cvar_registry *cvars;
  qa_mouse_settings mouse;
  qa_seat_input *input; /* borrowed */
} qa_app_prepared_seat;
/* The constructor's completed-restart surface rides the host; the
 * seat roster is created by set_seat_count. */
qa_app_prepared_startup *qa_app_prepared_startup_create(qa_command_buffer *commands,
    qa_app_prepared_startup_host host);
qa_command_buffer *qa_app_prepared_startup_commands(const qa_app_prepared_startup *startup);
const qa_app_prepared_seat *qa_app_prepared_startup_seats(const qa_app_prepared_startup *startup, size_t *out_count);
void qa_app_prepared_startup_close(qa_app_prepared_startup *startup);
#endif

/* Port of src/app/bootstrap/application.ts, region 1244-1251: the accessors
 * that read members of other regions (captureBlocksTransition,
 * afterCommandDispatch). Separate from application_core.c so the
 * constructor and storage link without them. Layout:
 * docs/application_layout.md. GPL-2.0-or-later. */
#include "application_internal.h"

/* TS get clientCommandsBlocked. */
bool qa_application_client_commands_blocked(qa_application *app) {
  if (qa_application_capture_blocks_transition(app)) return true;
  qa_app_video_restart *restart = qa_application_video_restart(app);
  return restart != NULL && qa_app_video_restart_pending(restart);
}
/* TS advanceClientStartup. */
bool qa_application_advance_client_startup(qa_application *app) {
  if (qa_application_client_commands_blocked(app)) return true;
  if (app->graphical != NULL) return qa_app_application_input_advance_startup(app->graphical->input);
  if (app->prepared_startup != NULL) return qa_app_prepared_startup_advance_frame(app->prepared_startup);
  return false;
}
void qa_application_flush_client_commands(qa_application *app) { qa_application_after_command_dispatch(app); }

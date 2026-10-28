/* Port of src/app/bootstrap/player-progress-library.ts. The async
 * store load becomes synchronous; generation/participant guards are
 * kept but cannot interleave. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PLAYER_PROGRESS_LIBRARY_H
#define QA_APP_BOOTSTRAP_PLAYER_PROGRESS_LIBRARY_H
#include <stddef.h>
#include "qa/app/bootstrap/player_progress.h"
#include "qa/ui/library/menu.h"
typedef struct qa_app_player_progress_library qa_app_player_progress_library;
/* store() returns a borrowed store or an error string via *error_out;
 * participant() returns a borrowed string. */
typedef qa_app_player_progress_store *(*qa_app_progress_store_fn)(void *ctx, const char **error_out);
typedef const char *(*qa_app_progress_participant_fn)(void *ctx);
qa_app_player_progress_library *qa_app_player_progress_library_create(
    qa_app_progress_store_fn store, void *store_ctx,
    qa_app_progress_participant_fn participant, void *participant_ctx);
void qa_app_player_progress_library_destroy(qa_app_player_progress_library *library);
const qa_ui_library_entry *qa_app_player_progress_library_entries(
    qa_app_player_progress_library *library, size_t *out_count);
const char *qa_app_player_progress_library_status(qa_app_player_progress_library *library);
void qa_app_player_progress_library_refresh(qa_app_player_progress_library *library);
void qa_app_player_progress_library_load(qa_app_player_progress_library *library);
void qa_app_player_progress_library_activate(qa_app_player_progress_library *library, const char *id);
/* As the landed LibraryMenuService vtable. */
qa_ui_library_menu_service qa_app_player_progress_library_as_service(qa_app_player_progress_library *library);
#endif

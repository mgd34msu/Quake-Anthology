/* Port of src/ui/settings/local-lobby.ts. Async operations are
 * synchronous and return an error message (borrowed) or NULL,
 * mirroring rankings.h; busy/generation guards are kept.
 * ApplicationLocalLobby / LocalLobbySelection
 * (src/app/bootstrap/local-lobby.ts, not yet ported) are mirrored
 * here as a fn+ctx vtable over the landed qa_lobby type; the
 * mirror goes away when app/bootstrap lands.
 * GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_LOCAL_LOBBY_H
#define QA_UI_SETTINGS_LOCAL_LOBBY_H

#include <stddef.h>

#include "qa/network/services/online.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/* Mirror: TS LocalLobbySelection (src/app/bootstrap/local-lobby.ts). */
typedef struct qa_ui_local_lobby_selection {
  const qa_composition_identity *composition; /* borrowed */
} qa_ui_local_lobby_selection;

/* Mirror: TS ApplicationLocalLobby (src/app/bootstrap/local-lobby.ts).
 * current/list results are borrowed until the next lobby operation.
 * host/join/ready/start/leave return an error message or NULL. */
typedef struct qa_ui_application_local_lobby {
  void *ctx;
  const char *account_id; /* TS account.id, borrowed */
  const qa_lobby *(*current)(void *ctx);
  const qa_lobby *const *(*list)(void *ctx, size_t *out_count);
  const char *(*host)(void *ctx, const char *name, double capacity,
                      qa_ui_local_lobby_selection selection, double seats);
  const char *(*join)(void *ctx, const char *id, double seats);
  const char *(*ready)(void *ctx, bool value);
  const char *(*start)(void *ctx);
  const char *(*leave)(void *ctx);
} qa_ui_application_local_lobby;

/** TS registerLocalLobbyMenu(controller, current, hostSelection,
 * readSeats). current returns NULL for the TS null;
 * host_selection returns an error message or NULL. */
qa_ui_settings_menus qa_ui_register_local_lobby_menu(
    qa_native_ui_controller *controller,
    const qa_ui_application_local_lobby *(*current)(void *ctx),
    void *current_ctx,
    const char *(*host_selection)(void *ctx,
                                  qa_ui_local_lobby_selection *out),
    void *host_selection_ctx, double (*read_seats)(void *ctx),
    void *read_seats_ctx);

#endif

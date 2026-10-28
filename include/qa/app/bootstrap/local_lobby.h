/* Port of src/app/bootstrap/local-lobby.ts. Async operations become
 * synchronous and return a borrowed error string or NULL; the TS
 * promise tail is therefore trivially serialized. Supersedes the
 * mirror in qa/ui/settings/local_lobby.h (see unresolved list).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_LOCAL_LOBBY_H
#define QA_APP_BOOTSTRAP_LOCAL_LOBBY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/network/services/online.h"
/* TS LocalLobbyTransitions. host fills endpoint/wire on success. */
typedef struct qa_app_local_lobby_transitions {
  void *ctx;
  const char *(*host)(void *ctx, const qa_lobby *lobby, qa_net_address *endpoint_out, qa_wire_selection *wire_out);
  const char *(*join)(void *ctx, const qa_lobby *lobby);
  const char *(*leave)(void *ctx, const qa_lobby *lobby);
  const char *(*completed)(void *ctx, const qa_lobby *lobby);
} qa_app_local_lobby_transitions;
/* TS LocalLobbySelection. */
typedef struct qa_app_local_lobby_selection {
  const qa_composition_identity *composition; /* borrowed */
} qa_app_local_lobby_selection;
typedef struct qa_app_application_local_lobby {
  qa_local_lobby_service *service;         /* borrowed */
  const qa_online_account *account;        /* borrowed */
  qa_app_local_lobby_transitions transitions;
  char *membership;                        /* owned LobbyId, NULL = TS null */
  const qa_lobby *last_lobby;              /* borrowed from service */
  double launched_generation;
  double completed_generation;
  bool closed;
} qa_app_application_local_lobby;
/* service/account borrowed; account id/name must outlive the lobby. */
void qa_app_application_local_lobby_init(qa_app_application_local_lobby *lobby,
    qa_local_lobby_service *service, const qa_online_account *account,
    qa_app_local_lobby_transitions transitions);
void qa_app_application_local_lobby_destroy(qa_app_application_local_lobby *lobby);
size_t qa_app_application_local_lobby_list(qa_app_application_local_lobby *lobby, const qa_lobby ***out);
const qa_lobby *qa_app_application_local_lobby_current(qa_app_application_local_lobby *lobby);
const char *qa_app_application_local_lobby_host(qa_app_application_local_lobby *lobby,
    const char *name, double capacity, qa_app_local_lobby_selection selection, double seats);
const char *qa_app_application_local_lobby_join(qa_app_application_local_lobby *lobby, const char *id, double seats);
const char *qa_app_application_local_lobby_ready(qa_app_application_local_lobby *lobby, bool value);
const char *qa_app_application_local_lobby_start(qa_app_application_local_lobby *lobby);
const char *qa_app_application_local_lobby_poll(qa_app_application_local_lobby *lobby);
const char *qa_app_application_local_lobby_complete(qa_app_application_local_lobby *lobby);
const char *qa_app_application_local_lobby_leave(qa_app_application_local_lobby *lobby);
const char *qa_app_application_local_lobby_close(qa_app_application_local_lobby *lobby);
#endif

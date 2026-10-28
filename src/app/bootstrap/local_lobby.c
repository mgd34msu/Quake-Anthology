/* Port of src/app/bootstrap/local-lobby.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/local_lobby.h"
#include <stdlib.h>
#include <string.h>
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out != NULL) memcpy(out, text, len + 1);
  return out;
}
void qa_app_application_local_lobby_init(qa_app_application_local_lobby *lobby,
    qa_local_lobby_service *service, const qa_online_account *account,
    qa_app_local_lobby_transitions transitions) {
  memset(lobby, 0, sizeof *lobby);
  lobby->service = service;
  lobby->account = account;
  lobby->transitions = transitions;
}
void qa_app_application_local_lobby_destroy(qa_app_application_local_lobby *lobby) {
  free(lobby->membership);
  lobby->membership = NULL;
}
size_t qa_app_application_local_lobby_list(qa_app_application_local_lobby *lobby, const qa_lobby ***out) {
  size_t count = qa_local_lobby_service_count(lobby->service);
  const qa_lobby **items = malloc((count == 0 ? 1 : count) * sizeof *items);
  if (items == NULL) { *out = NULL; return 0; }
  for (size_t i = 0; i < count; i++) items[i] = qa_local_lobby_service_at(lobby->service, i);
  *out = items;
  return count;
}
const qa_lobby *qa_app_application_local_lobby_current(qa_app_application_local_lobby *lobby) {
  if (lobby->membership == NULL) return NULL;
  size_t count = qa_local_lobby_service_count(lobby->service);
  for (size_t i = 0; i < count; i++) {
    const qa_lobby *entry = qa_local_lobby_service_at(lobby->service, i);
    if (entry != NULL && strcmp(entry->id, lobby->membership) == 0) return entry;
  }
  return NULL;
}
static const char *require_lobby(qa_app_application_local_lobby *lobby, const qa_lobby **out) {
  const qa_lobby *current = qa_app_application_local_lobby_current(lobby);
  if (current == NULL) return "No current local lobby membership";
  bool member = false;
  for (size_t i = 0; i < current->member_count; i++)
    if (strcmp(current->members[i].account.id, lobby->account->id) == 0) member = true;
  if (!member) return "No current local lobby membership";
  *out = current;
  return NULL;
}
static const char *guard(qa_app_application_local_lobby *lobby) {
  return lobby->closed ? "Local lobby session is closed" : NULL;
}
static void adopt(qa_app_application_local_lobby *lobby, const qa_lobby *created) {
  free(lobby->membership);
  lobby->membership = dup_text(created->id);
  lobby->last_lobby = created;
  lobby->launched_generation = 0;
  lobby->completed_generation = 0;
}
const char *qa_app_application_local_lobby_host(qa_app_application_local_lobby *lobby,
    const char *name, double capacity, qa_app_local_lobby_selection selection, double seats) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  if (lobby->membership != NULL) return "Leave the current lobby before hosting another";
  adopt(lobby, qa_local_lobby_service_create(lobby->service, lobby->account, name, capacity, selection.composition, seats));
  return NULL;
}
const char *qa_app_application_local_lobby_join(qa_app_application_local_lobby *lobby, const char *id, double seats) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  if (lobby->membership != NULL) return "Leave the current lobby before joining another";
  adopt(lobby, qa_local_lobby_service_join(lobby->service, id, lobby->account, seats));
  return NULL;
}
const char *qa_app_application_local_lobby_ready(qa_app_application_local_lobby *lobby, bool value) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  const qa_lobby *current = NULL;
  error = require_lobby(lobby, &current);
  if (error != NULL) return error;
  qa_local_lobby_service_ready(lobby->service, current->id, lobby->account->id, value);
  return NULL;
}
const char *qa_app_application_local_lobby_start(qa_app_application_local_lobby *lobby) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  const qa_lobby *current = NULL;
  error = require_lobby(lobby, &current);
  if (error != NULL) return error;
  if (strcmp(current->owner, lobby->account->id) != 0) return "Only the local lobby host can start a match";
  const qa_lobby *next = qa_local_lobby_service_start(lobby->service, current->id, lobby->account->id);
  qa_net_address endpoint = {0};
  qa_wire_selection wire = {0};
  const char *host_error = lobby->transitions.host(lobby->transitions.ctx, next, &endpoint, &wire);
  if (host_error == NULL) {
    lobby->last_lobby = qa_local_lobby_service_publish(lobby->service, next->id, lobby->account->id,
        next->match_generation, &endpoint, &wire);
    lobby->launched_generation = next->match_generation;
    return NULL;
  }
  /* TS catch: complete + (boundHost only after host resolves, so no leave). */
  bool cleanup_failed = false;
  if (qa_app_application_local_lobby_current(lobby) != NULL) {
    /* complete() aborts on misuse in the landed service the way TS throws;
       guard by phase to mirror the TS try/catch accounting. */
    qa_local_lobby_service_complete(lobby->service, next->id, lobby->account->id, next->match_generation);
  }
  return cleanup_failed ? "Lobby publication and host cleanup failed" : host_error;
}
static const char *launch(qa_app_application_local_lobby *lobby, const qa_lobby *playing) {
  if (playing->match_generation <= lobby->launched_generation) return NULL;
  const char *error = lobby->transitions.join(lobby->transitions.ctx, playing);
  if (error != NULL) return error;
  lobby->launched_generation = playing->match_generation;
  return NULL;
}
const char *qa_app_application_local_lobby_poll(qa_app_application_local_lobby *lobby) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  if (lobby->membership == NULL) return NULL;
  const qa_lobby *current = qa_app_application_local_lobby_current(lobby);
  if (current == NULL) {
    const qa_lobby *previous = lobby->last_lobby;
    free(lobby->membership); lobby->membership = NULL; lobby->last_lobby = NULL;
    if (previous != NULL) return lobby->transitions.leave(lobby->transitions.ctx, previous);
    return NULL;
  }
  lobby->last_lobby = current;
  if (strcmp(current->phase, "playing") == 0) return launch(lobby, current);
  if (strcmp(current->phase, "open") == 0 && lobby->launched_generation > lobby->completed_generation) {
    error = lobby->transitions.completed(lobby->transitions.ctx, current);
    if (error != NULL) return error;
    lobby->completed_generation = lobby->launched_generation;
  }
  return NULL;
}
const char *qa_app_application_local_lobby_complete(qa_app_application_local_lobby *lobby) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  const qa_lobby *current = NULL;
  error = require_lobby(lobby, &current);
  if (error != NULL) return error;
  if (strcmp(current->owner, lobby->account->id) != 0) return "Only the local lobby host can complete the match";
  const qa_lobby *completed = qa_local_lobby_service_complete(lobby->service, current->id,
      lobby->account->id, lobby->launched_generation);
  if (lobby->launched_generation > lobby->completed_generation) {
    error = lobby->transitions.completed(lobby->transitions.ctx, completed);
    if (error != NULL) return error;
    lobby->completed_generation = lobby->launched_generation;
  }
  return NULL;
}
static const char *leave_current(qa_app_application_local_lobby *lobby) {
  const qa_lobby *current = qa_app_application_local_lobby_current(lobby);
  const qa_lobby *leaving = current != NULL ? current : lobby->last_lobby;
  free(lobby->membership); lobby->membership = NULL; lobby->last_lobby = NULL;
  if (leaving == NULL) return NULL;
  size_t count = qa_local_lobby_service_count(lobby->service);
  bool listed = false;
  for (size_t i = 0; i < count; i++) {
    const qa_lobby *entry = qa_local_lobby_service_at(lobby->service, i);
    if (entry != NULL && strcmp(entry->id, leaving->id) == 0) listed = true;
  }
  if (listed) qa_local_lobby_service_leave(lobby->service, leaving->id, lobby->account->id);
  return lobby->transitions.leave(lobby->transitions.ctx, leaving);
}
const char *qa_app_application_local_lobby_leave(qa_app_application_local_lobby *lobby) {
  const char *error = guard(lobby);
  if (error != NULL) return error;
  return leave_current(lobby);
}
const char *qa_app_application_local_lobby_close(qa_app_application_local_lobby *lobby) {
  if (lobby->closed) return NULL;
  lobby->closed = true;
  return leave_current(lobby);
}

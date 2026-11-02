/* Port of src/network/services/online.ts. scrypt uses Node defaults
 * (N=16384, r=8, p=1) so saved hashes verify across ports. Random ids
 * come from /dev/urandom. Async providers -> synchronous fn+ctx.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_ONLINE_H
#define QA_NETWORK_SERVICES_ONLINE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/session.h"
/* AccountId/LobbyId/AccessToken are plain strings with TS prefixes. */
typedef struct qa_online_account { char *id, *name; } qa_online_account;
void qa_online_account_destroy(qa_online_account *a);
typedef struct qa_authorization {
  const char *kind; /* "authorized"|"denied"|"unavailable" */
  qa_online_account account; char *token, *reason, *provider;
} qa_authorization;
void qa_authorization_destroy(qa_authorization *a);
typedef struct qa_authorization_provider {
  void *ctx;
  void (*authorize)(void *ctx, const char *name, const char *credential, qa_authorization *out);
} qa_authorization_provider;
typedef struct qa_local_authorization qa_local_authorization;
void qa_local_authorization_init(qa_local_authorization *a);
void qa_local_authorization_destroy(qa_local_authorization *a);
void qa_local_authorization_register(qa_local_authorization *a, const char *name, const char *password, qa_online_account *out);
void qa_local_authorization_authorize(qa_local_authorization *a, const char *name, const char *credential, qa_authorization *out);
int qa_local_authorization_get_account(qa_local_authorization *a, const char *token, qa_online_account *out);
void qa_local_authorization_revoke(qa_local_authorization *a, const char *token);
char *qa_local_authorization_save(qa_local_authorization *a);
void qa_local_authorization_restore(qa_local_authorization *a, const char *text);
qa_authorization_provider qa_local_authorization_as_provider(qa_local_authorization *a);

typedef struct qa_lobby_member { qa_online_account account; double seats; bool ready; } qa_lobby_member;
typedef struct qa_lobby {
  char *id, *owner, *name; double capacity;
  const qa_composition_identity *composition; /* borrowed */
  qa_lobby_member *members; size_t member_count;
  double match_generation;
  const char *phase; /* "open"|"starting"|"playing" */
  bool has_endpoint; qa_net_address endpoint; qa_wire_selection wire;
} qa_lobby;
typedef struct qa_local_lobby_service qa_local_lobby_service;
void qa_local_lobby_service_init(qa_local_lobby_service *s);
void qa_local_lobby_service_destroy(qa_local_lobby_service *s);
/* Heap form for composition roots (TS new LocalLobbyService());
 * free releases the service after destroy. NULL only on OOM. */
qa_local_lobby_service *qa_local_lobby_service_new(void);
void qa_local_lobby_service_free(qa_local_lobby_service *s);
const qa_lobby *qa_local_lobby_service_create(qa_local_lobby_service *s, const qa_online_account *owner,
                                              const char *name, double capacity,
                                              const qa_composition_identity *composition, double seats);
size_t qa_local_lobby_service_count(qa_local_lobby_service *s);
const qa_lobby *qa_local_lobby_service_at(qa_local_lobby_service *s, size_t index);
const qa_lobby *qa_local_lobby_service_join(qa_local_lobby_service *s, const char *id, const qa_online_account *account, double seats);
const qa_lobby *qa_local_lobby_service_ready(qa_local_lobby_service *s, const char *id, const char *account, bool ready);
const qa_lobby *qa_local_lobby_service_start(qa_local_lobby_service *s, const char *id, const char *owner);
const qa_lobby *qa_local_lobby_service_publish(qa_local_lobby_service *s, const char *id, const char *owner,
                                               double match_generation, const qa_net_address *endpoint, const qa_wire_selection *wire);
const qa_lobby *qa_local_lobby_service_complete(qa_local_lobby_service *s, const char *id, const char *owner, double match_generation);
void qa_local_lobby_service_leave(qa_local_lobby_service *s, const char *id, const char *account);

typedef struct qa_stat_entry { const char *key; double value; } qa_stat_entry;
typedef struct qa_ranking_report_player {
  const char *account; double score; bool won;
  const qa_stat_entry *statistics; size_t statistic_count;
} qa_ranking_report_player;
typedef struct qa_online_ranking_report {
  const char *match; qa_provider_id rules;
  const qa_ranking_report_player *players; size_t player_count;
} qa_online_ranking_report;
typedef struct qa_ranking_entry {
  char *account; double matches, wins, score;
  qa_stat_entry *statistics; size_t statistic_count;
  char **stat_keys;
} qa_ranking_entry;
void qa_ranking_entries_destroy(qa_ranking_entry *entries, size_t count);
typedef struct qa_local_ranking_service qa_local_ranking_service;
void qa_local_ranking_service_init(qa_local_ranking_service *s);
void qa_local_ranking_service_destroy(qa_local_ranking_service *s);
bool qa_local_ranking_service_submit(qa_local_ranking_service *s, const qa_online_ranking_report *report);
qa_ranking_entry *qa_local_ranking_service_standings(qa_local_ranking_service *s, qa_provider_id rules, size_t *out_count);
char *qa_local_ranking_service_save(qa_local_ranking_service *s);
void qa_local_ranking_service_restore(qa_local_ranking_service *s, const char *text);

typedef struct qa_addon_catalog_entry {
  qa_content_id content; const char *title; bool installed; const char *download; /* NULL = none */
} qa_addon_catalog_entry;
typedef struct qa_addon_catalog_provider {
  void *ctx;
  const qa_addon_catalog_entry *(*list)(void *ctx, size_t *out_count);
} qa_addon_catalog_provider;
typedef struct qa_external_service_state {
  const char *kind; const char *provider, *reason;
} qa_external_service_state;
#endif

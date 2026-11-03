/* Port of src/network/services/rankings.ts. Provider promises ->
 * synchronous fn+ctx calls in queue order; provider failures are
 * reported through an error string the provider fills (empty = ok) and
 * abort via qa_range_error at the TS throw points. GPL-2.0-or-later. */
#ifndef QA_NETWORK_SERVICES_RANKINGS_H
#define QA_NETWORK_SERVICES_RANKINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_ranking_account { uint64_t player_id; double rank; } qa_ranking_account;
typedef struct qa_ranking_match { uint64_t game_id; } qa_ranking_match;
typedef struct qa_ranking_account_request {
  const char *kind; /* "login"|"create" */
  const char *username, *password, *email;
} qa_ranking_account_request;
typedef struct qa_ranking_login_result {
  bool active; qa_ranking_account account; const char *reason;
} qa_ranking_login_result;
typedef struct qa_ranking_report {
  bool is_string;
  uint64_t self_id, other_id; double key, value; bool accumulate; const char *text;
} qa_ranking_report;
typedef struct qa_ranking_provider {
  void *ctx; const char *endpoint;
  /* Each returns 1 on success; on 0, *error (if set) is borrowed. */
  int (*begin)(void *ctx, const char *game_key, qa_ranking_match *out, const char **error);
  int (*login)(void *ctx, qa_ranking_match match, const qa_ranking_account_request *request, qa_ranking_login_result *out, const char **error);
  int (*join)(void *ctx, qa_ranking_match match, qa_ranking_account account, const char **error);
  int (*report)(void *ctx, qa_ranking_match match, const qa_ranking_report *report, const char **error);
  int (*poll)(void *ctx, const char **error);
  int (*logout)(void *ctx, qa_ranking_match match, qa_ranking_account account, const char **error);
  int (*finish)(void *ctx, qa_ranking_match match, const char **error);
} qa_ranking_provider;
typedef struct qa_ranking_service_state {
  const char *kind; const char *reason; uint64_t game_id;
} qa_ranking_service_state;
typedef struct qa_ranking_player_state {
  const char *kind; qa_ranking_account account; const char *reason;
} qa_ranking_player_state;
typedef void (*qa_ranking_changed_fn)(void *ctx, double slot, qa_ranking_player_state state);
typedef void (*qa_ranking_service_changed_fn)(void *ctx, qa_ranking_service_state state);
typedef struct qa_ranking_lifecycle qa_ranking_lifecycle;
void qa_ranking_lifecycle_init(qa_ranking_lifecycle *l, const qa_ranking_provider *provider,
                               qa_ranking_changed_fn changed, void *changed_ctx,
                               qa_ranking_service_changed_fn service_changed, void *service_ctx);
void qa_ranking_lifecycle_destroy(qa_ranking_lifecycle *l);
/* Heap form (the struct is private to rankings.c): NULL only on OOM; free destroys. */
qa_ranking_lifecycle *qa_ranking_lifecycle_create(const qa_ranking_provider *provider,
                                                  qa_ranking_changed_fn changed, void *changed_ctx,
                                                  qa_ranking_service_changed_fn service_changed, void *service_ctx);
void qa_ranking_lifecycle_free(qa_ranking_lifecycle *l);
void qa_ranking_lifecycle_unavailable(qa_ranking_lifecycle *l, const char *reason);
qa_ranking_service_state qa_ranking_lifecycle_get_state(const qa_ranking_lifecycle *l);
qa_ranking_player_state qa_ranking_lifecycle_player(const qa_ranking_lifecycle *l, double slot);
void qa_ranking_lifecycle_begin(qa_ranking_lifecycle *l, bool enabled, bool single_player, const char *game_key);
void qa_ranking_lifecycle_account(qa_ranking_lifecycle *l, double slot, const qa_ranking_account_request *request);
void qa_ranking_lifecycle_report_int(qa_ranking_lifecycle *l, double self_slot, double other_slot, double key, double value, bool accumulate);
void qa_ranking_lifecycle_report_string(qa_ranking_lifecycle *l, double self_slot, double other_slot, double key, const char *value);
void qa_ranking_lifecycle_frame(qa_ranking_lifecycle *l);
void qa_ranking_lifecycle_reset(qa_ranking_lifecycle *l, double slot);
void qa_ranking_lifecycle_spectate(qa_ranking_lifecycle *l, double slot);
void qa_ranking_lifecycle_disconnect(qa_ranking_lifecycle *l, double slot);
void qa_ranking_lifecycle_end(qa_ranking_lifecycle *l);
#endif

/* Port of src/app/bootstrap/q3-rankings.ts. The landed ranking
 * lifecycle is opaque with an init-only constructor, so its storage
 * is supplied by the host's create_lifecycle. Entity field reads
 * ride the host view. Async lifecycle calls are synchronous in the
 * landed port. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_RANKINGS_H
#define QA_APP_BOOTSTRAP_Q3_RANKINGS_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/level.h"
#include "qa/network/services/rankings.h"
#include "qa/ui/settings/rankings.h"
/* TS Q3RankingHost. */
typedef struct qa_app_q3_ranking_host {
  void *ctx;
  void (*status)(void *ctx, double slot, qa_ranking_player_state state);
  void (*service_status)(void *ctx, qa_ranking_service_state state); /* may be NULL */
  void (*menu)(void *ctx, double slot);
  void (*spectator)(void *ctx, double slot);
  void (*activate)(void *ctx, double slot);
  void (*scoreboard)(void *ctx, double slot);
  void (*drop_bot)(void *ctx, double slot);
  double (*game_type)(void *ctx);
  const char *(*cvar)(void *ctx, const char *name);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
  /* Entity reads over pool.get(slot). */
  bool (*entity_inuse)(void *ctx, double slot);
  double (*entity_sv_flags)(void *ctx, double slot);
  double (*entity_weapon)(void *ctx, double slot);
  double (*entity_session_team)(void *ctx, double slot);
  /* Lifecycle storage (opaque landed type). */
  qa_ranking_lifecycle *(*create_lifecycle)(void *ctx, const qa_ranking_provider *provider,
      qa_ranking_changed_fn changed, void *changed_ctx,
      qa_ranking_service_changed_fn service_changed, void *service_ctx);
  void (*destroy_lifecycle)(void *ctx, qa_ranking_lifecycle *lifecycle);
} qa_app_q3_ranking_host;
typedef struct qa_app_q3_rankings qa_app_q3_rankings;
/* provider NULL = TS null. pool/level borrowed. */
qa_app_q3_rankings *qa_app_q3_rankings_create(qa_q3_entity_pool *pool, const qa_q3_game_level *level,
    const qa_ranking_provider *provider, qa_app_q3_ranking_host host);
qa_ranking_lifecycle *qa_app_q3_rankings_lifecycle(qa_app_q3_rankings *rankings);
void qa_app_q3_rankings_begin(qa_app_q3_rankings *rankings, bool enabled, bool single_player, const char *game_key);
void qa_app_q3_rankings_account(qa_app_q3_rankings *rankings, double slot, const qa_ranking_account_request *request);
qa_ui_ranking_account_actions qa_app_q3_rankings_account_actions(qa_app_q3_rankings *rankings, double slot);
void qa_app_q3_rankings_reset(qa_app_q3_rankings *rankings, double slot);
void qa_app_q3_rankings_spectate(qa_app_q3_rankings *rankings, double slot);
void qa_app_q3_rankings_disconnect(qa_app_q3_rankings *rankings, double slot);
void qa_app_q3_rankings_frame(qa_app_q3_rankings *rankings);
void qa_app_q3_rankings_game_over(qa_app_q3_rankings *rankings);
void qa_app_q3_rankings_close(qa_app_q3_rankings *rankings);
#endif

/* Port of src/app/bootstrap/match-results.ts: the family-neutral match result
 * of a finished match (matchResultOf) and ApplicationMatchRankings, the local
 * accounts and standings fed through a match-result event ring. TS async
 * members are synchronous (PORTING.md); a TS throw is qa_raise.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MATCH_RESULTS_H
#define QA_APP_BOOTSTRAP_MATCH_RESULTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/player_progress.h"
#include "qa/app/bootstrap/q2_match_reports.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/contracts/identity.h"
#include "qa/network/services/online.h"

/* TS MatchResult.players[number]. identity NULL is TS null (bots and unnamed clients). */
typedef struct qa_app_match_result_player {
  double slot;
  double score;
  bool won;
  char *identity;
} qa_app_match_result_player;
/* TS MatchResult. family is static text ("q1" | "q2" | "q3"); the other strings are owned. */
typedef struct qa_app_match_result {
  const char *family;
  char *rules;
  char *match;
  char *map;
  qa_app_match_result_player *players; size_t player_count;
} qa_app_match_result;
void qa_app_match_result_free(qa_app_match_result *result);

/* TS matchResultOf(simulation, match, transition?): false is TS null; transition NULL is
 * TS undefined. *out is owned by the caller (qa_app_match_result_free). */
bool qa_app_match_result_of(qa_app_shared_simulation *simulation, const char *match,
    const qa_app_native_q2_map_transition *transition, qa_app_match_result *out);

/* TS userinfoIdentity(userinfo): malloc'd; NULL is TS null (also for a NULL userinfo). */
char *qa_app_match_userinfo_identity(const char *userinfo);

/* TS ApplicationMatchRankings. */
typedef struct qa_app_match_rankings qa_app_match_rankings;
qa_app_match_rankings *qa_app_match_rankings_create(void);
void qa_app_match_rankings_destroy(qa_app_match_rankings *rankings);
/* TS attach(store): the store is borrowed until destroy or the next attach. */
void qa_app_match_rankings_attach(qa_app_match_rankings *rankings, qa_app_player_progress_store *store);
/* TS bind(slot, name): raises RangeError "Ranking slot is out of range". The account is
 * owned by the rankings. */
const qa_online_account *qa_app_match_rankings_bind(qa_app_match_rankings *rankings, double slot, const char *name);
void qa_app_match_rankings_unbind(qa_app_match_rankings *rankings, double slot);
/* TS get droppedResults. */
double qa_app_match_rankings_dropped_results(const qa_app_match_rankings *rankings);
/* TS observe(simulation, transition?): NULL is TS null. The result is the ring's record,
 * borrowed until the next settle or destroy. The simulation is compared by address. */
const qa_app_match_result *qa_app_match_rankings_observe(qa_app_match_rankings *rankings,
    qa_app_shared_simulation *simulation, const qa_app_native_q2_map_transition *transition);
/* TS settle(): malloc'd entries (qa_ranking_entries_destroy). */
qa_ranking_entry *qa_app_match_rankings_settle(qa_app_match_rankings *rankings, size_t *out_count);
/* TS standings(rules): malloc'd entries (qa_ranking_entries_destroy). */
qa_ranking_entry *qa_app_match_rankings_standings(qa_app_match_rankings *rankings, qa_provider_id rules, size_t *out_count);
#endif

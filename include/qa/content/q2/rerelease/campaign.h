/* Port of src/content/q2/rerelease/campaign.ts. GPL-2.0-or-later.
 * visitedMaps/levels are owned dynamic arrays (insertion order). */
#ifndef QA_CONTENT_Q2_RERELEASE_CAMPAIGN_H
#define QA_CONTENT_Q2_RERELEASE_CAMPAIGN_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/foundation/host.h"

typedef struct qa_q2_rerelease_level_entry {
  char *map;   /* owned */
  char *name;  /* owned */
  double visit_order, total_secrets, found_secrets, total_monsters, killed_monsters, time;
} qa_q2_rerelease_level_entry;
typedef struct qa_q2rr_mission { char *primary, *secondary; double primary_changes, secondary_changes; } qa_q2rr_mission;
typedef struct qa_q2_rerelease_campaign_state {
  double cross_unit_flags;
  char **visited_maps; size_t visited_count, visited_cap;
  qa_q2_rerelease_level_entry *levels; size_t level_count, level_cap;
  qa_q2rr_mission mission;
} qa_q2_rerelease_campaign_state;

struct qa_q2_rerelease_players;
bool qa_q2_create_rerelease_campaign_state(qa_q2_rerelease_campaign_state *out);
void qa_q2_rerelease_campaign_destroy(qa_q2_rerelease_campaign_state *c);
bool qa_q2_rerelease_campaign_clone(qa_q2_rerelease_campaign_state *dst, const qa_q2_rerelease_campaign_state *src);
qa_q2_rerelease_level_entry *qa_q2_rerelease_campaign_get_level(qa_q2_rerelease_campaign_state *c, const char *map);
void qa_q2_enter_rerelease_level(qa_q2_game_services *g, struct qa_q2_rerelease_players *p, qa_q2_rerelease_campaign_state *c);
void qa_q2_update_rerelease_level(qa_q2_game_services *g, qa_q2_rerelease_campaign_state *c);
/* Malloc'd copies sorted per TS; free with _unit_report_free. */
qa_q2_rerelease_level_entry *qa_q2_rerelease_unit_report(const qa_q2_rerelease_campaign_state *c, size_t *n);
void qa_q2_rerelease_unit_report_free(qa_q2_rerelease_level_entry *e, size_t n);
#endif

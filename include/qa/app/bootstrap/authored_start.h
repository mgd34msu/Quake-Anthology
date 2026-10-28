/* Port of src/app/bootstrap/authored-start.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUTHORED_START_H
#define QA_APP_BOOTSTRAP_AUTHORED_START_H
#include <stdbool.h>
#include "qa/app/bootstrap/q2_travel.h"
#include "qa/content/catalog/start_maps.h"
/* target owned; start_items borrowed from the catalog. */
typedef struct qa_app_authored_campaign_start {
  qa_app_q2_travel_target *target;
  const char *start_items;
} qa_app_authored_campaign_start;
/* False = TS null. Aborts via parseQ2StartItems/parseQ2Travel as TS. */
bool qa_app_authored_campaign_start_for(const qa_authored_start_catalog *catalog, const char *selected_map,
                                    qa_app_authored_campaign_start *out);
#endif

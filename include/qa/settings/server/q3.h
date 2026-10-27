/*
 * Port of src/settings/server/q3.ts: Q3 limit, combat and match-type
 * collections. `product` is "baseq3" | "missionpack". Source
 * defaults are from q3GameCvarDefinitions (src/content/q3/base/
 * settings.ts, content module not yet ported): timelimit "0",
 * fraglimit "20", capturelimit "8", g_friendlyFire "0",
 * g_gametype "0" — identical for both products; the GameType values
 * are from src/content/q3/base/shared/definitions.ts. Returned
 * collections point at static definitions; do not free.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_Q3_H
#define QA_SETTINGS_SERVER_Q3_H

#include "qa/settings/server/types.h"

qa_server_setting_collection qa_q3_limit_settings(const char *product);
qa_server_setting_collection qa_q3_combat_settings(const char *product);
qa_server_setting_collection qa_q3_match_settings(const char *product);

#endif

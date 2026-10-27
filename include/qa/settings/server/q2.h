/*
 * Port of src/settings/server/q2.ts: Q2 limit, CTF capture, combat,
 * DeathBall, rerelease and item-spawn setting collections. Defaults
 * come from createQ2PlayerRules() (timeLimitMinutes 0, fragLimit 0)
 * and createQ2CtfRules() (captureLimit 0) in the content module,
 * which is not yet ported; the built-in rerelease defaults are the
 * literals in the TS file itself. Returned collections point at
 * static definitions; do not free.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_Q2_H
#define QA_SETTINGS_SERVER_Q2_H

#include "qa/settings/server/types.h"

qa_server_setting_collection qa_q2_limit_settings(void);
qa_server_setting_collection qa_q2_ctf_capture_settings(void);
qa_server_setting_collection qa_q2_combat_settings(void);
qa_server_setting_collection qa_q2_death_ball_settings(void);
qa_server_setting_collection qa_q2_rerelease_settings(void);
/** These bits are read while the selected Q2 item owner spawns map
 * entities. */
qa_server_setting_collection qa_q2_spawn_settings(void);

#endif

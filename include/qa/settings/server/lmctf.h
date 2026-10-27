/*
 * Port of src/settings/server/lmctf.ts: LMCTF limit, rune and weapon
 * collections. Defaults and the rune table are from createLmctfRules
 * (timeLimitMinutes 0, fragLimit 0, runes 15, fastSwitch false) and
 * LMCTF_RUNES in src/content/q2/multiplayer/lmctf/types.ts (content
 * module not yet ported). Returned collections point at static
 * definitions; do not free.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_LMCTF_H
#define QA_SETTINGS_SERVER_LMCTF_H

#include "qa/settings/server/types.h"

qa_server_setting_collection qa_lmctf_limit_settings(void);
qa_server_setting_collection qa_lmctf_rune_settings(void);
qa_server_setting_collection qa_lmctf_weapon_settings(void);

#endif

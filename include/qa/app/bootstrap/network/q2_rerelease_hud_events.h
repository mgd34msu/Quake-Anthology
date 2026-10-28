/* Port of src/app/bootstrap/network/q2-rerelease-hud-events.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_RERELEASE_HUD_EVENTS_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_RERELEASE_HUD_EVENTS_H
#include <stdbool.h>
#include "qa/app/bootstrap/network/q2_service_presentation.h"
/* The host carries image_config_offset (TS intersection member). */
bool qa_app_translate_q2_rerelease_hud_record(const qa_q2_server_record *record,
    const qa_app_q2_service_host *host);
#endif

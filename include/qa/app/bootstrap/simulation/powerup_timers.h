/* Port of src/app/bootstrap/simulation/powerup-timers.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_POWERUP_TIMERS_H
#define QA_APP_BOOTSTRAP_SIMULATION_POWERUP_TIMERS_H
#include <stddef.h>
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/contracts/gameplay.h"
/* Writes up to capacity timers; returns full count. expansion may be NULL. */
size_t qa_app_sim_q2_powerup_timers(qa_q2_player_powerups base, const qa_q2mp_powerups *expansion, double now,
    qa_active_powerup_timer *out, size_t capacity);
typedef struct qa_app_sim_q1_powerup_entry { const char *kind; double expires; } qa_app_sim_q1_powerup_entry;
size_t qa_app_sim_q1_powerup_timers(const qa_app_sim_q1_powerup_entry *powerups, size_t count, double now_seconds,
    qa_active_powerup_timer *out, size_t capacity);
size_t qa_app_sim_q3_public_powerup_timers(double (*expires)(void *ctx, double powerup), void *ctx, double now_ms,
    qa_active_powerup_timer *out, size_t capacity);
/* TS GameClient view: flags/clientNum/powerups/invulnerabilityTime. */
typedef struct qa_app_sim_q3_timer_client {
  double pm_flags; double client_num; double invulnerability_time;
  double (*powerup_expires)(void *ctx, double powerup); void *powerup_ctx;
} qa_app_sim_q3_timer_client;
size_t qa_app_sim_q3_powerup_timers(qa_app_sim_q3_timer_client client, double now_ms,
    qa_app_sim_q3_timer_client (*client_at)(void *ctx, double client_number), void *client_at_ctx,
    qa_active_powerup_timer *out, size_t capacity);
#endif

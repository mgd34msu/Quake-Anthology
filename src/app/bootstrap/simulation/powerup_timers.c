/* Port of src/app/bootstrap/simulation/powerup-timers.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/powerup_timers.h"
#include <string.h>
#include "qa/movement/q3/constants.h"
static size_t push(qa_active_powerup_timer *out, size_t cap, size_t n, qa_item_id item, const char *label, double remaining) {
  if (!(remaining > 0)) return n;
  if (n < cap) { out[n].item = item; out[n].label = label; out[n].remaining_seconds = remaining; }
  return n + 1;
}
size_t qa_app_sim_q2_powerup_timers(qa_q2_player_powerups base, const qa_q2mp_powerups *expansion, double now,
    qa_active_powerup_timer *out, size_t capacity) {
  size_t n = 0;
  n = push(out, capacity, n, "q2:item_quad", "Quad Damage", base.quad_until - now);
  n = push(out, capacity, n, "q2:item_quadfire", "DualFire Damage", (expansion ? expansion->quad_fire_until : 0) - now);
  n = push(out, capacity, n, "q2:item_double", "Double Damage", (expansion ? expansion->double_until : 0) - now);
  n = push(out, capacity, n, "q2:item_invulnerability", "Invulnerability", base.invulnerability_until - now);
  n = push(out, capacity, n, "q2:item_enviro", "Environment Suit", base.enviro_until - now);
  n = push(out, capacity, n, "q2:item_breather", "Rebreather", base.breather_until - now);
  n = push(out, capacity, n, "q2:item_ir_goggles", "IR Goggles", (expansion ? expansion->ir_until : 0) - now);
  n = push(out, capacity, n, "q3:item_haste", "Haste", base.haste_until - now);
  n = push(out, capacity, n, "q3:item_regen", "Regeneration", base.regen_until - now);
  n = push(out, capacity, n, "q3:item_flight", "Flight", base.flight_until - now);
  n = push(out, capacity, n, "q3:item_invis", "Invisibility", base.invis_until - now);
  return n;
}
size_t qa_app_sim_q1_powerup_timers(const qa_app_sim_q1_powerup_entry *powerups, size_t count, double now_seconds,
    qa_active_powerup_timer *out, size_t capacity) {
  static const struct { const char *kind; qa_item_id item; const char *label; } table[] = {
    {"quad", "q1:item_artifact_super_damage", "Quad Damage"},
    {"invulnerability", "q1:item_artifact_invulnerability", "Invulnerability"},
    {"invisibility", "q1:item_artifact_invisibility", "Invisibility"},
    {"suit", "q1:item_artifact_envirosuit", "Environment Suit"},
    {"mg3:lavasuit", "q1:item_artifact_lavasuit", "Lava Suit"},
    {"hipnotic:wetsuit", "q1:item_artifact_wetsuit", "Wetsuit"},
    {"hipnotic:empathy", "q1:item_artifact_empathy_shields", "Empathy Shields"},
    {"rogue:shield", "q1:item_powerup_shield", "Power Shield"},
    {"rogue:antigrav", "q1:item_powerup_belt", "Anti-gravity Belt"},
    {"q3:haste", "q3:item_haste", "Haste"},
    {"q3:regen", "q3:item_regen", "Regeneration"},
    {"q3:flight", "q3:item_flight", "Flight"},
  };
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    if (!(powerups[i].expires > now_seconds)) continue;
    for (size_t t = 0; t < sizeof table / sizeof table[0]; t++)
      if (powerups[i].kind && strcmp(powerups[i].kind, table[t].kind) == 0)
        n = push(out, capacity, n, table[t].item, table[t].label, powerups[i].expires - now_seconds);
  }
  return n;
}
static const struct { double powerup; qa_item_id item; const char *label; } q3_table[] = {
  {QA_Q3_PW_QUAD, "q3:item_quad", "Quad Damage"}, {QA_Q3_PW_BATTLESUIT, "q3:item_enviro", "Battle Suit"},
  {QA_Q3_PW_HASTE, "q3:item_haste", "Haste"}, {QA_Q3_PW_INVIS, "q3:item_invis", "Invisibility"},
  {QA_Q3_PW_REGEN, "q3:item_regen", "Regeneration"}, {QA_Q3_PW_FLIGHT, "q3:item_flight", "Flight"},
};
size_t qa_app_sim_q3_public_powerup_timers(double (*expires)(void *ctx, double powerup), void *ctx, double now_ms,
    qa_active_powerup_timer *out, size_t capacity) {
  size_t n = 0;
  for (size_t i = 0; i < sizeof q3_table / sizeof q3_table[0]; i++)
    n = push(out, capacity, n, q3_table[i].item, q3_table[i].label, (expires(ctx, q3_table[i].powerup) - now_ms) / 1000);
  return n;
}
size_t qa_app_sim_q3_powerup_timers(qa_app_sim_q3_timer_client client, double now_ms,
    qa_app_sim_q3_timer_client (*client_at)(void *ctx, double client_number), void *client_at_ctx,
    qa_active_powerup_timer *out, size_t capacity) {
  qa_app_sim_q3_timer_client viewed = ((int)client.pm_flags & QA_Q3_FLAG_FOLLOW) != 0
      ? client_at(client_at_ctx, client.client_num) : client;
  size_t n = qa_app_sim_q3_public_powerup_timers(viewed.powerup_expires, viewed.powerup_ctx, now_ms, out, capacity);
  n = push(out, capacity, n, "q3:holdable_invulnerability", "Invulnerability", (viewed.invulnerability_time - now_ms) / 1000);
  return n;
}

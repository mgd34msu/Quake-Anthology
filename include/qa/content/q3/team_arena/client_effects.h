/* Port of src/content/q3/team-arena/client-effects.ts. GPL-2.0-or-later.
 * Mirror accommodations (landed game/state player-state mirror):
 * ps.eventSequence is the mirror's generic ring counter (state.c);
 * ps.damagePitch/Yaw/Event/Count, ps.commandTime and
 * ps.entityEventSequence have no mirror fields, so damageFeedback skips
 * those stores and snapshot conversion is a host callback. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_CLIENT_EFFECTS_H
#define QA_CONTENT_Q3_TEAM_ARENA_CLIENT_EFFECTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/player_state.h"

/* TS ClientEffectsContext. snapshot/extrapolate replace the TS
 * playerStateToEntityState* calls on the mirror player state. */
typedef struct qa_q3_ta_client_effects_context {
  qa_q3_game_combat_context *combat;
  double intermission_time;
  bool smooth_clients;
  double fry_sound;
  void *ctx;
  double (*random_int)(void *ctx);
  double (*sound_index)(void *ctx, const char *path);
  void (*sound)(void *ctx, qa_q3_game_entity *entity, double channel, double sound_index);
  void (*spectator_end_frame)(void *ctx, qa_q3_game_entity *entity);
  void (*snapshot)(void *ctx, qa_q3_game_client *client, qa_q3_game_entity *entity);
  void (*snapshot_extrapolate)(void *ctx, qa_q3_game_client *client, qa_q3_game_entity *entity, double time);
} qa_q3_ta_client_effects_context;
/* TS EventContext = Pick<ClientEffectsContext, "combat">. */
typedef struct qa_q3_ta_event_context { qa_q3_game_combat_context *combat; } qa_q3_ta_event_context;

void qa_q3_ta_damage_feedback(qa_q3_ta_event_context context, qa_q3_game_entity *player);
void qa_q3_ta_world_effects(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity);
void qa_q3_ta_set_client_sound(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity);

/* TS Q3AmmoRegenerationRule. Returned pointer is a static row. */
typedef struct qa_q3_ta_ammo_regeneration_rule { double weapon, max, increment, time; } qa_q3_ta_ammo_regeneration_rule;
double qa_q3_ta_client_speed_multiplier(const qa_q3_base_player_state *ps);
const qa_q3_ta_ammo_regeneration_rule *qa_q3_ta_ammo_regeneration_rule_for(double weapon);
/* TS step result: has_count false is TS count null. */
typedef struct qa_q3_ta_ammo_regeneration_step { double milliseconds; bool has_count; double count; } qa_q3_ta_ammo_regeneration_step;
qa_q3_ta_ammo_regeneration_step qa_q3_ta_step_ammo_regeneration(
    const qa_q3_ta_ammo_regeneration_rule *rule, double count, double milliseconds, double msec);

/* TS Q3MappedAmmoTimer: current() closure is fn+ctx; count/elapsed are
 * read/written through accessors so mapped storage stays live. */
typedef struct qa_q3_ta_mapped_ammo_timer {
  const qa_q3_ta_ammo_regeneration_rule *rule;
  void *ctx;
  bool (*current)(void *ctx);
  double (*get_count)(void *ctx);
  void (*set_count)(void *ctx, double value);
  double (*get_elapsed)(void *ctx);
  void (*set_elapsed)(void *ctx, double value);
} qa_q3_ta_mapped_ammo_timer;
/* TS ClientTimerOwnership: ammo NULL (count 0 with has_ammo false) is TS null. */
typedef struct qa_q3_ta_client_timer_ownership {
  bool ordinary_decay;
  bool has_ammo; const qa_q3_ta_mapped_ammo_timer *ammo; size_t ammo_count;
} qa_q3_ta_client_timer_ownership;
/* ownership NULL is the TS nativeTimers default. */
void qa_q3_ta_client_timer_actions(qa_q3_ta_event_context context, qa_q3_game_entity *entity,
    double msec, const qa_q3_ta_client_timer_ownership *ownership);
/* TS sendPendingPredictableEvents(context, ps): ps is the mirror player
 * state (generic = eventSequence); entity_event_sequence is explicit
 * because the mirror has no such field (the TS snapshot conversion owns
 * advancing it). The pool comes from context.combat->entities. */
void qa_q3_ta_send_pending_predictable_events(qa_q3_ta_event_context context,
    qa_q3_game_player_state_mirror *ps, double entity_event_sequence);
void qa_q3_ta_update_client_powerups(qa_q3_ta_event_context context, qa_q3_game_client *client);
void qa_q3_ta_client_end_frame(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity);
#endif

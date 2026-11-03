/* Port of src/content/q1/missionpacks/world/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_INDEX_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_INDEX_H
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/world/rogue_runes.h"
#include "qa/content/q1/missionpacks/world/rogue_tag.h"
#include "qa/content/q1/missionpacks/world/rogue_teams.h"

/* TS MissionpackWorldHooks: a host interface, fn + ctx; NULL fn = TS absent.
 * charmer returns false for TS null. present_finale receives a TS
 * Q1SourceFinale (kind "finale" | "sell-screen"). */
struct qa_q1mp_world_hooks {
  void *ctx;
  bool (*charmer)(void *ctx, qa_actor_id *out);
  void (*charm)(void *ctx, qa_q1_actor *entity, qa_actor_id charmer);
  qa_q1_actor *(*become_decoy)(void *ctx, const char *target, qa_vec3 origin);
  void (*present_finale)(void *ctx, const qa_q1_intermission_result *result);
  double (*gamecfg)(void *ctx);
  double (*team_color)(void *ctx, qa_actor_id actor);
  void (*set_team_color)(void *ctx, qa_actor_id actor, double team);
  void (*add_frags)(void *ctx, qa_actor_id actor, double delta);
  double (*frags)(void *ctx, qa_actor_id actor);
  void (*disconnect)(void *ctx, qa_actor_id actor);
  double (*player_frame)(void *ctx, qa_actor_id actor);
  const char *(*player_name)(void *ctx, qa_actor_id actor);
};
/* TS Q1MissionpackWorld. runes/teams/tag NULL for hipnotic. Must not move
 * after init: hooks is copied in and borrowed by the registered callbacks. */
typedef struct qa_q1mp_missionpack_world {
  qa_q1_services *game; qa_q1mp_pack pack; qa_q1mp_world_hooks hooks;
  qa_q1mp_rogue_runes runes_storage; qa_q1mp_rogue_runes *runes;
  qa_q1mp_rogue_teams teams_storage; qa_q1mp_rogue_teams *teams;
  qa_q1mp_rogue_tag tag_storage; qa_q1mp_rogue_tag *tag;
} qa_q1mp_missionpack_world;
/* hooks NULL = TS {}. */
void qa_q1mp_missionpack_world_init(qa_q1mp_missionpack_world *world, qa_q1_services *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks);
void qa_q1mp_missionpack_world_destroy(qa_q1mp_missionpack_world *world);
qa_q1mp_missionpack_world *qa_q1mp_register_missionpack_world(qa_q1_services *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks, qa_q1mp_missionpack_world *out);
void qa_q1mp_world_after_physics(qa_q1mp_missionpack_world *world, qa_actor_id actor, double seconds);
void qa_q1mp_world_player_spawned(qa_q1mp_missionpack_world *world, qa_actor_id actor);
void qa_q1mp_world_drop_carried_flag(qa_q1mp_missionpack_world *world, qa_actor_id actor);
bool qa_q1mp_world_impulse(qa_q1mp_missionpack_world *world, qa_actor_id actor, double impulse);
/* TS suicide: false refuses the suicide (Rogue teams), true otherwise. */
bool qa_q1mp_world_suicide(qa_q1mp_missionpack_world *world, qa_actor_id actor);
double qa_q1mp_world_saved_team(qa_q1mp_missionpack_world *world, qa_actor_id actor);
/* NULL = TS undefined. */
qa_q1_actor *qa_q1mp_world_select_spawn(qa_q1mp_missionpack_world *world, qa_actor_id actor);
double qa_q1mp_world_tag_score(qa_q1mp_missionpack_world *world, qa_actor_id victim, qa_actor_id attacker);
/* attacker NULL = TS null. */
void qa_q1mp_world_confirmed_damage(qa_q1mp_missionpack_world *world, qa_actor_id target, const qa_actor_id *attacker);
void qa_q1mp_world_player_died(qa_q1mp_missionpack_world *world, qa_actor_id actor, const qa_actor_id *attacker);
double qa_q1mp_world_rune_attack_delay(qa_q1mp_missionpack_world *world, qa_actor_id actor, double delay);
void qa_q1mp_world_rune_attack_sound(qa_q1mp_missionpack_world *world, qa_actor_id actor);
double qa_q1mp_world_rune_damage(qa_q1mp_missionpack_world *world, qa_actor_id actor, double amount);
double qa_q1mp_world_rune_resistance(qa_q1mp_missionpack_world *world, qa_actor_id actor, double amount);
bool qa_q1mp_world_has_regeneration_rune(qa_q1mp_missionpack_world *world, qa_actor_id actor);
void qa_q1mp_world_crash_time_machine(qa_q1mp_missionpack_world *world);

#endif

/* Port of src/content/q2/equipment/hand-grenades.ts. GPL-2.0-or-later.
 * Mirrored unported types (TS owners: weapons/hand-action.ts HandAction,
 * weapons/hand-grenade.ts HandProjectileSpec, weapons/ballistics.ts
 * Q2Ballistics, weapons/types.ts Q2WeaponInput fields): qa_q2f_* names. */
#ifndef QA_CONTENT_Q2_EQUIPMENT_HAND_GRENADES_H
#define QA_CONTENT_Q2_EQUIPMENT_HAND_GRENADES_H
#include "qa/content/q2/foundation/callbacks.h"
extern const qa_item_id QA_HAND_GRENADE_AMMO;
typedef struct qa_hand_grenade_loadout { bool enabled; double initial_ammo, capacity; bool infinite_ammo; } qa_hand_grenade_loadout;
/* TS HandAction union on kind (exact strings). */
typedef struct qa_q2f_hand_action { const char *kind; double frame, next_at, expires_at, throw_at, ready_at; bool release_queued, require_release; } qa_q2f_hand_action;
typedef struct qa_q2f_hand_projectile_spec { qa_vec3 start, direction; double damage, speed, fuse, radius, gravity; bool held; } qa_q2f_hand_projectile_spec;
typedef struct qa_q2f_hand_grenade_launch { qa_q2f_hand_projectile_spec spec; double timer; bool players_collide; } qa_q2f_hand_grenade_launch;
typedef struct qa_q2f_projection { qa_vec3 start, direction; } qa_q2f_projection;
/** TS Q2Ballistics (mirrored): callbacks + hooks + fireHandGrenade. */
typedef struct qa_q2f_ballistics {
  void *ctx;
  qa_q2_callback_definitions callbacks;
  bool has_firing_interval; double (*firing_interval)(void *ctx, qa_actor_id a, double seconds);
  bool has_quad_multiplier; double (*quad_multiplier)(void *ctx, qa_actor_id a);
  bool has_source_damage_multiplier; double (*source_damage_multiplier)(void *ctx, qa_actor_id a);
  void (*ammo_changed)(void *ctx, qa_actor_id a, qa_item_id ammo);
  void (*fire_hand_grenade)(void *ctx, qa_actor_id a, qa_q2_game_services *g, qa_q2f_hand_grenade_launch launch);
} qa_q2f_ballistics;
typedef struct qa_hand_grenade_equipment_input {
  qa_vec3 angles; double gravity, quad_until, double_until, quad_fire_until;
  bool haste, no_stack_double, players_collide, pressed, held, released;
  const char *lifecycle; /* "alive"|"dead"|"removing"|"removed" */
  void *project_ctx; qa_q2f_projection (*project)(void *ctx, qa_vec3 angles, qa_vec3 offset);
} qa_hand_grenade_equipment_input;
typedef struct qa_hand_grenade_equipment_state { qa_hand_grenade_loadout config; qa_q2f_hand_action action; } qa_hand_grenade_equipment_state;
typedef struct qa_hand_grenade_checkpoint_entry { qa_saved_actor_id actor; qa_hand_grenade_equipment_state state; } qa_hand_grenade_checkpoint_entry;
typedef struct qa_hand_grenades_checkpoint { double version; qa_q2_edition edition; const qa_hand_grenade_checkpoint_entry *actors; size_t actor_count; } qa_hand_grenades_checkpoint;
typedef struct qa_q2_hand_grenade_equipment qa_q2_hand_grenade_equipment;
qa_q2_hand_grenade_equipment *qa_q2_hand_grenade_equipment_create(qa_q2_game_services *g, qa_q2f_ballistics ballistics);
void qa_q2_hand_grenade_equipment_destroy(qa_q2_hand_grenade_equipment *e);
void qa_q2_hand_grenade_equipment_configure(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_loadout cfg);
bool qa_q2_hand_grenade_equipment_state_of(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_state *out);
void qa_q2_hand_grenade_equipment_step(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_input in);
qa_hand_grenades_checkpoint qa_q2_hand_grenade_equipment_capture(qa_q2_hand_grenade_equipment *e);
void qa_q2_hand_grenade_equipment_restore(qa_q2_hand_grenade_equipment *e, qa_hand_grenades_checkpoint cp);
#endif

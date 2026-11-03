/* Port of src/content/q2/equipment/hand-grenades.ts. GPL-2.0-or-later.
 * The action, throw and launch types are the foundation weapons ports
 * (weapons/hand-action.ts, hand-grenade.ts, ballistics.ts), as in TS. */
#ifndef QA_CONTENT_Q2_EQUIPMENT_HAND_GRENADES_H
#define QA_CONTENT_Q2_EQUIPMENT_HAND_GRENADES_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/content/q2/foundation/weapons/hand_action.h"
extern const qa_item_id QA_HAND_GRENADE_AMMO;
/** TS HandGrenadeLoadout. */
typedef struct qa_hand_grenade_loadout { bool enabled; double initial_ammo, capacity; bool infinite_ammo; } qa_hand_grenade_loadout;
/** TS HandGrenadeEquipmentInput: Pick<Q2WeaponInput, angles | gravity |
 * quadUntil | doubleUntil | quadFireUntil | haste | noStackDouble |
 * playersCollide> plus the hand-action pressed/held/released, lifecycle
 * and throw.project. */
typedef struct qa_hand_grenade_equipment_input {
  qa_vec3 angles; double gravity, quad_until, double_until, quad_fire_until;
  bool haste, no_stack_double, players_collide, pressed, held, released;
  qa_q2w_lifecycle lifecycle;
  qa_q2w_project_fn project;
} qa_hand_grenade_equipment_input;
/** TS HandGrenadeEquipmentState. */
typedef struct qa_hand_grenade_equipment_state { qa_hand_grenade_loadout config; qa_q2w_hand_action action; } qa_hand_grenade_equipment_state;
typedef struct qa_hand_grenade_checkpoint_entry { qa_saved_actor_id actor; qa_hand_grenade_equipment_state state; } qa_hand_grenade_checkpoint_entry;
/** TS HandGrenadesCheckpoint. */
typedef struct qa_hand_grenades_checkpoint { double version; qa_q2_edition edition; const qa_hand_grenade_checkpoint_entry *actors; size_t actor_count; } qa_hand_grenades_checkpoint;
/** TS Q2HandGrenadeEquipment. ballistics is borrowed (TS Q2Ballistics
 * reference) and must outlive the equipment. */
typedef struct qa_q2_hand_grenade_equipment qa_q2_hand_grenade_equipment;
qa_q2_hand_grenade_equipment *qa_q2_hand_grenade_equipment_create(qa_q2_game_services *g, qa_q2w_ballistics *ballistics);
void qa_q2_hand_grenade_equipment_destroy(qa_q2_hand_grenade_equipment *e);
/* TS readonly game. */
qa_q2_game_services *qa_q2_hand_grenade_equipment_game(const qa_q2_hand_grenade_equipment *e);
void qa_q2_hand_grenade_equipment_configure(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_loadout cfg);
/* TS state(): false is TS null; out receives a copy. */
bool qa_q2_hand_grenade_equipment_state_of(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_state *out);
void qa_q2_hand_grenade_equipment_step(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_input in);
/* The returned actors array is owned by the equipment until the next capture or destroy. */
qa_hand_grenades_checkpoint qa_q2_hand_grenade_equipment_capture(qa_q2_hand_grenade_equipment *e);
void qa_q2_hand_grenade_equipment_restore(qa_q2_hand_grenade_equipment *e, qa_hand_grenades_checkpoint cp);
#endif

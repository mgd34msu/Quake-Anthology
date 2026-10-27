/* Port of src/content/q3/foundation/arsenal.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_ARSENAL_H
#define QA_CONTENT_Q3_FOUNDATION_ARSENAL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/movement/q3/weapon.h"

typedef struct qa_q3_weapon_item { double weapon; qa_item_id item; qa_item_id ammo; /* NULL = none */ } qa_q3_weapon_item;
extern const qa_q3_weapon_item qa_q3_weapon_items[];
enum { QA_Q3_WEAPON_ITEM_COUNT = 13 };
/* Getter named with _get_ infix: qa_q3_weapon_item is the type. NULL = TS null. */
const qa_q3_weapon_item *qa_q3_get_weapon_item(double weapon);

typedef struct qa_q3_arsenal_controls { bool attack, use_holdable; double requested_weapon; } qa_q3_arsenal_controls;
typedef struct qa_q3_arsenal_runtime_state {
  const char *product; /* "baseq3" | "missionpack" */
  double max_health; bool spectator; double persistent_powerup_tag;
  double holdable_item, holdable_tag; bool respawned, use_item_held;
  double event_sequence, fractional_milliseconds;
  qa_q3_external_weapon_slot external_slot;
  bool has_requested_weapon; double requested_weapon; /* has=false is TS null */
} qa_q3_arsenal_runtime_state;

qa_q3_arsenal_runtime_state qa_q3_request_weapon(qa_q3_arsenal_runtime_state runtime, double weapon);
qa_q3_arsenal_runtime_state qa_q3_request_weapon_holster(qa_q3_arsenal_runtime_state runtime);
qa_q3_arsenal_runtime_state qa_q3_request_weapon_resume(qa_q3_arsenal_runtime_state runtime);

/* effects/ammo/torso storage is malloc'd; free with destroy (does not free borrowed strings). */
typedef struct qa_q3_arsenal_step {
  qa_weapon_step_result base;
  qa_q3_arsenal_runtime_state runtime;
  const double *torso_animations; size_t torso_animation_count;
} qa_q3_arsenal_step;
typedef double (*qa_q3_firing_delay_fn)(void *ctx, double milliseconds);
qa_q3_arsenal_step qa_q3_step_arsenal(qa_weapon_step_input input, qa_q3_arsenal_runtime_state runtime,
    qa_q3_arsenal_controls controls, qa_q3_firing_delay_fn firing_delay, void *firing_delay_ctx);
void qa_q3_arsenal_step_destroy(qa_q3_arsenal_step *step);

qa_q3_arsenal_runtime_state qa_q3_spawn_arsenal_runtime(const char *product, double max_health, double event_sequence);
/* arsenal.ammo is malloc'd; caller frees it. */
qa_arsenal_state qa_q3_spawn_loadout(qa_provider_id provider, const char *product, bool team_deathmatch);
/* TS Extract<AnimationState,{kind:"q3"}>. */
qa_animation_state qa_q3_spawn_animation(void);
#endif

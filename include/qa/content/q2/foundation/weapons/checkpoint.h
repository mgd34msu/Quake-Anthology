/* Port of src/content/q2/foundation/weapons/checkpoint.ts (types only). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_CHECKPOINT_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_CHECKPOINT_H
#include "qa/content/q2/foundation/weapons/types.h"

typedef struct qa_q2w_noise_checkpoint {
  qa_saved_actor_id actor; qa_vec3 origin; double time; bool secondary;
} qa_q2w_noise_checkpoint;
typedef struct qa_q2w_noise_checkpoint_result { bool has_value; qa_q2w_noise_checkpoint value; } qa_q2w_noise_checkpoint_result;
typedef struct qa_q2w_charge_entry { qa_saved_actor_id actor; double charges; } qa_q2w_charge_entry;
typedef struct qa_q2w_state_entry { qa_saved_actor_id actor; qa_q2w_state state; } qa_q2w_state_entry;
typedef struct qa_q2w_input_entry { qa_saved_actor_id actor; qa_q2w_input input; } qa_q2w_input_entry;
typedef struct qa_q2w_noises_entry {
  qa_saved_actor_id actor;
  qa_q2w_noise_checkpoint_result primary, secondary;
} qa_q2w_noises_entry;
typedef struct qa_q2w_cause_entry { qa_saved_actor_id actor; double means_of_death; } qa_q2w_cause_entry;

typedef struct qa_q2w_weapons_checkpoint {
  double format_version; /* literal 2 */
  const qa_q2w_charge_entry *silencer_charges; size_t silencer_charge_count;
  const char *source_rules; /* "base"|"ctf"|"lmctf" */
  const char *const *registered; size_t registered_count;
  const char *const *fallback_order; size_t fallback_order_count; bool has_fallback_order;
  const qa_q2w_state_entry *states; size_t state_count;
  const qa_q2w_input_entry *inputs; size_t input_count;
  const qa_q2w_noises_entry *noises; size_t noise_count;
  qa_q2w_noise_checkpoint_result sound_entity, sound2_entity;
  const qa_q2w_cause_entry *blaster_causes; size_t blaster_cause_count;
} qa_q2w_weapons_checkpoint;
#endif

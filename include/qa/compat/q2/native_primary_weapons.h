/* Port of src/compat/q2/native-primary-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_WEAPONS_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_WEAPONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/source_items.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_region_span_fwd { double entry; double join; } qa_compat_q2_native_region_span_fwd;
typedef struct qa_compat_q2_weapon_projection { qa_native_item_field field; double value; } qa_compat_q2_weapon_projection;
typedef struct qa_compat_q2_native_primary_weapon_profile {
  qa_content_digest digest; qa_native_abi abi;
  qa_native_weapon_dispatcher dispatcher;
  const qa_native_weapon_decision *decisions; size_t decision_count;
  double spawn_entry; const qa_native_item_test *spawn_accepted; size_t spawn_accepted_count;
  const qa_native_item_test *active; size_t active_count;
  const qa_native_item_test_row *committed_input; size_t committed_input_count;
  const qa_native_item_test_row *continuations; size_t continuation_count;
  double time_address; qa_native_mod_scalar time_encoding; double time_milliseconds;
  double entity_client; qa_native_item_field entity_water_level, entity_view_height, entity_max_health;
  double client_byte_length, client_view_angles; qa_native_item_field client_buttons, client_latched_buttons;
  double attack_animation_entry; const qa_compat_q2_native_region_span_fwd *attack_skip; size_t attack_skip_count;
  qa_native_item_field anim_frame, anim_end, anim_priority, anim_duck, anim_run;
  const qa_source_equipment_context *equipment_contexts; size_t equipment_context_count;
  qa_native_item_field delay_flag; double delay_entry, delay_join;
  const char *delay_evaluate_kind; /* "source-flag"|"source-animation" */
  const double *delay_factors; size_t delay_factor_count;
  double delay_anim_entry, delay_baseline_ms; const qa_compat_q2_weapon_projection *delay_projection; size_t delay_projection_count;
  const qa_native_item_field *delay_writes; size_t delay_write_count;
  const char *damage_kind; /* "source-result"|"source-flag" */
  double damage_entry; const char *damage_result; double damage_address; qa_native_mod_scalar damage_encoding;
  const double *damage_factors; size_t damage_factor_count; double damage_region_entry, damage_region_join;
} qa_compat_q2_native_primary_weapon_profile;
/* TS NativePrimaryWeaponHost/Hooks: guest host is unlanded in this
 * slice (TS owner: native-primary-weapons.ts host + guest abi);
 * mirrored as fn+ctx. */
typedef struct qa_compat_q2_native_primary_weapon_host {
  void *ctx; const char *module_digest; const char *module_id;
  void (*on_close)(void *ctx);
} qa_compat_q2_native_primary_weapon_host;
typedef struct qa_compat_q2_native_primary_weapon_hooks {
  void *ctx; bool (*selected)(void *ctx, qa_actor_id actor);
  void (*completed)(void *ctx, qa_actor_id actor, bool reached);
  void (*spawned)(void *ctx, qa_actor_id actor);
} qa_compat_q2_native_primary_weapon_hooks;
typedef struct qa_compat_q2_native_primary_weapons {
  qa_compat_q2_native_primary_weapon_host host;
  const qa_compat_q2_native_primary_weapon_profile *profile;
  qa_compat_q2_native_primary_weapon_hooks hooks; bool closed;
} qa_compat_q2_native_primary_weapons;
void qa_compat_q2_native_primary_weapons_init(qa_compat_q2_native_primary_weapons *s, qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_weapon_profile *profile, qa_compat_q2_native_primary_weapon_hooks hooks);
void qa_compat_q2_native_primary_weapons_close(qa_compat_q2_native_primary_weapons *s);
#endif

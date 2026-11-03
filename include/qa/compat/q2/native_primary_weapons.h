/* Port of src/compat/q2/native-primary-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_WEAPONS_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_WEAPONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/movement.h"
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
/** TS NativePrimaryWeaponHost. memory/runner are the mirrors of
 * MappedGuestMemory/GuestCallRunner shared by compat/q2. actor and record_for
 * return false for the TS null. */
typedef struct qa_compat_q2_native_primary_weapon_host {
  qa_compat_q2_memory *memory;
  qa_compat_q2_runner *runner;
  qa_guest_address image;
  void *ctx;
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address address, qa_compat_q2_guest_call_signature signature,
                                 const qa_guest_call_value *values, size_t count);
  qa_guest_address (*entry)(void *ctx, const char *name);
  qa_raw_entity_view (*record)(void *ctx, qa_guest_address address);
  bool (*actor)(void *ctx, qa_raw_entity_view record, qa_actor_id *out);
  bool (*record_for)(void *ctx, qa_actor_id actor, qa_raw_entity_view *out);
} qa_compat_q2_native_primary_weapon_host;
/** TS NativePrimaryWeaponHooks. */
typedef struct qa_compat_q2_native_primary_weapon_hooks {
  void *ctx;
  bool (*selected)(void *ctx, qa_actor_id actor);
  void (*completed)(void *ctx, qa_actor_id actor, bool reached_decision);
  void (*spawned)(void *ctx, qa_actor_id actor);
} qa_compat_q2_native_primary_weapon_hooks;
/** TS NativePrimaryWeapons.input() result. */
typedef struct qa_compat_q2_native_primary_weapon_input {
  double buttons, latched_buttons;
  qa_vec3 view_angles;
  double view_height, water_level, max_health;
} qa_compat_q2_native_primary_weapon_input;

typedef struct qa_compat_q2_native_primary_weapons qa_compat_q2_native_primary_weapons;
/* TS constructor; NULL never returned (allocation failure aborts). */
qa_compat_q2_native_primary_weapons *qa_compat_q2_native_primary_weapons_create(
    qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_weapon_profile *profile,
    qa_compat_q2_native_primary_weapon_hooks hooks);
const qa_compat_q2_native_primary_weapon_profile *qa_compat_q2_native_primary_weapons_profile(const qa_compat_q2_native_primary_weapons *s);
double qa_compat_q2_native_primary_weapons_read(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, qa_native_item_field field);
qa_compat_q2_native_primary_weapon_input qa_compat_q2_native_primary_weapons_input(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
/* Fills kind "q2", frame, end_frame, priority, duck, run. */
qa_animation_state qa_compat_q2_native_primary_weapons_animation(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
double qa_compat_q2_native_primary_weapons_damage_factor(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
void qa_compat_q2_native_primary_weapons_attack_animation(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
double qa_compat_q2_native_primary_weapons_weapon_delay(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, double milliseconds);
bool qa_compat_q2_native_primary_weapons_available(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
bool qa_compat_q2_native_primary_weapons_continuing(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
double qa_compat_q2_native_primary_weapons_time_milliseconds(qa_compat_q2_native_primary_weapons *s);
void qa_compat_q2_native_primary_weapons_release(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor);
/* TS close(); idempotent. */
void qa_compat_q2_native_primary_weapons_close(qa_compat_q2_native_primary_weapons *s);
/* C-only: close then free. */
void qa_compat_q2_native_primary_weapons_destroy(qa_compat_q2_native_primary_weapons *s);
#endif

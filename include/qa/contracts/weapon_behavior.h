/*
 * Port of src/contracts/weapon-behavior.ts: artifact-pinned weapon
 * trajectory behavior definitions and identity comparisons.
 * Runtime comparisons live in weapon_behavior.c and preserve the TS
 * field selection exactly (title is deliberately not compared).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_WEAPON_BEHAVIOR_H
#define QA_CONTRACTS_WEAPON_BEHAVIOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"

/* TS ProjectileRole (exact strings): 'rocket'|'grenade'|'nail'|
 * 'bolt'|'plasma'|'energy'|'grapple'. Compared as strings by
 * sameWeaponBehavior. */
typedef const char *qa_projectile_role;

/* TS WeaponBehaviorCallback = Extract<GuestCallbackReference,
 * {kind:'quakec'|'qvm'}> | {kind:'native-artifact', module,
 * imageOffset: bigint, abi: NativeCallAbi}. */
typedef enum qa_weapon_behavior_callback_kind {
  QA_WEAPON_CALLBACK_QUAKEC, QA_WEAPON_CALLBACK_QVM, QA_WEAPON_CALLBACK_NATIVE_ARTIFACT
} qa_weapon_behavior_callback_kind;
typedef struct qa_weapon_behavior_callback {
  qa_weapon_behavior_callback_kind kind;
  qa_module_identity module;
  double function_index;    /* quakec */
  double instruction_index; /* qvm */
  int64_t image_offset;     /* native-artifact (TS bigint) */
  qa_native_call_abi abi;   /* native-artifact */
} qa_weapon_behavior_callback;

/* TS aspect is the literal 'trajectory'. activate may be null
 * (has_activate false). */
struct qa_weapon_behavior_definition {
  const char *id; /* `${string}:${string}` */
  const char *title;
  qa_module_identity module;
  qa_projectile_role role;
  const char *aspect;
  bool has_activate;
  qa_weapon_behavior_callback activate;
  qa_weapon_behavior_callback fire;
};

typedef struct qa_weapon_behavior_launch {
  qa_owned_actor projectile;
  qa_actor_id shooter;
  qa_item_id weapon;
  qa_projectile_role role;
  double time_seconds;
  qa_body_state body;
} qa_weapon_behavior_launch;

typedef struct qa_weapon_trajectory_update {
  qa_vec3 origin;
  qa_vec3 velocity;
  qa_vec3 angles;
} qa_weapon_trajectory_update;
typedef struct qa_weapon_trajectory_update_result {
  bool has_value;
  qa_weapon_trajectory_update value;
} qa_weapon_trajectory_update_result;

/** TS WeaponBehaviorInstance. */
typedef struct qa_weapon_behavior_instance {
  void *ctx;
  qa_weapon_trajectory_update initial;
  qa_weapon_behavior_definition definition;
  qa_weapon_trajectory_update_result (*step)(void *ctx, qa_body_state body,
                                             double time_seconds);
  void (*close)(void *ctx);
} qa_weapon_behavior_instance;
typedef struct qa_weapon_behavior_instance_result {
  bool has_value;
  qa_weapon_behavior_instance value;
} qa_weapon_behavior_instance_result;

/** TS WeaponBehaviorSource. */
typedef struct qa_weapon_behavior_source {
  void *ctx;
  qa_weapon_behavior_definition definition;
  qa_weapon_behavior_instance_result (*attach)(void *ctx, qa_weapon_behavior_launch launch);
  qa_weapon_behavior_instance (*resume)(void *ctx, qa_actor_id projectile);
} qa_weapon_behavior_source;

/* TS WeaponBehaviorCompatibility: {kind:'supported',definition} |
 * {kind:'unsupported',reason}. */
typedef struct qa_weapon_behavior_compatibility {
  bool is_supported;
  qa_weapon_behavior_definition definition;
  const char *reason;
} qa_weapon_behavior_compatibility;

typedef struct qa_weapon_behavior_attachment {
  qa_saved_actor_id projectile;
  qa_provider_id owner;
  qa_weapon_behavior_definition definition;
} qa_weapon_behavior_attachment;
typedef struct qa_weapon_behavior_attachment_checkpoint {
  int64_t version; /* TS literal 1 */
  const qa_weapon_behavior_attachment *attachments;
  size_t attachment_count;
} qa_weapon_behavior_attachment_checkpoint;

/* TS sameWeaponBehavior: compares id, role, aspect, module identity,
 * and fire/activate callbacks -- NOT title. */
bool qa_same_weapon_behavior(qa_weapon_behavior_definition left,
                             qa_weapon_behavior_definition right);

/** TS WeaponBehaviorProjectilePort. */
typedef struct qa_weapon_behavior_projectile_port {
  void *ctx;
  bool (*controls_trajectory)(void *ctx, qa_actor_id projectile);
  qa_weapon_trajectory_update_result (*launch)(void *ctx, qa_weapon_behavior_launch input);
  qa_weapon_trajectory_update_result (*step)(void *ctx, qa_owned_actor projectile,
                                             qa_body_state body, double time_seconds);
} qa_weapon_behavior_projectile_port;

/* TS QvmWeaponBehaviorLayout; fireAbi is the literal
 * 'entity-pointer-start-direction' (compared by sameQvmWeaponLayout). */
struct qa_qvm_weapon_behavior_layout {
  double entity_stride;
  double level_time;
  double allocate;
  double free;
  double field_inuse;
  double field_nextthink;
  double field_think;
  double field_health;
  const char *fire_abi;
};
bool qa_same_qvm_weapon_layout(qa_qvm_weapon_behavior_layout left,
                               qa_qvm_weapon_behavior_layout right);

#endif

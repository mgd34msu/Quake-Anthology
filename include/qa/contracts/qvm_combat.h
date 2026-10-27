/*
 * Port of src/contracts/qvm-combat.ts (types only).
 * QvmCombatCall<Role> is generic over role names; the C form stores
 * role->argument-index pairs with the exact TS role strings
 * (QvmDamageRole: "target"|"inflictor"|"attacker"|"direction"|
 * "point"|"amount"|"flags"|"method"; QvmArmorRole: "target"|"amount"|
 * "flags"). GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QVM_COMBAT_H
#define QA_CONTRACTS_QVM_COMBAT_H

#include <stdbool.h>
#include <stddef.h>

typedef const char *qa_qvm_damage_role;
typedef const char *qa_qvm_armor_role;

typedef struct qa_qvm_combat_role_index {
  const char *role;
  double index;
} qa_qvm_combat_role_index;

/* extras kind: "int32" | "float32" | "address" (exact strings). */
typedef struct qa_qvm_combat_extra {
  double index;
  const char *kind;
  double value;
} qa_qvm_combat_extra;

typedef struct qa_qvm_combat_call {
  const qa_qvm_combat_role_index *roles;
  size_t role_count;
  const qa_qvm_combat_extra *extras;
  size_t extra_count;
} qa_qvm_combat_call;

typedef struct qa_qvm_reaction_call {
  double arguments;
  double role_target;
  double role_amount;
} qa_qvm_reaction_call;

typedef struct qa_qvm_damage_flags {
  double radius, no_armor, no_knockback, no_protection, no_team_protection;
} qa_qvm_damage_flags;

/* TS QvmCombatMass: {kind:"constant",value} | {kind:"entity",offset,
 * storage:"int32"|"float32"}. */
typedef struct qa_qvm_combat_mass {
  bool is_constant;
  double value;
  double offset;
  const char *storage;
} qa_qvm_combat_mass;

typedef struct qa_qvm_combat_team {
  double value;
  const char *team; /* `${string}:${string}` */
} qa_qvm_combat_team;

#endif

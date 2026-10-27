/*
 * Port of src/contracts/native-weapon-behavior.ts (types only).
 * Literal fields: version 1, kind 'q2-api2023-trajectory', abi
 * 'windows-x86-64', aspect 'trajectory', time storage
 * 'int64-milliseconds', signatures 'entity-void' / 'void-pointer'.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_NATIVE_WEAPON_BEHAVIOR_H
#define QA_CONTRACTS_NATIVE_WEAPON_BEHAVIOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/weapon_behavior.h"

typedef struct qa_native_weapon_registration_layout {
  double byte_length, name, tag, callback;
} qa_native_weapon_registration_layout;

typedef struct qa_native_weapon_registration {
  double rva;
  const char *name;
  double tag;
  qa_native_weapon_registration_layout layout;
} qa_native_weapon_registration;

typedef struct qa_native_weapon_entry {
  double rva;
  const qa_native_weapon_registration *registration; /* nullable */
} qa_native_weapon_entry;

typedef struct qa_native_weapon_command {
  qa_string_view_list arguments;
  const char *tail;
} qa_native_weapon_command;

typedef struct qa_native_weapon_cvar { const char *name; const char *value; } qa_native_weapon_cvar;

typedef struct qa_native_weapon_entity_layout {
  double byte_length;
  double origin, angles, velocity, client, owner, view_height;
  double generation, next_think, think_callback, think_registration, touch_callback;
} qa_native_weapon_entity_layout;
typedef struct qa_native_weapon_client_layout {
  double byte_length;
  double weapon, view_angles, forward;
} qa_native_weapon_client_layout;
typedef struct qa_native_weapon_equipped {
  double byte_length;
  double callback;
  qa_native_weapon_entry expected;
} qa_native_weapon_equipped;
typedef struct qa_native_weapon_time { double rva; } qa_native_weapon_time;
typedef struct qa_native_weapon_think {
  double tag;
  qa_native_weapon_registration_layout registration;
} qa_native_weapon_think;
typedef struct qa_native_weapon_entry_point { /* signature noted in field comment */
  qa_native_weapon_entry entry;
} qa_native_weapon_entry_point;
typedef struct qa_native_weapon_call_list {
  const qa_native_weapon_entry *calls;
  size_t call_count;
} qa_native_weapon_call_list;

struct qa_native_weapon_behavior_declaration {
  int64_t version; /* 1 */
  const char *kind;
  const char *abi;
  const char *artifact_path;
  qa_content_digest artifact_digest;
  const char *id; /* `${string}:${string}` */
  const char *title;
  qa_projectile_role role;
  const char *aspect;
  qa_native_weapon_entity_layout entity;
  qa_native_weapon_client_layout client;
  qa_native_weapon_equipped equipped_weapon;
  qa_native_weapon_time time;
  qa_native_weapon_think think;
  qa_native_weapon_entry_point allocate; /* signature 'void-pointer' */
  qa_native_weapon_entry_point free;     /* signature 'entity-void' */
  qa_native_weapon_entry projectile_touch;
  qa_native_weapon_call_list equip;  /* signature 'entity-void' */
  qa_native_weapon_call_list launch; /* signature 'entity-void' */
  bool has_activate_rva;
  double activate_rva;
  double fire_rva;
  qa_string_view_list initialization_classes;
  const qa_native_weapon_command *equipment; size_t equipment_count;
  qa_native_weapon_command ammunition;
  const qa_native_weapon_cvar *initial_cvars; size_t initial_cvar_count;
  const qa_native_weapon_cvar *provisioning_cvars; size_t provisioning_cvar_count;
};

#endif

/*
 * Port of src/contracts/qvm-mod-callbacks.ts (types only): QVM mod
 * callback declarations. String-literal unions keep their exact TS
 * strings. Optional/cross-module payloads (actor frame, items,
 * presentation) are pointers to break include cycles.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QVM_MOD_CALLBACKS_H
#define QA_CONTRACTS_QVM_MOD_CALLBACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/source_match.h"

typedef struct qa_qvm_mod_actor_frame qa_qvm_mod_actor_frame;
typedef struct qa_qvm_mod_items qa_qvm_mod_items;
typedef struct qa_qvm_mod_presentation_declaration qa_qvm_mod_presentation_declaration;

/* TS QvmModScalar = "int32" | "float32" (returns add "void"). */
typedef const char *qa_qvm_mod_scalar;

/* Shared input name union: "self"|"other"|"activator"|"attacker"|"inflictor". */
typedef const char *qa_qvm_mod_input_name;

/* TS QvmModValue union on kind: scalar|"vector"|"string" carrying a
 * ModCallbackValue | "actor"(record,input) | "client"(input) |
 * "time"(input "time"|"elapsed", units "seconds"|"milliseconds",
 * encoding) | "address"(value number). */
typedef struct qa_qvm_mod_value {
  const char *kind;
  qa_mod_callback_value value;
  const char *record;
  qa_qvm_mod_input_name input;
  const char *time_input;
  const char *units;
  qa_qvm_mod_scalar encoding;
  double address_value;
} qa_qvm_mod_value;

typedef struct qa_qvm_mod_global_value {
  double address;
  qa_qvm_mod_value value;
} qa_qvm_mod_global_value;

typedef struct qa_qvm_mod_source_call {
  double entry;
  const qa_qvm_mod_value *arguments;
  size_t argument_count;
  const qa_qvm_mod_global_value *globals;
  size_t global_count;
  const char *returns; /* QvmModScalar | "void" */
} qa_qvm_mod_source_call;

typedef struct qa_qvm_mod_pickup_context {
  const char *record;
  double offset;
  qa_qvm_mod_value value;
} qa_qvm_mod_pickup_context;

/* TS QvmModPickup extends ModPickupRule<QvmModSourceCall>: its
 * operation payloads point at qa_qvm_mod_source_call. */
typedef struct qa_qvm_mod_pickup {
  qa_mod_pickup_rule rule;
  const qa_qvm_mod_pickup_context *context;
  size_t context_count;
} qa_qvm_mod_pickup;

/* TS QvmModCallback = ModCallbackBinding & QvmModSourceCall. */
typedef struct qa_qvm_mod_callback {
  qa_mod_callback_binding binding;
  qa_qvm_mod_source_call call;
} qa_qvm_mod_callback;

/* TS QvmModActorField = {offset, access?} & bindings. access:
 * "read-only"|"read-write". binding: SourceMatchField's
 * "score"|"team" (with encoding + team values) | "health" |
 * "inventory"(item) | "origin"|"velocity"|"angles"|"bounds-min"|
 * "bounds-max" | "record"(record) | "constant"(value) |
 * "constant-vector"(value) | "private"(byteLength). */
typedef struct qa_qvm_mod_actor_field {
  double offset;
  const char *access; /* optional */
  const char *binding;
  qa_qvm_mod_scalar encoding;
  const qa_source_team_value *team_values;
  size_t team_value_count;
  qa_item_id item;
  const char *record;
  double constant_value;
  qa_vec3 constant_vector;
  double byte_length;
} qa_qvm_mod_actor_field;

typedef struct qa_qvm_mod_actor_record {
  const char *id;
  double address, stride, capacity;
  const qa_qvm_mod_actor_field *fields;
  size_t field_count;
} qa_qvm_mod_actor_record;

typedef struct qa_qvm_mod_release_entry { double entry, argument; } qa_qvm_mod_release_entry;
/* Callback offsets are number | null in TS (has_ flags). */
typedef struct qa_qvm_mod_source_callbacks {
  bool has_touch, has_use, has_pain, has_die;
  double touch, use, pain, die;
} qa_qvm_mod_source_callbacks;

typedef struct qa_qvm_mod_source_actors {
  double allocate;
  qa_qvm_mod_release_entry release;
  double inuse, event_entity_type;
  const qa_qvm_mod_source_call *update; /* nullable */
  const qa_qvm_mod_actor_frame *frame;  /* optional */
  qa_double_view initial_stores;        /* optional */
  const qa_qvm_mod_source_callbacks *callbacks; /* optional */
} qa_qvm_mod_source_actors;

typedef struct qa_qvm_mod_combat_client {
  double pointer;
  const char *record;
  double health, armor, protection, team;
} qa_qvm_mod_combat_client;

/* The CombatFields base of TS QvmModCombat. */
typedef struct qa_qvm_mod_combat_fields {
  double entry, health, takedamage, flags, godmode, no_knockback;
  const qa_qvm_mod_global_value *globals;
  size_t global_count;
  const qa_qvm_mod_combat_client *client; /* nullable */
} qa_qvm_mod_combat_fields;

/* Role sets per call are fixed in TS; the C form keeps the generic
 * role->index pairs of qa_qvm_combat_call. */
typedef struct qa_qvm_mod_combat_calls {
  qa_qvm_combat_call damage, touch, use, pain, die;
} qa_qvm_mod_combat_calls;

/* abi: "q3-g-damage" | "declared" (declared adds calls/damageFlags/
 * mass/teams). */
typedef struct qa_qvm_mod_combat {
  qa_qvm_mod_combat_fields fields;
  const char *abi;
  qa_qvm_mod_combat_calls calls;
  qa_qvm_damage_flags damage_flags;
  qa_qvm_combat_mass mass;
  const qa_qvm_combat_team *teams;
  size_t team_count;
} qa_qvm_mod_combat;

typedef struct qa_qvm_mod_protection_scalar {
  const char *record;
  double offset;
  qa_qvm_mod_scalar encoding;
} qa_qvm_mod_protection_scalar;

/* TS QvmModProtectionSelection<Value>: regular selects ItemId|null,
 * powered selects "none"|"screen"|"shield". Both rows in one struct.
 * mask is number | null (has_mask). */
typedef struct qa_qvm_mod_protection_selection_value {
  double value;
  qa_item_id selected_item;   /* regular; nullable */
  const char *selected_kind;  /* powered */
} qa_qvm_mod_protection_selection_value;
typedef struct qa_qvm_mod_protection_selection {
  qa_qvm_mod_protection_scalar field;
  bool has_mask;
  double mask;
  const qa_qvm_mod_protection_selection_value *values;
  size_t value_count;
} qa_qvm_mod_protection_selection;

/* TS QvmModProtection = call {id, admission, absorb, flags} &
 * (channel "regular" storage | channel "powered" storage). The
 * regular selection is optional; powered's is required. */
typedef struct qa_qvm_mod_protection {
  const char *id;
  qa_mod_protection_admission admission;
  qa_qvm_mod_source_call absorb;
  qa_mod_qc_protection_flags flags;
  const char *channel; /* "regular" | "powered" */
  qa_qvm_mod_protection_scalar storage_points; /* regular */
  qa_item_id storage_item;                     /* regular, nullable */
  qa_qvm_mod_protection_scalar storage_cells;  /* powered */
  bool has_selection;
  qa_qvm_mod_protection_selection selection;
} qa_qvm_mod_protection;

/* QvmModObjectiveAddress scalar payload for objectives. */
typedef struct qa_qvm_mod_objective_scalar {
  /* address: number | {kind:"global",address,indirections,offset} */
  bool address_is_global_pointer;
  double address;
  qa_double_view indirections;
  double offset;
  qa_qvm_mod_scalar encoding;
} qa_qvm_mod_objective_scalar;

/* TS QvmModInputPointer = ({kind:"argument",index} |
 * {kind:"global",address}) & {indirections, offset}. */
typedef struct qa_qvm_mod_input_pointer {
  bool is_global;
  double index;
  double address;
  qa_double_view indirections;
  double offset;
} qa_qvm_mod_input_pointer;

/* TS QvmModObjectiveAddress = number | global input pointer. */
typedef struct qa_qvm_mod_objective_address {
  bool is_global_pointer;
  double address;
  qa_qvm_mod_input_pointer pointer;
} qa_qvm_mod_objective_address;

/* TS QvmModInputOutput union on kind: "field" | "handler" | "command".
 * handler/command share QvmModInputHandler {entry, actor}.
 * inputs exclude "view-angles" (command also excludes "impulse").
 * field value: {input:"view-angles"} | {input, encoding, scale}. */
typedef struct qa_qvm_mod_input_output {
  const char *kind;
  const char *record;
  double offset;
  qa_mod_client_input value_input;
  qa_qvm_mod_scalar value_encoding;
  double value_scale;
  double entry;
  const char *actor_record;
  qa_qvm_mod_input_pointer actor_pointer;
  const qa_mod_client_input *inputs;
  size_t input_count;
  bool has_returns;
  qa_qvm_mod_scalar returns_encoding;
  double returns_value;
  qa_qvm_mod_input_pointer command;
} qa_qvm_mod_input_output;

typedef struct qa_qvm_mod_record_offset {
  const char *record;
  double offset;
} qa_qvm_mod_record_offset;

/* outputs instantiate Scalar=qa_qvm_mod_protection_scalar and
 * Vector=qa_qvm_mod_record_offset through the generic declaration's
 * opaque slots. input bindings instantiate
 * Call=qa_qvm_mod_source_call, Output=qa_qvm_mod_input_output. */
typedef struct qa_qvm_mod_clients {
  const qa_mod_client_output_declaration *outputs; /* optional */
  size_t output_count;
  double maximum;
  qa_string_view_list records;
  const char *player_state_record;
  const qa_qvm_mod_source_call *admit;
  size_t admit_count;
  const qa_qvm_mod_source_call *userinfo;
  size_t userinfo_count;
  const qa_qvm_mod_source_call *disconnect;
  size_t disconnect_count;
  const qa_qvm_mod_source_call *frame; /* optional */
  size_t frame_count;
  const qa_mod_client_input_binding *input; /* optional */
  size_t input_count;
} qa_qvm_mod_clients;

/* TS version is 1 and runtime "qvm". objectives instantiate
 * Scalar=qa_qvm_mod_objective_scalar,
 * Reference=qa_qvm_mod_objective_address,
 * Call=qa_qvm_mod_source_call. spawnEntities is optional and
 * nullable (has_spawn_entities distinguishes absent from null). */
typedef struct qa_qvm_mod_callback_declaration {
  const qa_source_objective_declaration *objectives; /* optional */
  size_t objective_count;
  int64_t version;
  const char *runtime;
  qa_mod_program program;
  qa_qvm_abi_profile abi_profile;
  const qa_qvm_mod_presentation_declaration *presentation; /* optional */
  bool has_spawn_entities;
  const char *spawn_entities; /* nullable */
  const qa_qvm_mod_clients *clients; /* optional */
  const qa_qvm_mod_actor_record *actor_records;
  size_t actor_record_count;
  const char *entity_record; /* nullable */
  const qa_qvm_mod_source_actors *source_actors; /* optional */
  const qa_qvm_mod_combat *combat; /* optional */
  const qa_qvm_mod_protection *protection; /* optional */
  size_t protection_count;
  const qa_qvm_mod_pickup *pickups; /* optional */
  size_t pickup_count;
  const qa_qvm_mod_items *items; /* optional */
  const qa_qvm_mod_source_call *initialize;
  size_t initialize_count;
  const qa_qvm_mod_callback *callbacks;
  size_t callback_count;
} qa_qvm_mod_callback_declaration;

#endif

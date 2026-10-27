/*
 * Port of src/contracts/native-mod-callbacks.ts (types only): native
 * mod callback declarations. The protection-region payload is a
 * forward-declared pointer (native_mod_region.h defines it) to
 * break the TS type-only include cycle. NativeModActorField's
 * Exclude<QvmModActorField, ...> part is embedded as
 * qa_qvm_mod_actor_field for the retained bindings.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_NATIVE_MOD_CALLBACKS_H
#define QA_CONTRACTS_NATIVE_MOD_CALLBACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/mod_client_presentation.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/source_match.h"

typedef struct qa_native_mod_protection_region qa_native_mod_protection_region;
typedef struct qa_native_mod_items qa_native_mod_items;

/* TS NativeModScalar: "int8"|"uint8"|"int16"|"uint16"|"int32"|
 * "uint32"|"int64"|"uint64"|"float32"|"float64" (returns add "void"). */
typedef const char *qa_native_mod_scalar;

typedef struct qa_native_mod_address {
  double rva;
  qa_double_view indirections;
} qa_native_mod_address;

/* TS NativeModEntry: {kind:"export",name} | {kind:"rva",rva}.
 * Source calls additionally allow {kind:"game-export",name}. */
typedef struct qa_native_mod_entry {
  const char *kind;
  const char *name;
  double rva;
} qa_native_mod_entry;

/* TS NativeModValue union on kind: scalar|"vector"|"string"
 * (ModCallbackValue) | "actor"(record,input) | "client"(input) |
 * "userinfo"(input) | "user-command" | "time"(input,units,encoding)
 * | "address"(NativeModAddress | null). input names:
 * "self"|"other"|"activator"|"attacker"|"inflictor". */
typedef struct qa_native_mod_value {
  const char *kind;
  qa_mod_callback_value value;
  const char *record;
  const char *input;
  const char *time_input; /* "time" | "elapsed" */
  const char *units;      /* "seconds" | "milliseconds" */
  qa_native_mod_scalar encoding;
  const qa_native_mod_address *address_value; /* nullable */
} qa_native_mod_value;

typedef struct qa_native_mod_global_value {
  qa_native_mod_address address;
  qa_native_mod_value value;
} qa_native_mod_global_value;

typedef struct qa_native_mod_skip { double entry, join; } qa_native_mod_skip;

typedef struct qa_native_mod_source_call {
  qa_native_mod_entry entry;
  const qa_native_mod_value *arguments;
  size_t argument_count;
  const qa_native_mod_global_value *globals;
  size_t global_count;
  const char *returns; /* NativeModScalar | "void" */
  const qa_native_mod_skip *skips; /* optional */
  size_t skip_count;
} qa_native_mod_source_call;

/* TS NativeModCallback = ModCallbackBinding & NativeModSourceCall. */
typedef struct qa_native_mod_callback {
  qa_mod_callback_binding binding;
  qa_native_mod_source_call call;
} qa_native_mod_callback;

typedef struct qa_native_mod_pickup_context {
  const char *record;
  double offset;
  qa_native_mod_value value;
} qa_native_mod_pickup_context;

/* TS NativeModPickup extends ModPickupRule<NativeModSourceCall>. */
typedef struct qa_native_mod_pickup {
  qa_mod_pickup_rule rule;
  const qa_native_mod_pickup_context *context;
  size_t context_count;
} qa_native_mod_pickup;

/* accepts: "always" | "nonzero". */
typedef struct qa_native_mod_admission_call {
  qa_native_mod_source_call call;
  const char *accepts;
} qa_native_mod_admission_call;

typedef struct qa_native_mod_client_input_field {
  const char *record;
  double offset;
  qa_native_mod_value value;
} qa_native_mod_client_input_field;

/* TS NativeModInputOutput: {kind:"field",record,offset} |
 * {kind:"handler",entry,arguments,inputs}. */
typedef struct qa_native_mod_input_output {
  const char *kind;
  const char *record;
  double offset;
  qa_native_mod_entry entry;
  const qa_native_mod_value *arguments;
  size_t argument_count;
  const qa_mod_client_input *inputs;
  size_t input_count;
} qa_native_mod_input_output;

typedef struct qa_native_mod_scalar_field {
  double offset;
  qa_native_mod_scalar encoding;
} qa_native_mod_scalar_field;

typedef struct qa_native_mod_armor_field {
  qa_native_mod_scalar_field field;
  const char *record;
} qa_native_mod_armor_field;

typedef struct qa_native_mod_record_offset {
  const char *record;
  double offset;
} qa_native_mod_record_offset;

typedef struct qa_native_mod_client_pose_crouched {
  qa_native_mod_armor_field field;
  double mask;
} qa_native_mod_client_pose_crouched;
typedef struct qa_native_mod_client_pose {
  qa_native_mod_armor_field view_height;
  qa_native_mod_client_pose_crouched crouched;
} qa_native_mod_client_pose;

/* outputs instantiate Scalar=qa_native_mod_armor_field and
 * Vector=qa_native_mod_record_offset. input bindings instantiate
 * Call=qa_native_mod_source_call, Output=qa_native_mod_input_output. */
typedef struct qa_native_mod_clients {
  const qa_mod_client_output_declaration *outputs; /* optional */
  size_t output_count;
  double maximum;
  qa_string_view_list records;
  const qa_native_mod_admission_call *admit;
  size_t admit_count;
  const qa_native_mod_source_call *userinfo;
  size_t userinfo_count;
  const qa_native_mod_source_call *disconnect;
  size_t disconnect_count;
  const qa_native_mod_source_call *command;
  size_t command_count;
  const qa_mod_client_input_binding *input; /* optional */
  size_t input_count;
  const qa_native_mod_source_call *frame; /* optional */
  size_t frame_count;
  const qa_native_mod_source_call *end_frame; /* optional */
  size_t end_frame_count;
  const qa_native_mod_client_input_field *input_fields; /* optional */
  size_t input_field_count;
  const qa_native_mod_client_pose *pose; /* optional */
} qa_native_mod_clients;

/* TS NativeModActorField: retained QvmModActorField bindings
 * (embedded, qvm_field.encoding unused) plus native variants tagged
 * by `binding`: "address"(value NativeModAddress|null),
 * SourceMatchField ("score"|"team") with NativeModScalar encoding,
 * "health", "inventory"(item), "inventory-capacity"(item),
 * "constant"(numeric value) — all with encoding. */
typedef struct qa_native_mod_actor_field {
  double offset;
  const char *binding;
  qa_qvm_mod_actor_field qvm_field;
  const qa_native_mod_address *address_value; /* nullable */
  qa_native_mod_scalar encoding;
  const qa_source_team_value *team_values;
  size_t team_value_count;
  qa_item_id item;
  double constant_value;
} qa_native_mod_actor_field;

/* base: {kind:"entities"|"clients"} | {kind:"address", rva, indirections}. */
typedef struct qa_native_mod_actor_record_base {
  const char *kind;
  qa_native_mod_address address;
} qa_native_mod_actor_record_base;

typedef struct qa_native_mod_actor_record {
  const char *id;
  qa_native_mod_actor_record_base base;
  double stride, first_slot, capacity;
  const qa_native_mod_actor_field *fields;
  size_t field_count;
} qa_native_mod_actor_record;

typedef struct qa_native_mod_clock_entry {
  qa_native_mod_address address;
  const char *input; /* "time" | "frame" */
  qa_native_mod_scalar encoding;
  const char *units; /* "seconds" | "milliseconds" */
} qa_native_mod_clock_entry;

typedef struct qa_native_mod_timed_field {
  double offset;
  qa_native_mod_scalar encoding;
  const char *units; /* "seconds" | "milliseconds" */
} qa_native_mod_timed_field;

/* fields.use is number | null (has_use). */
typedef struct qa_native_mod_source_fields {
  double velocity, ground;
  bool has_use;
  double use;
  double think;
  qa_native_mod_timed_field nextthink;
} qa_native_mod_source_fields;

typedef struct qa_native_mod_update_entry {
  qa_native_mod_entry entry;
  const char *returns; /* NativeModScalar | "void" */
} qa_native_mod_update_entry;

/* abi: "q2-classic"|"q2-rerelease"; offsets number | null. */
typedef struct qa_native_mod_source_callbacks {
  const char *abi;
  bool has_touch, has_pain, has_die;
  double touch, pain, die;
} qa_native_mod_source_callbacks;

typedef struct qa_native_mod_combat qa_native_mod_combat;

typedef struct qa_native_mod_source_actors {
  qa_native_mod_entry allocate;
  qa_native_mod_entry release;
  qa_native_mod_update_entry update;
  double frame_seconds;
  const qa_native_mod_clock_entry *clock;
  size_t clock_count;
  qa_native_mod_source_fields fields;
  const qa_native_mod_source_callbacks *callbacks; /* optional */
  const qa_native_mod_combat *combat;              /* optional */
} qa_native_mod_source_actors;

/* TS NativeModArmorSelection: {kind:"positive",field} |
 * {kind:"enum",field,value,none}. */
typedef struct qa_native_mod_armor_selection {
  bool is_enum;
  qa_native_mod_armor_field field;
  double value, none;
} qa_native_mod_armor_selection;

typedef struct qa_native_mod_armor_enabled {
  qa_native_mod_armor_field field;
  double mask;
} qa_native_mod_armor_enabled;

/* kind: "screen" | "shield". */
typedef struct qa_native_mod_power_armor_item {
  qa_item_id item;
  const char *kind;
  qa_native_mod_armor_selection selection;
  qa_native_mod_armor_field cells;
  const qa_native_mod_armor_enabled *enabled; /* nullable */
} qa_native_mod_power_armor_item;

typedef struct qa_native_mod_regular_armor_item {
  qa_item_id item; /* nullable */
  qa_native_mod_armor_selection selection;
  qa_native_mod_armor_field points;
} qa_native_mod_regular_armor_item;

typedef struct qa_native_mod_q2_regular_armor_item {
  qa_item_id item;
  qa_native_mod_armor_selection selection;
  qa_native_mod_armor_field points;
  double normal_protection, energy_protection;
} qa_native_mod_q2_regular_armor_item;

/* TS NativeModArmor union on kind: "none" | "q2" | "source". */
typedef struct qa_native_mod_armor {
  const char *kind;
  const qa_native_mod_q2_regular_armor_item *q2_regular;
  size_t q2_regular_count;
  const qa_native_mod_regular_armor_item *regular;
  size_t regular_count;
  const qa_native_mod_power_armor_item *power;
  size_t power_count;
} qa_native_mod_armor;

/* TS NativeModQ2ArmorCall: flags "q2-classic"|"q2-rerelease",
 * globals optional. */
typedef struct qa_native_mod_q2_armor_call {
  qa_native_mod_entry entry;
  const char *flags;
  const qa_native_mod_global_value *globals;
  size_t global_count;
} qa_native_mod_q2_armor_call;

/* TS NativeModProtectionDefinition = claim {id, admission?} &
 * (channel "powered"|"regular", storage, absorb). absorb is one of:
 * {abi:"source-call", call} | NativeModProtectionRegion
 * (abi "source-region") | Q2 armor calls (abi "q2-check-power-armor"
 * / "q2-check-armor"+sparks). admission kind "claim" here is the
 * only no-owner kind besides "replace-current-primary". */
typedef struct qa_native_mod_protection_definition {
  const char *id;
  qa_mod_protection_admission admission;
  const char *channel; /* "powered" | "regular" */
  const qa_native_mod_power_armor_item *power_storage;
  size_t power_storage_count;
  const qa_native_mod_regular_armor_item *regular_storage;
  size_t regular_storage_count;
  const char *absorb_abi;
  qa_native_mod_source_call absorb_call;
  const qa_native_mod_protection_region *absorb_region;
  qa_native_mod_q2_armor_call absorb_q2;
  double absorb_sparks;
} qa_native_mod_protection_definition;

typedef struct qa_native_mod_deferred_damage {
  qa_native_mod_entry process;
  double attacker, inflictor;
  qa_native_mod_scalar_field blood, knockback;
  double point, mod, receipt;
} qa_native_mod_deferred_damage;

typedef struct qa_native_mod_damage_entry {
  qa_native_mod_entry entry;
  const char *abi; /* "q2-classic" | "q2-rerelease" */
} qa_native_mod_damage_entry;
/* causes: {edition:"classic", game:"base"|"xatrix"|"rogue"|"ctf"} |
 * {edition:"rerelease"}. */
typedef struct qa_native_mod_causes {
  const char *edition;
  const char *game;
} qa_native_mod_causes;
typedef struct qa_native_mod_combat_flags {
  qa_native_mod_scalar_field field;
  double invulnerable, no_knockback;
} qa_native_mod_combat_flags;

struct qa_native_mod_combat {
  qa_native_mod_damage_entry damage;
  qa_native_mod_causes causes;
  qa_native_mod_scalar_field health, mass, takedamage;
  qa_native_mod_combat_flags flags;
  qa_native_mod_armor armor;
  const qa_native_mod_deferred_damage *deferred; /* optional */
};

/* target pairs a Q2 game api with its single allowed ABI:
 * q2-classic-game/windows-i386 or q2-rerelease-game/windows-x86-64. */
typedef struct qa_native_mod_target {
  qa_q2_game_api_identity api;
  qa_native_abi abi;
} qa_native_mod_target;

/* objectives instantiate Scalar={address: qa_native_mod_address +
 * encoding below}, Reference=qa_native_mod_address,
 * Call=qa_native_mod_source_call. */
typedef struct qa_native_mod_objective_scalar {
  qa_native_mod_address address;
  qa_native_mod_scalar encoding;
} qa_native_mod_objective_scalar;

/* TS version is 1 and runtime "native". spawnEntities and
 * entityRecord are string | null (kept nullable). */
typedef struct qa_native_mod_declaration {
  const qa_source_objective_declaration *objectives; /* optional */
  size_t objective_count;
  const qa_native_mod_client_presentation *client_presentation; /* optional */
  int64_t version;
  const char *runtime;
  qa_mod_program program;
  qa_native_mod_target target;
  const qa_native_mod_source_actors *source_actors; /* optional */
  const qa_native_mod_protection_definition *protection; /* optional */
  size_t protection_count;
  const qa_native_mod_pickup *pickups; /* optional */
  size_t pickup_count;
  const qa_native_mod_items *items; /* optional */
  const qa_native_mod_clients *clients; /* optional */
  const qa_mod_named_string *cvars;
  size_t cvar_count;
  const char *spawn_entities; /* nullable */
  const qa_native_mod_actor_record *actor_records;
  size_t actor_record_count;
  const char *entity_record; /* nullable */
  const qa_native_mod_source_call *initialize;
  size_t initialize_count;
  const qa_native_mod_source_call *project;
  size_t project_count;
  const qa_native_mod_source_call *release;
  size_t release_count;
  const qa_native_mod_callback *callbacks;
  size_t callback_count;
} qa_native_mod_declaration;

#endif

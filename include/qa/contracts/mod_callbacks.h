/*
 * Port of src/contracts/mod-callbacks.ts (types only): QuakeC mod
 * callback declarations. Also the home of shared mod-callback base
 * shapes used by qvm-mod-callbacks.ts / native-mod-callbacks.ts.
 * String-literal unions keep their exact TS strings (documented per
 * field); the generic ModClientInputBinding<Call,Output> stores its
 * element lists as opaque storage + elem size. The QC statement
 * shape repeated inline in several TS files is named once here.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MOD_CALLBACKS_H
#define QA_CONTRACTS_MOD_CALLBACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/mod_client_presentation.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/source_match.h"

typedef struct qa_qc_weapon_stage_declaration qa_qc_weapon_stage_declaration;

/* TS QcModObjectiveStorage = string | {kind:"entity-field", global,
 * indirections, field}. */
typedef struct qa_qc_mod_objective_storage {
  bool is_entity_field;
  const char *name;  /* plain-string form */
  const char *global;
  qa_string_view_list indirections;
  const char *field;
} qa_qc_mod_objective_storage;

/* item: "q1:item_armor1" | "q1:item_armor2" | "q1:item_armorInv". */
typedef struct qa_mod_qc_empty_armor {
  qa_item_id item;
  double absorption;
} qa_mod_qc_empty_armor;

/* TS ModClientInput (exact strings): "view-angles"|"attack"|"jump"|
 * "impulse"|"forward-move"|"side-move"|"up-move". */
typedef const char *qa_mod_client_input;
/* TS ModCallbackInput = ModClientInput | "self"|"other"|"activator"|
 * "attacker"|"inflictor"|"amount"|"damage-flags"|
 * "regular-protection-scale"|"knockback"|"point"|"direction"|
 * "normal"|"item"|"time"|"elapsed"|"result"|"pickup-count"|
 * "pickup-has-count"|"pickup-dropped". */
typedef const char *qa_mod_callback_input;

/* TS ModClientInputOutput union on kind. The "set" value is a Vec3
 * for input "view-angles" and a number otherwise. */
typedef enum qa_mod_client_input_output_kind {
  QA_MOD_CLIENT_INPUT_OUTPUT_SET, QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME
} qa_mod_client_input_output_kind;
typedef struct qa_mod_client_input_output {
  qa_mod_client_input_output_kind kind;
  qa_mod_client_input input;   /* set */
  qa_vec3 vector_value;         /* set + view-angles */
  double number_value;          /* set otherwise */
  const qa_mod_client_input *inputs; /* consume (excludes view-angles) */
  size_t input_count;
} qa_mod_client_input_output;

/* TS ModCallbackValue union on kind: "input"(name) | "float"(value) |
 * "string"(value) | "vector"(value). */
typedef struct qa_mod_callback_value {
  const char *kind;
  qa_mod_callback_input name;
  double number_value;
  const char *string_value;
  qa_vec3 vector_value;
} qa_mod_callback_value;

/* TS ModActorField = {field} & bindings. binding is a
 * SourceMatchField binding ("score"|"team", team carries values) or
 * "health"|"origin"|"velocity"|"angles"|"bounds-min"|"bounds-max"|
 * "think"|"nextthink"|"private"|"classname"|"view-offset" |
 * "client-flags"(grounded?, privateMask?) |
 * "client-input"(input, update "always"|"nonzero", scale?) |
 * "userinfo"(key) | "inventory"(item) | "constant"(non-input value). */
typedef struct qa_mod_actor_field {
  const char *field;
  const char *binding;
  const qa_source_team_value *team_values; /* binding "team" */
  size_t team_value_count;
  bool has_grounded;
  bool has_private_mask;
  double private_mask;
  qa_mod_client_input input;
  const char *update;
  bool has_scale;
  double scale;
  const char *key;
  qa_item_id item;
  qa_mod_callback_value constant_value;
} qa_mod_actor_field;

/* TS ModActorOperation: "actor.think"|"actor.touch"|"actor.use"|
 * "actor.pain"|"actor.die". ModCallbackOperation adds "damage"|
 * "inventory.give"|"inventory.consume". Exact strings. */
typedef const char *qa_mod_actor_operation;
typedef const char *qa_mod_callback_operation;

typedef struct qa_mod_named_value {
  const char *name;
  qa_mod_callback_value value;
} qa_mod_named_value;

typedef struct qa_mod_source_call {
  const char *function;
  const qa_mod_callback_value *arguments;
  size_t argument_count;
  const qa_mod_named_value *globals;
  size_t global_count;
} qa_mod_source_call;

/* TS ModCallbackBinding: {id} & union on stage:
 * "observe"(operation any) | "transform" damage (result
 * "amount"|"knockback") | "transform" inventory.give/consume
 * (result "amount") | "replace" actor ops (result "boolean"). */
typedef struct qa_mod_callback_binding {
  const char *id; /* `${string}:${string}` */
  qa_mod_callback_operation operation;
  const char *stage;  /* "observe"|"transform"|"replace" */
  const char *result; /* "amount"|"knockback"|"boolean"; NULL = observe */
} qa_mod_callback_binding;

/* TS ModCallback = ModSourceCall & ModCallbackBinding. */
typedef struct qa_mod_callback {
  qa_mod_source_call call;
  qa_mod_callback_binding binding;
} qa_mod_callback;

/* TS ModClientInputBinding<Call, Output = never>: {scope, calls} &
 * ({phase:"before", outputs?} | {phase:"after"}). Generic element
 * lists are opaque storage + elem size; the concrete Call/Output
 * types are those of the instantiating declaration. scope:
 * "client-command"|"movement-slice"; phase: "before"|"after". */
typedef struct qa_mod_client_input_binding {
  const char *scope;
  const void *calls;
  size_t call_count;
  size_t call_elem_size;
  const char *phase;
  const void *outputs; /* optional, before only */
  size_t output_count;
  size_t output_elem_size;
} qa_mod_client_input_binding;

/* TS ModQcInputOutput union on kind: "field"(field) |
 * "handler"(function, inputs excluding view-angles). */
typedef struct qa_mod_qc_input_output {
  bool is_handler;
  const char *field;
  const char *function;
  const qa_mod_client_input *inputs;
  size_t input_count;
} qa_mod_qc_input_output;

/* TS ModRuntimeValue = non-input ModCallbackValue |
 * {kind:"actor", value: ActorId | null}. */
typedef struct qa_mod_runtime_value {
  const char *kind; /* "float"|"string"|"vector"|"actor" */
  double number_value;
  const char *string_value;
  qa_vec3 vector_value;
  const qa_actor_id *actor_value; /* nullable */
} qa_mod_runtime_value;

/* TS ModConsoleValue = non-input ModCallbackValue |
 * {kind:"argument",index,type "string"|"float"} |
 * {kind:"arguments-text"|"argument-count"}. */
typedef struct qa_mod_console_value {
  const char *kind;
  double number_value;
  const char *string_value;
  qa_vec3 vector_value;
  double index;
  const char *type;
} qa_mod_console_value;

typedef struct qa_mod_console_named_value {
  const char *name;
  qa_mod_console_value value;
} qa_mod_console_named_value;

typedef struct qa_mod_console_command {
  const char *name;
  const char *function;
  const qa_mod_console_value *arguments;
  size_t argument_count;
  const qa_mod_console_named_value *globals;
  size_t global_count;
} qa_mod_console_command;

/* QC statement word, inline in several TS declaration files. */
typedef struct qa_qc_statement {
  double opcode, a, b, c;
} qa_qc_statement;

/* kind (optional): "multiplier" | "identity" | "transform". */
typedef struct qa_mod_qc_damage_scale {
  const char *kind;
  const char *function;
  double entry, exit, damage;
  const qa_qc_statement *statements;
  size_t statement_count;
} qa_mod_qc_damage_scale;

typedef struct qa_mod_qc_armor_regular_scale {
  const char *caller;
  double statement, scale;
} qa_mod_qc_armor_regular_scale;
/* flags: {kind:"none"} | {kind:"bits", word, noArmor, ...}. */
typedef struct qa_mod_qc_armor_flags {
  bool is_bits;
  double word, no_armor, no_power_armor, no_regular_armor, energy;
} qa_mod_qc_armor_flags;

typedef struct qa_mod_qc_armor_stage {
  const char *function;
  double entry, exit, target, damage, saved;
  const qa_mod_qc_armor_regular_scale *regular_scale; /* optional */
  size_t regular_scale_count;
  qa_mod_qc_armor_flags flags;
  const qa_qc_statement *statements;
  size_t statement_count;
} qa_mod_qc_armor_stage;

/* Shared protection admission, also used by the qvm/native
 * declarations: absent, or kind "claim"|"replace-current-primary" |
 * "replace-primary" with owner. */
typedef struct qa_mod_protection_admission {
  const char *kind; /* NULL = absent */
  qa_provider_id owner;
} qa_mod_protection_admission;

typedef struct qa_mod_qc_protection_flags {
  double no_armor, no_power_armor, no_regular_armor, energy, radius;
} qa_mod_qc_protection_flags;

typedef struct qa_mod_qc_protection_item_value {
  double value;
  qa_item_id item; /* nullable */
} qa_mod_qc_protection_item_value;
typedef struct qa_mod_qc_protection_kind_value {
  double value;
  const char *kind; /* "none" | "screen" | "shield" */
} qa_mod_qc_protection_kind_value;

/* TS ModQcProtection: base {id, admission?, absorb, flags} &
 * (channel "regular" storage | channel "powered" storage).
 * absorb: {kind:"function",call} | {kind:"region",call,stage}.
 * storage selections are optional (has_selection) with optional mask. */
typedef struct qa_mod_qc_protection {
  const char *id;
  qa_mod_protection_admission admission;
  bool absorb_is_region;
  qa_mod_source_call absorb_call;
  qa_mod_qc_armor_stage absorb_stage;
  qa_mod_qc_protection_flags flags;
  const char *channel; /* "regular" | "powered" */
  const char *storage_points; /* regular */
  qa_item_id storage_item;     /* regular, nullable */
  const char *storage_cells;  /* powered */
  const char *storage_kind;   /* powered: "screen" | "shield" */
  bool has_selection;
  const char *selection_field;
  bool has_selection_mask;
  double selection_mask;
  const qa_mod_qc_protection_item_value *item_values;
  size_t item_value_count;
  const qa_mod_qc_protection_kind_value *kind_values;
  size_t kind_value_count;
} qa_mod_qc_protection;

/* Item definition rows: base {item,label,icon?,admission,actions?} &
 * ({kind:"counter"} | SourceWeaponItem). icon may be absent
 * (has_icon false) or present-but-null. admission:
 * "add"|"replace-primary". This shape repeats in qvm-mod-items.ts
 * and native-mod-items.ts with their own call types. */
typedef struct qa_mod_qc_item_definition {
  qa_item_id item;
  const char *label;
  bool has_icon;
  const qa_source_item_icon_declaration *icon; /* nullable */
  const char *admission;
  bool has_actions;
  const qa_mod_source_call *action_use;  /* nullable */
  const qa_mod_source_call *action_drop; /* nullable */
  bool is_weapon;
  qa_source_weapon_item weapon;
} qa_mod_qc_item_definition;

/* capacity: {kind:"constant",value} | {kind:"field",field}. */
typedef struct qa_mod_qc_item_capacity {
  bool is_constant;
  double value;
  const char *field;
} qa_mod_qc_item_capacity;
typedef struct qa_mod_qc_item_mask { qa_item_id item; double mask; } qa_mod_qc_item_mask;
/* storage union on kind: "counter" | "bits". */
typedef struct qa_mod_qc_item_storage {
  const char *kind;
  const char *field;
  qa_item_id item;                       /* counter */
  qa_mod_qc_item_capacity capacity;      /* counter */
  double private_mask;                   /* bits */
  const qa_mod_qc_item_mask *items;      /* bits */
  size_t item_count;
} qa_mod_qc_item_storage;

typedef struct qa_mod_qc_weapon_value { double value; qa_item_id item; } qa_mod_qc_weapon_value;
typedef struct qa_mod_qc_weapons {
  const qa_qc_weapon_stage_declaration *stage;
  const char *selected_field;
  const qa_mod_qc_weapon_value *selected_values;
  size_t selected_value_count;
  const char *select_field;
  const qa_mod_qc_weapon_value *select_values;
  size_t select_value_count;
  qa_mod_source_call select_call;
  const qa_mod_source_call *resume;
  size_t resume_count;
  const char *model_field;
  const char *model_frame;
} qa_mod_qc_weapons;

typedef struct qa_mod_qc_items {
  const qa_mod_qc_item_definition *definitions;
  size_t definition_count;
  const qa_mod_qc_item_storage *storage;
  size_t storage_count;
  const qa_mod_qc_weapons *weapons; /* optional */
} qa_mod_qc_items;

typedef struct qa_mod_program {
  const char *path;
  qa_content_digest digest;
} qa_mod_program;

typedef struct qa_mod_named_string {
  const char *name;
  const char *value;
} qa_mod_named_string;

/* ModCallbackDeclaration.clients. outputs instantiate
 * ModClientOutputDeclaration<string, string>: the opaque scalar/
 * vector slots hold const char *. input bindings instantiate
 * Call=qa_mod_source_call, Output=qa_mod_qc_input_output. */
typedef struct qa_mod_qc_clients {
  const qa_mod_client_output_declaration *outputs; /* optional */
  size_t output_count;
  double maximum;
  const qa_mod_source_call *admit;
  size_t admit_count;
  const qa_mod_source_call *userinfo;
  size_t userinfo_count;
  const qa_mod_source_call *disconnect;
  size_t disconnect_count;
  const qa_mod_source_call *frame; /* optional */
  size_t frame_count;
  const qa_mod_client_input_binding *input; /* optional */
  size_t input_count;
} qa_mod_qc_clients;

/* combat: damageScale/armorStage/emptyArmor optional. */
typedef struct qa_mod_qc_combat {
  qa_mod_source_call damage;
  const qa_mod_qc_damage_scale *damage_scale;
  const qa_mod_qc_armor_stage *armor_stage;
  const qa_mod_qc_empty_armor *empty_armor;
} qa_mod_qc_combat;

/* TS version is the literal 1 and runtime "quakec". objectives
 * instantiate Scalar/Reference=qa_qc_mod_objective_storage and
 * Call=qa_mod_source_call through the generic declaration pointers.
 * pickups instantiate Call=qa_mod_source_call in
 * qa_mod_pickup_rule (call_elem_size = sizeof that struct). */
typedef struct qa_mod_callback_declaration {
  const qa_source_objective_declaration *objectives; /* optional */
  size_t objective_count;
  const qa_qc_mod_client_presentation *client_presentation; /* optional */
  int64_t version;
  const char *runtime;
  qa_mod_program program;
  const qa_mod_actor_field *actor_fields;
  size_t actor_field_count;
  const qa_mod_callback *callbacks;
  size_t callback_count;
  const qa_mod_qc_clients *clients; /* optional */
  const qa_mod_named_string *cvars; /* optional */
  size_t cvar_count;
  const qa_mod_source_call *initialize; /* optional */
  size_t initialize_count;
  const qa_mod_source_call *frame; /* optional */
  const qa_mod_console_command *commands; /* optional */
  size_t command_count;
  const qa_mod_qc_combat *combat; /* optional */
  const qa_mod_qc_protection *protection; /* optional */
  size_t protection_count;
  const qa_mod_pickup_rule *pickups; /* optional */
  size_t pickup_count;
  const qa_mod_qc_items *items; /* optional */
} qa_mod_callback_declaration;

#endif

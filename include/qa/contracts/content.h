/*
 * Port of src/contracts/content.ts: content identities, mounts,
 * resource references, launch selections, and the executable recipe.
 * Branded template-literal ids are C strings built by the qa_create_*
 * helpers in content.c (malloc'd; caller frees; NULL only on OOM;
 * invalid components abort with the exact TS RangeError message).
 * Cross-module payloads that would cycle includes (QvmGrappleDefinition,
 * WeaponBehaviorDefinition, ResolvedGameplayMod, ...) are forward
 * declared and held by pointer.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_CONTENT_H
#define QA_CONTRACTS_CONTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/time.h"

typedef struct qa_qvm_grapple_definition qa_qvm_grapple_definition;
typedef struct qa_weapon_behavior_definition qa_weapon_behavior_definition;
typedef struct qa_qvm_weapon_behavior_layout qa_qvm_weapon_behavior_layout;
typedef struct qa_native_weapon_behavior_declaration qa_native_weapon_behavior_declaration;
typedef struct qa_resolved_gameplay_mod qa_resolved_gameplay_mod;

/* TS GameFamily = "q1" | "q2" | "q3". */
typedef enum qa_game_family {
  QA_GAME_FAMILY_Q1, /* "q1" */
  QA_GAME_FAMILY_Q2, /* "q2" */
  QA_GAME_FAMILY_Q3  /* "q3" */
} qa_game_family;
const char *qa_game_family_name(qa_game_family family);
/* Returns false when the string is not an exact TS GameFamily. */
bool qa_game_family_parse(const char *text, qa_game_family *out);

typedef const char *qa_content_id;    /* `q1|q2|q3:...:...:...` */
typedef const char *qa_recipe_id;     /* `recipe:...:...` */
typedef const char *qa_resource_id;   /* `resource:...` */
typedef const char *qa_mount_id;      /* `mount:...:...` */
typedef const char *qa_mount_plan_id; /* `mount-plan:...:...` */
typedef const char *qa_content_digest; /* `sha256:<64 lowercase hex>` */

typedef struct qa_content_identity {
  qa_game_family family;
  const char *edition;
  const char *package;
  const char *revision;
} qa_content_identity;

char *qa_create_content_id(qa_content_identity identity);
bool qa_is_content_id(const char *value);
char *qa_create_recipe_id(const char *name_space, const char *revision);
char *qa_create_mount_id(const char *name_space, const char *name);
char *qa_create_mount_plan_id(const char *name_space, const char *revision);
/* Lowercases the 64 hex digits, as TS does. */
char *qa_create_content_digest(const char *hex);
bool qa_is_content_digest(const char *value);

typedef struct qa_mount_identity {
  qa_mount_id id;
  qa_content_id content;
  int64_t generation;
} qa_mount_identity;
/* TS Object.freeze has no C counterpart; the copy is by value. */
qa_mount_identity qa_create_mount_identity(qa_mount_id id, qa_content_id content, int64_t generation);

/* TS ArchiveFormat = "pak" | "pk3" | "kpf" | "zip". */
typedef enum qa_archive_format {
  QA_ARCHIVE_FORMAT_PAK, QA_ARCHIVE_FORMAT_PK3,
  QA_ARCHIVE_FORMAT_KPF, QA_ARCHIVE_FORMAT_ZIP
} qa_archive_format;

typedef struct qa_archive_mount {
  /* TS kind is the literal "archive". */
  qa_mount_identity identity;
  qa_archive_format format;
  const char *archive_path;
  qa_content_digest archive_digest;
} qa_archive_mount;

typedef struct qa_loose_mount {
  /* TS kind is the literal "loose". */
  qa_mount_identity identity;
  const char *root_path;
} qa_loose_mount;

/* TS ContentMount = ArchiveMount | LooseMount. */
typedef enum qa_content_mount_kind {
  QA_CONTENT_MOUNT_ARCHIVE, QA_CONTENT_MOUNT_LOOSE
} qa_content_mount_kind;
typedef struct qa_content_mount {
  qa_content_mount_kind kind;
  union { qa_archive_mount archive; qa_loose_mount loose; } as;
} qa_content_mount;

/* TS ResourceProvenance union on kind. */
typedef struct qa_resource_provenance {
  qa_content_mount_kind kind;
  const qa_archive_mount *archive_mount; /* archive */
  const qa_loose_mount *loose_mount;     /* loose */
  const char *member_path;
  int64_t member_index; /* archive */
} qa_resource_provenance;

typedef struct qa_prefix_mount_order {
  const char *prefix;
  const qa_mount_id *mounts; /* highest priority first */
  size_t mount_count;
} qa_prefix_mount_order;

typedef struct qa_resolved_mount_plan {
  qa_mount_plan_id id;
  const qa_content_mount *mounts;
  size_t mount_count;
  const qa_mount_id *default_order;
  size_t default_order_count;
  const qa_prefix_mount_order *prefix_orders; /* first matching prefix wins */
  size_t prefix_order_count;
} qa_resolved_mount_plan;

/* TS ResourceResolution union on kind:
 * "default-order"|"prefix-order"|"link". */
typedef enum qa_resource_resolution_kind {
  QA_RESOURCE_RESOLUTION_DEFAULT_ORDER,
  QA_RESOURCE_RESOLUTION_PREFIX_ORDER,
  QA_RESOURCE_RESOLUTION_LINK
} qa_resource_resolution_kind;
typedef struct qa_resource_resolution {
  qa_resource_resolution_kind kind;
  qa_mount_plan_id plan;
  int64_t rank;              /* default/prefix-order */
  const char *prefix;        /* prefix-order */
  const char *source_prefix; /* link */
  const char *target_path;   /* link */
} qa_resource_resolution;

typedef struct qa_resolved_resource_reference {
  qa_resource_id id;
  const char *requested_path;
  qa_resource_provenance provenance;
  qa_content_digest digest;
  int64_t byte_length;
  qa_resource_resolution resolution;
  /* C-only: the JS key order save encodings preserve. False (the zero
   * default) is the mounts/index.ts order `{ ...resource, id }` with catalog
   * provenance and mount records; true is persistence/recipe.ts readResource
   * order `{ id, ...resource }` with readProvenance/readMount records. */
  bool parsed_order;
} qa_resolved_resource_reference;

/* TS createResourceId takes Omit<ResolvedResourceReference, "id">;
 * the C port takes the full struct and ignores its id field. */
char *qa_create_resource_id(const qa_resolved_resource_reference *resource);

/* Ownership of a ResolvedResourceReference value. TS references are plain
 * immutable values, so one kept past the mount set, reader or save tree it
 * came from must be an owned copy. An owned reference holds its own copy of
 * every string (id, requested path, digest, member path, resolution plan,
 * prefix, source prefix, target path) and one malloc'd provenance mount
 * record (archive or loose) whose identity id, content id and archive path,
 * archive digest or root path are owned too.
 * qa_resolved_resource_copy fills *out with such a copy of *source (out may
 * equal source); false only on OOM, leaving *out untouched.
 * qa_resolved_resource_release frees an owned reference (the result of
 * qa_resolved_resource_copy or qa_read_resource) and zeroes it; it must not
 * be called on a borrowed view such as a mount set's opened reference. */
bool qa_resolved_resource_copy(const qa_resolved_resource_reference *source,
                               qa_resolved_resource_reference *out);
void qa_resolved_resource_release(qa_resolved_resource_reference *resource);

typedef struct qa_provider_reference {
  qa_provider_id provider;
  qa_content_id content;
} qa_provider_reference;

typedef struct qa_resource_request {
  qa_content_id content;
  const char *path;
} qa_resource_request;

/* TS LaunchSelection<T> generic: {kind:"preset"} |
 * {kind:"selected", value:T}. The value pointer's type is the field's
 * TS type, documented at each use below. */
typedef struct qa_launch_selection {
  bool is_selected;
  const void *value;
} qa_launch_selection;

typedef struct qa_map_selection {
  qa_resource_request geometry;
  qa_provider_reference entities;
} qa_map_selection;

/* TS CampaignSelection: {kind:"none"} |
 * {kind:"campaign", mission, gamecode}. */
typedef struct qa_campaign_selection {
  bool is_campaign;
  qa_provider_reference mission;
  qa_provider_reference gamecode;
} qa_campaign_selection;

typedef struct qa_character_selection {
  qa_provider_reference definition;
  qa_provider_reference appearance;
} qa_character_selection;

typedef struct qa_monster_definition_reference {
  qa_provider_reference source;
  const char *classname;
} qa_monster_definition_reference;

/* TS MonsterSelectionTarget = reference | {kind:"map-defined"}. */
typedef struct qa_monster_selection_target {
  bool is_map_defined;
  qa_monster_definition_reference reference;
} qa_monster_selection_target;

typedef struct qa_monster_by_classname {
  const char *classname;
  qa_monster_selection_target target;
} qa_monster_by_classname;

/* TS EnemySelection union on kind: "map-defined"|"replace"|"players".
 * byClassname (TS Record) is a pointer+count pair list. */
typedef enum qa_enemy_selection_kind {
  QA_ENEMY_SELECTION_MAP_DEFINED, QA_ENEMY_SELECTION_REPLACE, QA_ENEMY_SELECTION_PLAYERS
} qa_enemy_selection_kind;
typedef struct qa_enemy_selection {
  qa_enemy_selection_kind kind;
  qa_monster_selection_target default_target; /* replace (TS `default`) */
  const qa_monster_by_classname *by_classname;
  size_t by_classname_count;
  qa_provider_reference character; /* players */
} qa_enemy_selection;

/* TS GrappleSelection: {kind:"disabled"} | enabled with
 * binding "slot"|"offhand" and mechanic "q1-threewave" (edition
 * "classic"|"rerelease") | "q2-ctf" (classic|rerelease) | "q2-lmctf"
 * (classic) | "q3-qvm" (classic, + profile). Mechanic/edition/binding
 * keep their exact TS strings. */
typedef struct qa_grapple_selection {
  bool is_enabled;
  qa_provider_reference source;
  const char *binding;
  const char *mechanic;
  const char *edition;
  const qa_qvm_grapple_definition *profile; /* q3-qvm only */
} qa_grapple_selection;

/* TS HandGrenadeSelection: disabled | enabled (binding "offhand",
 * edition "classic"|"rerelease"). */
typedef struct qa_hand_grenade_selection {
  bool is_enabled;
  qa_provider_reference source;
  const char *edition;
  double initial_ammo;
  double capacity;
} qa_hand_grenade_selection;

typedef struct qa_equipment_selection {
  qa_grapple_selection grapple;
  qa_hand_grenade_selection hand_grenades;
} qa_equipment_selection;

/* TS DopplerSelection: {kind:"source"|"disabled"}. */
typedef struct qa_doppler_selection { bool is_source; } qa_doppler_selection;

/* TS EnvironmentSelection union on kind:
 * "audio-content"|"disabled"|"selected". */
typedef enum qa_environment_selection_kind {
  QA_ENVIRONMENT_AUDIO_CONTENT, QA_ENVIRONMENT_DISABLED, QA_ENVIRONMENT_SELECTED
} qa_environment_selection_kind;
typedef struct qa_environment_selection {
  qa_environment_selection_kind kind;
  qa_resource_request resource;
} qa_environment_selection;

typedef struct qa_presentation_selection {
  qa_doppler_selection doppler;
  qa_environment_selection environment;
  qa_content_id assets;
  qa_provider_reference hud;
  qa_provider_reference effects;
  qa_provider_reference audio;
} qa_presentation_selection;

/* TS NativeModuleProfile = NativeAbi. */
typedef qa_native_abi qa_native_module_profile;

/* TS ExecutionModule<TArtifact> union on kind:
 * "typescript"|"quakec"|"qvm"|"native". The generic artifact is a
 * const void *: a qa_resource_request for qa_execution_selection and
 * a qa_resolved_resource_reference for qa_resolved_execution_module
 * (typescript carries implementation instead of artifact). Role/api
 * pairing follows the TS SourceModuleApi intersections. */
/* TS SourceModuleApi["role"] (the ModuleRole alias was dropped):
 * "server-game" | "client-game" | "ui" (exact strings). */
typedef const char *qa_module_role;
typedef enum qa_execution_module_kind {
  QA_EXECUTION_MODULE_TYPESCRIPT, QA_EXECUTION_MODULE_QUAKEC,
  QA_EXECUTION_MODULE_QVM, QA_EXECUTION_MODULE_NATIVE
} qa_execution_module_kind;
typedef struct qa_execution_module {
  qa_execution_module_kind kind;
  qa_provider_reference owner;
  qa_module_role role;
  qa_game_api_identity api;
  qa_provider_id implementation; /* typescript */
  const void *artifact;          /* quakec/qvm/native */
  qa_native_module_profile profile; /* native */
} qa_execution_module;
typedef qa_execution_module qa_execution_selection;
typedef qa_execution_module qa_resolved_execution_module;

typedef struct qa_launch_choice {
  qa_recipe_id preset;
  qa_launch_selection map;          /* value: const qa_map_selection * */
  qa_launch_selection campaign;     /* value: const qa_campaign_selection * */
  qa_launch_selection movement;     /* value: const qa_provider_reference * */
  qa_launch_selection character;    /* value: const qa_character_selection * */
  qa_launch_selection weapons;      /* value: const qa_provider_reference_list * */
  qa_launch_selection equipment;    /* value: const qa_equipment_selection * */
  qa_launch_selection enemies;      /* value: const qa_enemy_selection * */
  qa_launch_selection presentation; /* value: const qa_presentation_selection * */
  qa_launch_selection engine_behavior;
  qa_launch_selection combat;
  qa_launch_selection inventory;
  qa_launch_selection match;
  qa_launch_selection transition;
  qa_launch_selection execution;    /* value: const qa_execution_selection_list * */
} qa_launch_choice;

/* List shapes referenced by the generic launch selections. */
typedef struct qa_provider_reference_list {
  const qa_provider_reference *values;
  size_t count;
} qa_provider_reference_list;
typedef struct qa_execution_selection_list {
  const qa_execution_selection *values;
  size_t count;
} qa_execution_selection_list;

typedef struct qa_resolved_map {
  qa_content_id geometry_content;
  qa_resolved_resource_reference geometry;
  qa_provider_reference entities;
} qa_resolved_map;

typedef struct qa_provider_timing {
  qa_provider_id provider;
  qa_clock_profile clock;
  qa_numeric_profile numeric;
} qa_provider_timing;

/* TS ResolvedWeaponBehaviorSelection.component union on kind:
 * "qvm" (abiProfile + layout) | "rerelease-native" (declaration). */
typedef struct qa_resolved_weapon_behavior_selection {
  bool has_component;
  bool component_is_qvm;
  qa_qvm_abi_profile component_abi_profile;
  const qa_qvm_weapon_behavior_layout *component_layout;
  const qa_native_weapon_behavior_declaration *component_declaration;
  qa_provider_reference source;
  qa_resolved_resource_reference artifact;
  const qa_weapon_behavior_definition *definition;
} qa_resolved_weapon_behavior_selection;

typedef struct qa_executable_recipe {
  const qa_resolved_weapon_behavior_selection *weapon_behaviors; /* optional */
  size_t weapon_behavior_count;
  const qa_resolved_gameplay_mod *const *mods; /* optional (mods.h) */
  size_t mod_count;
  int64_t schema_version; /* TS literal 3 */
  qa_recipe_id id;
  qa_recipe_id preset;
  qa_resolved_map map;
  qa_campaign_selection campaign;
  qa_provider_reference movement;
  qa_character_selection character;
  qa_provider_reference_list weapons;
  qa_equipment_selection equipment;
  qa_enemy_selection enemies;
  qa_presentation_selection presentation;
  qa_provider_reference engine_behavior;
  qa_provider_reference combat;
  qa_provider_reference inventory;
  qa_provider_reference match;
  qa_provider_reference transition;
  const qa_resolved_execution_module *execution;
  size_t execution_count;
  qa_resolved_mount_plan mounts;
  const qa_resolved_resource_reference *resources;
  size_t resource_count;
  const qa_provider_timing *timing;
  size_t timing_count;
  qa_frame_ordering ordering;
  /* C-only: the JS key order the save encoder preserves. A recipe resolved by
   * resolveLaunch is `{ ...selected, schemaVersion, map, execution, timing,
   * ordering, mounts, resources }` (selected ends ...ordering, preset); a parsed
   * or literal recipe keeps the declared order (false). */
  bool launch_key_order;
} qa_executable_recipe;

#endif

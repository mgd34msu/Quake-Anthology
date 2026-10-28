/*
 * Port of src/contracts/qvm-mod-presentation.ts (types only).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QVM_MOD_PRESENTATION_H
#define QA_CONTRACTS_QVM_MOD_PRESENTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"

typedef struct qa_qvm_presentation_program {
  const char *path;
  qa_content_digest digest;
  qa_qvm_abi_profile abi_profile;
} qa_qvm_presentation_program;

/* TS QvmBodyPart = "body" | "lower" | "upper" | "head". */
typedef const char *qa_qvm_body_part;

/* TS QvmPresentationArgument: {kind:"int32"|"float32"|"address",
 * value:number} | {kind:"source", value:"player-state"|
 * "entity-state"|"centity"|"origin"|"snapshot"|"client-number"|
 * "time"|"event"|"parameter"|"snapshot-number"|
 * "server-command-sequence"} (exact source strings). */
typedef struct qa_qvm_presentation_argument {
  const char *kind;
  double number_value;
  const char *source_value;
} qa_qvm_presentation_argument;

/* when (optional) is the literal "weapon-presented". */
typedef struct qa_qvm_presentation_call {
  double entry;
  const char *when;
  const qa_qvm_presentation_argument *arguments;
  size_t argument_count;
} qa_qvm_presentation_call;

/* hud mode: "overlay" | "replace-status". */
typedef struct qa_qvm_presentation_hud {
  const char *mode;
  const qa_qvm_presentation_call *frame;
  size_t frame_count;
} qa_qvm_presentation_hud;

/* TS QvmPresentationBase (module-private in TS); version is 1. */
typedef struct qa_qvm_presentation_base {
  int64_t version;
  qa_qvm_presentation_program gameplay;
  qa_qvm_presentation_program cgame;
  const qa_qvm_presentation_call *initialize;
  size_t initialize_count;
  const qa_qvm_presentation_call *refresh;
  size_t refresh_count;
  const qa_qvm_presentation_call *frame;
  size_t frame_count;
  const qa_qvm_presentation_hud *hud; /* optional */
} qa_qvm_presentation_base;

/* snapshot kind is the literal "synthetic-player-event". */
typedef struct qa_qvm_player_event_snapshot_storage {
  double address;
  qa_double_view pointers;
} qa_qvm_player_event_snapshot_storage;
typedef struct qa_qvm_player_event_centities {
  double address, stride, capacity, state, origin;
} qa_qvm_player_event_centities;
typedef struct qa_qvm_player_event_storage {
  double game_state, player_state;
  qa_qvm_player_event_snapshot_storage snapshot;
  qa_qvm_player_event_centities centities;
  qa_double_view time, frame_time, view_origin;
  qa_double_view view_angles; /* optional: empty view when absent */
  qa_double_view view_axis;   /* optional */
} qa_qvm_player_event_storage;

/* runtime is the literal "qvm-player-events". */
typedef struct qa_qvm_player_event_presentation {
  qa_qvm_presentation_base base;
  const char *runtime;
  qa_qvm_player_event_storage storage;
  const qa_qvm_presentation_call *project;
  size_t project_count;
  qa_qvm_presentation_call event;
} qa_qvm_player_event_presentation;

typedef struct qa_qvm_scene_centities {
  double address, stride, capacity, state, previous_event, snapshot_time;
} qa_qvm_scene_centities;
typedef struct qa_qvm_scene_storage {
  double game_state, server_command_sequence;
  qa_double_view time, frame_time, view_origin;
  qa_double_view view_angles; /* optional */
  qa_double_view view_axis;   /* optional */
  qa_qvm_scene_centities centities;
} qa_qvm_scene_storage;

typedef struct qa_qvm_scene_entry_arg { double entry, centity_argument; } qa_qvm_scene_entry_arg;
typedef struct qa_qvm_body_part_call { double call; qa_qvm_body_part part; } qa_qvm_body_part_call;
typedef struct qa_qvm_scene_body_mesh {
  double entry, entity_argument, state_argument, shader_offset;
  const qa_qvm_body_part_call *parts; /* optional */
  size_t part_count;
} qa_qvm_scene_body_mesh;
typedef struct qa_qvm_scene_body {
  qa_qvm_scene_entry_arg player;
  qa_qvm_scene_body_mesh mesh;
} qa_qvm_scene_body;

typedef struct qa_qvm_presentation_cvar { const char *name; const char *value; } qa_qvm_presentation_cvar;

/* runtime is the literal "qvm-scene". */
typedef struct qa_qvm_scene_presentation {
  qa_qvm_presentation_base base;
  const char *runtime;
  const qa_qvm_presentation_cvar *cvars;
  size_t cvar_count;
  qa_qvm_scene_storage storage;
  const qa_qvm_presentation_call *snapshots;
  size_t snapshot_count;
  double event_entity_type;
  qa_qvm_scene_entry_arg event_check;
  qa_qvm_scene_body body;
} qa_qvm_scene_presentation;

/* TS QvmModPresentationDeclaration = player-events | scene. */
typedef struct qa_qvm_mod_presentation_declaration {
  bool is_scene;
  union {
    const qa_qvm_player_event_presentation *player_events;
    const qa_qvm_scene_presentation *scene;
  } as;
} qa_qvm_mod_presentation_declaration;

#endif

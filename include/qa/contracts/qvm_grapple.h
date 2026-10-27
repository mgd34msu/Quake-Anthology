/*
 * Port of src/contracts/qvm-grapple.ts (types only).
 * TS Readonly<Record<string,string>> initialCvars becomes a
 * name/value pair list. GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QVM_GRAPPLE_H
#define QA_CONTRACTS_QVM_GRAPPLE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"

typedef struct qa_qvm_grapple_fields {
  double inuse, client, parent, target;
  bool has_mover;
  double mover;
  double hook, health, takedamage, event_time, free_after_event;
} qa_qvm_grapple_fields;

typedef struct qa_qvm_grapple_globals {
  double time, frame, movement, forward, ground_plane;
} qa_qvm_grapple_globals;

typedef struct qa_qvm_grapple_callbacks {
  double allocate, free, fire, release, force_release, missile;
  bool has_follow;
  double follow;
  double think, pull;
  bool has_move_mover_hooks;
  double move_mover_hooks;
  double damage, same_team, player_move;
} qa_qvm_grapple_callbacks;

typedef struct qa_qvm_grapple_movement_word { double offset, value; } qa_qvm_grapple_movement_word;
typedef struct qa_qvm_grapple_movement {
  double byte_length;
  const qa_qvm_grapple_movement_word *words;
  size_t word_count;
} qa_qvm_grapple_movement;

typedef struct qa_qvm_grapple_cvar { const char *name; const char *value; } qa_qvm_grapple_cvar;

typedef struct qa_qvm_grapple_view_anchor {
  const char *path;
  const char *tag;
  qa_vec3 offset;
  double fov_offset_above, fov_offset_scale;
} qa_qvm_grapple_view_anchor;
typedef struct qa_qvm_grapple_view_attachment { const char *path; const char *tag; } qa_qvm_grapple_view_attachment;
/* cable: {kind:"shader",path,width} | {kind:"model",flight,pull,hold,segmentLength}. */
typedef struct qa_qvm_grapple_cable {
  bool is_shader;
  const char *path;
  double width;
  const char *flight, *pull, *hold;
  double segment_length;
} qa_qvm_grapple_cable;
typedef struct qa_qvm_grapple_presentation {
  const char *projectile_model, *view_model;
  double weapon_index;
  qa_qvm_grapple_view_anchor view_anchor;
  const qa_qvm_grapple_view_attachment *view_attachments;
  size_t view_attachment_count;
  qa_qvm_grapple_cable cable;
  const char *fire_sound;    /* nullable strings below */
  const char *attach_sound;
  const char *release_sound;
  const char *pull_sound;
  const char *hang_sound;
} qa_qvm_grapple_presentation;

struct qa_qvm_grapple_definition {
  const char *id;
  const char *title;
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  double entity_stride, client_stride;
  qa_qvm_grapple_fields fields;
  qa_qvm_grapple_globals globals;
  qa_qvm_grapple_callbacks callbacks;
  qa_double_view fire_arguments;
  qa_qvm_grapple_movement movement;
  const qa_qvm_grapple_cvar *initial_cvars;
  size_t initial_cvar_count;
  double event_lifetime_milliseconds;
  double grapple_damage_method;
  qa_qvm_grapple_presentation presentation;
  double pulling_flag;
};

#endif

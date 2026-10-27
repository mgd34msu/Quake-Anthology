/*
 * Port of src/contracts/source-match.ts: source match players,
 * objectives, and team/score field projections. Method interfaces
 * become function tables + ctx. Generic SourceObjectiveDeclaration
 * payloads (Scalar/Reference/Call) are opaque pointers whose concrete
 * types belong to the instantiating module.
 * TS throws plain Error here; the C port aborts with the identical
 * message (see source_match.c). GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_SOURCE_MATCH_H
#define QA_CONTRACTS_SOURCE_MATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"

typedef struct qa_source_team_value {
  double value;
  const char *team; /* nullable */
} qa_source_team_value;

/** TS SourceMatchPlayer. */
typedef struct qa_source_match_player {
  void *ctx;
  qa_provider_id owner;
  const char *(*team)(void *ctx); /* nullable result */
  double (*score)(void *ctx);
  void (*set_team)(void *ctx, const char *team);
  void (*set_score)(void *ctx, double score);
} qa_source_match_player;

typedef struct qa_source_objective_state {
  const char *stage;
  bool complete;
  const qa_actor_id *carrier; /* nullable */
  const qa_actor_id *target;  /* nullable */
} qa_source_objective_state;

/* TS change() request: Omit<SourceObjectiveState, "complete">. */
typedef struct qa_source_objective_change {
  const char *stage;
  const qa_actor_id *carrier;
  const qa_actor_id *target;
} qa_source_objective_change;

/** TS SourceObjectiveBinding. */
typedef struct qa_source_objective_binding {
  void *ctx;
  qa_provider_id owner;
  qa_objective_id id;
  bool campaign_gate;
  bool bot_goal;
  qa_source_objective_state (*read)(void *ctx);
  void (*change)(void *ctx, qa_source_objective_change request);
} qa_source_objective_binding;

typedef struct qa_source_match_player_result {
  bool has_value;
  qa_source_match_player value;
} qa_source_match_player_result;
typedef struct qa_source_objective_state_result {
  bool has_value;
  qa_source_objective_state value;
} qa_source_objective_state_result;
typedef struct qa_source_objective_listing {
  qa_objective_id id;
  qa_source_objective_state state;
  bool bot_goal;
} qa_source_objective_listing;

/* A TS `() => void` unbind closure: fn ptr + ctx. */
typedef struct qa_source_match_unbind {
  void *ctx;
  void (*invoke)(void *ctx);
} qa_source_match_unbind;

/** TS SourceMatchServices. */
typedef struct qa_source_match_services {
  void *ctx;
  qa_source_match_player_result (*player)(void *ctx, qa_actor_id actor);
  qa_source_match_unbind (*bind_source)(void *ctx, qa_provider_id owner,
      qa_source_match_player_result (*resolve)(void *resolve_ctx, qa_actor_id actor),
      void *resolve_ctx);
  qa_source_objective_state_result (*objective)(void *ctx, qa_objective_id id);
  qa_source_objective_state_result (*change_objective)(void *ctx, qa_objective_id id,
                                                       qa_source_objective_change request);
  qa_source_match_unbind (*bind_objective)(void *ctx, qa_source_objective_binding binding);
  const qa_source_objective_listing *(*objectives)(void *ctx, size_t *out_count);
  const qa_mission_gate *(*gates)(void *ctx, size_t *out_count);
} qa_source_match_services;

/* Runtime helpers (abort with the TS Error message when undeclared). */
const char *qa_source_team(const qa_source_team_value *values, size_t count, double value);
double qa_original_team(const qa_source_team_value *values, size_t count, const char *team);

/* TS SourceMatchField: {binding:"score"} | {binding:"team", values}. */
typedef struct qa_source_match_field {
  bool is_score;
  const qa_source_team_value *values;
  size_t value_count;
} qa_source_match_field;

void qa_validate_source_match_field(qa_source_match_field field);
/* NULL match = TS undefined (aborts, as TS throws). */
double qa_read_source_match_field(const qa_source_match_services *match,
                                  qa_actor_id actor, qa_source_match_field field);
void qa_write_source_match_field(const qa_source_match_services *match,
                                 qa_actor_id actor, qa_source_match_field field,
                                 double value);

typedef struct qa_source_team_alias {
  const char *source; /* nullable */
  const char *team;   /* nullable */
} qa_source_team_alias;

/* TS SourceTeamCommand extends SourceTeamAlias with arguments. */
typedef struct qa_source_team_command_entry {
  qa_source_team_alias alias;
  qa_string_view_list arguments;
} qa_source_team_command_entry;

typedef struct qa_source_primary_match {
  double score;
  const qa_source_team_command_entry *teams;
  size_t team_count;
} qa_source_primary_match;

/* Returns the declared command arguments for the team (aborts when
 * the source has none). */
qa_string_view_list qa_source_team_command(qa_source_primary_match profile,
                                           const char *team);
/* Validates an int32 score and returns it unchanged. */
double qa_source_score(double value);

typedef struct qa_source_objective_value {
  double value;
  const char *stage;
  bool complete;
} qa_source_objective_value;

/* TS SourceObjectiveDeclaration<Scalar, Reference, Call>: generic
 * storage/carrier/target/change payloads are opaque pointers.
 * role "owned" carries campaignGate/botGoal/change; role "borrowed"
 * carries writable. */
typedef struct qa_source_objective_declaration {
  qa_objective_id id;
  const void *state_storage; /* Scalar */
  const qa_source_objective_value *state_values;
  size_t state_value_count;
  const void *carrier; /* Reference | null */
  const void *target;  /* Reference | null */
  bool is_owned;
  bool campaign_gate;
  bool bot_goal;
  const void *change; /* Call | null */
  bool writable;
} qa_source_objective_declaration;

#endif

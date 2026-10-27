/* Port of src/compat/qc/mod-combat.ts. GPL-2.0-or-later.
 * Bridges the landed qa_q1qc_* mirror types (content/q1/quakec)
 * to the real compat program/machine via owned view storage. */
#ifndef QA_COMPAT_QC_MOD_COMBAT_H
#define QA_COMPAT_QC_MOD_COMBAT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/source_call.h"
#include "qa/content/q1/quakec/id1_damage.h"
#include "qa/contracts/execution.h"
#include "qa/world/session/mods.h"
void qa_compat_qc_validate_mod_combat(qa_compat_qc_program *program, qa_mod_qc_combat declaration);
size_t qa_compat_qc_damage_inputs(qa_damage_request request, double seconds, qa_compat_qc_mod_input_entry *out, size_t capacity);
typedef struct qa_compat_qc_mod_combat_options {
  qa_compat_qc_program *program; qa_module_identity module;
  qa_mod_qc_combat *declaration; qa_mod_host_services *services;
  qa_compat_qc_machine *machine; qa_source_actor_slots *slots; qa_source_actor_slots_options slots_options;
  void *ctx;
  qa_actor_id (*actor)(void *ctx, int64_t reference);
  int64_t (*reference)(void *ctx, const qa_actor_id *actor);
  double (*invoke)(void *ctx, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t input_count);
  /* Mirrored match player team access (TS services.match). */
  const char *(*player_team)(void *ctx, qa_actor_id actor);
  void (*player_set_team)(void *ctx, qa_actor_id actor, const char *team);
  bool (*slot_free)(void *ctx, int64_t slot);
} qa_compat_qc_mod_combat_options;
typedef struct qa_compat_qc_mod_combat {
  qa_compat_qc_mod_combat_options options;
  qa_q1_id1_damage_binding *damage;
  qa_q1qc_program qprogram; qa_q1qc_definition *qglobals, *qfields; qa_q1qc_function *qfunctions; qa_q1qc_statement *qstatements;
  qa_q1qc_entity_memory qentities; qa_q1qc_machine qmachine; qa_q1qc_source qsource;
  qa_damage_request *incoming; bool *entered; qa_damage_outcome *outcomes; bool *has_outcome;
  size_t incoming_count, incoming_capacity;
  struct qa_compat_qc_mod_combat *self_ref;
} qa_compat_qc_mod_combat;
bool qa_compat_qc_mod_combat_init(qa_compat_qc_mod_combat *c, qa_compat_qc_mod_combat_options options);
void qa_compat_qc_mod_combat_destroy(qa_compat_qc_mod_combat *c);
void qa_compat_qc_mod_combat_admit(qa_compat_qc_mod_combat *c, qa_owned_actor actor);
void qa_compat_qc_mod_combat_pain(qa_compat_qc_mod_combat *c, qa_pain_reaction reaction);
void qa_compat_qc_mod_combat_die(qa_compat_qc_mod_combat *c, qa_death_reaction reaction);
#endif

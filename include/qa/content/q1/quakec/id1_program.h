/* Port of src/content/q1/quakec/id1-program.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ID1_PROGRAM_H
#define QA_CONTENT_Q1_QUAKEC_ID1_PROGRAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/damage_call.h"

/* TS EnvironmentalSite. native/attacker NULL = TS absent. */
typedef struct qa_q1_environmental_site {
  int64_t caller; const char *name; int64_t statement;
  const char *hazard; const char *context;
  const char *native; const char *attacker;
} qa_q1_environmental_site;
typedef struct qa_q1_id1_statement {
  int64_t index; qa_q1qc_opcode opcode; int64_t a; int64_t b; int64_t c;
} qa_q1_id1_statement;
typedef struct qa_q1_id1_reaction { int64_t statement; const char *kind; } qa_q1_id1_reaction;
typedef struct qa_q1_id1_attacks {
  int64_t axe; int64_t shotgun; int64_t super_shotgun; int64_t add_multi; int64_t trace_attack;
  const int64_t *axe_damage; size_t axe_damage_count;
  int64_t apply_multi_damage[2];
} qa_q1_id1_attacks;
/* TS Id1ProgramBinding. damage_kind "sites" uses health_store/take/
 * death/pain/statements; "calls" uses reactions. call owned. */
typedef struct qa_q1_id1_program_binding {
  const char *kind; const char *digest; const char *attribution;
  const char *armor_field; double armor_masks[3];
  int64_t damage_index; int64_t damage_first_statement; int64_t damage_parameter_start;
  int64_t damage_local_words; int64_t damage_global;
  qa_q1qc_damage_call_layout *damage_call;
  const char *damage_kind;
  int64_t health_store; int64_t take; int64_t death[2]; int64_t pain[2];
  const qa_q1_id1_statement *statements; size_t statement_count;
  qa_q1_id1_reaction *reactions; size_t reaction_count;
  const qa_q1_id1_attacks *attacks;
  const qa_q1_environmental_site *environment; size_t environment_count;
} qa_q1_id1_program_binding;
/* declared_damage NULL = TS undefined. Returned bindings are cached
 * per program (TS WeakMap); do not destroy. */
const qa_q1_id1_program_binding *qa_q1_id1_program_binding_get(
    const qa_q1qc_program *program, const qa_mod_source_call *declared_damage);
const qa_q1_id1_program_binding *qa_q1_derive_native_program_binding(const qa_q1qc_program *program);
double qa_q1_id1_damage_multiplier(qa_q1qc_machine *machine, int64_t attacker, int64_t inflictor);

#endif

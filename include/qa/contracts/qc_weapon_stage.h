/*
 * Port of src/contracts/qc-weapon-stage.ts (types only).
 * The string | ModSourceCall union members are tagged pairs.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QC_WEAPON_STAGE_H
#define QA_CONTRACTS_QC_WEAPON_STAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/mod_callbacks.h"

typedef struct qa_qc_weapon_stage_repeat {
  const char *function;
  double entry, exit;
  double result_word;
  int result_value; /* TS 0 | 1 */
  const qa_qc_statement *statements;
  size_t statement_count;
} qa_qc_weapon_stage_repeat;

struct qa_qc_weapon_stage_declaration {
  const char *dispatcher;
  qa_string_view_list continuations;
  const qa_qc_weapon_stage_repeat *repeats;
  size_t repeat_count;
};

/* string | ModSourceCall */
typedef struct qa_qc_named_call {
  bool is_call;
  const char *function;      /* string form */
  qa_mod_source_call call;   /* call form */
} qa_qc_named_call;

/* objectives: {kind:"none"} | {kind:"call",function} | {kind:"call",call}. */
typedef struct qa_qc_primary_objectives {
  const char *kind;
  const char *function;
  qa_mod_source_call call;
} qa_qc_primary_objectives;

typedef struct qa_qc_primary_weapon_stage_declaration {
  qa_qc_weapon_stage_declaration stage;
  qa_qc_named_call client_spawn;
  qa_qc_named_call client_select_spawn;
  qa_qc_primary_objectives client_objectives;
} qa_qc_primary_weapon_stage_declaration;

#endif

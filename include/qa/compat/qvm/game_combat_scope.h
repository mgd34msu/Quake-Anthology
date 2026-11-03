/*
 * Port of src/compat/qvm/game-combat-scope.ts. Function calls use
 * qa_qvm_module_function_call (module.h).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_GAME_COMBAT_SCOPE_H
#define QA_COMPAT_QVM_GAME_COMBAT_SCOPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/identity.h"
#include "qa/world/gameplay/authority.h"

typedef struct qa_qvm_damage_reactions {
  double pain, die;
  qa_qvm_reaction_call pain_call, die_call;
} qa_qvm_damage_reactions;

typedef struct qa_qvm_damage_frame {
  qa_owned_actor actor;
  double pointer;
  qa_damage_request request;
  qa_qvm_module_function_call *call; /* borrowed */
  qa_qvm_cancellation_scope cancellation;
  bool cancelled;
  char cancelled_error[2048]; /* TS frame.cancelled.error: the thrown message */
  bool reacting;
  struct run_ctx *run;  /* the owning run while it is active */
} qa_qvm_damage_frame;

typedef struct qa_qvm_damage_scope_options {
  qa_qvm_game *game;
  double health, target_argument, points_stat;
  bool has_tier_stat;
  double tier_stat;
  const double *mode_words;
  size_t mode_word_count;
  qa_qvm_damage_reactions reactions;
  void *ctx;
  qa_armor_state (*armor)(void *ctx, double slot);
  bool (*live)(void *ctx, qa_owned_actor actor);
} qa_qvm_damage_scope_options;

typedef struct qa_qvm_damage_scopes qa_qvm_damage_scopes;

qa_qvm_damage_scopes *qa_qvm_damage_scopes_create(qa_qvm_damage_scope_options options);
void qa_qvm_damage_scopes_destroy(qa_qvm_damage_scopes *scopes);
/* Borrowed frame, NULL when none matches. */
qa_qvm_damage_frame *qa_qvm_damage_scopes_current(qa_qvm_damage_scopes *scopes, double pointer);
/* TS cancel(): never returns; the cancellation thrown here is caught by the
 * frame's run at its proceed, which then returns its result. */
void qa_qvm_damage_scopes_cancel(qa_qvm_damage_scopes *scopes, qa_qvm_damage_frame *frame,
                                 qa_qvm_module_function_call *call);
qa_source_damage_result qa_qvm_damage_scopes_run(qa_qvm_damage_scopes *scopes,
                                                 qa_qvm_module_function_call *call, qa_owned_actor actor,
                                                 double slot, qa_damage_request request,
                                                 qa_source_damage_observer observer);

#endif

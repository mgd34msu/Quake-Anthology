/*
 * Port of src/compat/qvm/game-combat-scope.ts. Function calls use
 * the module slice's qa_qvmy_function_call mirror.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_GAME_COMBAT_SCOPE_H
#define QA_COMPAT_QVM_GAME_COMBAT_SCOPE_H

#include <stdbool.h>
#include <stddef.h>

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
  qa_qvmy_function_call *call; /* borrowed */
  qa_qvm_cancellation_scope cancellation;
  bool cancelled;
  bool reacting;
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
/* TS cancel(): never returns (cancellation aborts through longjmp in
 * the interpreter; the frame records the attempt first). */
void qa_qvm_damage_scopes_cancel(qa_qvm_damage_scopes *scopes, qa_qvm_damage_frame *frame,
                                 qa_qvmy_function_call *call);
qa_source_damage_result qa_qvm_damage_scopes_run(qa_qvm_damage_scopes *scopes,
                                                 qa_qvmy_function_call *call, qa_owned_actor actor,
                                                 double slot, qa_damage_request request,
                                                 qa_source_damage_observer observer);

#endif

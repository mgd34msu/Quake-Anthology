/*
 * Port of src/contracts/qvm-mod-actor-frame.ts (types only).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QVM_MOD_ACTOR_FRAME_H
#define QA_CONTRACTS_QVM_MOD_ACTOR_FRAME_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/qvm_mod_callbacks.h"

typedef struct qa_qvm_mod_actor_frame_clock {
  double address, store, argument;
} qa_qvm_mod_actor_frame_clock;

typedef struct qa_qvm_mod_actor_frame_owned {
  double instruction, local_instruction;
} qa_qvm_mod_actor_frame_owned;

typedef struct qa_qvm_mod_actor_frame_end {
  double instruction;
  bool completed_taken;
} qa_qvm_mod_actor_frame_end;

struct qa_qvm_mod_actor_frame {
  qa_qvm_mod_source_call call;
  qa_qvm_mod_actor_frame_clock clock;
  const qa_qvm_mod_actor_frame_owned *owned;
  size_t owned_count;
  qa_qvm_mod_actor_frame_end end;
};

#endif

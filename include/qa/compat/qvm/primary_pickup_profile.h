/*
 * Port of src/compat/qvm/primary-pickup-profile.ts. The item layout
 * is the landed qa_qvm_item_layout and its parser
 * qa_qvm_parse_item_layout (TS owner src/compat/qvm/item-catalog.ts;
 * item_catalog.h); the profile/grant are the landed
 * qa_qvm_pickup_profile / qa_qvm_pickup_grant (TS owner
 * src/compat/qvm/game-pickups.ts; game_pickups.h). The grant
 * eligible closure becomes qa_qvm_primary_pickup_grant_eligible
 * over the declaration's branch overrides (qa_qvm_pickup_branches
 * ctx). Reader-owned arrays are freed by
 * qa_qvm_primary_pickup_profile_free.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_PICKUP_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_PICKUP_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/game_pickups.h"
#include "qa/compat/qvm/item_catalog.h"
#include "qa/compat/qvm/module.h"
#include "qa/persistence/value.h"

typedef struct qa_qvm_pickup_branch {
  int64_t instruction_index;
  bool taken;
} qa_qvm_pickup_branch;

/* eligible ctx: the TS branches list captured by the closure. */
typedef struct qa_qvm_pickup_branches {
  qa_qvm_pickup_branch *branches; /* owned */
  size_t branch_count;
} qa_qvm_pickup_branches;

/* TS grant eligible closure; ctx is qa_qvm_pickup_branches or NULL. */
bool qa_qvm_primary_pickup_grant_eligible(void *ctx, qa_qvm_pickup_eligibility context);

void qa_qvm_read_primary_pickup_profile(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                        qa_qvm_pickup_profile *out);
void qa_qvm_primary_pickup_profile_free(qa_qvm_pickup_profile *profile);

#endif

/* Port of src/content/q1/quakec/armor-points.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ARMOR_POINTS_H
#define QA_CONTENT_Q1_QUAKEC_ARMOR_POINTS_H

#include <stdbool.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/mod_callbacks.h"

/* TS qcEmptyArmor's returned closure, as data: has=false is the TS
 * undefined return. declared NULL = TS undefined. */
typedef struct qa_q1qc_empty_armor {
  bool has;
  qa_item_id item;
  double absorption; /* already Math.fround-staged */
} qa_q1qc_empty_armor;

qa_q1qc_empty_armor qa_q1qc_empty_armor_for(const qa_q1qc_program *program,
                                            const qa_mod_qc_empty_armor *declared);
/* TS returned closure: points -> {kind:"q1", item, absorption, points}. */
qa_regular_armor_state qa_q1qc_empty_armor_apply(qa_q1qc_empty_armor armor, double points);

#endif

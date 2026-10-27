/* Port of src/content/q1/quakec/weapon-stage-declaration.ts.
 * Returned strings borrow from the save tree; continuation/repeat/
 * statement arrays (and any nested source-call arrays) are malloc'd
 * and released by the matching destroy. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_WEAPON_STAGE_DECLARATION_H
#define QA_CONTENT_Q1_QUAKEC_WEAPON_STAGE_DECLARATION_H

#include "qa/contracts/qc_weapon_stage.h"
#include "qa/persistence/value.h"

qa_qc_weapon_stage_declaration qa_q1qc_read_weapon_stage_declaration(const qa_save_reader *reader);
qa_qc_primary_weapon_stage_declaration qa_q1qc_read_primary_weapon_stage(const qa_save_reader *reader);
void qa_q1qc_weapon_stage_declaration_destroy(qa_qc_weapon_stage_declaration *declaration);
void qa_q1qc_primary_weapon_stage_destroy(qa_qc_primary_weapon_stage_declaration *declaration);

#endif

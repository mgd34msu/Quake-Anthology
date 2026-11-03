/* Port of src/content/q1/missionpacks/monsters/tables/hipdecoy.ts. Generated mechanically from TS via bun JSON dump; see header. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/tables/hipdecoy.h"

static const qa_q1_monster_operation ops_0[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "hipdecoy:decoy_stand1"}
};
static const qa_q1_monster_operation ops_1[] = {
  {.kind = QA_Q1_OP_ACTION, .name = "hipdecoy:decoy_walk1"}
};
const qa_q1_monster_frame_entry qa_q1mpm_tables_hipdecoy_entries[] = {
  {"decoy_stand1", {17.0, "decoy_stand1", ops_0, 1}},
  {"decoy_walk1", {6.0, "decoy_walk1", ops_1, 1}}
};
const qa_q1_monster_frame_table qa_q1mpm_tables_hipdecoy_frames = {qa_q1mpm_tables_hipdecoy_entries, 2};

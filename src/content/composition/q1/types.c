/* Port of src/content/composition/q1/types.ts (types only; program names). GPL-2.0-or-later. */
#include "qa/content/composition/q1/types.h"

const char *qa_comp_q1_program_name(qa_comp_q1_program program) {
  static const char *const NAMES[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3", "ctf"};
  return NAMES[program];
}

/* Port of src/content/q2/missionpacks/monsters/tables/rogue-turret.ts (data dumped
 * from TS via bun JSON, converted mechanically). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TABLES_ROGUE_TURRET_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TABLES_ROGUE_TURRET_H
#include "qa/content/q2/foundation/monsters/types.h"
typedef struct qa_q2mpm_rogue_turret_frames {
  double stand01;
  double stand02;
  double active01;
  double active02;
  double active03;
  double active04;
  double active05;
  double active06;
  double run01;
  double run02;
  double pow01;
  double pow02;
  double pow03;
  double pow04;
  double death01;
  double death02;
} qa_q2mpm_rogue_turret_frames;
extern const qa_q2mpm_rogue_turret_frames qa_q2mpm_rogue_turret_frames_value;
extern const qa_q2m_move qa_q2mpm_rogue_turret_moves[];
extern const size_t qa_q2mpm_rogue_turret_move_count;
#endif

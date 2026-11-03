/* Port of src/content/q2/foundation/monsters/muzzle.ts (data dumped
 * from TS via bun JSON). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_MUZZLE_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_MUZZLE_H
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/contracts/math.h"
#include <stddef.h>
typedef struct qa_q2m_muzzle_ids {
  double INFANTRY_MACHINEGUN_1;
  double INFANTRY_MACHINEGUN_2;
  double INFANTRY_MACHINEGUN_3;
  double INFANTRY_MACHINEGUN_4;
  double INFANTRY_MACHINEGUN_5;
  double INFANTRY_MACHINEGUN_6;
  double INFANTRY_MACHINEGUN_7;
  double INFANTRY_MACHINEGUN_8;
  double INFANTRY_MACHINEGUN_9;
  double INFANTRY_MACHINEGUN_10;
  double INFANTRY_MACHINEGUN_11;
  double INFANTRY_MACHINEGUN_12;
  double INFANTRY_MACHINEGUN_13;
  double SOLDIER_BLASTER_1;
  double SOLDIER_BLASTER_2;
  double SOLDIER_SHOTGUN_1;
  double SOLDIER_SHOTGUN_2;
  double SOLDIER_MACHINEGUN_1;
  double SOLDIER_MACHINEGUN_2;
  double SOLDIER_BLASTER_3;
  double SOLDIER_SHOTGUN_3;
  double SOLDIER_MACHINEGUN_3;
  double SOLDIER_BLASTER_4;
  double SOLDIER_SHOTGUN_4;
  double SOLDIER_MACHINEGUN_4;
  double SOLDIER_BLASTER_5;
  double SOLDIER_SHOTGUN_5;
  double SOLDIER_MACHINEGUN_5;
  double SOLDIER_BLASTER_6;
  double SOLDIER_SHOTGUN_6;
  double SOLDIER_MACHINEGUN_6;
  double SOLDIER_BLASTER_7;
  double SOLDIER_SHOTGUN_7;
  double SOLDIER_MACHINEGUN_7;
  double SOLDIER_BLASTER_8;
  double SOLDIER_SHOTGUN_8;
  double SOLDIER_MACHINEGUN_8;
  double SOLDIER_RIPPER_1;
  double SOLDIER_RIPPER_2;
  double SOLDIER_RIPPER_3;
  double SOLDIER_RIPPER_4;
  double SOLDIER_RIPPER_5;
  double SOLDIER_RIPPER_6;
  double SOLDIER_RIPPER_7;
  double SOLDIER_RIPPER_8;
  double SOLDIER_HYPERGUN_1;
  double SOLDIER_HYPERGUN_2;
  double SOLDIER_HYPERGUN_3;
  double SOLDIER_HYPERGUN_4;
  double SOLDIER_HYPERGUN_5;
  double SOLDIER_HYPERGUN_6;
  double SOLDIER_HYPERGUN_7;
  double SOLDIER_HYPERGUN_8;
  double INFANTRY_MACHINEGUN_14;
  double INFANTRY_MACHINEGUN_15;
  double INFANTRY_MACHINEGUN_16;
  double INFANTRY_MACHINEGUN_17;
  double INFANTRY_MACHINEGUN_18;
  double INFANTRY_MACHINEGUN_19;
  double INFANTRY_MACHINEGUN_20;
  double INFANTRY_MACHINEGUN_21;
  double SOLDIER_BLASTER_9;
  double SOLDIER_SHOTGUN_9;
  double SOLDIER_MACHINEGUN_9;
  double SOLDIER_RIPPER_9;
  double SOLDIER_HYPERGUN_9;
  double INFANTRY_MACHINEGUN_22;
} qa_q2m_muzzle_ids;
extern const qa_q2m_muzzle_ids qa_q2m_muzzle_ids_value;
extern const qa_vec3 qa_q2m_muzzle_classic[];
extern const size_t qa_q2m_muzzle_classic_count;
extern const qa_vec3 qa_q2m_muzzle_rerelease[];
extern const size_t qa_q2m_muzzle_rerelease_count;
qa_vec3 qa_q2m_muzzle_offset(qa_q2_edition edition, double flash);
#endif

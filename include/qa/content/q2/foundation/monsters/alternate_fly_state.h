/* Port of src/content/q2/foundation/monsters/alternate-fly-state.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_ALTERNATE_FLY_STATE_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_ALTERNATE_FLY_STATE_H
#include <stdbool.h>
#include "qa/contracts/math.h"
typedef struct qa_q2m_pathing {
  qa_vec3 first_move_point, second_move_point;
  bool traversal_pending;
} qa_q2m_pathing;
typedef struct qa_q2m_alternate_fly_state {
  bool alternate_fly;
  double fly_min_distance, fly_max_distance, fly_acceleration, fly_speed;
  qa_vec3 fly_ideal_position;
  double fly_position_time;
  bool fly_buzzard, fly_above, fly_pinned, fly_thrusters;
  double fly_recovery_time;
  qa_vec3 fly_recovery_direction;
  bool hint_path;
  bool has_pathing;
  qa_q2m_pathing pathing;
} qa_q2m_alternate_fly_state;
qa_q2m_alternate_fly_state qa_q2m_create_alternate_fly_state(void);
#endif

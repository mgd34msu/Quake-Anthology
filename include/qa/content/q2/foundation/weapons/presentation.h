/* Port of src/content/q2/foundation/weapons/presentation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_PRESENTATION_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_PRESENTATION_H
#include "qa/content/q2/foundation/weapons/types.h"
typedef struct qa_q2w_recoil_result { qa_vec3 kick_origin, kick_angles; } qa_q2w_recoil_result;
typedef struct qa_q2w_frame_range { double first, last; } qa_q2w_frame_range;
typedef struct qa_q2w_animation_rate_input {
  bool quick_switch; double frame_seconds; qa_q2w_phase phase; double frame;
  double quad_fire_until; bool haste; double now;
  bool no_haste; /* TS noHaste?: false = undefined */
} qa_q2w_animation_rate_input;
typedef struct qa_q2w_powerup_sound_input { double quad_until, double_until; bool rerelease; double now; } qa_q2w_powerup_sound_input;
/* duration default: rerelease ? 0.2 : 0 (qa_q2w_set_recoil_default). */
void qa_q2w_set_recoil(qa_q2w_state *state, qa_q2_edition edition, double now, qa_vec3 origin, qa_vec3 angles, double duration);
void qa_q2w_set_recoil_default(qa_q2w_state *state, qa_q2_edition edition, double now, qa_vec3 origin, qa_vec3 angles);
qa_q2w_recoil_result qa_q2w_weapon_recoil(const qa_q2w_state *state, qa_q2_edition edition, double now);
qa_q2w_frame_range qa_q2w_attack_frames(bool ducked, double offset);
qa_q2w_frame_range qa_q2w_reverse_frames(bool ducked);
double qa_q2w_animation_rate(qa_q2w_animation_rate_input input);
const char *qa_q2w_powerup_sound(qa_q2w_powerup_sound_input input); /* NULL = null */
#endif

/* Port of src/app/bootstrap/simulation/quakec-character-animation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_character_animation.h"
#include <string.h>
#include "qa/content/q2/base/player/view.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_actor_animation_state qa_app_sim_quakec_character_animation(qa_actor_animation_state animation, qa_app_sim_quakec_character_reaction reaction, bool ducked) {
  qa_animation_state state = animation.state;
  if (!is(state.kind, "q2")) return animation;
  if (is(reaction, "alive")) {
    if (state.priority != 5) return animation;
    qa_actor_animation_state out = animation;
    out.state.frame = 0; out.state.end_frame = 39; out.state.priority = 0;
    out.state.duck = false; out.state.run = false;
    return out;
  }
  if (state.priority == 5 || (is(reaction, "pain") && state.priority >= 3)) return animation;
  double first, last;
  if (is(reaction, "attack")) { qa_q2w_frame_range f = qa_q2w_attack_frames(ducked, 1); first = f.first; last = f.last; }
  else if (is(reaction, "pain")) { qa_q2_base_frame_range f = qa_q2_pain_animation_frames(ducked, 1); first = f.first; last = f.last; }
  else { qa_q2_base_frame_range f = qa_q2_death_animation_frames(ducked, 1); first = f.first; last = f.last; }
  qa_actor_animation_state out = animation;
  out.state.frame = first; out.state.end_frame = last;
  out.state.priority = is(reaction, "death") ? 5 : is(reaction, "pain") ? 3 : 4;
  out.state.duck = ducked;
  return out;
}

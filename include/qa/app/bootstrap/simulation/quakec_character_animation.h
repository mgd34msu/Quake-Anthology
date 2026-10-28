/* Port of src/app/bootstrap/simulation/quakec-character-animation.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_CHARACTER_ANIMATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_CHARACTER_ANIMATION_H
#include <stdbool.h>
#include "qa/contracts/movement.h"
typedef const char *qa_app_sim_quakec_character_reaction; /* "attack"|"pain"|"death"|"alive" */
qa_actor_animation_state qa_app_sim_quakec_character_animation(qa_actor_animation_state animation, qa_app_sim_quakec_character_reaction reaction, bool ducked);
#endif

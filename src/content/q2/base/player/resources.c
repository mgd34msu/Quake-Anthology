/* Port of src/content/q2/base/player/resources.ts. SP_worldspawn
 * character precaches. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/resources.h"

const char *const qa_q2_character_sounds[] = {
    "*death1.wav", "*death2.wav", "*death3.wav", "*death4.wav",
    "*fall1.wav", "*fall2.wav", "*gurp1.wav", "*gurp2.wav", "*jump1.wav",
    "*pain25_1.wav", "*pain25_2.wav", "*pain50_1.wav", "*pain50_2.wav",
    "*pain75_1.wav", "*pain75_2.wav", "*pain100_1.wav", "*pain100_2.wav",
    "player/fry.wav", "player/lava1.wav", "player/lava2.wav", "player/lava_in.wav",
    "player/burn1.wav", "player/burn2.wav", "player/drown1.wav",
    "player/gasp1.wav", "player/gasp2.wav", "player/watr_in.wav", "player/watr_out.wav",
    "player/watr_un.wav", "player/u_breath1.wav", "player/u_breath2.wav",
    "player/land1.wav", "misc/udeath.wav", "misc/h2ohit1.wav", "world/land.wav",
    "player/step1.wav", "player/step2.wav", "player/step3.wav", "player/step4.wav",
};
const size_t qa_q2_character_sound_count = sizeof qa_q2_character_sounds / sizeof qa_q2_character_sounds[0];

const char *const qa_q2_character_models[] = {
    "models/objects/gibs/sm_meat/tris.md2", "models/objects/gibs/arm/tris.md2",
    "models/objects/gibs/bone/tris.md2", "models/objects/gibs/bone2/tris.md2",
    "models/objects/gibs/chest/tris.md2", "models/objects/gibs/skull/tris.md2",
    "models/objects/gibs/head2/tris.md2",
};
const size_t qa_q2_character_model_count = sizeof qa_q2_character_models / sizeof qa_q2_character_models[0];

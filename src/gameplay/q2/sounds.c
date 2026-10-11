#include "qa/q2_sound.h"
static const char *const sounds[] = {
#define QA_Q2_SOUND_PATH(name) QA_Q2_SOUND_##name,
    QA_Q2_SOUND_LIST(QA_Q2_SOUND_PATH)
#undef QA_Q2_SOUND_PATH
};
size_t qa_q2_sound_count(void) { return sizeof(sounds)/sizeof(sounds[0]); }
const char *qa_q2_sound_path(size_t index)
{ return index < qa_q2_sound_count() ? sounds[index] : NULL; }
const qa_q2_sound_index qa_q2_tank_attack_sounds[5] = {
    QA_Q2_SFX_TANK_TNKATK2A,
    QA_Q2_SFX_TANK_TNKATK2B,
    QA_Q2_SFX_TANK_TNKATK2C,
    QA_Q2_SFX_TANK_TNKATK2D,
    QA_Q2_SFX_TANK_TNKATK2E,
};
const qa_q2_sound_index qa_q2_machinegun_sounds[5] = {
    QA_Q2_SFX_WEAPONS_MACHGF1B,
    QA_Q2_SFX_WEAPONS_MACHGF2B,
    QA_Q2_SFX_WEAPONS_MACHGF3B,
    QA_Q2_SFX_WEAPONS_MACHGF4B,
    QA_Q2_SFX_WEAPONS_MACHGF5B,
};

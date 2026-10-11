#include "internal.h"
static const char *const sounds[Q2_NAME_COUNT] = {
    NULL,
#define QA_Q2_SOUND_PATH(name) QA_Q2_SOUND_##name,
    QA_Q2_SOUND_LIST(QA_Q2_SOUND_PATH)
#undef QA_Q2_SOUND_PATH
#define Q2_NAME_PATH(key, text) text,
    Q2_RUNTIME_NAME_LIST(Q2_NAME_PATH)
#undef Q2_NAME_PATH
};
size_t qa_q2_sound_count(void) { return QA_Q2_SFX_COUNT-1; }
const char *qa_q2_sound_path(size_t index)
{ return index < qa_q2_sound_count() ? sounds[index+1] : NULL; }
bool qa_q2_sound_names_bind(qa_strings *strings, const qa_string_id **out, qa_error *error)
{ return qa_strings_bind_names(strings,sounds,Q2_NAME_COUNT,out,error); }
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

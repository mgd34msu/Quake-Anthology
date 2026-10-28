/* Port of src/app/bootstrap/audio/menu.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_MENU_H
#define QA_APP_BOOTSTRAP_AUDIO_MENU_H
#include "qa/contracts/content.h"
/* TS menuSoundPath(family, event: UiSound). Returns a static
 * "misc/menu<N>.wav" string (N in 1..4); do not free. */
const char *qa_app_menu_sound_path(qa_game_family family, const char *event);
#endif

/* Port of src/app/bootstrap/audio/menu.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio/menu.h"
#include <string.h>
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
const char *qa_app_menu_sound_path(qa_game_family family, const char *event) {
  int index;
  if (family == QA_GAME_FAMILY_Q1)
    index = is(event, "open") ? 2 : is(event, "move") ? 1 : 3;
  else
    index = is(event, "open") ? 1 : is(event, "close") ? 3
      : is(event, "reject") && family == QA_GAME_FAMILY_Q3 ? 4 : 2;
  switch (index) {
    case 1: return "misc/menu1.wav";
    case 2: return "misc/menu2.wav";
    case 3: return "misc/menu3.wav";
    default: return "misc/menu4.wav";
  }
}

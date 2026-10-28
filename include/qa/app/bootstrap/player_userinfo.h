/* Port of src/app/bootstrap/player-userinfo.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PLAYER_USERINFO_H
#define QA_APP_BOOTSTRAP_PLAYER_USERINFO_H
#include <stdbool.h>
#include "qa/core/cvars/index.h"
/* model NULL selects the TS default "male". index is a TS number. */
void qa_app_register_player_userinfo(qa_cvar_registry *cvars, double index, const char *model);
/* Malloc'd info string; caller frees. */
char *qa_app_player_userinfo(qa_cvar_registry *cvars);
/* TS CharacterAppearance. Strings borrowed; skin NULL = absent. */
typedef struct qa_app_character_appearance {
  const char *character;
  const char *character_model;
  const char *character_skin; /* NULL = absent */
  bool has_top_color; double top_color;
  bool has_bottom_color; double bottom_color;
} qa_app_character_appearance;
void qa_app_apply_character_appearance(qa_cvar_registry *cvars, qa_app_character_appearance options);
#endif

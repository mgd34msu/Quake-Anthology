/* Port of src/app/bootstrap/match-modes.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MATCH_MODES_H
#define QA_APP_BOOTSTRAP_MATCH_MODES_H
#include <stddef.h>
#include "qa/contracts/content.h"
/* TS MatchRules exact strings: "standard"|"ctf"|"lmctf"|"tag"|"deathball"|"horde". */
typedef const char *qa_app_match_rules;
typedef struct qa_app_match_mode_selection {
  qa_game_family family;
  const char *edition;   /* borrowed */
  const char *campaign;  /* borrowed */
  const char *mode;      /* "singleplayer"|"coop"|"deathmatch" */
  qa_app_match_rules rules;
} qa_app_match_mode_selection;
/* Borrowed static string or NULL (TS null). */
const char *qa_app_match_mode_unavailable(qa_app_match_mode_selection selection);
/* For the "missing: a, b" forms the result is written into scratch
 * (borrowed until next call with same scratch); NULL when available. */
const char *qa_app_match_map_unavailable(qa_app_match_mode_selection selection,
    const char *const *classnames, size_t classname_count, char *scratch, size_t scratch_size);
#endif

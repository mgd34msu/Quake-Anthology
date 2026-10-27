/* Port of src/settings/server/lmctf-cvars.ts. GPL-2.0-or-later. */
#include "qa/settings/server/lmctf_cvars.h"

#include <stdlib.h>

typedef struct lmctf_definition {
  const char *name;
  const char *value;
  int flags;
} lmctf_definition;

/* The TS numeric, boolean and string definition tables, in order. */
static const lmctf_definition definitions[] = {
    {"ctfflags", "0", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"refset", "0", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"runes", "15", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"skinset", "0", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"disabled_weps", "0", 0},
    {"timelimit", "0", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"fraglimit", "0", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"countdown_time", "15", 0},
    {"flag_init", "0", 0},
    {"fastswitch", "0", 0},
    {"autolock", "0", 0},
    {"refpassword", "", 0},
    {"rcon_password", "", 0},
};

const char *const qa_lmctf_console_names[] = {
    "ctfflags", "refset", "runes", "skinset", "disabled_weps", "timelimit",
    "fraglimit", "countdown_time", "flag_init", "fastswitch", "autolock",
    "refpassword", "rcon_password", "maplist_file"};
const size_t qa_lmctf_console_name_count =
    sizeof qa_lmctf_console_names / sizeof *qa_lmctf_console_names;

void qa_register_lmctf_console_cvars(qa_cvar_registry *cvars) {
  for (size_t i = 0; i < sizeof definitions / sizeof *definitions; i++) {
    qa_cvar_snapshot snapshot;
    (void)qa_cvar_registry_register(cvars, definitions[i].name,
                                    definitions[i].value, definitions[i].flags,
                                    &snapshot);
    qa_cvar_read_free(&snapshot);
  }
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_register(cvars, "maplist_file", "maplist.txt", 0, &snapshot);
  qa_cvar_read_free(&snapshot);
}

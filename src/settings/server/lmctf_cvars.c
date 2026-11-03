/* Port of src/settings/server/lmctf-cvars.ts. GPL-2.0-or-later. */
#include "qa/settings/server/lmctf_cvars.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* TS String(number): the shortest decimal that round-trips. */
static void number_text(double value, char *out, size_t size) {
  qa_net_js_number(value, out, size);
}

static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_set(cvars, name, value, false, &snapshot);
  qa_cvar_read_free(&snapshot);
}

/* TS registerValue. */
static void register_value(qa_cvar_registry *cvars, const char *name, const char *initial) {
  const lmctf_definition *definition = NULL;
  for (size_t i = 0; i < sizeof definitions / sizeof *definitions; i++)
    if (strcmp(definitions[i].name, name) == 0) definition = &definitions[i];
  if (definition == NULL) qa_range_error("Unknown LMCTF console rule: %s", name);
  qa_cvar_read found;
  const bool present = qa_cvar_registry_find(cvars, name, &found);
  if (present) qa_cvar_read_free(&found);
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_register(cvars, name, definition->value, definition->flags, &snapshot);
  qa_cvar_read_free(&snapshot);
  if (!present && strcmp(initial, definition->value) != 0) set_cvar(cvars, name, initial);
}

static void register_number(qa_cvar_registry *cvars, const char *name, double value) {
  char text[40];
  number_text(value, text, sizeof text);
  register_value(cvars, name, text);
}

static char *dup_text(const char *text) {
  const size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}

void qa_lmctf_rules_pull_cvars(qa_cvar_registry *cvars, qa_lmctf_rules *rules) {
  rules->ctf_flags = qa_cvar_registry_variable_value(cvars, "ctfflags");
  rules->ref_flags = qa_cvar_registry_variable_value(cvars, "refset");
  rules->runes = qa_cvar_registry_variable_value(cvars, "runes");
  rules->skin_set = qa_cvar_registry_variable_value(cvars, "skinset");
  rules->disabled_weapons = qa_cvar_registry_variable_value(cvars, "disabled_weps");
  rules->time_limit_minutes = qa_cvar_registry_variable_value(cvars, "timelimit");
  rules->frag_limit = qa_cvar_registry_variable_value(cvars, "fraglimit");
  rules->countdown_seconds = qa_cvar_registry_variable_value(cvars, "countdown_time");
  rules->flag_init = qa_cvar_registry_variable_value(cvars, "flag_init") != 0;
  rules->fast_switch = qa_cvar_registry_variable_value(cvars, "fastswitch") != 0;
  rules->auto_lock = trunc(qa_cvar_registry_variable_value(cvars, "autolock")) != 0;
  char *ref = qa_cvar_registry_variable_string(cvars, "refpassword");
  char *rcon = qa_cvar_registry_variable_string(cvars, "rcon_password");
  free(rules->ref_password);
  free(rules->rcon_password);
  rules->ref_password = ref;
  rules->rcon_password = rcon;
}

void qa_bind_lmctf_console_rules(qa_cvar_registry *cvars, qa_lmctf_rules *rules) {
  register_number(cvars, "ctfflags", rules->ctf_flags);
  register_number(cvars, "refset", rules->ref_flags);
  register_number(cvars, "runes", rules->runes);
  register_number(cvars, "skinset", rules->skin_set);
  register_number(cvars, "disabled_weps", rules->disabled_weapons);
  register_number(cvars, "timelimit", rules->time_limit_minutes);
  register_number(cvars, "fraglimit", rules->frag_limit);
  register_number(cvars, "countdown_time", rules->countdown_seconds);
  register_value(cvars, "flag_init", rules->flag_init ? "1" : "0");
  register_value(cvars, "fastswitch", rules->fast_switch ? "1" : "0");
  register_value(cvars, "autolock", rules->auto_lock ? "1" : "0");
  register_value(cvars, "refpassword", rules->ref_password != NULL ? rules->ref_password : "");
  register_value(cvars, "rcon_password", rules->rcon_password != NULL ? rules->rcon_password : "");
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_register(cvars, "maplist_file", "maplist.txt", 0, &snapshot);
  qa_cvar_read_free(&snapshot);
  qa_lmctf_rules_pull_cvars(cvars, rules);
}

void qa_lmctf_rules_set_number(qa_cvar_registry *cvars, qa_lmctf_rules *rules, const char *name, double value) {
  char text[40];
  number_text(value, text, sizeof text);
  set_cvar(cvars, name, text);
  qa_lmctf_rules_pull_cvars(cvars, rules);
}

void qa_lmctf_rules_set_string(qa_cvar_registry *cvars, qa_lmctf_rules *rules, const char *name, const char *value) {
  char *held = dup_text(value); /* value may alias a rules string freed by the pull */
  set_cvar(cvars, name, held);
  free(held);
  qa_lmctf_rules_pull_cvars(cvars, rules);
}

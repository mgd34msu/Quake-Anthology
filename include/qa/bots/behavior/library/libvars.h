/* Port of src/bots/behavior/library/libvars.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_LIBVARS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_LIBVARS_H
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_bot_lib_var {
  char *name;
  char *string;
  int32_t flags;
  bool modified;
  float value;
  struct qa_bot_lib_var *next;
} qa_bot_lib_var;
typedef struct qa_bot_lib_vars {
  qa_bot_lib_var *head;
} qa_bot_lib_vars;
float qa_lib_var_string_value(const char *text);
void qa_bot_lib_vars_init(qa_bot_lib_vars *v);
void qa_bot_lib_vars_destroy(qa_bot_lib_vars *v);
qa_bot_lib_var *qa_bot_lib_vars_get(qa_bot_lib_vars *v, const char *name);
const char *qa_bot_lib_vars_get_string(qa_bot_lib_vars *v, const char *name);
float qa_bot_lib_vars_get_value(qa_bot_lib_vars *v, const char *name);
qa_bot_lib_var *qa_bot_lib_vars_get_or_create(qa_bot_lib_vars *v, const char *name,
                                              const char *def);
const char *qa_bot_lib_vars_string(qa_bot_lib_vars *v, const char *name, const char *def);
float qa_bot_lib_vars_value(qa_bot_lib_vars *v, const char *name, const char *def);
void qa_bot_lib_vars_set(qa_bot_lib_vars *v, const char *name, const char *value);
bool qa_bot_lib_vars_changed(qa_bot_lib_vars *v, const char *name);
void qa_bot_lib_vars_set_not_modified(qa_bot_lib_vars *v, const char *name);
void qa_bot_lib_vars_clear(qa_bot_lib_vars *v);
#endif

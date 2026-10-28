/* Port of src/app/bootstrap/startup-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_COMMANDS_H
#define QA_APP_BOOTSTRAP_STARTUP_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
typedef struct qa_app_startup_command { char *text; size_t end; } qa_app_startup_command;
typedef struct qa_app_startup_variable { char *name; char *value; } qa_app_startup_variable;
typedef struct qa_app_startup_phases {
  char *early; char *late; char *stuffed; bool safe;
  qa_app_startup_variable *variables; size_t variable_count;
} qa_app_startup_phases;
qa_app_startup_command qa_app_read_startup_command(const char *const *argv, size_t argc, size_t index);
bool qa_app_startup_requests_world(const char *const *lines, size_t count);
qa_app_startup_phases qa_app_startup_command_phases(const char *const *lines, size_t count, qa_command_dialect dialect);
void qa_app_startup_phases_free(qa_app_startup_phases *p);
#endif

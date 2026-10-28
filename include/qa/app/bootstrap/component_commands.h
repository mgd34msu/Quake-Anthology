/* Port of src/app/bootstrap/component-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_COMMANDS_H
#define QA_APP_BOOTSTRAP_COMPONENT_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/contracts/common.h"
/* TS ReadonlySet<string>: membership helper over the built set. */
bool qa_app_component_engine_command(qa_command_dialect dialect, const qa_app_loaded_content *content, const char *name);
/* Malloc'd lowercased unique names; free array + strings. */
char **qa_app_component_engine_commands(qa_command_dialect dialect, const qa_app_loaded_content *content, size_t *out_count);
#endif

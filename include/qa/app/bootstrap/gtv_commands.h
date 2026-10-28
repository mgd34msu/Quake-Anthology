/* Port of src/app/bootstrap/gtv-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_GTV_COMMANDS_H
#define QA_APP_BOOTSTRAP_GTV_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/core/cvars/index.h"
/* Borrowed strings (args/defaults); label NULL = TS null. */
typedef struct qa_app_gtv_connect_request {
  const char *address, *username, *password, *label;
} qa_app_gtv_connect_request;
typedef struct qa_app_gtv_current { double id; const char *label; } qa_app_gtv_current;
void qa_app_register_gtv_cvars(qa_cvar_registry *cvars);
/* False = TS null (-h/--help). Aborts with exact TS Errors. */
bool qa_app_parse_gtv_connect(const char *const *args, size_t arg_count, const char *default_username,
                              const char *default_password, qa_app_gtv_connect_request *out);
/* current NULL mirrors TS null. */
bool qa_app_matches_gtv_disconnect(const char *const *args, size_t arg_count, const qa_app_gtv_current *current);
#endif

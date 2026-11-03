/* Port of src/app/bootstrap/gtv-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/gtv_commands.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <string.h>
#include "qa/foundation/support.h"

static bool reg_cvar(qa_cvar_registry *r, const char *name, const char *value, double flags, void *unused) {
  (void)unused;
  qa_cvar_snapshot snapshot;
  bool ok = qa_cvar_registry_register(r, name, value, flags, &snapshot);
  qa_cvar_read_free(&snapshot);
  return ok;
}

void qa_app_register_gtv_cvars(qa_cvar_registry *cvars) {
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  if (dialect != QA_COMMAND_DIALECT_Q2_CLASSIC && dialect != QA_COMMAND_DIALECT_Q2_RERELEASE) return;
  reg_cvar(cvars, "mvd_username", "unnamed", 0, NULL);
  reg_cvar(cvars, "mvd_password", "", QA_Q2_CVAR_FLAG_PRIVATE, NULL);
}
bool qa_app_parse_gtv_connect(const char *const *args, size_t arg_count, const char *default_username,
                              const char *default_password, qa_app_gtv_connect_request *out) {
  const char *username = default_username, *password = default_password, *label = NULL;
  size_t index = 0;
  for (; index < arg_count; index++) {
    const char *option = args[index];
    if (option && strcmp(option, "--") == 0) { index++; break; }
    if (!option || option[0] != '-') break;
    if (strcmp(option, "-h") == 0 || strcmp(option, "--help") == 0) return false;
    bool is_user = strcmp(option, "-u") == 0 || strcmp(option, "--user") == 0;
    bool is_pass = strcmp(option, "-p") == 0 || strcmp(option, "--pass") == 0;
    bool is_name = strcmp(option, "-n") == 0 || strcmp(option, "--name") == 0;
    if (!is_user && !is_pass && !is_name) qa_range_error("Unknown mvdconnect option: %s", option);
    const char *value = ++index < arg_count ? args[index] : NULL;
    if (!value) qa_range_error("Missing value for %s", option);
    /* TS: no NUL (impossible in C strings) and single-byte text; UTF-8
     * lead/continuation bytes above 127 are bytes here, matching the
     * byte-string reading for ASCII input. */
    if (is_user) username = value;
    else if (is_pass) password = value;
    else label = value;
  }
  const char *address = index < arg_count ? args[index] : NULL;
  if (!address || address[0] == '\0' || index + 1 != arg_count)
    qa_range_error("Usage: mvdconnect [-u user] [-p password] [-n name] <address[:port]>");
  *out = (qa_app_gtv_connect_request){address, username, password, label};
  return true;
}
bool qa_app_matches_gtv_disconnect(const char *const *args, size_t arg_count, const qa_app_gtv_current *current) {
  if (arg_count > 1) qa_range_error("Usage: mvdisconnect [-a|--all] [conn_id]");
  const char *selected = arg_count > 0 ? args[0] : NULL;
  if (selected && (strcmp(selected, "-h") == 0 || strcmp(selected, "--help") == 0)) return false;
  if (!current) qa_range_error("No GTV connections.");
  if (!selected || strcmp(selected, "-a") == 0 || strcmp(selected, "--all") == 0) return true;
  char id[64]; snprintf(id, sizeof id, "%s", QA_JS_NUMBER(current->id));
  if (strcmp(selected, id) == 0 || (current->label && strcmp(selected, current->label) == 0)) return true;
  qa_range_error("No such connection ID: %s", selected);
}

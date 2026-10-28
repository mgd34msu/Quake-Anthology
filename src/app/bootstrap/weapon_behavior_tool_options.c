/* Port of src/app/bootstrap/weapon-behavior-tool-options.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/weapon_behavior_tool_options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/content/mounts/paths.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
const char qa_app_weapon_behavior_tool_help[] =
    "Usage:\n"
    "  quake-typescript weapon-behavior inspect PRODUCT [options]\n"
    "  quake-typescript weapon-behavior declare-qvm PRODUCT --profile MOUNTED_PROFILE_JSON [options]\n"
    "  quake-typescript weapon-behavior declare-native PRODUCT --profile MOUNTED_PROFILE_JSON [options]\n"
    "  quake-typescript weapon-behavior declare PRODUCT --id NAMESPACE:ID --role ROLE --fire CALLBACK [options]\n"
    "\n"
    "  --content PATH        Installed content root\n"
    "  --user-content PATH   Writable user content root\n"
    "  --artifact PATH       Mounted program (QC descriptor/progs.dat/qwprogs.dat, vm/qagame.qvm, or native game DLL)\n"
    "  --profile PATH        Author-written mounted QVM or API2023 Windows x64 native profile with exact digest, entries and entity layout\n"
    "  --title TEXT          Display title (defaults to declaration ID)\n"
    "  --activate CALLBACK   Optional source activation callback\n"
    "  --role ROLE           rocket, grenade, nail, bolt, plasma, energy, grapple\n"
    "  --help                Show this help\n"
    "\n"
    "Inspection reports actual bytecode callbacks and think assignments. It does not infer\n"
    "projectile roles or activation gates. Declare only callbacks whose behavior you have\n"
    "established from the source. Declarations bind the exact current program digest.\n";
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static char *resolve_path(const char *path) {
  if (path[0] == '/') return xdup(path);
  char cwd[4096];
  if (getcwd(cwd, sizeof cwd) == NULL) return xdup(path);
  size_t n = strlen(cwd) + 1 + strlen(path) + 1;
  char *r = malloc(n);
  if (!r) qa_range_error("Out of memory");
  snprintf(r, n, "%s/%s", cwd, path);
  return r;
}
void qa_app_weapon_behavior_tool_command_free(qa_app_weapon_behavior_tool_command *c) {
  free(c->product); free(c->corpus_root); free(c->user_content_root); free(c->artifact);
  free(c->profile); free(c->id); free(c->title); free(c->fire); free(c->activate);
  memset(c, 0, sizeof *c);
}
static bool behavior_id_ok(const char *v) {
  /* /^[^:\s]+:[^\s]+$/ */
  const char *colon = strchr(v, ':');
  if (colon == NULL || colon == v || colon[1] == '\0') return false;
  for (const char *p = v; *p; p++) if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') return false;
  return true;
}
static const char *role_or_abort(const char *v) {
  static const char *const roles[] = {"rocket", "grenade", "nail", "bolt", "plasma", "energy", "grapple"};
  for (int i = 0; i < 7; i++) if (strcmp(v, roles[i]) == 0) return roles[i];
  qa_range_error("Invalid projectile role: %s", v);
}
qa_app_weapon_behavior_tool_command qa_app_parse_weapon_behavior_tool(const char *const *argv, size_t argc) {
  qa_app_weapon_behavior_tool_command out; memset(&out, 0, sizeof out);
  for (size_t i = 0; i < argc; i++)
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { out.action = "help"; return out; }
  const char *action = argc > 0 ? argv[0] : NULL;
  const char *product = argc > 1 ? argv[1] : NULL;
  bool action_ok = action && (strcmp(action, "inspect") == 0 || strcmp(action, "declare") == 0 ||
                               strcmp(action, "declare-qvm") == 0 || strcmp(action, "declare-native") == 0);
  if (!action_ok || product == NULL || strncmp(product, "--", 2) == 0)
    qa_range_error("%s", qa_app_weapon_behavior_tool_help);
  static const char *const keys[] = {"--content", "--user-content", "--artifact", "--id", "--title", "--role", "--fire", "--activate", "--profile"};
  const char *flags[9]; memset(flags, 0, sizeof flags);
  for (size_t index = 2; index < argc; index += 2) {
    const char *key = argv[index];
    const char *value = index + 1 < argc ? argv[index + 1] : NULL;
    int at = -1;
    for (int i = 0; i < 9; i++) if (key && strcmp(key, keys[i]) == 0) at = i;
    if (key == NULL || at < 0) qa_range_error("Unknown behavior option: %s", key ? key : "undefined");
    if (value == NULL || strncmp(value, "--", 2) == 0 || value[0] == '\0')
      qa_range_error("Missing or invalid value for %s", key);
    if (flags[at] != NULL) qa_range_error("Repeated behavior option: %s", key);
    flags[at] = value;
  }
  out.action = action;
  out.product = xdup(product);
  if (flags[0]) out.corpus_root = resolve_path(flags[0]);
  else {
    qa_content_root_search search; memset(&search, 0, sizeof search);
    out.corpus_root = qa_resolve_default_content_root(search, false);
    if (!out.corpus_root) qa_range_error("Out of memory");
  }
  if (flags[1]) out.user_content_root = resolve_path(flags[1]);
  else {
    char *def = qa_default_user_content_root();
    if (!def) qa_range_error("Out of memory");
    out.user_content_root = resolve_path(def);
    free(def);
  }
  if (flags[2]) { out.artifact = qa_normalize_resource_path(flags[2]); if (!out.artifact) qa_range_error("Out of memory"); }
  bool is_qvm_native = strcmp(action, "declare-qvm") == 0 || strcmp(action, "declare-native") == 0;
  if (is_qvm_native) {
    if (flags[8] == NULL || flags[3] || flags[4] || flags[5] || flags[6] || flags[7])
      qa_range_error("%s requires --profile and takes identity, role and callbacks from that declaration", action);
    out.profile = qa_normalize_resource_path(flags[8]);
    if (!out.profile) qa_range_error("Out of memory");
    return out;
  }
  if (flags[8]) qa_range_error("--profile requires declare-qvm or declare-native");
  if (strcmp(action, "inspect") == 0) {
    if (flags[3] || flags[4] || flags[5] || flags[6] || flags[7])
      qa_range_error("Declaration options require the declare action");
    return out;
  }
  if (flags[3] == NULL || !behavior_id_ok(flags[3]) || flags[6] == NULL || flags[5] == NULL)
    qa_range_error("Declare requires --id NAMESPACE:ID, --role ROLE and --fire CALLBACK");
  out.id = xdup(flags[3]);
  out.title = xdup(flags[4] ? flags[4] : flags[3]);
  out.role = role_or_abort(flags[5]);
  out.fire = xdup(flags[6]);
  if (flags[7]) out.activate = xdup(flags[7]);
  return out;
}

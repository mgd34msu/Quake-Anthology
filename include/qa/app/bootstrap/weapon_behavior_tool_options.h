/* Port of src/app/bootstrap/weapon-behavior-tool-options.ts.
 * node:path resolve() -> realpath-style absolutize via host default
 * (cwd join). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_WEAPON_BEHAVIOR_TOOL_OPTIONS_H
#define QA_APP_BOOTSTRAP_WEAPON_BEHAVIOR_TOOL_OPTIONS_H
#include <stddef.h>
/* TS WeaponBehaviorToolCommand discriminated on action (exact strings). */
typedef struct qa_app_weapon_behavior_tool_command {
  const char *action; /* help|inspect|declare|declare-qvm|declare-native */
  char *product; char *corpus_root; char *user_content_root; char *artifact; /* NULL absent */
  char *profile; char *id; char *title; const char *role; char *fire; char *activate;
} qa_app_weapon_behavior_tool_command;
extern const char qa_app_weapon_behavior_tool_help[];
qa_app_weapon_behavior_tool_command qa_app_parse_weapon_behavior_tool(const char *const *argv, size_t argc);
void qa_app_weapon_behavior_tool_command_free(qa_app_weapon_behavior_tool_command *c);
#endif

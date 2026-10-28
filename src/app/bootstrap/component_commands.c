/* Port of src/app/bootstrap/component-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_commands.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/server_administration.h"
static char *lower_dup(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  for (size_t i = 0; i <= len; i++) out[i] = (char)tolower((unsigned char)text[i]);
  return out;
}
char **qa_app_component_engine_commands(qa_command_dialect dialect, const qa_app_loaded_content *content, size_t *out_count) {
  char **names = NULL;
  size_t count = 0, cap = 0;
  qa_q3_product_policy policy;
  bool has_policy = content && qa_app_loaded_content_q3_policy(content, &policy);
  if (!has_policy) policy = (qa_q3_product_policy){QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL};
  size_t admin_count = 0, map_count = 0;
  const char *const *admin = qa_app_source_administration_command_names(dialect, &admin_count);
  const char *const *maps = qa_q3_product_map_commands(policy, &map_count);
  static const char *const extra[] = {"quit", "map", "gamemap", "changelevel", "map_restart", "save", "load", "exec", "cinematic"};
  const char *const *groups[3] = {admin, qa_app_application_audio_commands, maps};
  size_t group_counts[3] = {admin_count, qa_app_application_audio_command_count, map_count};
  for (size_t g = 0; g < 4; g++) {
    const char *const *group = g < 3 ? groups[g] : extra;
    size_t group_count = g < 3 ? group_counts[g] : 9;
    for (size_t i = 0; i < group_count; i++) {
      if (g == 0 && strcmp(group[i], "sv") == 0) continue;
      char *lowered = lower_dup(group[i]);
      bool seen = false;
      for (size_t j = 0; j < count; j++) if (strcmp(names[j], lowered) == 0) seen = true;
      if (seen) { free(lowered); continue; }
      if (count == cap) { cap = cap ? cap * 2 : 32; names = realloc(names, cap * sizeof *names); }
      names[count++] = lowered;
    }
  }
  *out_count = count;
  return names;
}
bool qa_app_component_engine_command(qa_command_dialect dialect, const qa_app_loaded_content *content, const char *name) {
  size_t count = 0;
  char **names = qa_app_component_engine_commands(dialect, content, &count);
  char *lowered = lower_dup(name);
  bool found = false;
  for (size_t i = 0; i < count; i++) { if (strcmp(names[i], lowered) == 0) found = true; free(names[i]); }
  free(names);
  free(lowered);
  return found;
}

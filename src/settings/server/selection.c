/* Port of src/settings/server/selection.ts. GPL-2.0-or-later. */
#include "qa/settings/server/selection.h"

#include <stdbool.h>
#include <string.h>

#include "qa/settings/server/profile.h"
#include "qa/settings/server/q1.h"
#include "qa/settings/server/q2_owner.h"
#include "qa/settings/server/q3.h"
#include "qa/settings/server/rotation.h"

static bool starts_with(const char *text, const char *prefix) {
  return text != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}
static bool includes(const char *text, const char *needle) {
  return text != NULL && strstr(text, needle) != NULL;
}

const qa_server_setting_definition **qa_server_definitions_for_recipe(
    const qa_executable_recipe *recipe, size_t *out_count) {
  return qa_server_definitions_for_selection(recipe->map.entities, recipe->match,
                                             recipe->combat, out_count);
}

const qa_server_setting_definition **qa_server_definitions_for_selection(
    qa_provider_reference source, qa_provider_reference match,
    qa_provider_reference combat, size_t *out_count) {
  if (starts_with(source.provider, "q1:")) {
    qa_server_setting_collection collections[1] = {qa_q1_match_settings(
        includes(source.content, ":rogue:")
            ? "rogue"
            : includes(source.content, ":ctf:") ? "ctf" : "standard")};
    return qa_collect_server_settings(collections, 1, out_count);
  }
  if (starts_with(source.provider, "q2:")) {
    qa_server_setting_collection collections[7];
    bool rerelease = includes(source.content, ":rerelease:");
    size_t count = qa_q2_server_setting_collections(
        match.provider, starts_with(combat.provider, "q2:"), rerelease,
        collections);
    collections[count++] = qa_q2_rotation_settings(rerelease);
    return qa_collect_server_settings(collections, count, out_count);
  }
  if (starts_with(source.provider, "q3:")) {
    const char *product = includes(match.content, "missionpack") ? "missionpack" : "baseq3";
    qa_server_setting_collection collections[3];
    collections[0] = qa_q3_limit_settings(product);
    collections[1] = qa_q3_match_settings(product);
    size_t count = 2;
    if (starts_with(combat.provider, "q3:"))
      collections[count++] = qa_q3_combat_settings(product);
    return qa_collect_server_settings(collections, count, out_count);
  }
  return qa_collect_server_settings(NULL, 0, out_count);
}

/* Port of src/settings/server/q1.ts. GPL-2.0-or-later. */
#include "qa/settings/server/q1.h"

#include <string.h>

static const qa_ui_choice standard_choices[] = {
    {"0", "Off"}, {"1", "No friendly fire"}, {"2", "Friendly fire"}};
static const qa_ui_choice rogue_choices[] = {
    {"0", "Off"}, {"1", "No friendly fire"}, {"2", "Friendly fire"},
    {"3", "Tag"}, {"4", "Capture the flag"}, {"5", "One flag CTF"},
    {"6", "Three team CTF"}};

static const qa_server_setting_definition standard_definitions[] = {{
    .id = "server:q1.teamplay",
    .label = "Quake teamplay",
    .description = "Select source team rules for the next map. Rogue objective "
                   "entities are initialized when the map starts.",
    .apply_at = QA_SERVER_APPLY_NEXT_MAP,
    .default_value = "0",
    .target = {.is_bit = false, .name = "teamplay"},
    .kind = QA_SERVER_SETTING_CHOICE,
    .choices = standard_choices,
    .choice_count = 3,
}};
static const qa_server_setting_definition rogue_definitions[] = {{
    .id = "server:q1.teamplay",
    .label = "Quake teamplay",
    .description = "Select source team rules for the next map. Rogue objective "
                   "entities are initialized when the map starts.",
    .apply_at = QA_SERVER_APPLY_NEXT_MAP,
    .default_value = "0",
    .target = {.is_bit = false, .name = "teamplay"},
    .kind = QA_SERVER_SETTING_CHOICE,
    .choices = rogue_choices,
    .choice_count = 7,
}};

qa_server_setting_collection qa_q1_match_settings(const char *program) {
  qa_server_setting_collection collection;
  collection.id = "q1:match";
  if (strcmp(program, "ctf") == 0) {
    collection.definitions = NULL;
    collection.definition_count = 0;
    return collection;
  }
  if (strcmp(program, "rogue") == 0) {
    collection.definitions = rogue_definitions;
    collection.definition_count = 1;
    return collection;
  }
  collection.definitions = standard_definitions;
  collection.definition_count = 1;
  return collection;
}

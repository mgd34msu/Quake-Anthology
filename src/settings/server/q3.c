/* Port of src/settings/server/q3.ts. GPL-2.0-or-later. */
#include "qa/settings/server/q3.h"

#include <stdbool.h>
#include <string.h>

#include "qa/settings/server/common.h"

/* q3GameCvarDefinitions defaults, shared by both products. */
static qa_server_setting_definition limit_definitions[3];
static qa_server_setting_definition combat_definitions[1];
static qa_server_setting_definition match_definitions[1];
static qa_server_setting_definition missionpack_match_definitions[1];
static bool built = false;

static const qa_ui_choice base_choices[] = {
    {"0", "Free for all"}, {"1", "Tournament"}, {"2", "Single player"},
    {"3", "Team deathmatch"}, {"4", "Capture the flag"}};
static const qa_ui_choice missionpack_choices[] = {
    {"0", "Free for all"}, {"1", "Tournament"}, {"2", "Single player"},
    {"3", "Team deathmatch"}, {"4", "Capture the flag"}, {"5", "One flag CTF"},
    {"6", "Overload"}, {"7", "Harvester"}};

static qa_server_setting_definition match_definition(const qa_ui_choice *choices,
                                                     size_t count) {
  qa_server_setting_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.id = "server:q3.game-type";
  definition.label = "Q3 match type";
  definition.description = "Select source match rules for the next map. "
                           "Equipment selections stay independent.";
  definition.apply_at = QA_SERVER_APPLY_NEXT_MAP;
  definition.default_value = "0"; /* g_gametype source default */
  definition.target.is_bit = false;
  definition.target.name = "g_gametype";
  definition.kind = QA_SERVER_SETTING_CHOICE;
  definition.choices = choices;
  definition.choice_count = count;
  return definition;
}

static void build(void) {
  if (built) return;
  built = true;
  qa_server_limit(&limit_definitions[0], 
      "server:time-limit", "timelimit", "Time limit (minutes)", 0,
      "End play after this many minutes. Zero disables the limit.",
      QA_SERVER_APPLY_LIVE, true);
  qa_server_limit(&limit_definitions[1], 
      "server:frag-limit", "fraglimit", "Frag limit", 20,
      "End applicable matches at this score. Zero disables the limit.",
      QA_SERVER_APPLY_LIVE, true);
  qa_server_limit(&limit_definitions[2], 
      "server:capture-limit", "capturelimit", "Capture limit", 8,
      "End applicable team matches at this capture count. Zero disables the "
      "limit.",
      QA_SERVER_APPLY_LIVE, true);
  /* g_friendlyFire source default "0" is off. */
  qa_server_setting_target friendly;
  memset(&friendly, 0, sizeof friendly);
  friendly.name = "g_friendlyFire";
  combat_definitions[0] = qa_server_friendly_fire(friendly, false);
  match_definitions[0] = match_definition(base_choices, 5);
  missionpack_match_definitions[0] = match_definition(missionpack_choices, 8);
}

qa_server_setting_collection qa_q3_limit_settings(const char *product) {
  (void)product; /* defaults are in the common cvar table */
  build();
  qa_server_setting_collection result = {"q3:limits", limit_definitions, 3};
  return result;
}
qa_server_setting_collection qa_q3_combat_settings(const char *product) {
  (void)product;
  build();
  qa_server_setting_collection result = {"q3:combat", combat_definitions, 1};
  return result;
}
qa_server_setting_collection qa_q3_match_settings(const char *product) {
  build();
  bool missionpack = strcmp(product, "missionpack") == 0;
  qa_server_setting_collection result = {
      "q3:match", missionpack ? missionpack_match_definitions : match_definitions, 1};
  return result;
}

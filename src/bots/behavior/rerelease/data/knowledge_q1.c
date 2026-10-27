/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/data/knowledge_q1.h"
#include "qa/bots/behavior/rerelease/data/source_files.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
bool qa_load_quake1_knowledge(qa_bot_knowledge *out, qa_bot_asset_files *files) {
  char *weapons = qa_read_bot_source_text(files, "bots/weapons.txt"); if (!weapons) return false;
  char *settings = qa_read_bot_source_text(files, "bots/settings_PC.txt");
  if (!settings) settings = qa_read_bot_source_text(files, "bots/settings_Consoles.txt");
  if (!settings) settings = qa_read_bot_source_text(files, "bots/settings_Nintendo.txt");
  if (!settings) qa_range_error("Quake rerelease bot weapons were mounted without source skill settings");
  const char *names[7] = {"bots/characters.txt","bots/items.txt","bots/monsters.txt","bots/interactables.txt","bots/game_rules.txt","bots/teams.txt","bots/chats.txt"};
  char *txt[7] = {0}; for (int i = 0; i < 7; i++) txt[i] = qa_read_bot_source_text(files, names[i]);
  qa_bot_data_files f = {txt[0] ? txt[0] : "", weapons, txt[1] ? txt[1] : "", txt[2] ? txt[2] : "", txt[3] ? txt[3] : "", txt[4] ? txt[4] : "", txt[5] ? txt[5] : "", txt[6] ? txt[6] : "", settings, NULL};
  bool ok = qa_bot_knowledge_init(out, f, QA_BOT_DATA_Q1, NULL, NULL);
  free(weapons); free(settings); for (int i = 0; i < 7; i++) free(txt[i]); return ok;
}

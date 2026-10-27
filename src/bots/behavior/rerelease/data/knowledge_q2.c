/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/data/knowledge_q2.h"
#include "qa/bots/behavior/rerelease/data/source_files.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const qa_bot_weapon_binding QA_BOT_WEAPON_BINDINGS[21] = {
  {"blaster", 1<<0, "weapon_blaster", ""}, {"chainfist", 1<<1, "weapon_chainfist", ""}, {"shotgun", 1<<2, "weapon_shotgun", "ammo_shells"},
  {"super_shotgun", 1<<3, "weapon_supershotgun", "ammo_shells"}, {"machinegun", 1<<4, "weapon_machinegun", "ammo_bullets"}, {"chaingun", 1<<5, "weapon_chaingun", "ammo_bullets"},
  {"etf_rifle", 1<<6, "weapon_etf_rifle", "ammo_flechettes"}, {"grenades", 1<<7, "ammo_grenades", "ammo_grenades"}, {"grenade_launcher", 1<<8, "weapon_grenadelauncher", "ammo_grenades"},
  {"prox_launcher", 1<<9, "weapon_proxlauncher", "ammo_prox"}, {"rocket_launcher", 1<<10, "weapon_rocketlauncher", "ammo_rockets"}, {"hyperblaster", 1<<11, "weapon_hyperblaster", "ammo_cells"},
  {"boomer", 1<<12, "weapon_boomer", "ammo_cells"}, {"plasmabeam", 1<<13, "weapon_plasmabeam", "ammo_cells"}, {"railgun", 1<<14, "weapon_railgun", "ammo_slugs"},
  {"phalanx", 1<<15, "weapon_phalanx", "ammo_magslug"}, {"bfg", 1<<16, "weapon_bfg", "ammo_cells"}, {"disintegrator", 1<<17, "weapon_disintegrator", "ammo_disruptor"},
  {"tesla", 1<<18, "ammo_tesla", "ammo_tesla"}, {"trap", 1<<19, "ammo_trap", "ammo_trap"}, {"grapple", 1<<20, "weapon_grapple", ""}};
const char *const QA_BOT_AMMO_CLASSNAMES[13] = {"ammo_shells","ammo_bullets","ammo_cells","ammo_rockets","ammo_grenades","ammo_slugs","ammo_flechettes","ammo_prox","ammo_tesla","ammo_trap","ammo_magslug","ammo_disruptor","ammo_nuke"};
const qa_bot_objective_binding QA_BOT_OBJECTIVE_BINDINGS[2] = {{"item_flag_team1", 1}, {"item_flag_team2", 2}};
static bool weapon_number(void *ctx, const char *classname, double *out) { (void)ctx; for (int i = 0; i < 21; i++) if (!strcmp(QA_BOT_WEAPON_BINDINGS[i].classname, classname)) { *out = (double)QA_BOT_WEAPON_BINDINGS[i].bit; return true; } return false; }
bool qa_bot_build_knowledge(qa_bot_knowledge *out, qa_bot_data_files files) { return qa_bot_knowledge_init(out, files, QA_BOT_DATA_Q2, weapon_number, NULL); }
qa_bot_game_mode qa_bot_get_game_mode_q2(const qa_bot_knowledge *k, double (*cv)(void *, const char *), void *ctx) { return qa_bot_knowledge_game_mode(k, cv, ctx); }
bool qa_bot_load_knowledge(qa_bot_knowledge *out, qa_bot_asset_files *files, const char *platform) {
  const char *plat = platform ? platform : "PC"; char path[512], spath[512];
  char *txt[10] = {0}; const char *names[8] = {"characters","items","monsters","interactables","game_rules","teams","chats","dangers"};
  char *weapons = qa_read_bot_source_text(files, "bots/weapons.txt");
  snprintf(spath, sizeof spath, "bots/settings_%s.txt", plat); char *settings = qa_read_bot_source_text(files, spath);
  if (!weapons || !settings) qa_range_error("Q2 rerelease bot source data unavailable: weapons.txt and settings_%s.txt are required", plat);
  for (int i = 0; i < 8; i++) { snprintf(path, sizeof path, "bots/%s.txt", names[i]); txt[i] = qa_read_bot_source_text(files, path); }
  qa_bot_data_files f = {txt[0] ? txt[0] : "", weapons, txt[1] ? txt[1] : "", txt[2] ? txt[2] : "", txt[3] ? txt[3] : "", txt[4] ? txt[4] : "", txt[5] ? txt[5] : "", txt[6] ? txt[6] : "", settings, txt[7] ? txt[7] : ""};
  bool ok = qa_bot_build_knowledge(out, f);
  free(weapons); free(settings); for (int i = 0; i < 8; i++) free(txt[i]); return ok;
}

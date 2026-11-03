/* Port of src/app/bootstrap/q3-map-command.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_map_command.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include <math.h>
void qa_app_q3_map_launch_make(qa_app_q3_map_launch *out, qa_q3_product_policy policy, const char *command, double current_game_type) {
  size_t count = 0;
  const char *const *commands = qa_q3_product_map_commands(policy, &count);
  bool known = false;
  for (size_t i = 0; i < count; i++) if (strcmp(commands[i], command) == 0) known = true;
  if (!known) qa_range_error("Unknown Q3 map command: %s", command);
  bool single_player = strncmp(command, "sp", 2) == 0;
  double game_type = single_player ? 2 : current_game_type == 2 ? 0 : current_game_type;
  memset(out, 0, sizeof *out);
  out->game_type = game_type;
  out->single_player = single_player;
  out->kill_bots = single_player || strcmp(command, "devmap") == 0;
  out->has_max_clients = single_player;
  out->max_clients = single_player ? 8 : 0;
  if ((fabs(game_type) < 9.2e18 && game_type == trunc(game_type)))
    snprintf(out->gametype_value, sizeof out->gametype_value, "%s", QA_JS_NUMBER(game_type));
  else
    snprintf(out->gametype_value, sizeof out->gametype_value, "%s", QA_JS_NUMBER(game_type));
  out->cvars[0].name = "g_gametype"; out->cvars[0].value = out->gametype_value;
  out->cvars[1].name = "sv_cheats"; out->cvars[1].value = strcmp(command, "devmap") == 0 ? "1" : "0";
  out->cvar_count = 2;
  if (single_player) {
    out->cvars[2].name = "g_doWarmup"; out->cvars[2].value = "0";
    out->cvars[3].name = "sv_maxclients"; out->cvars[3].value = "8";
    out->cvar_count = 4;
  }
}
void qa_app_apply_q3_map_launch(qa_cvar_registry *cvars, const qa_app_q3_map_launch *launch, const char *phase) {
  bool finish = strcmp(phase, "finish") == 0;
  bool spawn = strcmp(phase, "spawn") == 0;
  if (!finish) {
    size_t applied = 0;
    qa_cvar_snapshot *snaps = qa_cvar_registry_apply_latched(cvars, NULL, &applied);
    for (size_t i = 0; i < applied; i++) qa_cvar_read_free(&snaps[i]);
    free(snaps);
  }
  for (size_t i = 0; i < launch->cvar_count; i++) {
    const qa_app_q3_map_cvar *setting = &launch->cvars[i];
    if ((spawn && strcmp(setting->name, "sv_cheats") == 0) || (finish && strcmp(setting->name, "sv_cheats") != 0)) continue;
    qa_cvar_snapshot snap;
    if (qa_cvar_registry_set(cvars, setting->name, setting->value, true, &snap)) qa_cvar_read_free(&snap);
  }
}

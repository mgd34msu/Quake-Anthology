/* Port of src/app/bootstrap/q3-common-cvars.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_common_cvars.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <string.h>
#include "qa/network/q3/adapters.h"
#include <math.h>
void qa_app_q3_server_cvar_definitions_make(qa_app_q3_server_cvar_definitions *out, qa_app_q3_server_cvar_settings settings) {
  memset(out, 0, sizeof *out);
  snprintf(out->protocol_value, sizeof out->protocol_value, "%lld", (long long)qa_q3_protocol.version);
  /* TS String(number): integral values print without a fraction. */
  if ((fabs(settings.max_clients) < 9.2e18 && settings.max_clients == trunc(settings.max_clients)))
    snprintf(out->maxclients_value, sizeof out->maxclients_value, "%s", QA_JS_NUMBER(settings.max_clients));
  else
    snprintf(out->maxclients_value, sizeof out->maxclients_value, "%s", QA_JS_NUMBER(settings.max_clients));
  const qa_collision_cvar_definition common[] = {
    {"protocol", out->protocol_value, QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_pure", "1", QA_CVAR_FLAG_SYSTEM_INFO}, {"sv_allowDownload", "0", QA_CVAR_FLAG_SERVER_INFO},
    {"sv_maxRate", "0", QA_CVAR_FLAG_SERVER_INFO}, {"sv_fps", "20", QA_CVAR_FLAG_NONE},
    {"sv_serverid", "0", QA_CVAR_FLAG_SYSTEM_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_paks", "", QA_CVAR_FLAG_SYSTEM_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_pakNames", "", QA_CVAR_FLAG_SYSTEM_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_referencedPaks", "", QA_CVAR_FLAG_SYSTEM_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_referencedPakNames", "", QA_CVAR_FLAG_SYSTEM_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_maxclients", out->maxclients_value, QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_LATCH},
    {"mapname", settings.map_name, QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_mapname", "", QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_READ_ONLY},
    {"sv_privateClients", "0", QA_CVAR_FLAG_SERVER_INFO}, {"sv_privatePassword", "", QA_CVAR_FLAG_TEMPORARY},
    {"sv_reconnectlimit", "3", QA_CVAR_FLAG_NONE}, {"sv_minPing", "0", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_SERVER_INFO},
    {"sv_maxPing", "0", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_SERVER_INFO},
    {"sv_floodProtect", "1", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_SERVER_INFO},
    {"sv_strictAuth", "1", QA_CVAR_FLAG_ARCHIVE}, {"bot_enable", "1", QA_CVAR_FLAG_NONE},
  };
  size_t count = sizeof common / sizeof common[0];
  memcpy(out->items, common, sizeof common);
  for (size_t i = 0; i < 3; i++) out->items[count++] = qa_collision_map_cvar_definitions[i];
  out->count = count;
}
const char *const qa_app_q3_server_cvar_names[24] = {
  "protocol", "sv_pure", "sv_allowDownload", "sv_maxRate", "sv_fps", "sv_serverid", "sv_paks",
  "sv_pakNames", "sv_referencedPaks", "sv_referencedPakNames", "sv_maxclients", "mapname",
  "sv_mapname", "sv_privateClients", "sv_privatePassword", "sv_reconnectlimit", "sv_minPing",
  "sv_maxPing", "sv_floodProtect", "sv_strictAuth", "bot_enable",
  /* collisionMapCvarDefinitions names, in landed order (filled by init check in tests) */
  "cm_noAreas", "cm_noCurves", "cm_playerCurveClip",
};
void qa_app_register_q3_server_cvars(qa_cvar_registry *cvars, qa_app_q3_server_cvar_settings settings) {
  qa_app_q3_server_cvar_definitions defs;
  qa_app_q3_server_cvar_definitions_make(&defs, settings);
  for (size_t i = 0; i < defs.count; i++) {
    qa_cvar_snapshot snap;
    if (qa_cvar_registry_register(cvars, defs.items[i].name, defs.items[i].value, defs.items[i].flags, &snap))
      qa_cvar_read_free(&snap);
  }
}

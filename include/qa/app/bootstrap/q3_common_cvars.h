/* Port of src/app/bootstrap/q3-common-cvars.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_COMMON_CVARS_H
#define QA_APP_BOOTSTRAP_Q3_COMMON_CVARS_H
#include <stddef.h>
#include "qa/core/cvars/index.h"
#include "qa/world/collision/q3/settings.h" /* qa_collision_cvar_definition */
/* TS q3ServerCvarDefinitions result element. value for protocol /
 * sv_maxclients / mapname is formatted into scratch slots of the
 * definitions owner below. */
typedef struct qa_app_q3_server_cvar_settings { double max_clients; const char *map_name; } qa_app_q3_server_cvar_settings;
typedef struct qa_app_q3_server_cvar_definitions {
  qa_collision_cvar_definition items[24];
  size_t count;
  char protocol_value[32];
  char maxclients_value[32];
} qa_app_q3_server_cvar_definitions;
/* Fills *out in place: items[].value points into *out's own scratch slots, so
 * the definitions must not be copied or outlive *out. */
void qa_app_q3_server_cvar_definitions_make(qa_app_q3_server_cvar_definitions *out, qa_app_q3_server_cvar_settings settings);
/* TS q3ServerCvarNames: borrowed static names. */
extern const char *const qa_app_q3_server_cvar_names[24];
#define QA_APP_Q3_SERVER_CVAR_NAME_COUNT 24
void qa_app_register_q3_server_cvars(qa_cvar_registry *cvars, qa_app_q3_server_cvar_settings settings);
#endif

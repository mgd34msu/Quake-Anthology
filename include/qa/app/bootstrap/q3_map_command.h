/* Port of src/app/bootstrap/q3-map-command.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_MAP_COMMAND_H
#define QA_APP_BOOTSTRAP_Q3_MAP_COMMAND_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/core/cvars/index.h"
#include "qa/core/q3_product_policy.h"
typedef struct qa_app_q3_map_cvar { const char *name; const char *value; } qa_app_q3_map_cvar;
/* TS Q3MapLaunch. cvar values point at gametype_value or literals. */
typedef struct qa_app_q3_map_launch {
  double game_type;
  bool kill_bots;
  bool single_player;
  bool has_max_clients;
  double max_clients; /* 8 when has_max_clients */
  qa_app_q3_map_cvar cvars[4];
  size_t cvar_count;
  char gametype_value[32];
} qa_app_q3_map_launch;
/* Aborts (TS Error) for an unknown command. */
qa_app_q3_map_launch qa_app_q3_map_launch_make(qa_q3_product_policy policy, const char *command, double current_game_type);
/* phase: "all"|"spawn"|"finish". */
void qa_app_apply_q3_map_launch(qa_cvar_registry *cvars, const qa_app_q3_map_launch *launch, const char *phase);
#endif

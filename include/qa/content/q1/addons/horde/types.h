/* Port of src/content/q1/addons/horde/types.ts (types only). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_HORDE_TYPES_H
#define QA_CONTENT_Q1_ADDONS_HORDE_TYPES_H
#include "qa/content/q1/addons/context.h"
/* TS Q1HordeServices (host interface; every member required). */
typedef struct qa_q1ad_horde_services {
  void *ctx;
  /** Source deadflag is distinct from shared health during death animations. */
  double (*dead_flag)(void *ctx, qa_actor_id player);
  bool (*no_target)(void *ctx, qa_actor_id player);
  bool (*is_bot)(void *ctx, qa_actor_id player);
  void (*respawn_teammate)(void *ctx, qa_actor_id player);
  void (*add_score)(void *ctx, qa_actor_id player, double delta);
  /** Restarts the current map and applies source reset_flag to the new player travel parameters. */
  void (*restart_session)(void *ctx, const char *map, double starting_server_flags);
} qa_q1ad_horde_services;
#endif

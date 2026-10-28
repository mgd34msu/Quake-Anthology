/* Port of src/app/bootstrap/q2-travel.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_TRAVEL_H
#define QA_APP_BOOTSTRAP_Q2_TRAVEL_H
#include <stdbool.h>
#include <stddef.h>
/* Owned strings; free the chain with qa_app_q2_travel_target_destroy. */
typedef struct qa_app_q2_travel_target {
  const char *kind; /* "map"|"cinematic"|"picture"|"demo" */
  char *name;
  char *spawn_point;
  bool new_unit;
  struct qa_app_q2_travel_target *next; /* owned, NULL = TS null */
} qa_app_q2_travel_target;
/* Aborts (TS Error) on invalid input. Result malloc'd. */
qa_app_q2_travel_target *qa_app_parse_q2_travel(const char *expression);
void qa_app_q2_travel_target_destroy(qa_app_q2_travel_target *target);
/* Malloc'd command ("" when no next); caller frees. */
char *qa_app_q2_next_server_command(const qa_app_q2_travel_target *target);
#endif

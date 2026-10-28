/* Port of src/app/bootstrap/team-arena-demo.ts. Async exists -> sync fn+ctx. */
#ifndef QA_APP_BOOTSTRAP_TEAM_ARENA_DEMO_H
#define QA_APP_BOOTSTRAP_TEAM_ARENA_DEMO_H
#include <stdbool.h>
typedef struct qa_app_team_arena_demo { char *name; char *path; } qa_app_team_arena_demo;
void qa_app_team_arena_demo_free(qa_app_team_arena_demo *d);
/* Returns true and fills out (owned strings) when demo exists; false for TS null. */
bool qa_app_team_arena_demo_lookup(const char *map, double game_type, double protocol,
                                   bool (*exists)(void *ctx, const char *path), void *ctx,
                                   qa_app_team_arena_demo *out);
#endif

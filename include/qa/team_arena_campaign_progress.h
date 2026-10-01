#ifndef QA_TEAM_ARENA_CAMPAIGN_PROGRESS_H
#define QA_TEAM_ARENA_CAMPAIGN_PROGRESS_H
#include "qa/arena_progress_catalog.h"

typedef struct qa_team_arena_campaign qa_team_arena_campaign;
typedef struct qa_team_arena_skirmish qa_team_arena_skirmish;
typedef enum qa_team_arena_catalog_policy {
    QA_TEAM_ARENA_CATALOG_RETAIL, QA_TEAM_ARENA_CATALOG_DEMO
} qa_team_arena_catalog_policy;
typedef struct qa_team_arena_cursor { size_t game_type_index, map_index; } qa_team_arena_cursor;
typedef struct qa_team_arena_campaign_map {
    const char *map, *title, *opponent;
    uint32_t team_members;
} qa_team_arena_campaign_map;
typedef struct qa_team_arena_setting { const char *name, *value; } qa_team_arena_setting;
typedef struct qa_team_arena_campaign_bot {
    const char *ai, *name, *team;
    uint32_t delay_ms;
} qa_team_arena_campaign_bot;
typedef struct qa_team_arena_skirmish_view {
    qa_team_arena_cursor cursor;
    const char *map, *title, *player_team, *player_model, *player_head_model;
    uint32_t game_type, skill, max_clients;
    int64_t time_to_beat;
    const qa_team_arena_setting *source_cvars, *client_cvars;
    size_t source_cvar_count, client_cvar_count;
    const qa_team_arena_campaign_bot *bots;
    size_t bot_count;
} qa_team_arena_skirmish_view;

/* Policy comes from the genuine retained startup Q3 product policy. The
 * scoped VFS supplies mandatory metadata and the ordered retail .team list. */
bool qa_team_arena_campaign_create(qa_vfs *, qa_team_arena_catalog_policy,
    qa_team_arena_campaign **, qa_error *);
void qa_team_arena_campaign_destroy(qa_team_arena_campaign *);
bool qa_team_arena_campaign_ready(const qa_team_arena_campaign *, qa_error *);
bool qa_team_arena_campaign_policy(const qa_team_arena_campaign *, qa_team_arena_catalog_policy *);
size_t qa_team_arena_campaign_map_count(const qa_team_arena_campaign *);
const qa_team_arena_campaign_map *qa_team_arena_campaign_map_at(const qa_team_arena_campaign *, size_t);
size_t qa_team_arena_campaign_game_type_count(const qa_team_arena_campaign *);
bool qa_team_arena_campaign_game_type_at(const qa_team_arena_campaign *, size_t, int64_t *);
size_t qa_team_arena_campaign_team_count(const qa_team_arena_campaign *);
const char *qa_team_arena_campaign_team_at(const qa_team_arena_campaign *, size_t);
bool qa_team_arena_campaign_current(const qa_team_arena_campaign *, const char *map,
    uint32_t game_type, qa_team_arena_cursor *, qa_error *);
bool qa_team_arena_campaign_next(const qa_team_arena_campaign *, qa_team_arena_cursor,
    qa_team_arena_cursor *, qa_error *);
/* NULL cursor/teams select the authored donor defaults (3,0), Pagans/Stroggs.
 * Plans retain the catalog; their readonly strings survive caller release. */
bool qa_team_arena_campaign_plan(qa_team_arena_campaign *, const qa_team_arena_cursor *,
    uint32_t skill, const char *player_team, const char *opponent_team,
    qa_team_arena_skirmish **, qa_error *);
const qa_team_arena_skirmish_view *qa_team_arena_skirmish_read(const qa_team_arena_skirmish *);
void qa_team_arena_skirmish_destroy(qa_team_arena_skirmish *);
/* Capture after the genuine GAME applyLatched boundary and before applying
 * plan rows. Values borrow the given registry until its next mutation; the
 * launch owner copies them once and reapplies the same capture after travel. */
bool qa_team_arena_source_baseline(const qa_cvars *, qa_team_arena_setting out[7], qa_error *);
bool qa_team_arena_client_baseline(const qa_cvars *, qa_team_arena_setting *, qa_error *);
size_t qa_team_arena_campaign_resource_count(const qa_team_arena_campaign *);
const qa_resource *qa_team_arena_campaign_resource_at(const qa_team_arena_campaign *, size_t, const char **);
/* Restore resolves exact retained authored resources; it performs no listing
 * or VFS acquisition and reproduces the actual ordered merge. */
bool qa_team_arena_campaign_checkpoint(const qa_team_arena_campaign *,
    const qa_base_arena_catalog_refs *, qa_buffer *, qa_error *);
bool qa_team_arena_campaign_restore(qa_bytes, const qa_base_arena_catalog_refs *,
    qa_team_arena_campaign **, qa_error *);
#endif

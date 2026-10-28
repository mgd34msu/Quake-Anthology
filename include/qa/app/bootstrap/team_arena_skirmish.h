/* Port of src/app/bootstrap/team-arena-skirmish.ts. Catalog/mount
 * loading (loadTeamArenaCampaign/readTeamArenaCampaign async) becomes
 * synchronous host fn+ctx supplying latin1 file bytes. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_TEAM_ARENA_SKIRMISH_H
#define QA_APP_BOOTSTRAP_TEAM_ARENA_SKIRMISH_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_name_value { char *name; char *value; } qa_app_name_value;
typedef struct qa_app_team_arena_teams { const char *player; const char *opponent; } qa_app_team_arena_teams;
typedef struct qa_app_team_arena_cursor { double game_type_index, map_index; } qa_app_team_arena_cursor;
typedef struct qa_app_team_arena_bot { char *ai; char *name; char *team; double delay_milliseconds; } qa_app_team_arena_bot;
typedef struct qa_app_team_arena_skirmish {
  char *map; char *title; double game_type, max_clients, time_to_beat, skill;
  qa_app_name_value *cvars; size_t cvar_count;
  qa_app_name_value *client_cvars; size_t client_cvar_count;
  qa_app_team_arena_bot *bots; size_t bot_count;
  char *player_team; char *player_model; char *player_head_model;
  qa_app_team_arena_cursor cursor;
} qa_app_team_arena_skirmish;
typedef struct qa_app_team_arena_campaign qa_app_team_arena_campaign;
typedef struct qa_app_team_arena_override_row { const char *name; const char *saved; } qa_app_team_arena_override_row;
extern const qa_app_team_arena_override_row qa_app_team_arena_server_overrides[6];
qa_app_team_arena_campaign *qa_app_parse_team_arena_campaign(const char *game_info, const char *team_info);
void qa_app_team_arena_campaign_destroy(qa_app_team_arena_campaign *c);
char **qa_app_team_arena_team_choices(const qa_app_team_arena_campaign *c, size_t *out_count); /* borrowed keys array, free array only */
qa_app_team_arena_cursor qa_app_next_team_arena_cursor(const qa_app_team_arena_campaign *c, qa_app_team_arena_cursor current);
qa_app_team_arena_cursor qa_app_current_team_arena_cursor(const qa_app_team_arena_campaign *c, const char *map, double game_type);
void qa_app_team_arena_skirmish_init_default(qa_app_team_arena_skirmish *out, const qa_app_team_arena_campaign *c, double skill, qa_app_team_arena_cursor cursor, qa_app_team_arena_teams teams);
void qa_app_team_arena_skirmish_free(qa_app_team_arena_skirmish *s);
/* Baseline entries are borrowed name/value pairs. Results malloc'd. */
qa_app_name_value *qa_app_team_arena_source_cvars(const qa_app_team_arena_skirmish *setup, const qa_app_name_value *baseline, size_t baseline_count, size_t *out_count);
qa_app_name_value *qa_app_team_arena_client_cvars(const qa_app_team_arena_skirmish *setup, const qa_app_name_value *baseline, size_t baseline_count, size_t *out_count);
void qa_app_name_values_free(qa_app_name_value *values, size_t count);
/* Host for read/load campaign: supplies file contents (malloc'd) + .team listing. */
typedef struct qa_app_team_arena_campaign_host {
  void *ctx;
  char *(*read_file)(void *ctx, const char *path);       /* malloc'd latin1 text */
  char **(*list_team_files)(void *ctx, size_t *out_count); /* scripts .team basenames */
  bool additional_teams;
  const char *game_info_path; const char *team_info_path;
} qa_app_team_arena_campaign_host;
qa_app_team_arena_campaign *qa_app_read_team_arena_campaign(qa_app_team_arena_campaign_host host);
qa_app_team_arena_campaign *qa_app_load_team_arena_campaign(qa_app_team_arena_campaign_host host);
/* previous: has_previous false = TS undefined; advance false keeps current. */
void qa_app_read_team_arena_skirmish(qa_app_team_arena_skirmish *out, qa_app_team_arena_campaign_host host, double skill,
                                     bool has_previous, const char *prev_map, double prev_game_type, bool advance,
                                     bool has_teams, qa_app_team_arena_teams teams);
typedef struct qa_app_team_arena_launch_overrides qa_app_team_arena_launch_overrides;
typedef struct qa_app_team_arena_launch_seat { qa_seat_id id; qa_cvar_registry *cvars; } qa_app_team_arena_launch_seat;
qa_app_team_arena_launch_overrides *qa_app_team_arena_launch_overrides_create(const qa_app_team_arena_skirmish *setup);
void qa_app_team_arena_launch_overrides_destroy(qa_app_team_arena_launch_overrides *o);
void qa_app_team_arena_launch_overrides_apply(qa_app_team_arena_launch_overrides *o, qa_cvar_registry *source,
                                              const qa_app_team_arena_launch_seat *seats, size_t seat_count);
#endif

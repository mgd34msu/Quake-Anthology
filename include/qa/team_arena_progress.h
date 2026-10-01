#ifndef QA_TEAM_ARENA_PROGRESS_H
#define QA_TEAM_ARENA_PROGRESS_H
#include "qa/filesystem.h"

typedef struct qa_team_arena_progress qa_team_arena_progress;
typedef struct qa_team_arena_stats {
    int32_t accuracy, impressives, excellents, defends, assists, gauntlets;
    int32_t base_score, perfects, red_score, blue_score, end_time, captures;
} qa_team_arena_stats;
typedef struct qa_team_arena_score {
    int32_t score, red_score, blue_score, perfects, accuracy, impressives;
    int32_t excellents, defends, assists, gauntlets, captures, time;
    int32_t time_bonus, shutout_bonus, skill_bonus, base_score;
} qa_team_arena_score;
typedef struct qa_team_arena_score_input {
    qa_team_arena_stats stats;
    int32_t match_start_time;
    double skill, time_to_beat;
} qa_team_arena_score_input;
typedef struct qa_team_arena_score_result {
    qa_team_arena_score current, previous;
    bool won, new_high_score, new_best_time;
} qa_team_arena_score_result;
typedef struct qa_team_arena_progress_refs {
    void *context;
    bool (*root_ready)(void *, const qa_fs_root *, const char *saved_path,
                        const qa_fs_identity *, qa_error *);
} qa_team_arena_progress_refs;

/* Retains the selected source's real writable config root. Actual GAME
 * cvars supply the calculation inputs; this owner has no cvar substitute. */
bool qa_team_arena_progress_create(qa_fs_root *, qa_team_arena_progress **, qa_error *);
void qa_team_arena_progress_destroy(qa_team_arena_progress *);
bool qa_team_arena_progress_ready(const qa_team_arena_progress *, qa_error *);
const qa_fs_root *qa_team_arena_progress_root(const qa_team_arena_progress *);
bool qa_team_arena_score_calculate(const qa_team_arena_score_input *,
    const qa_team_arena_score *, qa_team_arena_score_result *, qa_error *);
void qa_team_arena_score_encode(const qa_team_arena_score *, uint8_t out[68]);
void qa_team_arena_score_decode(qa_bytes, qa_team_arena_score *);
void qa_team_arena_score_path(const char *source_map, int32_t game_type, char out[64]);
/* The complete native 68-byte .game replacement precedes result publication.
 * An I/O failure publishes no UI result; the next call rereads the real file. */
bool qa_team_arena_progress_record(qa_team_arena_progress *, const char *source_map,
    int32_t game_type, const qa_team_arena_score_input *, qa_team_arena_score_result *, qa_error *);
bool qa_team_arena_progress_checkpoint(const qa_team_arena_progress *, qa_buffer *, qa_error *);
/* Candidate reconstruction retains a separately qualified destination root,
 * performs no score-file I/O, and stays write-blocked until publication. */
bool qa_team_arena_progress_restore(qa_bytes, qa_fs_root *,
    const qa_team_arena_progress_refs *, qa_team_arena_progress **, qa_error *);
void qa_team_arena_progress_publish_restored(qa_team_arena_progress *);
#endif

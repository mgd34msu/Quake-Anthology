#ifndef QA_PLAYER_PROGRESS_H
#define QA_PLAYER_PROGRESS_H

#include "qa/filesystem.h"
#include "qa/gameplay.h"

typedef struct qa_player_progress qa_player_progress;
typedef enum qa_progress_kind {
    QA_PROGRESS_ACHIEVEMENT,
    QA_PROGRESS_LEVEL_COMPLETED,
    QA_PROGRESS_MATCH_COMPLETED
} qa_progress_kind;
typedef struct qa_progress_event {
    qa_progress_kind kind;
    qa_game_family source;
    qa_bytes participant, event;
    union {
        qa_bytes award;
        qa_bytes map;
        struct {
            qa_bytes map;
            double score;
        } match;
    } value;
} qa_progress_event;

/* One application thread owns a store for each profile file. All strings are
 * counted UTF-8, including embedded NULs. Version 1 JSON remains compatible
 * with the prototype's player-progress.json. Missing files start empty. */
bool qa_player_progress_open(qa_fs_root *, const char *relative, qa_player_progress **, qa_error *);
void qa_player_progress_close(qa_player_progress *);
/* Reads borrow strings until close or reload. Record preserves existing
 * strings except when recovering an earlier failed replacement by reloading.
 * A duplicate (source, participant, event) keeps its first recorded value. */
size_t qa_player_progress_count(const qa_player_progress *);
bool qa_player_progress_at(const qa_player_progress *, size_t, qa_progress_event *);
bool qa_player_progress_next(const qa_player_progress *, qa_bytes participant, size_t *cursor,
                             qa_progress_event *);
/* Synchronous durable replacement precedes publication in memory. On an I/O
 * failure the next record reloads the file first, since rename may already
 * have committed before directory synchronization failed. */
bool qa_player_progress_record(qa_player_progress *, const qa_progress_event *, bool *inserted,
                               qa_error *);
bool qa_player_progress_reload(qa_player_progress *, qa_error *);

#endif

/* Port of src/app/bootstrap/player-progress.ts. Async file IO becomes
 * synchronous (PORTING.md); the promise tail is trivially serialized.
 * IO failures other than a missing file abort via qa_range_error with
 * the strerror text (TS rethrows the fs error). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PLAYER_PROGRESS_H
#define QA_APP_BOOTSTRAP_PLAYER_PROGRESS_H
#include <stdbool.h>
#include <stddef.h>
/* TS PlayerProgressEvent union on kind. Strings borrowed on input,
 * owned in store-owned copies returned by list (valid until destroy). */
typedef struct qa_app_player_progress_event {
  const char *kind;   /* "achievement"|"level-completed"|"match-completed"|"ranked-match" */
  const char *source; /* "q1"|"q2"|"q3" */
  const char *participant;
  const char *event;
  const char *award; /* achievement */
  const char *map;   /* level/match */
  double score;      /* match / ranked-match */
  bool won;          /* ranked-match */
  const char *rules; /* ranked-match: ProviderId text */
} qa_app_player_progress_event;
typedef struct qa_app_player_progress_store qa_app_player_progress_store;
/* Torn, foreign or invalid text opens as empty progress (TS); other IO
 * errors abort with the strerror text. */
qa_app_player_progress_store *qa_app_player_progress_store_open(const char *file);
void qa_app_player_progress_store_destroy(qa_app_player_progress_store *store);
/* Borrowed events in insertion order; *out set (NULL when none). */
size_t qa_app_player_progress_store_list(qa_app_player_progress_store *store,
    const char *participant, const qa_app_player_progress_event ***out);
/* TS all(): borrowed events in insertion order (valid until the next record
 * or destroy); *out_count set. */
const qa_app_player_progress_event *const *qa_app_player_progress_store_all(qa_app_player_progress_store *store,
    size_t *out_count);
/* Validates via the TS decode() rules (aborts on invalid). Returns
 * false for a duplicate identity, true after durable record. */
bool qa_app_player_progress_store_record(qa_app_player_progress_store *store,
    qa_app_player_progress_event input);
/* TS flush(): synchronous port has nothing pending. */
void qa_app_player_progress_store_flush(qa_app_player_progress_store *store);
/* TS identity(): JSON.stringify([source, participant, event]).
 * Malloc'd; caller frees. */
char *qa_app_player_progress_identity(qa_app_player_progress_event event);
#endif

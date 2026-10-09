#ifndef QA_PLAYER_PROGRESS_SAVE_H
#define QA_PLAYER_PROGRESS_SAVE_H
#include "qa/player_progress.h"

/* Creates the real stable empty owner with its retained root/path and private
 * string table. Does not open or reload the progress file. */
bool qa_player_progress_create_restored(qa_fs_root *, const char *,
                                        qa_player_progress **empty, qa_error *);
bool qa_player_progress_checkpoint(const qa_player_progress *, qa_buffer *empty, qa_error *);
bool qa_player_progress_restore(qa_player_progress *, qa_bytes, qa_error *);
/* Allocation-free publication after whole-candidate qualification. Ordinary
 * record/reload operations remain blocked until this handoff. */
void qa_player_progress_publish_restored(qa_player_progress *);
#endif

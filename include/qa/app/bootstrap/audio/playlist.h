/* Port of src/app/bootstrap/audio/playlist.ts. Async listing is
 * synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_PLAYLIST_H
#define QA_APP_BOOTSTRAP_AUDIO_PLAYLIST_H
#include <stddef.h>
#include "qa/content/mounts.h"
/* TS mountedMusicTracks: malloc'd sorted track list; free each
 * string and the array. NULL only on OOM. */
char **qa_app_mounted_music_tracks(qa_content_mounted *mounts, size_t *out_count);
/* TS musicFileCue: malloc'd (quoted when the path has whitespace). */
char *qa_app_music_file_cue(const char *path);
/* TS shuffledTracks: malloc'd array of malloc'd strings. random is
 * the TS () => number source (unit interval). */
char **qa_app_shuffled_tracks(const char *const *tracks, size_t count, const char *previous,
                              double (*random)(void *ctx), void *random_ctx, size_t *out_count);
#endif

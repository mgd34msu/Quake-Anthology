#ifndef QA_GAME_Q3_CONFIGSTRINGS_H
#define QA_GAME_Q3_CONFIGSTRINGS_H

#include "qa/game_q3.h"

#define QA_Q3_NATIVE_CONFIGSTRINGS 1024u

/* The source engine's authoritative slot value. Borrowed text lasts until
 * that slot changes, the normal map resets or the native owner closes. */
bool qa_q3_configstring_read(const qa_q3_game *, uint32_t index,
                            const char **out, qa_error *);
/* A source write's transient identity qualifies callback continuations even
 * when nested writes restore the same text. It is not saved gameplay state. */
bool qa_q3_configstring_revision(const qa_q3_game *, uint32_t index,
                                 uint64_t *out, qa_error *);
/* NULL means empty, matching the source setter. Commit precedes its ordinary
 * authored notification. Fast round reset retains these engine-owned slots. */
bool qa_q3_configstring_write(qa_q3_game *, uint32_t index,
                             const char *source_text, qa_error *);
/* Genuine source G_FindConfigstringIndex producers. Slot zero means no name;
 * registering a new name uses the ordinary authoritative slot setter. */
bool qa_q3_model_index(qa_q3_game *, const char *, int32_t *, qa_error *);
bool qa_q3_sound_index(qa_q3_game *, const char *, int32_t *, qa_error *);

#endif

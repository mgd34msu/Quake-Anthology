#ifndef QA_GAME_Q3_CONFIGSTRINGS_H
#define QA_GAME_Q3_CONFIGSTRINGS_H

#include "qa/game_q3.h"

#define QA_Q3_NATIVE_CONFIGSTRINGS 1024u

/* The source engine's authoritative slot value. Borrowed text lasts until
 * that slot changes, the normal map resets or the native owner closes. */
bool qa_q3_configstring_read(const qa_q3_game *, uint32_t index,
                            const char **out, qa_error *);
/* NULL means empty, matching the source setter. Commit precedes its ordinary
 * authored notification. Fast round reset retains these engine-owned slots. */
bool qa_q3_configstring_write(qa_q3_game *, uint32_t index,
                             const char *source_text, qa_error *);

#endif

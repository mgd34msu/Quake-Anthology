#ifndef QA_CONSOLE_SAVE_H
#define QA_CONSOLE_SAVE_H
#include "qa/console.h"
#include "qa/session.h"

typedef enum qa_console_save_identity {
    QA_CONSOLE_SAVE_OWNER,
    QA_CONSOLE_SAVE_CLIENT
} qa_console_save_identity;
typedef struct qa_console_save_resolvers {
    void *context;
    /* Includes retired identities: preserve their exclusion from new live
     * lifetime IDs. Zero remains the engine/absent identity. */
    bool (*identity)(void *, qa_console_save_identity, uint64_t saved,
                     uint64_t *restored, qa_error *);
    /* Actor is already remapped through the candidate session. Remap lifetime
     * and publication stamps only; retain dialect, origin, seat and script.
     * Callbacks prepare private candidate identity, never dispatch commands. */
    bool (*command_context)(void *, uint64_t captured_registry, const qa_command_context *saved,
                            qa_command_context *restored, qa_error *);
} qa_console_save_resolvers;

bool qa_console_save_capture(const qa_console *, qa_session *, qa_buffer *, qa_error *);
/* Rebinds actual command handlers from the candidate's existing registration
 * table. All decoded allocations are scratch-owned until the final exchange;
 * source dispatch, output and script callbacks never run. */
bool qa_console_save_restore(qa_console *, qa_session *, const qa_console_save_resolvers *,
                             qa_bytes, qa_error *);
#endif

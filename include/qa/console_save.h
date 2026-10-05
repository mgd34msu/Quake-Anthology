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
    /* Mutable aliases and queued commands retain their real Source owner.
     * Zero remains the engine/absent identity. */
    bool (*identity)(void *, qa_console_save_identity, uint64_t saved,
                     uint64_t *restored, qa_error *);
    /* Actor is already remapped through the candidate session. Remap lifetime
     * and publication stamps only; retain dialect, origin, seat and script.
     * Callbacks prepare private candidate identity, never dispatch commands. */
    bool (*command_context)(void *, uint64_t captured_registry, const qa_command_context *saved,
                            qa_command_context *restored, qa_error *);
} qa_console_save_resolvers;

bool qa_console_save_capture(const qa_console *, qa_session *, qa_buffer *, qa_error *);
/* The enclosing owner supplies its retained save namespace. Foreign and empty
 * command namespaces remain unchanged. */
uint64_t qa_console_save_context_registry(qa_session *, uint64_t registry, uint64_t captured_registry);
bool qa_console_save_capture_in_registry(const qa_console *, qa_session *, uint64_t captured_registry,
                                         bool releases, qa_buffer *, qa_error *);
/* Restore mutable aliases and queued commands. Ordinary constructors retain
 * ownership of callbacks, options and process-local lifetime exclusions. */
bool qa_console_save_restore(qa_console *, qa_session *, const qa_console_save_resolvers *,
                             qa_bytes, qa_error *);
/* Capture/import the actual returned release roster, its active queue and
 * displaced queue. Enclosing Source retirement owns the lifetime proof.
 * Imported programmes must be claimed by their decoded physical input owner
 * before finish; neither operation dispatches commands. */
bool qa_console_release_save_capture(const qa_console *,qa_session *,qa_buffer *,qa_error *);
bool qa_console_release_save_present(const qa_console *);
bool qa_console_release_save_restore(qa_console *,qa_session *,const qa_console_save_resolvers *,qa_bytes,qa_error *);
struct qa_console_release;
bool qa_console_release_save_reference(const qa_console *,const struct qa_console_release *,uint64_t *);
struct qa_console_release *qa_console_release_restore_reference(qa_console *,uint64_t);
bool qa_console_release_restore_claim(struct qa_console_release *,qa_error *);
bool qa_console_release_restore_finish(const qa_console *,qa_error *);
bool qa_console_release_restore_unclaimed(const qa_console *);
/* Candidate cleanup only: refuses input-claimed programmes. */
bool qa_console_release_restore_abort(qa_console *,qa_error *);
#endif

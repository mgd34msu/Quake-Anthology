#ifndef QA_CONSOLE_PROGRAM_H
#define QA_CONSOLE_PROGRAM_H
#include "qa/console.h"

typedef struct qa_console_program qa_console_program;
typedef enum qa_console_program_identity {
    QA_CONSOLE_PROGRAM_OWNER,
    QA_CONSOLE_PROGRAM_CLIENT
} qa_console_program_identity;
typedef struct qa_console_program_resolvers {
    void *context;
    /* Resolve actual candidate declarations, including retired lifetime IDs.
     * Zero retains its engine/absent meaning. These callbacks never dispatch. */
    bool (*identity)(void *, qa_console_program_identity, uint64_t source,
                      uint64_t *candidate, qa_error *);
    bool (*command_context)(void *, const qa_command_context *source,
                             qa_command_context *candidate, qa_error *);
    /* Bind actual published actor/publication stamps in already owned nodes.
     * Preserve the qualified candidate lifetime, origin, dialect and script;
     * this callback allocates nothing and performs no source effects. */
    bool (*published_context)(void *, const qa_command_context *source,
        const qa_command_context *prepared, qa_command_context *published, qa_error *);
    /* Qualify actual candidate Init work after publication, including the
     * newly installed controlled actor of its authored seat. No allocation. */
    bool (*published_candidate_context)(void *, const qa_command_context *captured,
        qa_command_context *published, qa_error *);
    /* Read only the actual retained source and prepared candidate descriptors. */
    bool (*current)(void *, const qa_console *source, const qa_console *candidate, qa_error *);
    /* Prove that this exact candidate has successfully published. The retained
     * source may now be historical; candidate contexts must be actually live. */
    bool (*published)(void *, const qa_command_context *source_basis,
                       const qa_console *candidate, qa_error *);
    /* Required only for a retained physical console. Prove actual precommit
     * restoration or entered publication; refusal keeps the ticket owned. */
    bool (*retained_abort)(void *, bool *restore_original, qa_error *);
} qa_console_program_resolvers;

/* Retain only the real command program. Candidate registrations, callbacks and
 * cvars stay with their fresh owner. The candidate must have no program state;
 * inherited aliases become visible before its cfg prefix, while the old tail
 * remains isolated and the published source program remains unchanged. Both
 * consoles stay borrowed until checked seal or abort releases the source loan;
 * the candidate loan lasts until checked adoption or abort. */
qa_console_program *qa_console_program_prepare(qa_console *source, qa_console *candidate,
    const qa_console_program_resolvers *, qa_error *);
/* Keep the genuine same physical console through publication. No cfg prefix,
 * alias installation, registration or cvar transfer occurs. The lifecycle
 * below qualifies its actual tail and binds the new published actor stamps. */
qa_console_program *qa_console_program_retain(qa_console *,
    const qa_console_program_resolvers *, qa_error *);
/* For a fresh candidate, wait for the actual cfg prefix to drain. A retained
 * owner has no prefix. Allocate and qualify the actual mapped tail, deferred
 * work, wait and script ancestry without executing a source command. */
bool qa_console_program_preflight(qa_console_program *, qa_error *);
bool qa_console_program_ready(const qa_console_program *, qa_error *);
/* The final unchanged check immediately before committed source teardown.
 * Restores the qualified continuation into the actual candidate before GAME
 * Init. A fresh pair releases its old console loan; a retained console keeps
 * its single physical owner loan. Init may append, insert and directly
 * execute its explicit text; draining waits for actual publication. Subsequent
 * operations use only the candidate owner. No allocation or dispatch. */
bool qa_console_program_seal(qa_console_program *, qa_error *);
/* Requires the actual successful publication. Bind surviving carried work and
 * qualify actual Init work without allocation, replay or dispatch, then free
 * the ticket. Init's real queue, wait and alias changes remain in their order.
 * A rejection retains the ticket and candidate owner for a checked abort. */
bool qa_console_program_adopt(qa_console_program *, qa_error *);
/* Before seal, release the unchanged owner loan. For a sealed retained owner,
 * restore its actual original nodes only with a proved precommit disposition
 * and unchanged staged program. Entered publication keeps its real current
 * work fenced for the application's fault disposition. Refusal keeps ownership. */
bool qa_console_program_abort(qa_console_program *, qa_error *);
#endif

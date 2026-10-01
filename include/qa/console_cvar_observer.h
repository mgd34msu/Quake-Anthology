#ifndef QA_CONSOLE_CVAR_OBSERVER_H
#define QA_CONSOLE_CVAR_OBSERVER_H
#include "qa/console.h"

typedef uint64_t qa_cvar_observer_token;
typedef bool (*qa_cvar_observer_fn)(void *, qa_cvars *, const char *, qa_error *);

/* Observe actual value publications, including accepted equal writes. Latch
 * staging, registration and metadata changes do not publish values. Events
 * retain their name and the registration-ordered observer inventory, never a
 * cvar view. Callbacks resolve current views afresh and may mutate the registry
 * after the enclosing operation has published all effects and metadata.
 * Nested publications append to the synchronous drain; destruction is fenced
 * until the whole drain returns. A false callback makes the originating bool
 * mutation fail after all admitted callbacks drain; published values remain.
 * The callback name is borrowed only until that callback returns. */
bool qa_cvars_observe(qa_cvars *, const char *, uint64_t owner,
    qa_cvar_observer_fn, void *, qa_cvar_observer_token *, qa_error *);
/* Tokens belong to one borrowed registry and are never reused there. Detach
 * during a post callback cancels its still-pending calls. Registry owner
 * retirement detaches that owner's observers before removing its variables. */
void qa_cvars_unobserve(qa_cvars *, qa_cvar_observer_token);
/* Suppression excludes this token at publication admission, so restoring it
 * before drain completion cannot resurrect refresh-generated events. */
bool qa_cvars_observer_suppress(qa_cvars *, qa_cvar_observer_token, bool, qa_error *);
/* Capture/restore and borrowed registry retirement require a complete drain. */
bool qa_cvars_observer_idle(const qa_cvars *);
#endif

#ifndef QA_INPUT_RELEASE_H
#define QA_INPUT_RELEASE_H

#include "qa/input.h"
#include "qa/console_release.h"

typedef struct qa_input_release qa_input_release;
typedef struct qa_input_release_scope {
    bool all;
    bool clear_gamepad;
    int32_t controller; /* -1 selects no controller. */
    const int *keys;
    size_t key_count;
} qa_input_release_scope;
typedef enum qa_input_release_outcome {
    QA_INPUT_RELEASE_UNENTERED,
    QA_INPUT_RELEASE_WAITING,
    QA_INPUT_RELEASE_COMPLETED,
    QA_INPUT_RELEASE_FAILED
} qa_input_release_outcome;
/* Parent lifetime/capture qualification. A retained release, including a
 * failed entered history, keeps the actual physical seat and source alive. */
bool qa_input_release_idle(const qa_input_seat *);
const qa_console *qa_input_release_console(const qa_input_release *);

/* Preparation retains the actual physical seat, held binding records and
 * captured source commands. It dispatches nothing. Keys are copied and the
 * seat remains leased until publication or checked abort. */
bool qa_input_release_prepare(qa_input_seat *, const qa_input_release_scope *,
    double time_ms, qa_input_release **, qa_error *);
/* Enters the requested logical holder/gamepad clear once, then executes the
 * retained source release program through the actual console.
 * A wait retains the ticket. An entered failure is an observable outcome;
 * it cannot be represented as an unentered preparation rollback. */
bool qa_input_release_advance(qa_input_release *, qa_input_release_outcome *, qa_error *);
/* Pure qualification against an actual native owner's retained route/key
 * requirements. Success requires completed source releases, the same seat
 * lease, and coverage of the requested physical inputs. */
bool qa_input_release_ready(const qa_input_release *, const qa_input_seat *,
    const qa_input_release_scope *, qa_error *);
/* Publication consumes only a successfully admitted ticket. Native output
 * and decoder ownership remains with the native platform publication. */
void qa_input_release_publish(qa_input_release *);
/* A refusing abort retains its ticket. Successful entered completion retires
 * the actual released held records on abort too, so later key-up cannot replay
 * them. An entered source failure refuses abort and retains its outcome. */
bool qa_input_release_abort(qa_input_release *, qa_input_release_outcome *, qa_error *);
/* Consumes retained source history only after the actual owner qualifies
 * every captured actor retirement or the exact detached source lifetime.
 * It dispatches no commands and cannot use a changed publication as evidence. */
bool qa_input_release_retirement_ready(const qa_input_release *, qa_console_release_disposition,
    qa_console_release_retirement_fn, void *, qa_error *);
/* The complete parent may preflight every retained history before consuming
 * any. Publication requires that admission and runs no source callbacks. */
void qa_input_release_retirement_publish(qa_input_release *);
bool qa_input_release_retire(qa_input_release *, qa_console_release_disposition,
    qa_console_release_retirement_fn, void *, qa_error *);

#endif

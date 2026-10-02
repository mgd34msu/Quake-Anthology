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
/* Only the exact currently advancing captured programme may route its
 * synchronous command into an enclosing prepared owner. */
bool qa_input_release_context_current(const qa_input_release *,const qa_console *,
                                      const qa_command_context *);
/* Borrowed real programme lease for another physical seat sharing this
 * returned console. NULL means this input owner retained no command record. */
const qa_console_release *qa_input_release_program_parent(const qa_input_release *);

/* Preparation retains the actual physical seat, held binding records and
 * captured source commands. It dispatches nothing. Keys are copied and the
 * seat remains leased until publication or checked abort. */
bool qa_input_release_prepare(qa_input_seat *, const qa_input_release_scope *,
    double time_ms, qa_input_release **, qa_error *);
bool qa_input_release_prepare_sibling(qa_input_seat *,const qa_input_release_scope *,
    double time_ms,const qa_console_release *,qa_input_release **,qa_error *);
/* Before actual ENGINE detach, capture only held rows outside the retained
 * scope and extend that same physical lease to ALL. Existing entered/failed
 * programmes remain unchanged. Failure leaves the old scope/history intact;
 * preparation dispatches no commands or logical clears. An optional actual
 * same-console parent admits capture while another physical seat retains its
 * returned wait; NULL uses this owner's captured programme when present. */
bool qa_input_release_extend_all(qa_input_release *,double time_ms,
    const qa_console_release *same_console_parent,qa_error *);
/* Capture uncovered held histories before scoped dispatch. They remain dormant:
 * normal advancement and successful abort leave those physical rows held. */
bool qa_input_release_reserve_all(qa_input_release *,const qa_console_release *,qa_error *);
bool qa_input_release_all_reserved_is(const qa_input_release *,const qa_input_seat *);
bool qa_input_release_failed_is(const qa_input_release *,const qa_input_seat *);
bool qa_input_release_waiting_is(const qa_input_release *,const qa_input_seat *);
/* Exact returned unentered capture with no selected or dormant held rows.
 * The enclosing final resource preparation must not dispatch source commands. */
bool qa_input_release_unentered_empty_is(const qa_input_release *,const qa_input_seat *);
/* Activate the captured dormant programmes for ordinary live ALL release.
 * Requires the actual fault-clear source context. Existing completed rows and
 * cursor are retained; advancement dispatches only the uncovered programmes.
 * Captured command timestamps stay in their original physical wall domain. */
bool qa_input_release_reserved_activate(qa_input_release *,qa_error *);
/* Qualify all active and dormant histories under real retirement authority.
 * After every physical seat passes, publish activates ALL metadata without
 * acquiring or dispatching any source programme. */
bool qa_input_release_reserved_retirement_ready(const qa_input_release *,
    qa_console_release_disposition,qa_console_release_retirement_fn,void *,qa_error *);
void qa_input_release_reserved_retirement_publish(qa_input_release *,double retirement_time_ms);
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
/* Pure actual completed records, returned programme leases and physical scope.
 * This does not call the source context qualifier or establish its currentness. */
bool qa_input_release_completed_is(const qa_input_release *,const qa_input_seat *,
    const qa_input_release_scope *);
/* Actual completed physical metadata with no authored or dormant programme.
 * Cancellation requires no source-current claim when there is no history. */
bool qa_input_release_completed_empty_is(const qa_input_release *,const qa_input_seat *,
    const qa_input_release_scope *);
/* Pure retained-owner association for enclosing shutdown admission. Checks
 * the exact physical lease, held snapshot and scope coverage at a returned
 * console boundary. It proves neither completion nor retirement authority. */
bool qa_input_release_scope_owned(const qa_input_release *, const qa_input_seat *,
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
/* Native retirement checks the exact retained seat, held snapshot and scope
 * before disposing an entered endpoint. Every captured history uses the
 * actual retirement qualifier; an empty physical scope has no source history
 * to qualify. This neither requires nor claims source command completion. */
bool qa_input_release_retirement_scope_ready(const qa_input_release *, const qa_input_seat *,
    const qa_input_release_scope *, qa_console_release_disposition,
    qa_console_release_retirement_fn, void *, qa_error *);
/* The complete parent may preflight every retained history before consuming
 * any. Publication requires that admission and runs no source callbacks. */
void qa_input_release_retirement_publish(qa_input_release *);
bool qa_input_release_retire(qa_input_release *, qa_console_release_disposition,
    qa_console_release_retirement_fn, void *, qa_error *);

#endif

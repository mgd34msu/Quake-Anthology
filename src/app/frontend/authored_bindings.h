#ifndef QA_FRONTEND_AUTHORED_BINDINGS_H
#define QA_FRONTEND_AUTHORED_BINDINGS_H
#include "qa/input.h"
#include "qa/inventory.h"
#include "qa/source_save.h"
#include "qa/settings.h"

typedef struct frontend_authored_bindings frontend_authored_bindings;

frontend_authored_bindings *frontend_authored_bindings_create(qa_error *);
void frontend_authored_bindings_destroy(frontend_authored_bindings *);
bool frontend_authored_bindings_clone(const frontend_authored_bindings *,
    frontend_authored_bindings **,qa_error *);
/* Seed from the selected source's real registered catalog before default.cfg
 * finishes. This retains metadata without changing the staged input seat. */
bool frontend_authored_bindings_seed(frontend_authored_bindings *,qa_console_dialect,
    qa_strings *,const qa_item_definition *,size_t,qa_error *);
/* First-seat default.cfg has finished. Its source weapon commands are replaced
 * by the selected arsenal; other authored defaults remain available to Reset. */
bool frontend_authored_bindings_defaults(frontend_authored_bindings *,qa_input_seat *,
    qa_console_dialect movement,qa_error *);
/* Secondary seats share authored Reset defaults, then run their own configs.
 * Their selected defaults merge into input at the first live selection. */
bool frontend_authored_bindings_secondary(frontend_authored_bindings *,
    const frontend_authored_bindings *primary,qa_input_seat *,qa_console_dialect,qa_error *);
/* At the changed-profile archive boundary, retain the same authored seat's
 * live choices and differences from its previous selected defaults. The new
 * authored/selected Reset metadata stays with the candidate. */
bool frontend_authored_bindings_restore_previous(frontend_authored_bindings *,
    const frontend_authored_bindings *,const qa_input_seat *,qa_input_seat *,qa_error *);
bool frontend_authored_bindings_profile(frontend_authored_bindings *,qa_input_seat *,
    const qa_seat_settings *,qa_error *);
/* Borrow the actual selected-default and override metadata for settings encode. */
void frontend_authored_bindings_archive(const frontend_authored_bindings *,qa_seat_settings *);
void frontend_authored_bindings_finish(frontend_authored_bindings *);
bool frontend_authored_bindings_ready(const frontend_authored_bindings *);
bool frontend_authored_bindings_completed(const frontend_authored_bindings *);
/* Called at the real admitted command boundary, before binding dispatch. */
bool frontend_authored_bindings_observe(frontend_authored_bindings *,
    const qa_command_invocation *,qa_error *);
/* Items come from the actual player's registered inventory definitions. The
 * donor's known slots apply only when that exact weapon ID is present. */
bool frontend_authored_bindings_select(frontend_authored_bindings *,qa_input_seat *,
    qa_console_dialect,qa_strings *,const qa_item_definition *,size_t,int32_t controller,qa_error *);
bool frontend_authored_bindings_reset(frontend_authored_bindings *,qa_input_seat *,
    int32_t controller,qa_error *);
/* Pure metadata continuation; never changes an input seat during decode. */
bool frontend_authored_bindings_fields(frontend_authored_bindings *,qa_source_save_io *);
#endif

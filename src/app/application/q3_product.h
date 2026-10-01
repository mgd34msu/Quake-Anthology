#ifndef QA_APPLICATION_Q3_PRODUCT_H
#define QA_APPLICATION_Q3_PRODUCT_H

#include "qa/q3_product_policy.h"
#include "qa/catalog.h"
#include "qa/launch.h"
#include "qa/console.h"
#include "qa/source_save.h"

/* Apply the genuine startup +set rows before registering the three INIT
 * variables. Other commands remain for the ordinary startup owner. */
bool application_q3_product_initial(qa_cvars *, const char *const *, size_t,
    qa_q3_product_policy *, qa_error *);
/* Pure restore admission. No productid read, directory listing or acquisition. */
bool application_q3_product_import(qa_cvars *, const qa_q3_product_policy *,
    qa_q3_product_policy *, qa_error *);
/* Fresh catalog only, before drafts/snapshots retain it. A non-Q3 selection
 * leaves Q3 media identification pending. Resolved policy is never reread. */
bool application_q3_product_prepare(qa_catalog *, qa_product_id,
    qa_q3_product_policy *, qa_error *);
bool application_q3_product_register_source(const qa_q3_product_policy *,
    qa_cvars *, uint64_t owner, qa_error *);
/* Candidate constructors register INIT from their genuine staged owner. */
const qa_q3_product_policy *application_q3_product_source_policy(const qa_application *);
struct application_provider;
bool application_startup_create(qa_application *, const char *const *, size_t, qa_error *);
void application_startup_dispose(qa_application *);
bool application_startup_seed_engine(qa_application *, qa_product_id, qa_error *);
bool application_startup_seed_source(struct application_provider *, qa_cvars *, qa_error *);
struct qa_application_startup_source;
bool application_startup_seed_console(struct application_provider *,const struct qa_application_startup_source *,
                                       const qa_command_context *,qa_error *);
bool application_startup_fields(qa_source_save_io *, qa_application *);
bool application_startup_program_queue_ready(qa_application *,const struct qa_application_startup_source *,
    const struct qa_application_startup_source *,uint64_t previous_generation,qa_error *);
void application_startup_program_queue_publish(qa_application *,const struct qa_application_startup_source *,
    const struct qa_application_startup_source *,uint64_t previous_generation);
bool qa_application_startup_command_queue(qa_application *, size_t, qa_console *, const qa_command_context *, qa_error *);
bool qa_application_startup_command_queued_console(qa_application *, size_t, qa_console **, qa_error *);
bool qa_application_startup_console_queued(const qa_application *, const qa_console *);
/* Primitive fields in the application's existing versioned owner checkpoint. */
bool application_q3_product_fields(qa_source_save_io *, qa_q3_product_policy *);
typedef struct application_q3_product_preparation {
    qa_catalog *catalog;
    qa_launch_draft *draft;
    qa_q3_product_policy policy;
} application_q3_product_preparation;
/* Complete the catalog admission and draft rebase before artifact preparation.
 * Finish publishes the catalog only after a successful configuration commit. */
bool application_q3_product_prepare_draft(qa_application *, const qa_launch_draft *,
    application_q3_product_preparation *, qa_error *);
void application_q3_product_finish(qa_application *, application_q3_product_preparation *, bool published);
bool application_q3_product_validate_draft(const qa_q3_product_policy *, const qa_launch_draft *, qa_error *);
bool application_q3_product_validate_snapshot(const qa_q3_product_policy *, const qa_launch_snapshot *, qa_error *);

#endif

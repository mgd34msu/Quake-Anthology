#ifndef QA_APPLICATION_SAVE_PRIVATE_H
#define QA_APPLICATION_SAVE_PRIVATE_H
#include "internal.h"
#include "qa/save.h"
#include "qa/persistence_application.h"
#include "qa/persistence_gameplay.h"
#include "qa/console_save.h"

typedef struct application_save_console_context {
    qa_application *application;
    uint64_t saved_command_generation;
} application_save_console_context;
bool application_save_console_resolvers(const application_save_console_context *,
                                        qa_console_save_resolvers *, qa_error *);

typedef struct application_save_foundation {
    qa_strings *strings;
    qa_actor_checkpoint actors;
    qa_session_checkpoint session;
    qa_world_checkpoint world;
} application_save_foundation;

/* Called under the application persistence lease. Each output is an owned,
 * concrete record codec; no borrowed mutable state escapes the call. */
bool application_save_foundation_capture(qa_application *, qa_buffer out[4], qa_error *);
bool application_save_foundation_decode(const qa_save_image *, application_save_foundation *, qa_error *);
void application_save_foundation_free(application_save_foundation *);
/* Root owner construction must call this before creating any shared gameplay
 * service. Strings transfer into candidate->session only after success. */
bool application_save_session_create(const qa_session_options *, const qa_save_image *, qa_session **, qa_error *);
bool application_create_restored(const qa_application_options *, const qa_save_image *,
    qa_application_content_graph **, qa_application **, qa_error *);
bool application_save_prepare_content(qa_application *, const qa_launch_snapshot *, const qa_save_image *, qa_error *);
bool application_save_resolvers(qa_application *, qa_persistence_gameplay_resolvers *, qa_error *);
/* Providers and source body/collision bindings must already be reconstructed.
 * This is a candidate-only operation. Any failure requires candidate disposal. */
bool application_save_foundation_finish(qa_application *candidate,
                                        const application_save_foundation *, qa_error *);

bool application_save_metadata_capture(qa_application *, qa_buffer *, qa_error *);
bool application_save_metadata_restore(qa_application *candidate, qa_bytes, qa_error *);
bool application_save_q3_product_decode(const qa_save_image *, qa_q3_product_policy *, qa_error *);
bool application_save_startup_decode(const qa_save_image *, qa_application *, qa_error *);
bool application_physics_capture(qa_application *, qa_buffer *, qa_error *);
bool application_physics_restore(qa_application *, qa_bytes, qa_error *);

bool application_save_configuration_capture(qa_application *, qa_buffer *, qa_error *);
bool application_save_configuration_decode(qa_application *candidate, qa_bytes, qa_launch_draft **, qa_error *);
bool application_save_configuration_validate(qa_application *candidate, qa_bytes, qa_error *);

bool application_map_checkpoint_capture(qa_application *, qa_buffer *, qa_error *);
bool application_map_checkpoint_restore(qa_application *, qa_bytes, qa_error *);

#endif

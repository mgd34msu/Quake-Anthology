#ifndef QA_APPLICATION_NATIVE_Q2_PRESENTATION_H
#define QA_APPLICATION_NATIVE_Q2_PRESENTATION_H

#include "qa/application.h"
#include "qa/game_q2.h"
#include "qa/native_host.h"

typedef enum qa_application_native_q2_source_kind {
    QA_APPLICATION_NATIVE_Q2_BUILTIN,
    QA_APPLICATION_NATIVE_Q2_ORIGINAL
} qa_application_native_q2_source_kind;

typedef struct qa_application_native_q2_presentation {
    qa_session *session;
    const qa_launch_snapshot *publication;
    const qa_launch_instance *launch;
    qa_vfs *content;
    qa_actor_owner source_owner;
    qa_product_id content_product;
    qa_q2_edition edition;
    qa_application_native_q2_source_kind kind;
    union {
        const qa_q2_game *game;
        struct {
            const qa_native_host *host;
            qa_native_profile profile;
        } original;
    } source;
    qa_clock_config clock_config;
    qa_clock_state clock;
    uint64_t server_time_ns;
    uint64_t publication_generation, map_revision;
    bool retained;
} qa_application_native_q2_presentation;

/* Borrows the completed physical ENTITIES source. These server timestamps
 * grant no client sample time, recipient visibility, or source execution.
 * A different primary family returns found=false and leaves out unchanged. */
bool qa_application_native_q2_presentation_selected(qa_application *,
    qa_application_native_q2_presentation *, bool *found, qa_error *);
/* Pure restore/capture observation under the actual retained content lease. */
bool qa_application_native_q2_presentation_retained_selected(qa_application *,
    qa_application_native_q2_presentation *, bool *found, qa_error *);
bool qa_application_native_q2_presentation_current(qa_application *,
    const qa_application_native_q2_presentation *);
/* Reads the emitting GAME's actual compiled rules or original API profile,
 * including during its source callback. This grants no transport dialect or
 * PresentationOwner. Other source families leave edition unchanged. */
bool qa_application_native_q2_source_profile_read(qa_application *, qa_actor_owner,
    qa_q2_edition *edition, bool *found, qa_error *);
bool qa_application_native_q2_source_clock_read(qa_application *, qa_actor_owner,
    qa_q2_edition *edition, uint64_t *interval_ns, bool *found, qa_error *);

#endif

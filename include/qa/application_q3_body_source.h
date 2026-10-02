#ifndef QA_APPLICATION_Q3_BODY_SOURCE_H
#define QA_APPLICATION_Q3_BODY_SOURCE_H

#include "qa/q3_host.h"

typedef enum qa_application_q3_body_part {
    QA_APPLICATION_Q3_BODY, QA_APPLICATION_Q3_BODY_LOWER,
    QA_APPLICATION_Q3_BODY_UPPER, QA_APPLICATION_Q3_BODY_HEAD
} qa_application_q3_body_part;

typedef struct qa_application_q3_body_draw {
    void *token;
    bool hide_active, capture_active;
} qa_application_q3_body_draw;

/* The frontend owns the actual entered Draw lease and its source namespace.
 * Preparation publishes any partial token before returning an error. Release
 * consumes that token; it also consumes successful inactive Draw leases.
 * Current is pure. Actor and submit qualify the same full actor generation.
 * Submit resolves the supplied real source handles in that lease's registry.
 * Helper is the original qualified player-to-mesh CALL instruction; repeated
 * material passes from that same helper retain the same identity. */
typedef struct qa_application_q3_body_services {
    void *context;
    bool (*prepare)(void *, qa_actor_owner receiver, uint32_t seat,
        qa_application_q3_body_draw *, qa_error *);
    bool (*current)(void *, const qa_application_q3_body_draw *);
    bool (*actor)(void *, const qa_application_q3_body_draw *, qa_actor_id,
        bool *hidden, bool *selected, qa_error *);
    bool (*submit)(void *, const qa_application_q3_body_draw *, qa_actor_id,
        qa_application_q3_body_part, uint32_t helper, const qa_q3_ref_entity *,
        bool base, bool *handled, qa_error *);
    void (*release_draw)(void *, qa_application_q3_body_draw *);
} qa_application_q3_body_services;

#endif

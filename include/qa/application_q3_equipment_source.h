#ifndef QA_APPLICATION_Q3_EQUIPMENT_SOURCE_H
#define QA_APPLICATION_Q3_EQUIPMENT_SOURCE_H

#include "qa/application_equipment.h"
#include "qa/q3_host.h"

typedef struct qa_application_q3_equipment_draw {
    qa_actor_id actor;
    qa_application_ammo_warning warning;
    bool selected, view_visible;
} qa_application_q3_equipment_draw;

/* The real client frontend lease owns the callback context. Preparation fills
 * this output before host construction; the role retains it through teardown. */
typedef struct qa_application_q3_equipment_services {
    void *context;
    bool (*prepare)(void *, qa_actor_owner receiver, uint32_t seat,
        qa_application_q3_equipment_draw *, qa_error *);
    bool (*current)(void *, const qa_application_q3_equipment_draw *);
    void (*release_draw)(void *);
    /* Selection is admitted only with physical replacement media and its
     * authored parent. An authored none declaration also admits selection. */
    bool (*held_begin)(void *, qa_actor_id, const qa_q3_ref_entity *,
        void **token, bool *selected, qa_error *);
    bool (*held_pass)(void *, void *token, const qa_q3_ref_entity *, qa_error *);
    bool (*held_submit)(void *, void *token, qa_error *);
    void (*held_release)(void *, void *token);
} qa_application_q3_equipment_services;

#endif

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
    /* Observes refs emitted inside the real declared weapon function while
     * Source proceeds unchanged. View is its actual player-state argument. */
    bool (*held_source)(void *, qa_actor_id, bool view, const qa_q3_ref_entity *, qa_error *);
} qa_application_q3_equipment_services;

/* Pure requests from the current actual CGAME owner, during RenderScene or
 * after Draw. No selection or media preparation is repeated here. */
bool qa_application_q3_equipment_requests(qa_application *, qa_actor_owner,
    uint32_t seat, bool *hud, bool *view, qa_error *);

#endif

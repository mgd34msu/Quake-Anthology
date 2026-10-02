#ifndef QA_APPLICATION_Q3_COMPONENTS_H
#define QA_APPLICATION_Q3_COMPONENTS_H

#include "qa/application.h"
#include "qa/application_q3_component_body.h"

typedef struct application_q3_scene application_q3_scene;
typedef struct qa_application_q3_component_draw {
    qa_actor_owner owner;
    uint64_t generation,sequence,frontend_identity;
    application_q3_scene *scene;
    application_q3_component_body *bodies;
    qa_q3_presentation_assets *assets;
    const qa_scene_frame *frame;
} qa_application_q3_component_draw;

/* Enumerates actual admitted external scene declarations, preserving their
 * retained application registration order. The caller supplies its entered
 * Source camera and clock; this never reads a primary CG or native clock. */
size_t qa_application_q3_component_scene_count(const qa_application *);
bool qa_application_q3_component_draw_prepare(qa_application *,size_t,uint32_t physical_seat,
    qa_actor_id viewer,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time_ms,
    int32_t elapsed_ms,uint64_t sequence,qa_application_q3_component_draw *,qa_error *);
bool qa_application_q3_component_draw_current(const qa_application *,const qa_application_q3_component_draw *);

#endif

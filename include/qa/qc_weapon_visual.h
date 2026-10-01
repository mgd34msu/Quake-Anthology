#ifndef QA_QC_WEAPON_VISUAL_H
#define QA_QC_WEAPON_VISUAL_H

#include "qa/qc.h"

typedef struct qa_qc_weapon_visual {
    const char *model;
    float frame;
    qa_vec3 punch_angle;
    bool has_model, has_frame, has_punch_angle;
} qa_qc_weapon_visual;

/* Borrows optional typed fields of the exact idle physical actor. Missing
 * definitions remain explicit. No projection or guest access callback runs. */
bool qa_qc_weapon_visual_read(const qa_qc_instance *, uint32_t physical_slot,
    qa_actor_id expected, qa_qc_weapon_visual *, qa_error *);

#endif

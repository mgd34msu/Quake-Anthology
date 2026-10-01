#ifndef QA_QC_VISUAL_H
#define QA_QC_VISUAL_H
#include "qa/qc.h"

typedef struct qa_qc_visual {
    const char *model;
    float model_index, frame, skin, colormap, effects, alpha, scale;
} qa_qc_visual;

/* Borrows the actual physical row's model string at an idle boundary. No
 * projection, guest access callback, allocation or source execution runs.
 * The full expected actor qualifies the retained OWNED or BORROWED binding. */
bool qa_qc_visual_read(const qa_qc_instance *, uint32_t slot, qa_actor_id expected,
    qa_qc_visual *, qa_error *);
#endif

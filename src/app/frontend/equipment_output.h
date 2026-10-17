#ifndef QA_FRONTEND_EQUIPMENT_OUTPUT_H
#define QA_FRONTEND_EQUIPMENT_OUTPUT_H

#include "equipment_media.h"

typedef struct frontend_equipment_output {
    qa_q3_ref_entity *refs, *passes;
    size_t count, capacity, base_count, pass_count, pass_capacity;
    qa_q3_presentation_assets *assets, *source_assets;
    qa_arena *storage;
    frontend_held_carrier carrier;
    void *context;
    bool (*current)(void *);
    void (*release)(void *);
    bool (*base_group)(void *,size_t *,qa_error *);
    bool view, source_style;
} frontend_equipment_output;

bool frontend_equipment_output_collect(frontend_equipment_output *,qa_q3_presentation_assets *,
    const qa_q3_ref_entity *,qa_error *);
void frontend_equipment_output_parent(frontend_equipment_output *,const qa_q3_ref_entity *,bool alpha);
size_t frontend_equipment_output_count(const frontend_equipment_output *);
bool frontend_equipment_output_source_style(frontend_equipment_output *,
    qa_q3_presentation_assets *,const qa_q3_ref_entity *,qa_error *);
bool frontend_equipment_output_source_pass(frontend_equipment_output *,const qa_q3_ref_entity *,qa_error *);
bool frontend_equipment_output_submit(frontend_equipment_output *,qa_q3_presentation *,qa_vec3 offset,
    const qa_q3_scene_options *,uint32_t first_order,qa_scene_frame *,qa_error *);
void frontend_equipment_output_destroy(frontend_equipment_output *);

#endif

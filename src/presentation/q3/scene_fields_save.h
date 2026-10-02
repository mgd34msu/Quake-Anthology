#ifndef QA_Q3_SCENE_FIELDS_SAVE_H
#define QA_Q3_SCENE_FIELDS_SAVE_H
#include "qa/q3_presentation.h"
#include "qa/source_save.h"
bool q3p_packet_entity_fields(qa_source_save_io *,qa_q3_ref_entity *);
bool q3p_packet_vertex_fields(qa_source_save_io *,qa_scene_vertex *);
bool q3p_packet_view_fields(qa_source_save_io *,qa_scene_view *);
bool q3p_packet_fog_fields(qa_source_save_io *,qa_scene_fog_volume *);
#endif

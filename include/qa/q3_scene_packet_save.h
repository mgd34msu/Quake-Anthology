#ifndef QA_Q3_SCENE_PACKET_SAVE_H
#define QA_Q3_SCENE_PACKET_SAVE_H
#include "qa/q3_presentation.h"
#include "qa/scene_frame_save.h"
typedef struct qa_q3_scene_packet_owner qa_q3_scene_packet_owner;
typedef struct qa_q3_scene_packet_view {
    qa_q3_refdef definition;
    qa_q3_scene_options options;
    const qa_q3_ref_entity *entities;
    size_t entity_count;
    const qa_q3_scene_polygon *polygons;
    size_t polygon_count;
    const qa_scene_vertex *vertices;
    size_t vertex_count;
    const qa_scene_light *lights;
    size_t light_count;
} qa_q3_scene_packet_view;
/* Retained, returned output only. Callback bindings belong to the genuine
 * caller and are installed after primitive and image namespace decoding. */
bool qa_q3_scene_packet_checkpoint(const qa_q3_scene_packet_view *,const qa_scene_frame_checkpoint_refs *,qa_buffer *,qa_error *);
bool qa_q3_scene_packet_restore(qa_bytes,const qa_scene_frame_checkpoint_refs *,qa_q3_scene_packet_owner **,qa_error *);
bool qa_q3_scene_packet_read(const qa_q3_scene_packet_owner *,qa_q3_scene_packet_view *);
void qa_q3_scene_packet_destroy(qa_q3_scene_packet_owner *);
#endif

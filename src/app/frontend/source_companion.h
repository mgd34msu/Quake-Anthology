#ifndef QA_FRONTEND_SOURCE_COMPANION_H
#define QA_FRONTEND_SOURCE_COMPANION_H
#include "internal.h"
#include "qa/application_q3_arsenal_client.h"
#include "qa/application_q3_equipment_source.h"
typedef struct frontend_source_companion_view {
    qa_application_q3_arsenal_client receipt;
    qa_q3_presentation_assets *assets;
    uint64_t frame_sequence;
    int32_t source_time_ms;
    size_t packet_count;
} frontend_source_companion_view;
typedef struct frontend_source_companion_packet {
    qa_q3_refdef definition;
    const qa_q3_ref_entity *entities;
    const qa_actor_id *entity_actors;
    const bool *entity_views;
    size_t entity_count;
    const qa_q3_scene_polygon *polygons;
    const qa_actor_id *polygon_actors;
    const bool *polygon_views;
    size_t polygon_count;
    const qa_scene_vertex *vertices;
    size_t vertex_count;
    const qa_scene_light *lights;
    const qa_actor_id *light_actors;
    const bool *light_views;
    size_t light_count;
    const qa_application_q3_equipment_source_weapon *weapons;
    size_t weapon_count;
} frontend_source_companion_packet;
typedef struct frontend_source_companion_ref {
    qa_q3_ref_entity ref;
    const qa_q3_refdef *definition;
    bool view;
} frontend_source_companion_ref;
typedef struct frontend_source_companion_polygon {
    qa_q3_scene_polygon polygon;
    const qa_scene_vertex *vertices;
    const qa_q3_refdef *definition;
    bool view;
} frontend_source_companion_polygon;
typedef struct frontend_source_companion_light {
    qa_scene_light light;
    const qa_q3_refdef *definition;
    bool view;
} frontend_source_companion_light;
typedef struct frontend_source_companion_selection {
    qa_actor_id actor;
    frontend_source_companion_ref *refs;
    frontend_source_companion_polygon *polygons;
    frontend_source_companion_light *lights;
    qa_application_q3_equipment_source_weapon *weapons;
    size_t ref_count, polygon_count, light_count, weapon_count;
} frontend_source_companion_selection;
bool frontend_source_companion_ref_place(qa_frontend *,qa_actor_id,const frontend_source_companion_ref *,const qa_scene_world_input *,qa_q3_ref_entity *,qa_error *);
bool frontend_source_companion_polygon_vertices(const frontend_source_companion_polygon *,const qa_scene_world_input *,
    qa_arena *,const qa_scene_vertex **,qa_error *);
bool frontend_source_companion_selection_read(qa_frontend *,const frontend_source_companion_view *,
    const frontend_source_companion_selection **,qa_error *);
bool frontend_source_companion_read(const qa_frontend *,uint32_t physical,qa_actor_id,
    frontend_source_companion_view *,bool *present,qa_error *);
bool frontend_source_companion_current(const qa_frontend *,const frontend_source_companion_view *);
bool frontend_source_companion_presentation_read(const qa_frontend *,const frontend_source_companion_view *,
    qa_q3_presentation **,qa_error *);

#endif

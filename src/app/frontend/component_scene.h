#ifndef QA_FRONTEND_COMPONENT_SCENE_H
#define QA_FRONTEND_COMPONENT_SCENE_H
#include "internal.h"
#include "material_movies.h"
#include "material_movies_save.h"
#include "../application/guest_q3_component_scene_factory.h"
bool frontend_component_scene_prepare(void *,const application_q3_component_scene_preparation *,qa_error *);
bool frontend_component_scenes_idle(const qa_frontend *);
typedef struct frontend_component_scene_view {
    uint64_t identity,sequence,generation,service_owner;
    application_q3_component_scene_origin origin;
    qa_executable_recipe *recipe;
    const qa_recipe_provider *recipe_provider;
    size_t packet_count;
    qa_actor_owner receiver;
    uint32_t physical_seat;
    qa_actor_id viewer;
    const qa_launch_instance *descriptor;
    qa_catalog *catalog;
    qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_media_library *media;
    frontend_material_movies *movies;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    const qa_scene_frame *frame;
    qa_audio_music *music;
    const char *music_intro,*music_loop;
    bool begun,music_attached,music_looping,music_pending;
} frontend_component_scene_view;
size_t frontend_component_scene_count(const qa_frontend *);
bool frontend_component_scene_read(const qa_frontend *,size_t,frontend_component_scene_view *,qa_error *);
bool frontend_component_scene_metadata_read(const qa_frontend *,size_t,frontend_component_scene_view *,qa_error *);
bool frontend_component_scene_metadata_current(const qa_frontend *,const frontend_component_scene_view *,qa_error *);
/* Install the genuine claimed graph VFS and decoded constructor policy before
 * the app's saved scene roster invokes its empty frontend factory. */
bool frontend_component_scene_restore_prepare(qa_frontend *,uint64_t identity,qa_vfs **claimed,
    const qa_q3_presentation_options *,qa_error *);
bool frontend_component_scene_restores_destroy(qa_frontend *,qa_error *);
struct frontend_component_scene_packet;
bool frontend_component_scene_restore_output(qa_frontend *,uint64_t identity,uint64_t sequence,bool begun,
    qa_bytes,const qa_scene_frame_checkpoint_refs *,qa_error *);
bool frontend_component_scene_restore_packet(qa_frontend *,uint64_t identity,
    const struct frontend_component_scene_packet *,qa_error *);
bool frontend_component_scene_restore_music(qa_frontend *,uint64_t identity,qa_audio_music **,
    bool attached,const char *intro,const char *loop,bool looping,bool pending,qa_error *);
bool frontend_component_scene_restore_movies(qa_frontend *,uint64_t identity,
    const frontend_material_movies_refs *,qa_bytes,qa_error *);
bool frontend_component_scenes_finish_restore(qa_frontend *,qa_error *);
bool frontend_component_scene_packet_count(const qa_frontend *,uint64_t identity,uint64_t sequence,size_t *,qa_error *);
typedef struct frontend_component_scene_packet {
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
} frontend_component_scene_packet;
bool frontend_component_scene_packet_read(const qa_frontend *,uint64_t identity,uint64_t sequence,
    size_t ordinal,frontend_component_scene_packet *,qa_error *);
#endif

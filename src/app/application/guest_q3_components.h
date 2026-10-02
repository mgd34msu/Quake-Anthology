#ifndef QA_APPLICATION_GUEST_Q3_COMPONENTS_H
#define QA_APPLICATION_GUEST_Q3_COMPONENTS_H

#include "internal.h"
#include "guest_q3_component.h"
#include "guest_q3_component_scene_factory.h"
#include "qa/network_unified.h"

typedef struct application_q3_components application_q3_components;
typedef struct application_q3_component_publication_lease application_q3_component_publication_lease;
typedef struct application_q3_components_options {
    application_q3_components *previous;
    qa_bytes saved;
    bool restoring;
    qa_application *application;
    const qa_launch_snapshot *snapshot;
    application_provider *const *providers;
    size_t provider_count;
    application_provider *world_source;
    qa_world *world;
    qa_bytes entity_text;
    application_q3_component_clients clients;
    void *context;
    bool (*match_read)(void *,qa_actor_id,qa_string_id *,double *,qa_error *);
    bool (*match_write)(void *,qa_actor_id,bool,qa_string_id,double,qa_error *);
    application_q3_component_scene_factory scene_factory;
    void *weapon_context;
    bool (*weapon_presented)(void *,qa_actor_id,bool *,qa_error *);
} application_q3_components_options;
typedef struct application_q3_component_publication {
    const qa_catalog_mod *metadata;
    qa_catalog *catalog;
    const qa_product *product;
    const qa_launch_instance *descriptor;
    qa_actor_owner owner;
    uint64_t generation;
    application_q3_component *game;
    application_q3_component_source *source;
    qa_vfs *content;
    const qa_resource *program,*declaration;
    qa_qvm_abi abi;
    const char *presentation_runtime;
    const qa_unified_document *identity;
} application_q3_component_publication;

/* The actual launch selection creates this roster before any Init. Every
 * entry owns a separate physical GAME executor and admitted SOURCE clock. */
bool application_q3_components_create(const application_q3_components_options *,application_q3_components **,qa_error *);
bool application_q3_components_prepare(application_q3_components *,qa_error *);
bool application_q3_components_commit(application_q3_components *,qa_error *);
bool application_q3_components_initialize(application_q3_components *,qa_error *);
bool application_q3_components_drain(application_q3_components *,qa_error *);
bool application_q3_components_checkpoint(qa_application *,qa_buffer *,qa_error *);
bool application_q3_components_finish_restore(application_q3_components *,qa_error *);
bool application_q3_components_adopt(qa_application *,application_q3_components **,qa_error *);
bool application_q3_components_idle(const application_q3_components *);
bool application_q3_components_destroy(application_q3_components **,qa_error *);
size_t application_q3_components_count(const qa_application *);
bool application_q3_components_at(qa_application *,size_t,application_q3_component **,qa_error *);
size_t application_q3_components_publication_count(const application_q3_components *);
bool application_q3_components_publication_at(application_q3_components *,size_t,application_q3_component_publication *,qa_error *);
bool application_q3_components_event_source_read(const qa_application *,qa_actor_owner,application_q3_component_publication *,qa_error *);
bool application_q3_components_publication_borrow(application_q3_components *,size_t,
    qa_actor_id,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time_ms,int32_t frame_ms,
    application_q3_component_publication_lease **,application_q3_scene_context *,qa_error *);
bool application_q3_components_publication_current(const application_q3_component_publication_lease *);
void application_q3_components_publication_return(application_q3_component_publication_lease **);
bool application_q3_components_admit(application_q3_components *,qa_actor_id,qa_error *);
bool application_q3_components_actor_released(application_q3_components *,qa_actor_record,qa_error *);
application_q3_component *application_q3_components_actor_owner(const qa_application *,qa_actor_id);
bool application_q3_components_command(qa_application *,qa_actor_id,
    const qa_unified_document *owner,uint64_t generation,const qa_command_invocation *,bool *,qa_error *);
bool application_q3_components_content_visit(const application_q3_components *,const qa_application_content_visitor *,qa_error *);
bool application_q3_components_scene_prepare(application_q3_components *,size_t,uint32_t,
    qa_actor_id,const qa_vec3 *,const qa_vec3[3],int32_t,int32_t,application_q3_scene **,qa_error *);
bool application_q3_components_scene_advance(application_q3_components *,size_t,uint32_t,
    qa_actor_id,const qa_vec3 *,const qa_vec3[3],int32_t,int32_t,uint64_t,
    application_q3_scene **,const qa_scene_frame **,qa_error *);

#endif

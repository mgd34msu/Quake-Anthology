#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_SOURCE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_SOURCE_H

#include "guest_q3_scene.h"

typedef struct application_q3_component_source application_q3_component_source;
typedef struct application_q3_component_source_options {
    qa_session *session;
    qa_actor_owner owner;
    uint64_t generation;
    qa_qvm *vm;
    qa_q3_host *host;
    const qa_qvm_image *image;
    qa_qvm_abi abi;
    void *context;
    bool (*current)(void *);
    qa_q3_visibility_world visibility;
    bool scene;
} application_q3_component_source_options;
typedef struct application_q3_component_view {
    application_q3_component_source *source;
    qa_actor_id viewer;
    qa_vec3 origin, axis[3];
    int32_t time_ms, frame_ms;
    bool (*weapon_presented)(void *,qa_actor_id,bool *,qa_error *);
    void *weapon_context;
} application_q3_component_view;

bool application_q3_component_source_create(const application_q3_component_source_options *,
    application_q3_component_source **,qa_error *);
/* The publication allocation precedes host construction so the actual GAME
 * configstring/command callbacks can borrow it. Attach the genuine executor
 * before its first declared Init; the unbound allocation cannot publish. */
bool application_q3_component_source_attach(application_q3_component_source *,qa_qvm *,qa_q3_host *,qa_error *);
bool application_q3_component_source_destroy(application_q3_component_source **,qa_error *);
bool application_q3_component_source_idle(const application_q3_component_source *);
/* Bind only after the original source actor/client admission has reached its
 * actual host row. Ownership and admitted-client flags belong to that caller. */
bool application_q3_component_source_bind(application_q3_component_source *,uint32_t,
    qa_actor_id,bool owned,bool admitted_client,qa_error *);
bool application_q3_component_source_release_actor(application_q3_component_source *,qa_actor_id,qa_error *);
/* Real GAME interval completion supplies its time. It never reads a CG clock. */
bool application_q3_component_source_publish(application_q3_component_source *,int32_t time_ms,bool baseline,qa_error *);
bool application_q3_component_source_configstring(void *,uint32_t,const char **,qa_error *);
bool application_q3_component_source_set_configstring(void *,uint32_t,const char *,qa_error *);
bool application_q3_component_source_command(void *,int32_t physical_client,const char *,qa_error *);
application_q3_scene_source application_q3_component_view_services(application_q3_component_view *);
bool application_q3_component_source_checkpoint(application_q3_component_source *,qa_buffer *,qa_error *);
bool application_q3_component_source_restore(application_q3_component_source *,qa_bytes,qa_error *);
bool application_q3_component_source_validate(application_q3_component_source *,qa_error *);
bool application_q3_component_source_continuation_read(const application_q3_component_source *,
    int64_t *game_state_revision,int32_t *command_sequence,int64_t *publication_revision,qa_error *);

#endif

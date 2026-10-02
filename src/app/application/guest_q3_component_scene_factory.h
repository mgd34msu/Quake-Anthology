#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_SCENE_FACTORY_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_SCENE_FACTORY_H

#include "guest_q3_component_source.h"
#include "qa/catalog.h"
#include "qa/executable_recipe.h"

typedef enum application_q3_component_scene_origin {
    APPLICATION_Q3_COMPONENT_SCENE_LOCAL,
    APPLICATION_Q3_COMPONENT_SCENE_REMOTE
} application_q3_component_scene_origin;

typedef struct application_q3_component_scene_frontend {
    void *owner;
    bool (*idle)(const void *);
    /* Called after the genuine scene/host child has released its borrow.
     * Refusal keeps the physical renderer/content parent in the roster. */
    bool (*destroy)(void **,qa_error *);
    bool (*begin)(void *,uint64_t,qa_error *);
    bool (*finish)(void *,bool,qa_error *);
    bool (*completed)(void *,uint64_t,const qa_scene_frame **,qa_error *);
    bool (*identity_read)(const void *,uint64_t *);
    /* Stops executable services after host return while retained scene-bank
     * cells still own this physical renderer and its private dictionaries. */
    bool (*retire)(void *,qa_error *);
} application_q3_component_scene_frontend;

typedef struct application_q3_component_scene_preparation {
    application_q3_component_scene_origin origin;
    const qa_catalog_mod *component;
    const qa_launch_instance *descriptor;
    /* Remote rows borrow the genuinely admitted immutable recipe. They have
     * no launch execution storage or local GAME component. */
    qa_executable_recipe *recipe;
    const qa_recipe_provider *recipe_provider;
    qa_catalog *catalog;
    qa_actor_owner owner;
    uint64_t generation, service_owner;
    uint64_t frontend_identity;
    uint32_t physical_seat;
    qa_actor_id viewer;
    qa_vfs *content;
    const qa_resource *artifact;
    const qa_vfs_acquisition *acquisition;
    const application_q3_scene_profile *profile;
    application_q3_scene_source source;
    qa_q3_host_options *host;
    qa_q3_presentation_assets **assets;
    application_q3_component_scene_frontend *frontend;
    bool restoring;
    bool retired;
    void *context;
    /* Pure owner identity, valid through constructor and checked retirement.
     * Source execution remains qualified by preparation.source separately. */
    bool (*retained)(void *);
    bool (*published)(void *);
    bool (*publication_read)(void *,application_q3_scene_context *);
} application_q3_component_scene_preparation;

typedef struct application_q3_component_scene_factory {
    void *context;
    /* Successful host construction consumes host.frontend_lifetime. On an
     * earlier failure the roster calls host.release_frontend itself. */
    bool (*prepare)(void *, const application_q3_component_scene_preparation *, qa_error *);
} application_q3_component_scene_factory;

#endif

#ifndef QA_APPLICATION_GUEST_Q3_COMPONENTS_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENTS_PRIVATE_H
#include "guest_q3_components.h"
#include "guest_q3_mod_operations.h"
#include "qa/collision.h"
#include <stdlib.h>
#include <string.h>

typedef struct component_scene_row component_scene_row;
typedef struct component_game_row {
    application_q3_components *roster;
    application_provider *provider;
    qa_launch_instance_lease *metadata_lease;
    application_q3_component_publication publication;
    qa_resource *program,*declaration;
    qa_vfs_acquisition program_acquisition,declaration_acquisition;
    qa_qvm_image *image;
    qa_component participant;
    qa_component_admission *admission;
    uint64_t services;
    char *presentation_runtime;
    qa_unified_document *identity;
    component_scene_row *scenes;
    bool attached,initialized,activated,registered;
} component_game_row;
struct application_q3_components {
    application_q3_components_options options;
    component_game_row **rows;
    bool *retained;
    application_q3_components *retired;
    size_t count;
    bool closing;
    qa_error visibility_failure;
};
bool q3components_storage(void *);
bool q3components_current(void *);
bool q3components_create_game(component_game_row *,qa_error *);
bool q3components_scenes_destroy(component_game_row *,qa_error *);
bool q3components_scenes_idle(const component_game_row *);
bool q3components_identity(component_game_row *,qa_error *);
#endif

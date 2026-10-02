#ifndef QA_APPLICATION_GUEST_Q3_SCENE_H
#define QA_APPLICATION_GUEST_Q3_SCENE_H

#include "guest_q3_scene_profile.h"
#include "guest_q3_component_body.h"
#include "qa/q3_presentation.h"

typedef struct application_q3_scene application_q3_scene;
typedef struct application_q3_scene_actor { uint32_t slot; qa_actor_id actor; bool owned; } application_q3_scene_actor;
typedef struct application_q3_scene_command { int32_t sequence; const char *text; bool addressed; } application_q3_scene_command;
typedef struct application_q3_scene_context {
    uint64_t generation;
    int64_t revision, game_state_revision;
    int32_t time_ms, frame_ms, client_number;
    bool baseline;
    bool has_weapon_presented,weapon_presented;
    qa_vec3 origin, axis[3];
    const qa_q3_gamestate *game_state;
    const qa_q3_snapshot *snapshot;
    const application_q3_scene_actor *actors;
    size_t actor_count;
    const application_q3_scene_command *commands;
    size_t command_count;
} application_q3_scene_context;
typedef struct application_q3_scene_source {
    void *context;
    /* Acquire retains the actual source publication while guest calls run.
     * It supplies the real per-viewer PVS snapshot and all source actor rows. */
    bool (*acquire)(void *, bool baseline, application_q3_scene_context *, qa_error *);
    bool (*current)(void *, const application_q3_scene_context *);
    bool (*actor)(void *, uint32_t slot, qa_actor_id *, bool *owned, bool *found, qa_error *);
    bool (*live)(void *, qa_actor_id);
    void (*release)(void *, const application_q3_scene_context *);
    bool (*weapon_presented)(void *, qa_actor_id viewer, bool *, qa_error *);
} application_q3_scene_source;
typedef struct application_q3_scene_options {
    const application_q3_scene_profile *profile;
    qa_q3_host_options host;
    qa_q3_presentation_assets *assets;
    qa_actor_id viewer;
    application_q3_scene_source source;
    void *output_context;
    bool (*finish_output)(void *,bool,qa_error *);
} application_q3_scene_options;

bool application_q3_scene_create(const application_q3_scene_options *, bool restoring,
    application_q3_scene **, qa_error *);
bool application_q3_scene_initialize(application_q3_scene *, qa_error *);
bool application_q3_scene_advance(application_q3_scene *, uint64_t sequence, qa_error *);
bool application_q3_scene_hud(application_q3_scene *, uint64_t sequence, qa_error *);
bool application_q3_scene_console(application_q3_scene *, const qa_command_tokens *, bool *, qa_error *);
bool application_q3_scene_idle(const application_q3_scene *);
bool application_q3_scene_destroy(application_q3_scene **, qa_error *);
application_q3_component_body *application_q3_scene_bodies(application_q3_scene *);
bool application_q3_scene_entered_context(const application_q3_scene *,application_q3_scene_context *);
bool application_q3_scene_checkpoint(application_q3_scene *, qa_buffer *, qa_error *);
bool application_q3_scene_restore(application_q3_scene *, qa_bytes, qa_error *);
/* After the candidate's actual source, cvar namespaces and world graph have
 * been installed, publish the host continuation without Init or source calls. */
bool application_q3_scene_finish_restore(application_q3_scene *, qa_error *);

#endif

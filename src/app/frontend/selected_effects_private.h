#ifndef QA_FRONTEND_SELECTED_EFFECTS_PRIVATE_H
#define QA_FRONTEND_SELECTED_EFFECTS_PRIVATE_H

#include "selected_effects.h"
#include "qa/catalog_save.h"

typedef struct frontend_effects_primary {
    const q3n_frame *native;
    const struct frontend_source_effects *source;
    qa_q3_presentation *presentation;
    uint32_t physical_seat;
    qa_ui_preferences preferences;
} frontend_effects_primary;

struct frontend_selected_effects_group {
    struct frontend_selected_effects_group *next;
    frontend_selected_effects *owner;
    frontend_selected_effects_view view;
    qa_launch_instance_lease *launch_lease;
    const qa_launch_instance *launch;
    const qa_q3_presentation *primary;
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    uint64_t publication_generation, map_revision;
    uint64_t prepared_application_frame;
    uint64_t event_generation, event_cursor, audio_bus;
    int32_t time, previous_time;
    bool sampled, prepared, restoring;
    const qa_application_selected_effects *active_source;
    const qa_application_effect_event *active_event;
    const frontend_selected_effects_pose *active_pose;
    const frontend_effects_primary *active_primary;
    frontend_selected_effects_ref *refs;
    size_t ref_count, ref_capacity;
};
struct frontend_selected_effects {
    qa_frontend *frontend;
    frontend_selected_effects_group *groups, *tail;
    bool busy;
};

bool frontend_selected_effects_constructor(qa_frontend *, const frontend_effects_primary *,
    const qa_application_selected_effects *, const qa_application_effect_event *,
    frontend_selected_effects_group **, qa_error *);
bool frontend_selected_effects_group_current(const frontend_selected_effects_group *,
    const qa_application_selected_effects *);
bool frontend_selected_effects_group_dispose(frontend_selected_effects_group *, qa_error *);
bool frontend_selected_effects_empty(frontend_selected_effects_group *,
    const qa_q3_presentation_binding *, qa_error *);
bool frontend_selected_effects_parent_resolve(const qa_frontend *,
    frontend_selected_effects_parent_kind, size_t, qa_q3_presentation **, qa_error *);

#endif

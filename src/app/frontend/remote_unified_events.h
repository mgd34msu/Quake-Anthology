#ifndef QA_FRONTEND_REMOTE_UNIFIED_EVENTS_H
#define QA_FRONTEND_REMOTE_UNIFIED_EVENTS_H
#include "remote_unified_media.h"
#include "qa/source_save.h"
#include "qa/unified_frame_events.h"
#include "qa/unified_frame_components.h"

typedef struct frontend_unified_events frontend_unified_events;
typedef struct frontend_unified_event_options {
    uint64_t audio_owner;
    void *context;
    /* Source-specific records enter the retained CLIENT presentation owner.
     * Validation must not execute an event or acquire live renderer state. */
    bool (*presentation_validate)(void *, const qa_unified_presentation_event *, qa_error *);
    bool (*simulation_validate)(void *, const qa_unified_simulation_event *, qa_error *);
    bool (*presentation)(void *, const qa_unified_presentation_event *, bool *mirrored, qa_error *);
    bool (*simulation)(void *, const qa_unified_simulation_event *, qa_error *);
    bool (*audio_actor)(void *, qa_actor_id, uint64_t *, qa_error *);
} frontend_unified_event_options;

bool frontend_unified_events_create(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, const frontend_unified_event_options *, frontend_unified_events **, qa_error *);
/* Reliable controls are completely parsed and retained before return. They
 * may arrive before the first frame. Resource declarations precede sounds. */
bool frontend_unified_events_control(frontend_unified_events *, const qa_unified_document *, qa_error *);
/* Literal reliable component metadata is retained independently of the live
 * component VM. Retired tokens remain available to pending historical events. */
bool frontend_unified_events_component_admit(frontend_unified_events *, const qa_unified_component_owner *, const char *content, qa_error *);
bool frontend_unified_events_component_admit_created(frontend_unified_events *, const qa_unified_component_owner *, const char *content, bool *created, qa_error *);
bool frontend_unified_events_component_cancel(frontend_unified_events *, const qa_unified_component_owner *, qa_error *);
bool frontend_unified_events_component_retire(frontend_unified_events *, const qa_unified_component_owner *, qa_error *);
bool frontend_unified_events_component_current(const frontend_unified_events *, const qa_unified_component_owner *, const char *content, bool *active, qa_error *);
bool frontend_unified_events_frame_prepare(frontend_unified_events *, const qa_unified_document *, qa_error *);
bool frontend_unified_events_frame_ready(frontend_unified_events *, const qa_unified_document *, qa_error *);
void frontend_unified_events_frame_commit(frontend_unified_events *);
void frontend_unified_events_frame_abort(frontend_unified_events *);
/* Executes only records released by an actually committed frame. Successful
 * records advance their own cursor immediately; refusal retains the remainder. */
bool frontend_unified_events_enter(frontend_unified_events *, qa_error *);
bool frontend_unified_events_draw(frontend_unified_events *, qa_scene_rect, bool center_owned, qa_scene_frame *, qa_error *);
bool frontend_unified_events_idle(const frontend_unified_events *);
bool frontend_unified_events_destroy(frontend_unified_events **, qa_error *);
bool frontend_unified_events_sound_mirrored(frontend_unified_events *, const qa_unified_presentation_event *, bool *, qa_error *);
bool frontend_unified_events_sound_path(frontend_unified_events *, const char *content, const char *path,
    qa_actor_id, qa_vec3, double milliseconds, int32_t channel, float volume, float attenuation,
    double delay_seconds, qa_error *);
bool frontend_unified_events_sound_loop_path(frontend_unified_events *, const char *, const char *,
    qa_actor_id, qa_vec3 origin, qa_vec3 velocity, double milliseconds, int32_t channel,
    float volume, float attenuation, int32_t frame_number, bool persistent, qa_error *);
bool frontend_unified_events_sound_stop_loop(frontend_unified_events *, qa_actor_id, qa_error *);
uint64_t frontend_unified_events_audio_owner(const frontend_unified_events *);
bool frontend_unified_events_audio_actor(frontend_unified_events *, qa_actor_id, uint64_t *, qa_error *);
bool frontend_unified_events_checkpoint_ready(const frontend_unified_events *);
#endif

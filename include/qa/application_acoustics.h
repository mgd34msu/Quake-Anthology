#ifndef QA_APPLICATION_ACOUSTICS_H
#define QA_APPLICATION_ACOUSTICS_H
#include "qa/application.h"
#include "qa/audio.h"

typedef struct qa_application_acoustics qa_application_acoustics;
typedef struct qa_application_acoustics_view {
    qa_application *application;
    qa_world *world;
    qa_collision_geometry *geometry;
    const qa_resource *map_resource;
    uint64_t map_revision;
} qa_application_acoustics_view;
/* Owns a structural hold on the actual installed shared world and its map
 * bytes, including a genuine completely absent initial world. Constructors
 * for private Source/decoded remote worlds retain their own parent instead. */
bool qa_application_acoustics_hold(qa_application *, qa_application_acoustics **, qa_error *);
bool qa_application_acoustics_current(const qa_application_acoustics *);
bool qa_application_acoustics_read(const qa_application_acoustics *,
    qa_application_acoustics_view *, qa_error *);
/* Full actor is the actual listener identity resolved by its audio owner;
 * zero means the admitted listener genuinely has no actor. Trace includes
 * real linked collision/body rows and preserves source callback errors. */
bool qa_application_acoustics_trace(qa_application_acoustics *, qa_actor_id pass_actor,
    qa_vec3 start, qa_vec3 end, qa_audio_trace_hit *, qa_error *);
/* Pure structural drop. Do not release from inside this receipt's trace. */
void qa_application_acoustics_release(qa_application_acoustics *);
#endif

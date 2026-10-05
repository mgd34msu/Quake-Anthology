#ifndef QA_Q3_PRESENTATION_SAVE_H
#define QA_Q3_PRESENTATION_SAVE_H
#include "qa/q3_presentation.h"
typedef struct qa_q3_presentation_binding {
    qa_q3_presentation_options options;
    qa_scene_frame *frame;
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    qa_bytes entities;
} qa_q3_presentation_binding;
/* Borrow the actual installed policy and map/frame aliases under the outer
 * registry/scene lease, before any component codec starts. No callback runs. */
bool qa_q3_presentation_binding_read(const qa_q3_presentation *, qa_q3_presentation_binding *, qa_error *);
/* Borrow the installed map/frame aliases only inside this presentation's
 * actual submit_view callback, using the exact active options and frame. */
bool qa_q3_presentation_selected_binding_read(const qa_q3_presentation *,
    const qa_q3_scene_options *, const qa_scene_frame *, qa_q3_presentation_binding *, qa_error *);
/* The isolated empty registry and presentation attach already imported map
 * owners and exact retained entity bytes. This does not load/publish a world,
 * reset a parser, register an asset, submit a frame or dispatch a callback. */
bool qa_q3_presentation_prepare_restored(qa_q3_presentation *, qa_scene_frame *,
    qa_scene_world *, qa_collision_geometry *, qa_bytes entities, qa_error *);
#endif

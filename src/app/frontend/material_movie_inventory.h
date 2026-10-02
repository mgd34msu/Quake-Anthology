#ifndef QA_FRONTEND_MATERIAL_MOVIE_INVENTORY_H
#define QA_FRONTEND_MATERIAL_MOVIE_INVENTORY_H
#include "material_movie_bindings.h"
#include "scene_identity.h"
#include "qa/audio_save.h"
#include "q3_inventory.h"

/* Q3 dictionaries own their parent caches. Visual providers own their distinct
 * caches here; every actual shader-movie owner owns its playback once. */
bool frontend_material_movie_inventory_checkpoint(qa_frontend *,frontend_scene_namespace *,
    const qa_scene_frame_checkpoint_refs *,const qa_audio_checkpoint_refs *,frontend_q3_inventory *,qa_buffer *,qa_error *);
/* Shared images/materials/frame and Q3 caches precede this import. All rows and
 * retained resources decode before any cache or playback mutation. */
bool frontend_material_movie_inventory_restore(qa_frontend *,frontend_scene_namespace *,
    const qa_scene_frame_checkpoint_refs *,const qa_audio_checkpoint_refs *,frontend_q3_inventory *,qa_bytes,qa_error *);
/* Unified family topology needs its real bank caches before the audio-engine
 * import. Other playback owners remain in the ordinary late import. */
bool frontend_material_movie_inventory_restore_unified(qa_frontend *,frontend_scene_namespace *,
    const qa_scene_frame_checkpoint_refs *,const qa_audio_checkpoint_refs *,frontend_q3_inventory *,qa_bytes,qa_error *);
#endif

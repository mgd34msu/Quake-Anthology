#ifndef QA_FRONTEND_SAVE_PRIVATE_H
#define QA_FRONTEND_SAVE_PRIVATE_H
#include "internal.h"
#include "qa/source_save.h"
#include "qa/persistence_fields.h"
#include "qa/audio_save.h"
#include "qa/audio_bank_graph_save.h"
#include "scene_identity.h"
bool frontend_save_provider(qa_source_save_io *, qa_application *, qa_actor_owner *);
bool frontend_save_random(qa_source_save_io *, qa_builtin_random *);
bool frontend_save_q2_event(qa_source_save_io *, qa_q2_map_event *);
bool frontend_save_text(qa_source_save_io *, char **);
bool frontend_particle_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
bool frontend_particle_restore(qa_frontend *, qa_bytes, qa_error *);
bool frontend_event_checkpoint(qa_frontend *, const qa_audio_asset_inventory *, frontend_scene_identity_scope *, qa_buffer *, qa_error *);
bool frontend_event_restore(qa_frontend *, const qa_audio_asset_inventory *, frontend_scene_identity_scope *, qa_bytes, qa_error *);
#endif

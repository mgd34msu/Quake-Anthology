#ifndef QA_FRONTEND_REMOTE_UNIFIED_PRESENTATION_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_PRESENTATION_SAVE_H
#include "remote_unified_presentation.h"
#include "remote_unified_media_save.h"
#include "remote_unified_render_save.h"
#include "remote_unified_components_save.h"
#include "unified_q3_runtime_factory.h"

typedef struct frontend_unified_presentation_refs {
    frontend_unified_media_refs media, pending_media;
    frontend_unified_render_refs render;
    frontend_unified_event_refs events;
    frontend_unified_q1_refs q1;
    frontend_unified_q2_refs q2;
    frontend_unified_components_refs components;
    void *factory_context;
    bool (*factory_refs)(void *,frontend_remote_unified *,size_t,bool importing,
        const q3n_client_refs *,frontend_unified_q3_runtime_factory_refs *,qa_error *);
} frontend_unified_presentation_refs;

/* The actual shared graph producer supplies the physical dictionaries. */
bool frontend_remote_unified_presentation_checkpoint(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_buffer *,qa_error *);
/* Replica identities/recipes precede this whole-envelope decode and detached
 * media prefix. Shared renderer/audio dictionaries import before roots/finish. */
bool frontend_remote_unified_presentation_restore_prepare(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_bytes,qa_error *);
bool frontend_remote_unified_presentation_restore_roots(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_error *);
/* Private component topology claims its genuine frontend identity before the
 * enclosing shared renderer dictionaries import. */
bool frontend_remote_unified_presentation_restore_components(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_error *);
/* Image/model/audio-asset dictionaries precede this import; actual group music
 * buses are then available to the enclosing audio-engine namespace decoder. */
bool frontend_remote_unified_presentation_restore_audio_prefix(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_error *);
bool frontend_remote_unified_presentation_restore_factories(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_error *);
bool frontend_remote_unified_presentation_restore_activate(frontend_remote_unified *,qa_error *);
bool frontend_remote_unified_presentation_restore_finish(frontend_remote_unified *,
    const frontend_unified_presentation_refs *,qa_error *);
bool frontend_remote_unified_presentation_restore_ready(const frontend_remote_unified *,qa_error *);
/* Actual enclosing publication transfers restored audio route custody. */
void frontend_remote_unified_presentation_adopt(frontend_remote_unified *);
#endif

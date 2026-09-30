#ifndef QA_FRONTEND_AUDIO_INVENTORY_H
#define QA_FRONTEND_AUDIO_INVENTORY_H
#include "internal.h"
#include "qa/audio_bank_graph_save.h"
typedef struct frontend_event_audio_owner_view {
    qa_actor_owner owner;
    qa_audio_family family;
    qa_vfs *files;
    qa_audio_bank *sounds;
} frontend_event_audio_owner_view;
size_t frontend_event_audio_owner_count(const qa_frontend *);
bool frontend_event_audio_owner_read(const qa_frontend *, size_t,
    frontend_event_audio_owner_view *);
/* One entry per real retained event reference; caller frees the borrowed
 * pointer array. No registration, event projection or refcount change runs. */
bool frontend_event_audio_assets_read(const qa_frontend *, qa_audio_asset ***,
    size_t *, qa_error *);
#endif

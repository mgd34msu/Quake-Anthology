#ifndef QA_FRONTEND_NATIVE_Q2_SAVE_H
#define QA_FRONTEND_NATIVE_Q2_SAVE_H
#include "qa/frontend.h"
#include "qa/native.h"
#include "qa/font.h"
#include "qa/audio.h"

typedef struct frontend_native_q2_owner_view {
    qa_actor_owner owner;
    qa_native_profile profile;
    uint64_t identity;
    qa_vfs *mounts;
    const qa_vfs *provider_files;
    qa_scene_resources *images;
    qa_audio_bank *sounds;
    qa_font_library *fonts;
    bool prepared;
} frontend_native_q2_owner_view;
/* Physical saved lease order includes GAME leases whose resource heaps have
 * not been created. Native factories consume exact owner/profile/provider-view
 * rows while the enclosing frontend constructor policy is active. */
bool frontend_native_q2_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_native_q2_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_native_q2_topology_ready(const qa_frontend *, qa_error *);
void frontend_native_q2_topology_finish(qa_frontend *);
/* Retire real application leases first. Pending constructors and consumed
 * application leases retain their real views/heaps while a child is busy;
 * this destructor retries those actual unbound rows before unlinking them. */
bool frontend_native_q2_discard_unbound(qa_frontend *, qa_error *);
size_t frontend_native_q2_owner_count(const qa_frontend *);
bool frontend_native_q2_owner_read(const qa_frontend *, size_t,
    frontend_native_q2_owner_view *);
/* Pure asset provenance lookup during the real voice observer boundary;
 * does not require idle guest imports or advance their source clocks. */
bool frontend_native_q2_audio_view(const qa_frontend *,const qa_audio_asset *,qa_vfs **);
/* Pure physical child admission also works after the application lease retires.
 * Nullable heaps are observed directly, including their held capture callbacks. */
bool frontend_native_q2_children_idle(const qa_frontend *);
/* A Q3 cut requires Q3 character ownership. Original Q2 cgame imports require
 * a native KEX character producer, so installed cgame leases cannot join it. */
bool frontend_native_q2_q3_round_ready(const qa_frontend *, qa_error *);
/* Global image/font/audio families restore the actual heaps exposed above.
 * This section then imports true GAME lease clocks, classic-font selection and
 * retained world text. CGAME guest allocations/catalogs need a qualified full
 * original module producer and are explicitly unadmitted here. */
bool frontend_native_q2_private_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_native_q2_private_restore(qa_frontend *, qa_bytes, qa_error *);
#endif

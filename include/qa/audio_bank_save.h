#ifndef QA_AUDIO_BANK_SAVE_H
#define QA_AUDIO_BANK_SAVE_H
#include "qa/audio.h"

typedef struct qa_audio_bank_checkpoint_refs {
    void *context;
    bool (*view_encode)(void *, const qa_vfs *, uint64_t *, qa_error *);
    bool (*view_decode)(void *, uint64_t, const qa_vfs **, qa_error *);
    bool (*view_retain)(void *, uint64_t, qa_vfs **, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, uint64_t, const qa_resource **, qa_error *);
} qa_audio_bank_checkpoint_refs;
/* The external slots enumerate every owned asset reference outside this
 * bank, including pruned assets. Repeated slots preserve aliases; NULL is a
 * real empty holder. PCM aliases across these assets are retained. Callers
 * serialize mutation and supply read-only content identity resolvers.
 * view_retain gives the actual restored asset constructor one owned VFS
 * reference to the same view returned by the borrowed identity resolver. */
bool qa_audio_bank_checkpoint(const qa_audio_bank *, qa_audio_asset *const *, size_t,
    const qa_audio_bank_checkpoint_refs *, qa_buffer *empty, qa_error *);
/* Restore into an empty installed bank and empty external holder slots. Its
 * exact borrowed view must already be restored. Source bytes are decoded only
 * to qualify immutable PCM; no registration, playback or VFS acquisition runs.
 * Failure preserves the bank and all external slots. Success gives one owned
 * asset reference to each nonempty slot. */
bool qa_audio_bank_restore(qa_audio_bank *, qa_audio_asset **, size_t,
    const qa_audio_bank_checkpoint_refs *, qa_bytes, qa_error *);
#endif

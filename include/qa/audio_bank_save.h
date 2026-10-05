#ifndef QA_AUDIO_BANK_SAVE_H
#define QA_AUDIO_BANK_SAVE_H
#include "qa/audio.h"

/* Actual bank-graph identity resolvers. view_retain gives the restored asset
 * one owned VFS reference to the same view returned by view_decode. */
typedef struct qa_audio_bank_checkpoint_refs {
    void *context;
    bool (*view_encode)(void *, const qa_vfs *, uint64_t *, qa_error *);
    bool (*view_decode)(void *, uint64_t, const qa_vfs **, qa_error *);
    bool (*view_retain)(void *, uint64_t, qa_vfs **, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, uint64_t, const qa_resource **, qa_error *);
} qa_audio_bank_checkpoint_refs;
#endif

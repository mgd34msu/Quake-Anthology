#ifndef QA_FRONTEND_AUDIO_IDENTITY_SAVE_H
#define QA_FRONTEND_AUDIO_IDENTITY_SAVE_H
#include "internal.h"

/* The stable frontend actor table precedes every engine/event audio holder.
 * Restored logical IDs remain exact, while full actor generations resolve
 * through the candidate actor provenance table. No actor identity is minted. */
bool frontend_audio_id_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
bool frontend_audio_id_restore(qa_frontend *, qa_bytes, qa_error *);
bool frontend_audio_id_read(const qa_frontend *, uint64_t, qa_actor_id *, bool *retired);
bool frontend_audio_id_encode(void *, uint64_t, qa_buffer *, qa_error *);
bool frontend_audio_id_decode(void *, qa_bytes, uint64_t *, qa_error *);
#endif

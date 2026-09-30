#ifndef QA_FRONTEND_CONTENT_REFS_H
#define QA_FRONTEND_CONTENT_REFS_H
#include "qa/audio_bank_save.h"
#include "qa/persistence_content.h"
/* Borrows the actual application content graph only for its capture/candidate
 * lease. Returned callbacks never acquire content or add owner references. */
qa_audio_bank_checkpoint_refs frontend_audio_content_refs(qa_application_content_graph *);
#endif

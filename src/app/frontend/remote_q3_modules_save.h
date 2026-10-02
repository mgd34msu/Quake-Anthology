#ifndef QA_FRONTEND_REMOTE_Q3_MODULES_SAVE_H
#define QA_FRONTEND_REMOTE_Q3_MODULES_SAVE_H
#include "remote_q3_modules.h"
#include "qa/q3_presentation_media_save.h"

typedef struct frontend_remote_q3_modules_save_refs {
    void *context;
    bool (*movies)(void *, const frontend_remote_q3_module_topology *,
        qa_q3_movie_checkpoint_refs *, qa_error *);
} frontend_remote_q3_modules_save_refs;

bool frontend_remote_q3_modules_checkpoint(const frontend_remote_q3_modules *,
    const frontend_remote_q3_modules_save_refs *, qa_buffer *, qa_error *);
/* Parent dictionaries and map aliases precede host import. Numeric Q3AS,
 * audio buses and private role continuations follow this empty construction. */
bool frontend_remote_q3_modules_restore_decoded(frontend_remote_q3 *, qa_bytes wrapper,
    qa_bytes modules, frontend_remote_q3_modules **, qa_error *);
bool frontend_remote_q3_modules_restore_initial(qa_frontend *, frontend_remote_q3_initial *,
    qa_bytes wrapper, qa_bytes modules, frontend_remote_q3_modules **, qa_error *);
bool frontend_remote_q3_modules_restore_continuation(frontend_remote_q3_modules *,
    const frontend_remote_q3_modules_save_refs *, qa_error *);
bool frontend_remote_q3_modules_finish_restore(frontend_remote_q3_modules *,
    const frontend_remote_q3_modules_save_refs *, qa_error *);
#endif

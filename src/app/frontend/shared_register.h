#ifndef QA_FRONTEND_SHARED_REGISTER_H
#define QA_FRONTEND_SHARED_REGISTER_H
#include "internal.h"
/* The factory supplies its actual source dialect and native output defaults.
 * Register on the one ENGINE heap before configuration or pure QACV import. */
bool frontend_shared_register(qa_cvars *,qa_console_dialect,
    qa_audio_output_format,float gamma,qa_error *);
#endif

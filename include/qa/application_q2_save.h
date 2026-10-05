#ifndef QA_APPLICATION_Q2_SAVE_H
#define QA_APPLICATION_Q2_SAVE_H
#include "qa/application.h"
#include "qa/q2_save.h"
#include "qa/save.h"
#include "qa/network_q2_session.h"

/* Original engine directory values, with GAME-owned files. The caller owns
 * the returned value and writes it through qa_q2_save_directory_write. */
bool qa_application_q2_save_capture(qa_application *, qa_save_purpose,
    const qa_q2_config_entry *, size_t, qa_q2_save_data **, qa_error *);
/* The selected product is resolved once from the real server header/catalog.
 * Import creates only a fresh normal application; its retained file values
 * survive startup waits and real physical client admission. */
bool qa_application_q2_save_import_ready(const qa_application *, const qa_q2_save_data *,
    const char *product, qa_error *);
bool qa_application_q2_save_import(qa_application *, const qa_q2_save_data *,
    const char *product, qa_error *);
bool qa_application_q2_save_import_advance(qa_application *, bool *complete, qa_error *);
#endif

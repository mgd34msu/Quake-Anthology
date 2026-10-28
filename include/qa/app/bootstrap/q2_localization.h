/* Port of src/app/bootstrap/q2-localization.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_LOCALIZATION_H
#define QA_APP_BOOTSTRAP_Q2_LOCALIZATION_H
#include <stddef.h>
#include "qa/text/localization.h"
/* Malloc'd result; caller frees. NULL only on OOM. */
char *qa_app_q2_localized_text(qa_localization_table *catalog, const char *text,
    const char *const *args, size_t arg_count);
#endif

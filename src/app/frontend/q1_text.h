#ifndef QA_FRONTEND_Q1_TEXT_H
#define QA_FRONTEND_Q1_TEXT_H
#include "qa/common.h"
/* Classic source fallback after the actual source catalog misses its key.
 * Arguments already contain the genuine event's string/number values. */
bool frontend_q1_classic_text(const char *,const char *const *,size_t,char *,size_t,qa_error *);
#endif

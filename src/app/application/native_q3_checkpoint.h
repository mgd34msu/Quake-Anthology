#ifndef QA_APPLICATION_NATIVE_Q3_CHECKPOINT_H
#define QA_APPLICATION_NATIVE_Q3_CHECKPOINT_H
#include "qa/save.h"
struct application_provider;
bool application_native_q3_checkpoint_prepare(struct application_provider *, const qa_save_record *, qa_error *);
#endif

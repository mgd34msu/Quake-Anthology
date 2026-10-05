#ifndef APPLICATION_QC_ORIGINAL_SAVE_H
#define APPLICATION_QC_ORIGINAL_SAVE_H
#include "internal.h"
#include "qa/q1_save.h"
typedef struct application_q1_original_admission {
    const qa_product *product;
    uint64_t initial_ns;
} application_q1_original_admission;
extern const uint8_t application_q1_original_constructor[4];
bool application_q1_original_admit(const qa_application *,const qa_q1_save_data *,
    const char *product,application_q1_original_admission *,qa_error *);
bool application_q1_original_clock(const application_provider *,uint64_t *);
void application_q1_original_dispose(qa_application *);
#endif

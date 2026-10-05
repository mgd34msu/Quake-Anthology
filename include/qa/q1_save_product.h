#ifndef QA_Q1_SAVE_PRODUCT_H
#define QA_Q1_SAVE_PRODUCT_H
#include "qa/q1_save.h"
#include "qa/catalog.h"
/* Qualify the actual edition, installed map and saved Source definitions.
 * Ambiguous or unavailable content is rejected without guessing a product. */
bool qa_q1_save_select_product(const qa_catalog *,const qa_q1_save_data *,
    const char *path,const qa_product **,qa_error *);
bool qa_q2_save_select_product(const qa_catalog *,const qa_q2_save_data *,const qa_product **,qa_error *);
#endif

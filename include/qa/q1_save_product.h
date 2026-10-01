#ifndef QA_Q1_SAVE_PRODUCT_H
#define QA_Q1_SAVE_PRODUCT_H
#include "qa/q1_save.h"
#include "qa/catalog.h"
/* Installed map ownership, v6 game-directory suffix, then an explicit product
 * or unique path-directory context qualify a product. Ambiguous v5 content
 * requires an explicit selection. The returned product borrows the catalog. */
bool qa_q1_save_select_product(const qa_catalog *,const qa_q1_save_data *,
    const char *path,const char *selected,const qa_product **,qa_error *);
#endif

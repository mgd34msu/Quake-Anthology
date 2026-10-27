/* Port of src/network/q3/state/product.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_STATE_PRODUCT_H
#define QA_NETWORK_Q3_STATE_PRODUCT_H
#include <stdbool.h>
/* TS Product = "baseq3" | "missionpack". */
typedef enum qa_q3_product { QA_Q3_PRODUCT_BASEQ3, QA_Q3_PRODUCT_MISSIONPACK } qa_q3_product;
const char *qa_q3_product_name(qa_q3_product product);
bool qa_q3_product_parse(const char *text, qa_q3_product *out);
#endif

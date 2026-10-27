/* Port of src/network/q3/state/product.ts. GPL-2.0-or-later. */
#include "qa/network/q3/state/product.h"
#include <string.h>
const char *qa_q3_product_name(qa_q3_product product) {
  return product == QA_Q3_PRODUCT_MISSIONPACK ? "missionpack" : "baseq3";
}
bool qa_q3_product_parse(const char *text, qa_q3_product *out) {
  if (text == NULL) return false;
  if (strcmp(text, "baseq3") == 0) { *out = QA_Q3_PRODUCT_BASEQ3; return true; }
  if (strcmp(text, "missionpack") == 0) { *out = QA_Q3_PRODUCT_MISSIONPACK; return true; }
  return false;
}

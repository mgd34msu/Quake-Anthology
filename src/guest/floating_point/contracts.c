/* Port of src/guest/floating-point/contracts.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/contracts.h"

const char *qa_numeric_result_kind_name(qa_numeric_result_kind kind) {
  switch (kind) {
    case QA_NUMERIC_EXECUTED: return "executed";
    case QA_NUMERIC_UNSUPPORTED: return "unsupported";
    case QA_NUMERIC_EXCEPTION: return "exception";
  }
  return "";
}

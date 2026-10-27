/* Port of src/network/q2/errors.ts. GPL-2.0-or-later. */
#include "qa/network/q2/errors.h"

#include "qa/network/q2/constants.h"

noreturn void qa_q2_com_error(int code, const char *message) {
  qa_q2_com_error_throw(code, "%s", message);
}

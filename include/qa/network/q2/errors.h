/* Port of src/network/q2/errors.ts (TS Com_Error). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q2_ERRORS_H
#define QA_NETWORK_Q2_ERRORS_H

#include <stdnoreturn.h>

/* TS Com_Error(code, message): throws ComError. */
noreturn void qa_q2_com_error(int code, const char *message);

#endif

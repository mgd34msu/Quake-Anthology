/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_TEXT_H
#define QA_TEXT_H

#include "qa/common.h"

/* Locale-independent Unicode lowercase for source-defined name matching.
 * Output is owned UTF-8 with a trailing NUL outside the counted size. */
bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error);

#endif

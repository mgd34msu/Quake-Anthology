/*
 * Port of src/input/impulse.ts: Q_atoi and IN_Impulse from Quake
 * I/II cl_input.c; MSG_WriteByte emits the low byte.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_IMPULSE_H
#define QA_INPUT_IMPULSE_H

#include "qa/contracts/common.h"

double qa_parse_impulse(const char *text, qa_command_dialect dialect);

#endif

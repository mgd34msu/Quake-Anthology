/* Port of src/bots/navigation/aas-write.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_WRITE_H
#define QA_BOTS_NAVIGATION_AAS_WRITE_H
#include "qa/bots/navigation/aas.h"
#include <stddef.h>
#include <stdint.h>
/* Malloc'd bytes, caller frees; NULL only on OOM. Validates by re-parsing. */
uint8_t *qa_write_aas(const qa_aas_asset *world, size_t *out_length);
#endif

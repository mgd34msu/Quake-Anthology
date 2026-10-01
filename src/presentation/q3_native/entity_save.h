#ifndef QA_Q3_NATIVE_ENTITY_SAVE_H
#define QA_Q3_NATIVE_ENTITY_SAVE_H
#include "entity.h"
#include "qa/source_save.h"

/* Field kernel only. The containing local GAME or remote received owner
 * qualifies its actual row, actor provenance and aggregate version. */
bool q3n_entity_codec(qa_source_save_io *, q3n_entity *);
#endif

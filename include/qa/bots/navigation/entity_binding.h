/* Port of src/bots/navigation/entity-binding.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_ENTITY_BINDING_H
#define QA_BOTS_NAVIGATION_ENTITY_BINDING_H
#include "qa/contracts/math.h"
#include <stdbool.h>
bool qa_source_mover_bounds_match(qa_bounds authored, qa_bounds linked, qa_vec3 origin,
                                  qa_vec3 endpoint);
#endif

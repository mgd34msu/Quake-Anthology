#ifndef QA_APPLICATION_WORLD_BOUNDS_H
#define QA_APPLICATION_WORLD_BOUNDS_H
#include "qa/world.h"
typedef struct qa_application qa_application;
/* The supplied application owns this world and its actor/body namespace.
 * Isolated native reconstruction supplies its scratch application. */
qa_world_hooks application_world_hooks(qa_application *);
#endif

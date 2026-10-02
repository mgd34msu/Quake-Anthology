#ifndef QA_NATIVE_RESOURCE_INVENTORY_H
#define QA_NATIVE_RESOURCE_INVENTORY_H
#include "qa/native_process_resources.h"
typedef struct qa_native_resource_inventory qa_native_resource_inventory;
/* Historical native capability graph; checked release retains a refused row. */
bool qa_native_resource_inventory_release(qa_native_resource_inventory **,qa_error *);
#endif

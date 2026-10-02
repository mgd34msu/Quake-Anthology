#ifndef QA_FRONTEND_NATIVE_RESOURCE_INVENTORY_H
#define QA_FRONTEND_NATIVE_RESOURCE_INVENTORY_H
#include "qa/native_resource_inventory.h"
#include "qa/persistence_application.h"
typedef struct frontend_native_resource_context {
    qa_native_resource_inventory *captured;
    const qa_save_image *image;
} frontend_native_resource_context;
qa_application_native_resource_refs frontend_native_resource_refs(frontend_native_resource_context *);
#endif

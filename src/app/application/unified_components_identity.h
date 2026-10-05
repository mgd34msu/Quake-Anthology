#ifndef QA_APPLICATION_UNIFIED_COMPONENTS_IDENTITY_H
#define QA_APPLICATION_UNIFIED_COMPONENTS_IDENTITY_H
#include "qa/catalog.h"
#include "qa/network_unified_components.h"
/* Derives one owned tuple from the actual retained discovered declaration. */
bool application_unified_component_identity_create(const qa_catalog_mod *,const qa_product *,
    const char *instance,qa_unified_component_identity *,qa_error *);
#endif

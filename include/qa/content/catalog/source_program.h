/* Port of src/content/catalog/source-program.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_CATALOG_SOURCE_PROGRAM_H
#define QA_CONTENT_CATALOG_SOURCE_PROGRAM_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"

/* TS sourceProgramProduct. */
const qa_catalog_discovered_product *qa_source_program_product(
    const qa_content_catalog *catalog, const char *content);

/* TS sourceProgramImplementation: malloc'd provider id. */
char *qa_source_program_implementation(
    const qa_catalog_product_expectation *product);

/* TS selectedSourceProgram over {execution, map.entities}: returns
 * a malloc'd campaign string, or NULL for the TS undefined (also
 * NULL on OOM; *out_found tells them apart when non-NULL). */
char *qa_selected_source_program(const qa_execution_selection *execution,
                                 size_t execution_count,
                                 qa_provider_reference entities,
                                 bool *out_found);

#endif

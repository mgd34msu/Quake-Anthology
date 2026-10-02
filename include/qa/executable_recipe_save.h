#ifndef QA_EXECUTABLE_RECIPE_SAVE_H
#define QA_EXECUTABLE_RECIPE_SAVE_H
#include "qa/executable_recipe.h"
#include "qa/persistence_content.h"
/* Retained recipe graph capture: catalogs, private lookup views, policy
 * snapshots and immutable issued acquisition owners are already enumerated. */
bool qa_executable_recipe_checkpoint(const qa_executable_recipe *,
    qa_application_content_graph *, qa_buffer *, qa_error *);
/* Claims the actual restored graph. Imports pure launch/descriptor metadata
 * and saved collision continuation; never opens a catalog/path or prepares
 * an offer, Source application, provider, or renderer. */
bool qa_executable_recipe_restore(qa_application_content_graph *, qa_bytes,
    qa_executable_recipe **, qa_error *);
#endif

#ifndef QA_FRONTEND_NETWORK_Q1_RECIPE_SAVE_H
#define QA_FRONTEND_NETWORK_Q1_RECIPE_SAVE_H
#include "network_q1_restore.h"

bool frontend_network_q1_recipe_checkpoint(const frontend_network_q1_client_recipe *,qa_buffer *,qa_error *);
/* On success, the recipe owns its NQ declaration strings. No graph or runtime
 * identity is resolved here; the enclosing stage qualifies those receipts. */
bool frontend_network_q1_recipe_restore(qa_bytes,frontend_network_q1_client_recipe *,qa_error *);
/* Consumes only a successfully decoded recipe, never a borrowed capture. */
void frontend_network_q1_recipe_free(frontend_network_q1_client_recipe *);
#endif

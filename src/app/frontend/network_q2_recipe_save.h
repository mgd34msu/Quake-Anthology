#ifndef QA_FRONTEND_NETWORK_Q2_RECIPE_SAVE_H
#define QA_FRONTEND_NETWORK_Q2_RECIPE_SAVE_H
#include "network_q2_client.h"

/* Only constructor/request/policy value fields are encoded. The enclosing
 * graph owns Source descriptors, app actors and all child buffers. */
bool frontend_network_q2_recipe_checkpoint(const frontend_network_q2_client_state *,qa_buffer *,qa_error *);
bool frontend_network_q2_recipe_restore(qa_bytes,frontend_network_q2_client_state *,qa_error *);
#endif

/* Port of src/llm/responses.ts: readResponses over consumeSse.
 * GPL-2.0-or-later. */
#ifndef QA_LLM_RESPONSES_H
#define QA_LLM_RESPONSES_H

#include "qa/llm/request.h"

bool qa_llm_read_responses(const qa_llm_transport_request *input, qa_llm_http_response *response,
                           const char *label, bool require_content_type, char **out_text, qa_llm_error *error);

#endif

/* Port of src/llm/api.ts: Chat Completions and OpenAI Responses
 * transports (async becomes synchronous). GPL-2.0-or-later. */
#ifndef QA_LLM_API_H
#define QA_LLM_API_H

#include "qa/llm/request.h"

typedef struct qa_llm_api_credential {
  const char *api_key;
  const char *base_url;
} qa_llm_api_credential;

bool qa_llm_request_chat_completions(const qa_llm_transport_request *input, const qa_llm_api_credential *credential,
                                     qa_llm_fetch_ref fetcher, char **out_text, qa_llm_error *error);
bool qa_llm_request_open_ai_responses(const qa_llm_transport_request *input, const char *api_key,
                                      qa_llm_fetch_ref fetcher, char **out_text, qa_llm_error *error);

#endif

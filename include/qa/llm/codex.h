/* Port of src/llm/codex.ts: ChatGPT subscription (Codex) Responses
 * transport, account-id extraction and the prompt cache key.
 * GPL-2.0-or-later. */
#ifndef QA_LLM_CODEX_H
#define QA_LLM_CODEX_H

#include "qa/llm/auth.h"

/* subscriptionAccountId(token). Caller frees *out_id. */
bool qa_llm_subscription_account_id(const char *token, char **out_id, qa_llm_error *error);
/* promptCacheKey(model). Caller frees. */
char *qa_llm_prompt_cache_key(const char *model);
bool qa_llm_request_codex(const qa_llm_transport_request *input, const qa_llm_subscription_credential *credential,
                          qa_llm_fetch_ref fetcher, char **out_text, qa_llm_error *error);

#endif

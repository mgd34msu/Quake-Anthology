#include "qa/platform_services.h"
#include "qa/network_unified_session.h"

bool qa_unified_token_random(qa_unified_token *out, qa_error *e)
{
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing production nonce output"); return false; }
    qa_unified_token token = {{0}};
    if (!qa_platform_entropy(token.bytes,sizeof(token.bytes),e)) return false;
    *out = token;
    return true;
}

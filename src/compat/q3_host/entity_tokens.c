#include "internal.h"

bool qa_q3_host_set_entity_text(qa_q3_host *host, qa_bytes text, qa_error *error)
{
    if (!host || host->retired || host->calls)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 entity text requires an idle live host");
    qa_common_cursor cursor;
    if (!qa_common_cursor_init(&cursor, text, QA_COMMON_TERMINATED, error)) return false;
    host->entity_cursor = cursor; qa_common_parser_reset(&host->entity_parser); return true;
}

q3_service_result q3_entity_tokens(q3_call *call, int32_t *result, qa_error *error)
{
    qa_qvm_role role = call->host->options.role;
    if (!((role == QA_QVM_GAME && call->service == 37) ||
          (role == QA_QVM_CGAME && call->service == 86))) return Q3_UNHANDLED;
    qa_q3_host *host = call->host;
    if (!qa_common_parse(&host->entity_parser, &host->entity_cursor, true, error)) return Q3_FAILED;
    bool found = !host->entity_cursor.ended || host->entity_parser.token_length != 0;
    if (!q3_write_string(call, call->arguments[0], host->entity_parser.token, q3_integer(call, 1), error)) return Q3_FAILED;
    *result = found; return Q3_COMPLETED;
}

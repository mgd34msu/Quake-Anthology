#ifndef QA_CONSOLE_INTERNAL_H
#define QA_CONSOLE_INTERNAL_H

#include "qa/console.h"
#include "qa/command_lexer.h"
#include "qa/text.h"

bool qac_fail(qa_error *error, qa_status code, const char *message);
bool qac_dialect_valid(qa_ruleset_id dialect);
bool qac_q1(qa_ruleset_id dialect);
bool qac_q2(qa_ruleset_id dialect);
char *qac_copy(const char *text, qa_error *error);
char *qac_copy_n(const char *text, size_t length, qa_error *error);
int32_t qac_integer(const char *value);
float qac_number(const char *value, qa_ruleset_id dialect);
void qac_document_free(const qa_console_documentation *);
bool qac_document_replace(const qa_console_documentation **, const qa_console_documentation *, qa_error *);

#endif

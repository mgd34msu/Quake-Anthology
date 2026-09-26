#ifndef QA_CONSOLE_INTERNAL_H
#define QA_CONSOLE_INTERNAL_H

#include "qa/console.h"

typedef struct qac_text {
    char *data;
    size_t size;
    size_t capacity;
} qac_text;

typedef struct qac_token {
    size_t start;
    size_t size;
    size_t end;
    bool found;
} qac_token;

bool qac_fail(qa_error *error, qa_status code, const char *message);
bool qac_dialect_valid(qa_console_dialect dialect);
bool qac_q1(qa_console_dialect dialect);
bool qac_q2(qa_console_dialect dialect);
bool qac_equal(const char *left, const char *right);
char *qac_copy(const char *text, qa_error *error);
char *qac_copy_n(const char *text, size_t length, qa_error *error);
bool qac_text_add(qac_text *text, const char *data, size_t length, qa_error *error);
bool qac_text_string(qac_text *text, const char *value, qa_error *error);
bool qac_text_finish(qac_text *text, qa_buffer *out, qa_error *error);
bool qac_space(unsigned char byte, bool console_text);
bool qac_parse_token(const char *text, size_t length, size_t start,
                      qa_console_dialect dialect, bool console_text,
                      qac_token *out, qa_error *error);
int32_t qac_integer(const char *value);
float qac_number(const char *value, qa_console_dialect dialect);

#endif

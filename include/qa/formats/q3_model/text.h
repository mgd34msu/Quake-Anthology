/*
 * Port of src/formats/q3-model/text.ts. ModelTextError aborts printing
 * "ModelTextError: <source>:<offset>: <message>". The generic
 * indexedRecords becomes an element-size callback form.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MODEL_TEXT_H
#define QA_FORMATS_Q3_MODEL_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

#include "qa/contracts/math.h"

typedef struct qa_model_tokens {
  const char *text;   /* borrowed */
  size_t length;
  const char *source; /* borrowed */
  size_t offset;
} qa_model_tokens;

noreturn void qa_model_text_error(const char *source, size_t offset, const char *message);

void qa_model_tokens_init(qa_model_tokens *tokens, const char *text, const char *source);
noreturn void qa_model_tokens_fail(const qa_model_tokens *tokens, const char *message);
/* Malloc'd token, NULL at end (TS null). */
char *qa_model_tokens_next(qa_model_tokens *tokens);
/* Malloc'd token; aborts at end. */
char *qa_model_tokens_token(qa_model_tokens *tokens);
void qa_model_tokens_expect(qa_model_tokens *tokens, const char *expected);
double qa_model_tokens_number(qa_model_tokens *tokens);
double qa_model_tokens_float(qa_model_tokens *tokens);
double qa_model_tokens_integer(qa_model_tokens *tokens, double minimum, double maximum);
qa_vec3 qa_model_tokens_vector(qa_model_tokens *tokens);
void qa_model_tokens_end(qa_model_tokens *tokens);

/* TS at(): RangeError "Missing <label> <index>" when outside. */
size_t qa_q3_model_at(double index, size_t count, const char *label);

typedef void (*qa_q3_record_read_fn)(void *ctx, void *out_element);
/* Malloc'd array of count elements read via indexed records. */
void *qa_q3_indexed_records(qa_model_tokens *tokens, size_t count, const char *kind, size_t elem_size,
                            qa_q3_record_read_fn read_record, void *ctx);

#endif

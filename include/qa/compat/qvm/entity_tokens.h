/* Port of src/compat/qvm/entity-tokens.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_ENTITY_TOKENS_H
#define QA_COMPAT_QVM_ENTITY_TOKENS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/core/common_parse.h"
#include "qa/persistence/value.h"

typedef struct qa_qvm_entity_token_result {
  const char *token; /* borrowed (parser storage) */
  bool ended;
} qa_qvm_entity_token_result;

typedef struct qa_qvm_entity_token_services {
  void *ctx;
  qa_qvm_entity_token_result (*entity_token)(void *ctx);
} qa_qvm_entity_token_services;

typedef struct qa_qvm_entity_tokens {
  qa_common_parse_cursor cursor;
  qa_common_parse_state parser;
} qa_qvm_entity_tokens;

/* False only on OOM. */
bool qa_qvm_entity_tokens_init(qa_qvm_entity_tokens *tokens, const char *source, size_t length);
void qa_qvm_entity_tokens_destroy(qa_qvm_entity_tokens *tokens);
qa_qvm_entity_token_result qa_qvm_entity_tokens_entity_token(qa_qvm_entity_tokens *tokens);
qa_qvm_entity_token_services qa_qvm_entity_tokens_as_services(qa_qvm_entity_tokens *tokens);
/* Owned checkpoint tree {source,cursor,parser}; NULL only on OOM. */
qa_save_value *qa_qvm_entity_tokens_capture(const qa_qvm_entity_tokens *tokens);
/* `value` NULL is the TS undefined case. */
void qa_qvm_entity_tokens_restore(qa_qvm_entity_tokens *tokens, const qa_save_value *value);

/* TS null return becomes handled == false. */
bool qa_qvm_entity_token_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_entity_token_services services,
                                 qa_qvm_host_result *out);

#endif

/* Port of src/compat/qvm/entity-tokens.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/entity_tokens.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/foundation/support.h"

bool qa_qvm_entity_tokens_init(qa_qvm_entity_tokens *tokens, const char *source, size_t length) {
  if (!qa_common_parse_cursor_init(&tokens->cursor, source, length, false)) return false;
  qa_common_parse_state_init(&tokens->parser);
  return true;
}

void qa_qvm_entity_tokens_destroy(qa_qvm_entity_tokens *tokens) {
  if (tokens == NULL) return;
  qa_common_parse_cursor_destroy(&tokens->cursor);
}

qa_qvm_entity_token_result qa_qvm_entity_tokens_entity_token(qa_qvm_entity_tokens *tokens) {
  const char *token = qa_common_parse_state_parse(&tokens->parser, &tokens->cursor, true);
  qa_qvm_entity_token_result result = {token, !tokens->cursor.has_offset};
  return result;
}

static qa_qvm_entity_token_result tokens_entity_token(void *ctx) {
  return qa_qvm_entity_tokens_entity_token(ctx);
}

qa_qvm_entity_token_services qa_qvm_entity_tokens_as_services(qa_qvm_entity_tokens *tokens) {
  qa_qvm_entity_token_services services = {tokens, tokens_entity_token};
  return services;
}

qa_save_value *qa_qvm_entity_tokens_capture(const qa_qvm_entity_tokens *tokens) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *source = qa_save_value_string(tokens->cursor.source);
  qa_save_value *cursor = tokens->cursor.has_offset
                              ? qa_save_value_number((double)tokens->cursor.offset)
                              : qa_save_value_null();
  qa_save_value *parser = qa_common_parse_state_capture(&tokens->parser);
  if (source == NULL || cursor == NULL || parser == NULL ||
      !qa_save_value_set(object, "source", source) || !qa_save_value_set(object, "cursor", cursor) ||
      !qa_save_value_set(object, "parser", parser)) {
    qa_save_value_free(object);
    qa_save_value_free(source);
    qa_save_value_free(cursor);
    qa_save_value_free(parser);
    return NULL;
  }
  return object;
}

void qa_qvm_entity_tokens_restore(qa_qvm_entity_tokens *tokens, const qa_save_value *value) {
  if (value == NULL && tokens->cursor.length == 0) {
    qa_common_parse_cursor_set_offset(&tokens->cursor, 0);
    qa_common_parse_state fresh;
    qa_common_parse_state_init(&fresh);
    qa_save_value *captured = qa_common_parse_state_capture(&fresh);
    if (captured == NULL) qa_range_error("out of memory");
    qa_common_parse_state_restore(&tokens->parser, captured);
    qa_save_value_free(captured);
    return;
  }
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "qvm.entityTokens")) qa_range_error("out of memory");
  qa_save_reader source, cursor, parser;
  if (!qa_save_reader_field(&reader, "source", &source)) qa_range_error("out of memory");
  (void)qa_save_reader_literal_string(&source, tokens->cursor.source);
  qa_save_reader_destroy(&source);
  if (!qa_save_reader_field(&reader, "cursor", &cursor)) qa_range_error("out of memory");
  bool has_cursor = !qa_save_reader_is_null(&cursor);
  double cursor_value = has_cursor ? qa_save_reader_integer_min(&cursor, 0) : 0;
  qa_save_reader_destroy(&cursor);
  if (!qa_save_reader_field(&reader, "parser", &parser)) qa_range_error("out of memory");
  qa_common_parse_state_restore(&tokens->parser, parser.value);
  qa_save_reader_destroy(&parser);
  qa_save_reader_destroy(&reader);
  if (has_cursor) qa_common_parse_cursor_set_offset(&tokens->cursor, (size_t)cursor_value);
  else qa_common_parse_cursor_set_null(&tokens->cursor);
}

bool qa_qvm_entity_token_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_entity_token_services services,
                                 qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0 ||
      call->code != QA_QVM_GAME_IMPORT_G_GET_ENTITY_TOKEN)
    return false;
  qa_qvm_entity_token_result result = services.entity_token(services.ctx);
  qa_qvm_game_memory_write_string(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 1), result.token,
                             qa_qvm_host_call_word(call, 2));
  *out = (!result.ended || result.token[0] != '\0') ? 1 : 0;
  return true;
}

/* Port of src/bots/behavior/library/chat-data.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CHAT_DATA_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CHAT_DATA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_CHAT_ESCAPE "\x01"
#define QA_CHAT_MESSAGE_SIZE 256
bool qa_chat_text_equals(const char *expected, const char *input, bool case_sensitive);
typedef struct qa_chat_message {
  const char *text;
  double time;
} qa_chat_message;
typedef struct qa_chat_type_bots_a {
  const char *name;
  const qa_chat_message *messages;
  size_t message_count;
} qa_chat_type_bots_a;
typedef struct qa_initial_chat_bots_a {
  const qa_chat_type_bots_a *types;
  size_t type_count;
} qa_initial_chat_bots_a;
typedef struct qa_synonym_bots_a {
  const char *text;
  double weight;
} qa_synonym_bots_a;
typedef struct qa_synonym_group_bots_a {
  int64_t context;
  const qa_synonym_bots_a *entries;
  size_t entry_count;
  double total_weight;
} qa_synonym_group_bots_a;
typedef struct qa_chat_data_parser_bots_a {
  void *state;
  int64_t synonym_count, random_list_count, match_count, reply_count;
} qa_chat_data_parser_bots_a;
void qa_chat_data_parser_bots_a_init(qa_chat_data_parser_bots_a *p);
void qa_chat_data_parser_bots_a_destroy(qa_chat_data_parser_bots_a *p);
#endif

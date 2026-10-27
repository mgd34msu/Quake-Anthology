/* Port of src/bots/behavior/library/chat.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CHAT_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CHAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_chat_gender {
  QA_CHAT_GENDERLESS = 0,
  QA_CHAT_FEMALE = 1,
  QA_CHAT_MALE = 2
} qa_chat_gender;
typedef enum qa_chat_destination {
  QA_CHAT_ALL = 0,
  QA_CHAT_TEAM = 1,
  QA_CHAT_TELL = 2
} qa_chat_destination;
typedef struct qa_chat_capture {
  bool present;
  int64_t offset, length;
} qa_chat_capture;
typedef struct qa_chat_match_bots_a {
  const char *text;
  int64_t type, subtype;
  qa_chat_capture variables[8];
} qa_chat_match_bots_a;
typedef struct qa_chat_configuration_counts {
  int64_t synonyms, random_lists, matches, replies;
} qa_chat_configuration_counts;
int64_t qa_string_contains_bots_a(const char *text, const char *part, bool case_sensitive);
void qa_unify_white_spaces_bots_a(const char *input, char *out, size_t out_len);
void qa_unify_white_spaces_in_place_bots_a(uint8_t *bytes, size_t length);
typedef struct qa_bot_chat_library {
  void *states[65];
  int64_t setup_revision;
  double (*time_fn)(void *ctx);
  void *time_ctx;
  int64_t (*next_int)(void *ctx);
  void *random_ctx;
} qa_bot_chat_library;
void qa_bot_chat_library_init(qa_bot_chat_library *l);
void qa_bot_chat_library_destroy(qa_bot_chat_library *l);
#endif

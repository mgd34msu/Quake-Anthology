/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/chat_data.h"
#include "qa/foundation/support.h"
#include <string.h>
static int up(int c) { return (c >= 97 && c <= 122) ? c - 32 : c; }
bool qa_chat_text_equals(const char *expected, const char *input, bool cs) {
  if (input == NULL)
    return false;
  size_t el = strlen(expected);
  for (size_t i = 0; i <= el; i++) {
    int first = (i == el) ? 0 : (uint8_t)expected[i];
    int second = (i < strlen(input)) ? (uint8_t)input[i] : 0;
    if (first > 255 || second > 255)
      qa_range_error("bot chat strings must contain byte-valued code units");
    if (!cs) {
      first = up(first);
      second = up(second);
    }
    if (first != second)
      return false;
    if (first == 0)
      return true;
  }
  return false;
}
void qa_chat_data_parser_bots_a_init(qa_chat_data_parser_bots_a *p) { memset(p, 0, sizeof *p); }
void qa_chat_data_parser_bots_a_destroy(qa_chat_data_parser_bots_a *p) { (void)p; }

/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/chat.h"
#include <stdio.h>
#include <string.h>
static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int64_t qa_string_contains_bots_a(const char *text, const char *part, bool cs) {
  if (!text || !part)
    return -1;
  size_t tl = strlen(text), pl = strlen(part);
  if (pl == 0)
    return 0;
  for (size_t i = 0; i + pl <= tl; i++) {
    size_t j = 0;
    for (; j < pl; j++) {
      int a = (uint8_t)text[i + j], b = (uint8_t)part[j];
      if (!cs) {
        a = up(a);
        b = up(b);
      }
      if (a != b)
        break;
    }
    if (j == pl)
      return (int64_t)i;
  }
  return -1;
}
static bool ws(int b) {
  if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9'))
    return false;
  const char *s = "()?:'/,.[]-_+=";
  for (; *s; s++)
    if (*s == b)
      return false;
  return true;
}
void qa_unify_white_spaces_in_place_bots_a(uint8_t *bytes, size_t length) {
  if (!bytes)
    return;
  (void)length;
  size_t pointer = 0, old = 0;
  while (bytes[pointer] != 0) {
    while (bytes[pointer] != 0 && ws(bytes[pointer]))
      pointer++;
    if (pointer > old) {
      if (old > 0 && bytes[pointer] != 0)
        bytes[old++] = 32;
      if (pointer > old) {
        size_t end = pointer;
        while (bytes[end] != 0)
          end++;
        memmove(bytes + old, bytes + pointer, end + 1 - pointer);
        pointer = old + (end - pointer) - (end - pointer); /* keep scan at old */
        pointer = old;
      }
    }
    while (bytes[pointer] != 0 && !ws(bytes[pointer]))
      pointer++;
    old = pointer;
  }
}
void qa_unify_white_spaces_bots_a(const char *input, char *out, size_t out_len) {
  if (out_len == 0)
    return;
  snprintf(out, out_len, "%s", input);
  qa_unify_white_spaces_in_place_bots_a((uint8_t *)out, out_len);
}
void qa_bot_chat_library_init(qa_bot_chat_library *l) { memset(l, 0, sizeof *l); }
void qa_bot_chat_library_destroy(qa_bot_chat_library *l) { (void)l; }

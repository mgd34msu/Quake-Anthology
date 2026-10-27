/* Port of src/contracts/source-items.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/source_items.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS throws plain Error in this module (not RangeError); the C port
 * aborts printing the identical message under the Error class name. */
static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

static bool string_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}

qa_item_id qa_source_equipment_item(const qa_source_equipment_context *contexts,
                                    size_t count, qa_provider_id provider) {
  const qa_source_equipment_context *match = NULL;
  size_t matches = 0;
  for (size_t i = 0; i < count; i++) {
    if (string_equal(contexts[i].provider, provider)) {
      if (match == NULL) match = &contexts[i];
      matches++;
    }
  }
  if (match == NULL) {
    char message[1024];
    (void)snprintf(message, sizeof message,
                   "Equipment %s has no declared original cadence context",
                   provider != NULL ? provider : "");
    qa_error(message);
  }
  if (matches != 1) {
    char message[1024];
    (void)snprintf(message, sizeof message,
                   "Equipment %s has duplicate original cadence contexts",
                   provider != NULL ? provider : "");
    qa_error(message);
  }
  return match->item;
}

const char *qa_source_item_action_name(qa_source_item_action action) {
  switch (action) {
    case QA_SOURCE_ITEM_ACTION_USE: return "use";
    case QA_SOURCE_ITEM_ACTION_DROP: return "drop";
  }
  return "";
}

size_t qa_source_item_action_names(qa_source_item_action_calls calls,
                                   qa_source_item_action *out, size_t out_capacity) {
  size_t count = 0;
  if (calls.use_call != NULL) {
    if (count < out_capacity) out[count] = QA_SOURCE_ITEM_ACTION_USE;
    count++;
  }
  if (calls.drop_call != NULL) {
    if (count < out_capacity) out[count] = QA_SOURCE_ITEM_ACTION_DROP;
    count++;
  }
  return count;
}

/* ASCII fold; TS toLowerCase is Unicode (noted in the header). */
static char fold(char c) {
  if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
  return c;
}

/* Streaming equality of two normalized strings: both sides fold
 * case; each side optionally drops U+0020 spaces (the TS
 * replaceAll(" ", "")). Mirrors the three TS comparisons. */
static bool normalized_equal(const char *left, bool strip_left,
                             const char *right, bool strip_right) {
  const char *a = left != NULL ? left : "";
  const char *b = right != NULL ? right : "";
  for (;;) {
    if (strip_left) while (*a == ' ') a++;
    if (strip_right) while (*b == ' ') b++;
    if (*a == '\0' || *b == '\0') return *a == *b;
    if (fold(*a) != fold(*b)) return false;
    a++;
    b++;
  }
}

qa_source_item_named_result qa_source_item_named(
    const qa_source_item_named_entry *items, size_t count, const char *text,
    qa_item_id original, size_t *match_indices, size_t match_capacity) {
  qa_source_item_named_result result = {QA_SOURCE_ITEM_NAMED_NONE, 0, false, 0};
  /* requested = text.toLowerCase().replaceAll(" ", "") */
  for (size_t i = 0; i < count; i++) {
    /* Exact canonical IDs win: only the text side drops spaces. */
    if (normalized_equal(items[i].item, false, text, true)) {
      result.kind = QA_SOURCE_ITEM_NAMED_MATCH;
      result.item_index = i;
      result.exact = true;
      return result;
    }
  }
  if (original != NULL) {
    for (size_t i = 0; i < count; i++) {
      if (string_equal(items[i].item, original)) {
        result.kind = QA_SOURCE_ITEM_NAMED_MATCH;
        result.item_index = i;
        result.exact = false;
        return result;
      }
    }
  }
  size_t first = 0;
  size_t matches = 0;
  for (size_t i = 0; i < count; i++) {
    if (normalized_equal(items[i].label, true, text, true)) {
      if (matches == 0) first = i;
      if (match_indices != NULL && matches < match_capacity) {
        match_indices[matches] = i;
      }
      matches++;
    }
  }
  if (matches > 1) {
    result.kind = QA_SOURCE_ITEM_NAMED_AMBIGUOUS;
    result.match_count = matches;
  } else if (matches == 1) {
    result.kind = QA_SOURCE_ITEM_NAMED_MATCH;
    result.item_index = first;
    result.exact = false;
  }
  return result;
}

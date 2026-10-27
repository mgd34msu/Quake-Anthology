/* Port of src/core/commands/filter.ts. GPL-2.0-or-later. */
#include "qa/core/commands/filter.h"

#include <stdint.h>
#include <string.h>

#include "qa/foundation/support.h"

/* The TS byte() helper: 0 at the terminator, abort past it. Indices
 * are signed because bracket parsing probes ahead of the end. */
static int byte_at(const char *text, int64_t length, int64_t index) {
  if (index > length) {
    qa_range_error("Source filter reads beyond a string terminator");
  }
  return index == length ? 0 : (unsigned char)text[index];
}

static int upper(int value) {
  /* Signed char 0xff promotes to EOF, the one defined negative ctype input. */
  if (value == 255) return -1;
  return value >= 97 && value <= 122 ? value - 32 : value;
}

static int signed_byte(int value) { return value > 127 ? value - 256 : value; }

bool qa_source_filter(const char *filter_text, const char *name_text,
                      bool case_sensitive) {
  const char *filter = filter_text;
  const char *name = name_text;
  int64_t filter_length = (int64_t)strlen(filter);
  int64_t name_length = (int64_t)strlen(name);
  int (*fold)(int) = case_sensitive ? signed_byte : upper;
  int64_t pattern = 0, cursor = 0;
  while (byte_at(filter, filter_length, pattern) != 0) {
    if (byte_at(filter, filter_length, pattern) == 42) {
      pattern++;
      int64_t start = pattern;
      while (byte_at(filter, filter_length, pattern) != 0 &&
             byte_at(filter, filter_length, pattern) != 42 &&
             byte_at(filter, filter_length, pattern) != 63) {
        pattern++;
      }
      int64_t segment_length = pattern - start;
      if (segment_length >= 1024) {
        qa_range_error("Source filter star run exceeds its scratch buffer");
      }
      if (segment_length > 0) {
        int64_t found = -1;
        for (int64_t index = cursor; index <= name_length - segment_length;
             index++) {
          int matches = 1;
          for (int64_t character = 0; character < segment_length;
               character++) {
            if (fold(byte_at(name, name_length, index + character)) !=
                fold(byte_at(filter, filter_length, start + character))) {
              matches = 0;
              break;
            }
          }
          if (matches) {
            found = index;
            break;
          }
        }
        if (found < 0) return false;
        cursor = found + segment_length;
      }
    } else if (byte_at(filter, filter_length, pattern) == 63) {
      /* The TS reads the byte (for its past-the-end check) and drops it. */
      (void)byte_at(name, name_length, cursor);
      pattern++;
      cursor++;
    } else if (byte_at(filter, filter_length, pattern) == 91 &&
               byte_at(filter, filter_length, pattern + 1) == 91) {
      pattern++;
    } else if (byte_at(filter, filter_length, pattern) == 91) {
      pattern++;
      int found = 0;
      while (byte_at(filter, filter_length, pattern) != 0 && !found) {
        if (byte_at(filter, filter_length, pattern) == 93 &&
            byte_at(filter, filter_length, pattern + 1) != 93) {
          break;
        }
        if (byte_at(filter, filter_length, pattern + 1) == 45 &&
            byte_at(filter, filter_length, pattern + 2) != 0 &&
            (byte_at(filter, filter_length, pattern + 2) != 93 ||
             byte_at(filter, filter_length, pattern + 3) == 93)) {
          int current = fold(byte_at(name, name_length, cursor));
          if (current >= fold(byte_at(filter, filter_length, pattern)) &&
              current <= fold(byte_at(filter, filter_length, pattern + 2))) {
            found = 1;
          }
          pattern += 3;
        } else {
          if (fold(byte_at(filter, filter_length, pattern)) ==
              fold(byte_at(name, name_length, cursor))) {
            found = 1;
          }
          pattern++;
        }
      }
      if (!found) return false;
      while (byte_at(filter, filter_length, pattern) != 0) {
        if (byte_at(filter, filter_length, pattern) == 93 &&
            byte_at(filter, filter_length, pattern + 1) != 93) {
          break;
        }
        pattern++;
      }
      pattern++;
      cursor++;
    } else {
      if (fold(byte_at(filter, filter_length, pattern)) !=
          fold(byte_at(name, name_length, cursor))) {
        return false;
      }
      pattern++;
      cursor++;
    }
  }
  return true;
}

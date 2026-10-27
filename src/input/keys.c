/* Port of src/input/keys.ts. GPL-2.0-or-later. */
#include "qa/input/keys.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/input/key_codes.h"
#include "qa/input/support.h"

typedef struct key_name {
  const char *name;
  double number;
} key_name;

static const key_name fixed_names[] = {
    {"TAB", QA_KEY_TAB}, {"ENTER", QA_KEY_ENTER}, {"ESCAPE", QA_KEY_ESCAPE},
    {"SPACE", QA_KEY_SPACE}, {"BACKSPACE", QA_KEY_BACKSPACE},
    {"UPARROW", QA_KEY_UP}, {"DOWNARROW", QA_KEY_DOWN},
    {"LEFTARROW", QA_KEY_LEFT}, {"RIGHTARROW", QA_KEY_RIGHT},
    {"ALT", QA_KEY_ALT}, {"CTRL", QA_KEY_CONTROL}, {"SHIFT", QA_KEY_SHIFT},
    {"COMMAND", QA_KEY_COMMAND}, {"CAPSLOCK", QA_KEY_CAPS_LOCK},
    {"INS", QA_KEY_INSERT}, {"DEL", QA_KEY_DELETE},
    {"PGDN", QA_KEY_PAGE_DOWN}, {"PGUP", QA_KEY_PAGE_UP},
    {"HOME", QA_KEY_HOME}, {"END", QA_KEY_END},
    {"MWHEELUP", QA_KEY_MOUSE_WHEEL_UP}, {"MWHEELDOWN", QA_KEY_MOUSE_WHEEL_DOWN},
    {"KP_HOME", QA_KEY_KEYPAD_HOME}, {"KP_UPARROW", QA_KEY_KEYPAD_UP},
    {"KP_PGUP", QA_KEY_KEYPAD_PAGE_UP}, {"KP_LEFTARROW", QA_KEY_KEYPAD_LEFT},
    {"KP_5", QA_KEY_KEYPAD_5}, {"KP_RIGHTARROW", QA_KEY_KEYPAD_RIGHT},
    {"KP_END", QA_KEY_KEYPAD_END}, {"KP_DOWNARROW", QA_KEY_KEYPAD_DOWN},
    {"KP_PGDN", QA_KEY_KEYPAD_PAGE_DOWN}, {"KP_ENTER", QA_KEY_KEYPAD_ENTER},
    {"KP_INS", QA_KEY_KEYPAD_INSERT}, {"KP_DEL", QA_KEY_KEYPAD_DELETE},
    {"KP_SLASH", QA_KEY_KEYPAD_SLASH}, {"KP_MINUS", QA_KEY_KEYPAD_MINUS},
    {"KP_PLUS", QA_KEY_KEYPAD_PLUS}, {"KP_NUMLOCK", QA_KEY_KEYPAD_NUM_LOCK},
    {"KP_STAR", QA_KEY_KEYPAD_STAR}, {"KP_EQUALS", QA_KEY_KEYPAD_EQUALS},
    {"PAUSE", QA_KEY_PAUSE}, {"SEMICOLON", 59},
};

/* TS lower(): folds ASCII A-Z only. */
static bool name_equals(const char *text, const char *name) {
  while (*text != '\0' && *name != '\0') {
    char a = *text, b = *name;
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    if (a != b) return false;
    text++;
    name++;
  }
  return *text == '\0' && *name == '\0';
}

/* Generated table rows: F1..F12, MOUSE1..5, JOY1..32, AUX1..32. */
static bool generated_number(const char *text, double *out) {
  struct {
    const char *prefix;
    int count;
    double first;
  } families[4] = {{"F", 12, QA_KEY_F1},
                   {"MOUSE", 5, QA_KEY_MOUSE1},
                   {"JOY", 32, QA_KEY_JOY1},
                   {"AUX", 32, QA_KEY_AUX1}};
  for (int i = 0; i < 4; i++) {
    size_t prefix = strlen(families[i].prefix);
    /* Prefix compare under the same ASCII fold. */
    bool matches = true;
    for (size_t j = 0; j < prefix; j++) {
      char a = text[j], b = families[i].prefix[j];
      if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
      if (a != b) {
        matches = false;
        break;
      }
    }
    if (!matches) continue;
    char wanted[16];
    for (int n = 1; n <= families[i].count; n++) {
      snprintf(wanted, sizeof wanted, "%s%d", families[i].prefix, n);
      if (!name_equals(text, wanted)) continue;
      /* AUX17..AUX32 live at 256..271 in the TS table. */
      *out = i == 3 && n > 16 ? 256 + (n - 17) : families[i].first + (n - 1);
      return true;
    }
    return false;
  }
  return false;
}

static const char *generated_name(double key, char *buffer, size_t size) {
  if (key >= QA_KEY_F1 && key < QA_KEY_F1 + 12) {
    snprintf(buffer, size, "F%d", (int)(key - QA_KEY_F1) + 1);
    return buffer;
  }
  if (key >= QA_KEY_MOUSE1 && key < QA_KEY_MOUSE1 + 5) {
    snprintf(buffer, size, "MOUSE%d", (int)(key - QA_KEY_MOUSE1) + 1);
    return buffer;
  }
  if (key >= QA_KEY_JOY1 && key < QA_KEY_JOY1 + 32) {
    snprintf(buffer, size, "JOY%d", (int)(key - QA_KEY_JOY1) + 1);
    return buffer;
  }
  if (key >= QA_KEY_AUX1 && key < QA_KEY_AUX1 + 16) {
    snprintf(buffer, size, "AUX%d", (int)(key - QA_KEY_AUX1) + 1);
    return buffer;
  }
  if (key >= 256 && key <= 271) {
    snprintf(buffer, size, "AUX%d", (int)(key - 256) + 17);
    return buffer;
  }
  return NULL;
}

double qa_string_to_keynum(const char *value) {
  if (value == NULL) return -1;
  char *text = qa_source_command_text(value);
  if (text == NULL) return -1;
  double result = -1;
  size_t length = strlen(text);
  if (length == 0) {
    result = -1;
  } else if (length == 1) {
    unsigned char byte = (unsigned char)text[0];
    result = byte < 128 ? byte : (double)byte - 256;
  } else if (text[0] == '0' && text[1] == 'x' && length == 4) {
    double nibbles = 0;
    for (int i = 2; i < 4; i++) {
      int code = (unsigned char)text[i];
      double nibble = code >= 48 && code <= 57 ? code - 48
                      : code >= 97 && code <= 102 ? code - 87 : 0;
      nibbles = nibbles * 16 + nibble;
    }
    result = nibbles;
  } else {
    for (size_t i = 0; i < sizeof fixed_names / sizeof fixed_names[0]; i++) {
      if (name_equals(text, fixed_names[i].name)) {
        result = fixed_names[i].number;
        break;
      }
    }
    if (result == -1) generated_number(text, &result);
  }
  free(text);
  return result;
}

char *qa_keynum_to_string(double key) {
  if (key == -1) return qa_input_dup("<KEY NOT FOUND>");
  if (key != trunc(key) || key < 0 || key > 271) {
    return qa_input_dup("<OUT OF RANGE>");
  }
  if (key > 32 && key < 127 && key != 34 && key != 59) {
    char single[2] = {(char)key, '\0'};
    return qa_input_dup(single);
  }
  /* The TS scans the table in order; values are unique per family. */
  char buffer[16];
  const char *generated = generated_name(key, buffer, sizeof buffer);
  if (generated != NULL) {
    /* Fixed rows precede generated rows in the TS table only for
     * the F/MOUSE/JOY/AUX ranges, which no fixed row shares. */
    return qa_input_dup(generated);
  }
  for (size_t i = 0; i < sizeof fixed_names / sizeof fixed_names[0]; i++) {
    if (key == fixed_names[i].number) return qa_input_dup(fixed_names[i].name);
  }
  char hex[8];
  snprintf(hex, sizeof hex, "0x%02x", (unsigned)key);
  return qa_input_dup(hex);
}

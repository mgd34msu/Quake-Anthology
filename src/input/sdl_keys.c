/* Port of src/input/sdl-keys.ts. GPL-2.0-or-later. */
#include "qa/input/sdl_keys.h"

#include "qa/input/key_codes.h"
#include "qa/input/support.h"

static double special_key(int32_t code) {
  switch (code) {
    case 57: case 70: case 71: return 0;
    case 72: return QA_KEY_PAUSE;
    case 73: return QA_KEY_INSERT;
    case 74: return QA_KEY_HOME;
    case 75: return QA_KEY_PAGE_UP;
    case 77: return QA_KEY_END;
    case 78: return QA_KEY_PAGE_DOWN;
    case 79: return QA_KEY_RIGHT;
    case 80: return QA_KEY_LEFT;
    case 81: return QA_KEY_DOWN;
    case 82: return QA_KEY_UP;
    case 84: return QA_KEY_KEYPAD_SLASH;
    case 85: return 42;
    case 86: return QA_KEY_KEYPAD_MINUS;
    case 87: return QA_KEY_KEYPAD_PLUS;
    case 88: return QA_KEY_KEYPAD_ENTER;
    case 89: return QA_KEY_KEYPAD_END;
    case 90: return QA_KEY_KEYPAD_DOWN;
    case 91: return QA_KEY_KEYPAD_PAGE_DOWN;
    case 92: return QA_KEY_KEYPAD_LEFT;
    case 93: return QA_KEY_KEYPAD_5;
    case 94: return QA_KEY_KEYPAD_RIGHT;
    case 95: return QA_KEY_KEYPAD_HOME;
    case 96: return QA_KEY_KEYPAD_UP;
    case 97: return QA_KEY_KEYPAD_PAGE_UP;
    case 98: return QA_KEY_KEYPAD_INSERT;
    case 99: return QA_KEY_KEYPAD_DELETE;
    case 103: return 61;
    case 116: return QA_KEY_CONTROL;
    case 205: return QA_KEY_SPACE;
    case 224: return QA_KEY_CONTROL;
    case 225: return QA_KEY_SHIFT;
    case 226: case 227: return QA_KEY_ALT;
    case 228: return QA_KEY_CONTROL;
    case 229: return QA_KEY_SHIFT;
    case 230: case 231: return QA_KEY_ALT;
    default: return 0;
  }
}

/** XLookupString's ASCII control conversion. */
static int32_t control_byte(int32_t byte) {
  if ((byte >= 64 && byte < 127) || byte == 32) return byte & 31;
  if (byte == 50) return 0;
  if (byte >= 51 && byte <= 55) return byte - 24;
  if (byte == 56) return 127;
  if (byte == 47) return 31;
  return byte;
}

double qa_sdl_game_key(int32_t keycode, int32_t modifiers) {
  if ((keycode & 0x40000000) != 0) {
    int32_t code = keycode & ~0x40000000;
    if (code >= 58 && code <= 69) return QA_KEY_F1 + code - 58;
    /* XLateKey lists KP_Begin, but KP_5 takes the fallback byte path. */
    if (code == 93 && (modifiers & 0x1000) != 0) {
      return (modifiers & 0xc0) != 0 ? 29 : 53;
    }
    return special_key(code);
  }
  switch (keycode) {
    case 8: return QA_KEY_BACKSPACE;
    case 127: return QA_KEY_DELETE;
    case 33: return 49;
    case 64: return 50;
    case 35: return 51;
    case 36: return 52;
    case 37: return 53;
    case 94: return 54;
    case 38: return 55;
    case 42: return 56;
    case 40: return 57;
    case 41: return 48;
    case 178: return 126;
    /* XLookupString returns no byte for ISO_Left_Tab, which XLateKey omits. */
    case 9: return (modifiers & 3) != 0 ? 0 : QA_KEY_TAB;
    case 13: return QA_KEY_ENTER;
    case 27: return QA_KEY_ESCAPE;
    case 32: return QA_KEY_SPACE;
    default: {
      int32_t byte = (modifiers & 0xc0) != 0 ? control_byte(keycode) : keycode;
      if (byte >= 65 && byte <= 90) return byte + 32;
      if (byte >= 1 && byte <= 26) return byte + 96;
      return byte > 0 && byte <= 255 ? byte : 0;
    }
  }
}

double qa_sdl_event_time(double timestamp, double ticks, double now, bool subframe) {
  int32_t age = qa_input_to_int32(ticks - timestamp);
  return subframe && age >= 0 && age <= 30 ? (double)qa_input_to_int32(now - age)
                                           : now;
}

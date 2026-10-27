/* Port of src/core/game-numeric.ts. GPL-2.0-or-later. */
#include "qa/core/game_numeric.h"

#include <string.h>

#include "qa/foundation/support.h"

/* The TS NumberInput cursor: byte() yields a signed char, yields 0 at
 * the string's terminating NUL, and aborts past it. */
typedef struct number_input {
  const char *text;
  size_t length;
  size_t offset;
} number_input;

static void number_input_init(number_input *input, const char *text,
                              size_t offset) {
  input->text = text;
  input->length = strlen(text);
  if (offset > input->length) {
    qa_range_error("Game number cursor is outside its backing string");
  }
  input->offset = offset;
}

static int number_input_byte(const number_input *input) {
  if (input->offset > input->length) {
    qa_range_error("Game number scan reads beyond its backing string");
  }
  if (input->offset == input->length) return 0;
  int byte = (unsigned char)input->text[input->offset];
  return byte < 128 ? byte : byte - 256;
}

static int number_input_take(number_input *input) {
  int byte = number_input_byte(input);
  input->offset++;
  return byte;
}

static void number_input_skip_whitespace(number_input *input) {
  while (number_input_byte(input) <= 32 && number_input_byte(input) != 0) {
    input->offset++;
  }
}

static int number_input_sign(number_input *input) {
  int byte = number_input_byte(input);
  if (byte != 43 && byte != 45) return 1;
  input->offset++;
  return byte == 45 ? -1 : 1;
}

/* Math.fround at each TS rounding point: compute in double, cast. */
static float fround_f(double value) { return (float)value; }

enum read_mode { MODE_SCALAR, MODE_SCAN };

static float read_float(number_input *input, enum read_mode mode) {
  number_input_skip_whitespace(input);
  if (number_input_byte(input) == 0) return 0;
  int sign = number_input_sign(input);
  float value = 0;
  int character = mode == MODE_SCALAR ? number_input_byte(input) : 48;
  if (number_input_byte(input) != 46) {
    for (;;) {
      character = number_input_take(input);
      if (character < 48 || character > 57) break;
      value = fround_f((double)fround_f((double)value * 10) + (character - 48));
    }
  } else if (mode == MODE_SCALAR) {
    input->offset++;
  }

  if (character == 46) {
    float fraction = fround_f(0.1);
    for (;;) {
      character = number_input_take(input);
      if (character < 48 || character > 57) break;
      value = fround_f((double)value +
                       (double)fround_f((character - 48) * (double)fraction));
      fraction = fround_f((double)fraction * (double)fround_f(0.1));
    }
  }
  return fround_f((double)value * sign);
}

float qa_game_atof(const char *text) {
  number_input input;
  number_input_init(&input, text, 0);
  return read_float(&input, MODE_SCALAR);
}

int32_t qa_game_atoi(const char *text) {
  number_input input;
  number_input_init(&input, text, 0);
  number_input_skip_whitespace(&input);
  if (number_input_byte(&input) == 0) return 0;
  int sign = number_input_sign(&input);
  int32_t value = 0;
  for (;;) {
    int character = number_input_take(&input);
    if (character < 48 || character > 57) break;
    /* (Math.imul(value, 10) + digit) | 0 as uint32 wrap arithmetic. */
    value = (int32_t)((uint32_t)value * 10u + (uint32_t)(character - 48));
  }
  return (int32_t)((uint32_t)value * (uint32_t)sign);
}

qa_game_float_scan qa_scan_game_float(const char *text, size_t offset) {
  number_input input;
  number_input_init(&input, text, offset);
  qa_game_float_scan result;
  result.value = read_float(&input, MODE_SCAN);
  result.next_offset = input.offset;
  return result;
}

qa_game_vec3 qa_scan_game_vector(const char *text) {
  number_input input;
  number_input_init(&input, text, 0);
  float x = read_float(&input, MODE_SCAN);
  float y = read_float(&input, MODE_SCAN);
  float z = read_float(&input, MODE_SCAN);
  /* vec3() applies Math.fround per component; reads already rounded. */
  qa_game_vec3 result = {fround_f(x), fround_f(y), fround_f(z)};
  return result;
}

void qa_game_random_reset(qa_game_random *random, int64_t seed) {
  if (seed < -QA_MAX_SAFE_INTEGER || seed > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Game random seed must be a safe integer");
  }
  random->seed = (int32_t)(uint32_t)seed; /* TS `seed | 0` wrap. */
}

void qa_game_random_init(qa_game_random *random, int64_t seed) {
  qa_game_random_reset(random, seed);
}

int32_t qa_game_random_rand(qa_game_random *random) {
  /* qRand: int32(Math.imul(69069, seed) + 1) as uint32 wrap arithmetic. */
  random->seed = (int32_t)(69069u * (uint32_t)random->seed + 1u);
  return random->seed & 0x7fff;
}

float qa_game_random_random(qa_game_random *random) {
  return fround_f((double)qa_game_random_rand(random) / 0x7fff);
}

float qa_game_random_crandom(qa_game_random *random) {
  return fround_f(2 * (double)fround_f((double)qa_game_random_random(random) - 0.5));
}

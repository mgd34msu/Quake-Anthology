/*
 * Port of src/core/game-numeric.ts: id Software's bg_lib.c atof/atoi
 * (binary32 staged rounding, int32 digit wraparound), the _atof float
 * scanner, and the instance-owned rand/srand. C accommodations:
 *  - Text is a NUL-terminated byte string; the TS "byte character"
 *    (charCode > 255) check and the string's own terminating NUL are
 *    inherent to C strings. A TS string with content after an embedded
 *    NUL scans as if it ended at the NUL.
 *  - The TS imports vec3 from core/math and qRand from core/numeric
 *    (neither ported yet). vec3's per-component Math.fround is inlined
 *    into qa_scan_game_vector and the result uses the local vector type
 *    below, shaped like contracts' Vec3; qRand's 69069 LCG step is
 *    inlined into qa_game_random_rand.
 *  - Math.fround points compute in double and cast to float; results
 *    are returned as float (the TS number holds a binary32 value).
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_GAME_NUMERIC_H
#define QA_CORE_GAME_NUMERIC_H

#include <stddef.h>
#include <stdint.h>

typedef struct qa_game_float_scan {
  float value;
  size_t next_offset;
} qa_game_float_scan;

typedef struct qa_game_vec3 {
  float x;
  float y;
  float z;
} qa_game_vec3;

/* bg_lib atof: decimal prefix only, each operation rounded to binary32. */
float qa_game_atof(const char *text);
/* bg_lib atoi: every digit operation wraps in int32. */
int32_t qa_game_atoi(const char *text);
/* bg_lib _atof scan: consumes the first delimiter (even NUL), leaves a
 * leading dot; reads past the supplied string abort. `offset` must lie
 * within the string (its NUL position included). */
qa_game_float_scan qa_scan_game_float(const char *text, size_t offset);
/* QVM sscanf("%f %f %f") shape: three scans over one cursor. */
qa_game_vec3 qa_scan_game_vector(const char *text);

typedef struct qa_game_random {
  int32_t seed;
} qa_game_random;

/* `seed` must be a safe integer, as in the TS; stored via int32 wrap. */
void qa_game_random_init(qa_game_random *random, int64_t seed);
void qa_game_random_reset(qa_game_random *random, int64_t seed);
int32_t qa_game_random_rand(qa_game_random *random);
float qa_game_random_random(qa_game_random *random);
float qa_game_random_crandom(qa_game_random *random);

#endif

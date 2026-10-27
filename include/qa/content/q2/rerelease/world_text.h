/* Port of src/content/q2/rerelease/world-text.ts. GPL-2.0-or-later.
 * Input text is UTF-8 bytes here; the TS iterates UTF-16 units and
 * masks charCodeAt & 255. qa_q2_world_text_utf16 takes explicit
 * UTF-16 units for bit-faithful non-ASCII behavior; the char* form
 * treats each byte as a unit (identical for ASCII). out_text is a
 * caller buffer (>=128 bytes); returned qa_world_text.text points
 * at it. angles NULL = billboard. */
#ifndef QA_CONTENT_Q2_RERELEASE_WORLD_TEXT_H
#define QA_CONTENT_Q2_RERELEASE_WORLD_TEXT_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/text/world.h"
qa_world_text qa_q2_world_text_utf16(qa_vec3 origin, const qa_vec3 *angles,
                                     const uint16_t *text, size_t text_length,
                                     qa_vec4 color, double size, bool depth_test,
                                     char *out_text, size_t out_cap);
qa_world_text qa_q2_world_text(qa_vec3 origin, const qa_vec3 *angles, const char *text,
                               qa_vec4 color, double size, bool depth_test,
                               char *out_text, size_t out_cap);
#endif

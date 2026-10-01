#ifndef QA_APPLICATION_NATIVE_Q2_FEEDBACK_WIRE_H
#define QA_APPLICATION_NATIVE_Q2_FEEDBACK_WIRE_H
#include "qa/actors.h"
#include "qa/math.h"

/* Decode the source temp-entity position and byte-normal representation before
 * publishing its receipt. Outputs are unchanged on failure. */
bool application_native_q2_feedback_geometry(bool rerelease, uint8_t type,
    qa_vec3 point, qa_vec3 normal, qa_vec3 *decoded_point, qa_vec3 *decoded_normal,
    qa_error *);
#endif

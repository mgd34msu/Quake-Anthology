#ifndef QA_APPLICATION_GUEST_Q3_COLLISION_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_COLLISION_PROFILE_H
#include "qa/q3_host_collision.h"

bool application_q3_collision_profile_read(const qa_qvm_image *, qa_qvm_role,
    qa_qvm_abi, qa_bytes declaration, qa_q3_host_collision_profile *, qa_error *);

#endif
